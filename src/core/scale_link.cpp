#include "core/scale_link.h"

#include "core/system.h"

namespace {

constexpr const char* kPartnerSleepLabels[] = {"Off", "1 min", "5 min",
                                                "10 min", "30 min"};
constexpr const char* kPartnerBeepLabels[] = {"Off", "On"};
constexpr core::ScaleSettingDesc kPartnerSettings[] = {
    {"Beep", kPartnerBeepLabels, 2},
    {"Auto sleep", kPartnerSleepLabels, 5},
};
constexpr int kPartnerSettingCount = 2;  // no Unit row: the Umbra streams grams

// Address type UNKNOWN (a unit paired before the type was recorded, or the
// relearn attempt below): the transport tries both types, so each try is
// capped low — a wrong-type try can never succeed and only delays the other.
constexpr uint32_t kReconnectBackoffMs = 3000;
constexpr uint32_t kConnectTimeoutMs = 5000;
// Address type KNOWN: one try, so the listen can be long and the gap short.
// A connect-by-address is the controller listening for that one address; a
// restart buys nothing, and the dead time between tries is where the waits
// came from (X-8, 2026-10-21: a 5 s window caught an advertising Bookoo
// about half the time, each miss then cost the 8 s of public try + backoff).
// User actions (Disconnect, Forget, Scan) cancel an in-flight try.
constexpr uint32_t kKnownTypeTimeoutMs = 10000;
constexpr uint32_t kKnownTypeBackoffMs = 1000;
constexpr int kKnownTypeFailLimit = 3;  // then relearn with a both-types try
// After a finder sighting the scale is advertising right now: the initiator
// catches its next advertising event, so one short try is plenty.
constexpr uint32_t kSightingConnectMs = 3000;
// After a setting write, ignore readback that still echoes the OLD value for
// this long: the scale keeps reporting it for a frame or two (Bookoo) up to a
// full 2 s status-poll cycle (Lunar), and accepting the echo flickered the UI
// new -> old -> new. A matching readback ends the grace early; expiry accepts
// whatever the scale says (a refused write self-corrects, just late).
constexpr uint32_t kSettingEchoGraceMs = 4000;
// The Acaia family sometimes ACKS a write yet ignores it — the trait the
// double tare covers. Settings have readback, so instead of blind doubling,
// an unconfirmed write is resent at this gap until confirmed or exhausted.
constexpr int kSettingResends = 2;
constexpr uint32_t kSettingResendGapMs = 1200;
// Tap debounce + write spacing, verified against the Lunar on HW: bursts of
// setting writes get dropped. A tapped value goes out only after it stops
// changing (so cycling a row writes just the final choice), and writes are
// never spaced closer than the gap below (the vendor app locks its settings
// UI for the same 1200 ms after each write).
constexpr uint32_t kSettingDebounceMs = 400;
constexpr uint32_t kSettingWriteGapMs = 1200;

}  // namespace

namespace core {

void ScaleLink::set_address(std::string address, AddrType type) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    address_ = std::move(address);
    addr_type_ = type;
  }
  reconnect_requested_.store(true);
  arb_.abort_slot(slot_);  // a try for the OLD address must not run out its timeout
}

void ScaleLink::set_name(std::string name) {
  std::lock_guard<std::mutex> lk(mutex_);
  name_ = name.empty() ? "Scale" : std::move(name);
  // Classify eagerly so features() reflects the saved model before the first
  // connect. do_connect() makes a fresh instance per connection anyway.
  driver_ = make_scale_driver(name_.c_str());
}

void ScaleLink::set_partner_mode(bool enabled) {
  partner_mode_.store(enabled);
  partner_paired_.store(false);
  partner_connected_.store(false);
  reconnect_requested_.store(true);
  if (enabled) {
    std::lock_guard<std::mutex> lk(mutex_);
    driver_ = make_scale_driver(name_.c_str());
  }
  set_connected(false);
}

void ScaleLink::set_partner_connected(bool connected) {
  partner_connected_.store(connected);
  if (link_up() != connected) set_connected(connected);
}

void ScaleLink::set_connect_enabled(bool enabled) {
  connect_enabled_.store(enabled);
  reconnect_requested_.store(true);
  // Disconnect must land now, not when the current try times out — and
  // Connect right after it starts a fresh try instead of inheriting one.
  if (!enabled) arb_.abort_slot(slot_);
}

void ScaleLink::set_connected(bool c) {
  arb_.set_connected(slot_, c);
  std::lock_guard<std::mutex> lk(mutex_);
  connected_ = c;
  if (!c) weight_g_ = 0.0f;
  weight_streaming_ = false;
  // Either edge invalidates the on-scale settings cache: a fresh link must
  // re-read them (the scale may have been adjusted while away), and posted
  // writes must not fire on some future reconnect the user didn't ask for.
  for (int i = 0; i < kMaxScaleSettings; ++i) {
    setting_value_[i] = -1;
    setting_grace_until_ms_[i] = 0;
    retry_value_[i] = -1;
    retry_left_[i] = 0;
    pending_setting_[i].store(-1);
  }
}

void ScaleLink::on_weight(float grams) {
  std::lock_guard<std::mutex> lk(mutex_);
  weight_g_ = grams;
  weight_streaming_ = true;
  ++seq_;  // stream-rate probe: one tick per received weight update
}

void ScaleLink::on_timer(uint32_t timer_ms) {
  std::lock_guard<std::mutex> lk(mutex_);
  timer_ms_ = timer_ms;
}

void ScaleLink::on_battery(int pct) {
  std::lock_guard<std::mutex> lk(mutex_);
  battery_pct_ = pct;
  battery_valid_ = true;
}

void ScaleLink::on_device_setting(int index, int option_idx) {
  if (index < 0 || index >= kMaxScaleSettings) return;
  std::lock_guard<std::mutex> lk(mutex_);
  // The optimistic value from set_device_setting holds until the scale
  // CONFIRMS it: while the write is queued or inside the echo grace window,
  // a readback still reporting the old value is discarded (it flickered the
  // label new -> old -> new otherwise). Agreement ends the grace early;
  // expiry accepts the scale's word — it owns the value.
  if (pending_setting_[index].load() >= 0) return;
  if (setting_grace_until_ms_[index] != 0) {
    // Toggle-semantics settings (desc.nonzero_confirms — e.g. the Lunar's
    // beep) confirm on on/off AGREEMENT, not equality: writing "on" (1) makes
    // the scale re-enable its STORED volume and report that, so readback 3
    // after writing 1 is success — adopt the richer reported value.
    const bool toggle =
        driver_ && index < driver_->device_setting_count() &&
        driver_->device_setting(index).nonzero_confirms;
    const bool confirmed =
        toggle ? (option_idx == 0) == (setting_value_[index] == 0)
               : option_idx == setting_value_[index];
    if (confirmed) {
      setting_grace_until_ms_[index] = 0;
      setting_value_[index] = option_idx;
      return;
    }
    if (static_cast<int32_t>(now_ms() - setting_grace_until_ms_[index]) < 0)
      return;  // stale echo of the pre-write value
    setting_grace_until_ms_[index] = 0;  // grace over: the write didn't take
  }
  setting_value_[index] = option_idx;
}

// The arbiter needs this link's wants to decide who listens: the saved
// address + type (finder targets), whether we want a connection at all, and
// partner mode (an inbound peer: never "missing"). Cheap and idempotent, so
// it runs every loop pass rather than from every setter.
void ScaleLink::sync_radio_state() {
  std::string addr;
  AddrType type;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    addr = address_;
    type = addr_type_;
  }
  arb_.set_target(slot_, addr, type);
  arb_.set_wanted(slot_, !addr.empty() && connect_enabled_.load());
  arb_.set_partner_mode(slot_, partner_mode_.load());
}

void ScaleLink::run() {
  bool connected = false;
  bool retare = false;  // second tare send armed (see the tare block below)
  for (;;) {
    sync_radio_state();

    // The user's Settings scan: granted by the arbiter (it preempts whoever
    // holds the radio, including this link's own connect attempt) and run
    // here, in any link state.
    if (arb_.user_scan_pending(slot_)) {
      const RadioGrant g = arb_.poll(slot_, now_ms());
      if (g.work == RadioWork::kUserScan) {
        do_scan();
        arb_.release(slot_, g.ticket, ReleaseResult::kScanDone, now_ms());
      } else {
        sleep_ms(100);
        continue;
      }
    }

    // Partner-mode support for Umbra firmware 1.0.8.
    if (partner_mode_.load()) {
      if (connected) {
        ble_.disconnect();
        connected = false;
        set_connected(false);
      }
      const bool partner = partner_connected_.load() && connect_enabled_.load();
      if (link_up() != partner) set_connected(partner);
      if (partner && pending_tare_.exchange(false) && partner_setting_writer_)
        partner_setting_writer_(-1, 0);
      sleep_ms(100);
      continue;
    }

    std::string addr;
    std::shared_ptr<IScaleDriver> drv;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      addr = address_;
      drv = driver_;
    }

    // No scale saved, or user disabled it: idle (drop any link).
    if (addr.empty() || !connect_enabled_.load()) {
      if (connected) ble_.disconnect();
      if (connected) { connected = false; set_connected(false); }
      sleep_ms(500);
      continue;
    }

    if (reconnect_requested_.exchange(false) && connected) {
      ble_.disconnect();
      connected = false;
      set_connected(false);
    }

    if (!connected) {
      retare = false;  // a resend never crosses a link drop: taring whatever
                       // sits on a freshly relinked scale could zero a mid-shot cup
      // Ask the arbiter what this link may do with the radio right now.
      const RadioGrant g = arb_.poll(slot_, now_ms());
      if (g.work == RadioWork::kIdle) {
        sleep_ms(100);  // parked, or another link holds the radio
        continue;
      }
      if (g.work == RadioWork::kFinderChunk) {
        // Two scales missing: one passive scan for both saved addresses. A
        // hit is reported as a sighting; that slot's own loop turns it into a
        // short direct connect on its next poll.
        const Sighting s = ble_.scan_for(arb_.finder_targets(), RadioArbiter::kChunkMs);
        if (s.found) {
          logf("ScaleLink: finder saw %s (%s)\n", radio_slot_name(s.slot),
               addr_type_name(s.type));
          arb_.report_sighting(s.slot, s.type, now_ms());
        }
        // A chunk cut short (user scan, park, Forget) is not an empty one:
        // it must not grow the backoff.
        const bool pre = arb_.was_preempted(slot_, g.ticket);
        arb_.release(slot_, g.ticket, pre ? ReleaseResult::kPreempted : ReleaseResult::kScanDone,
                     now_ms());
        continue;
      }
      if (g.work != RadioWork::kDirectConnect) {
        sleep_ms(100);
        continue;
      }
      // Direct connect-by-address. From a sighting: the advertised type, one
      // short try (the scale is advertising right now). Otherwise the stored
      // type, with the safety valve: a stored type that keeps failing is
      // retried as unknown (both types) and relearned — see known_type_fails_.
      const bool relearn = !g.from_sighting && g.addr_type != AddrType::kUnknown &&
                           known_type_fails_ >= kKnownTypeFailLimit;
      const AddrType try_type = relearn ? AddrType::kUnknown : g.addr_type;
      const uint32_t timeout = g.from_sighting           ? kSightingConnectMs
                               : try_type != AddrType::kUnknown ? kKnownTypeTimeoutMs
                                                                : kConnectTimeoutMs;
      const bool ok = do_connect(addr, try_type, timeout);
      const bool preempted = arb_.was_preempted(slot_, g.ticket);
      arb_.release(slot_, g.ticket,
                   ok ? ReleaseResult::kConnected
                      : preempted ? ReleaseResult::kPreempted : ReleaseResult::kFailed,
                   now_ms());
      if (!ok) {
        set_connected(false);
        // A preempted try (user scan, park, Forget) is not a failed one: no
        // fail count, no backoff — the arbiter decides when we go again.
        if (preempted) continue;
        if (g.from_sighting) known_type_fails_ = 0;  // the sighting was authoritative
        else if (try_type != AddrType::kUnknown) ++known_type_fails_;
        else if (relearn) known_type_fails_ = 0;  // one both-types try per limit
        // Sliced so a Scan tapped mid-backoff runs promptly, not seconds later.
        const uint32_t backoff = try_type != AddrType::kUnknown
                                     ? kKnownTypeBackoffMs
                                     : kReconnectBackoffMs;
        for (uint32_t waited = 0; waited < backoff && !arb_.user_scan_pending(slot_);
             waited += 100) {
          sleep_ms(100);
        }
        continue;
      }
      known_type_fails_ = 0;
      connected = true;
      set_connected(true);
      continue;  // re-fetch the freshly created driver next pass
    }

    if (!ble_.connected()) {
      connected = false;
      set_connected(false);
      continue;
    }

    // Every tare is sent twice, one pass (~100ms) apart: the Lunar drops a
    // lone tare ~5% of the time (write acked, command ignored — a known
    // Acaia trait), and taring an undisturbed scale twice is a no-op, so the
    // resend costs nothing on scales that don't need it.
    if (pending_tare_.exchange(false) && drv) {
      drv->tare(ble_);
      retare = true;
    } else if (retare && drv) {
      retare = false;
      drv->tare(ble_);
    }

    // Posted on-scale setting writes (UI thread -> here, like tare), with
    // the pacing the Lunar demands (see the constants above): debounced past
    // the last tap, at most ONE write per pass, never two writes closer than
    // kSettingWriteGapMs — and resent while readback still disagrees (the
    // Acaia family drops acked writes; readback matching zeroes the grace,
    // which ends the cycle).
    if (drv) {
      int send_idx = -1, send_val = -1;
      const uint32_t now = now_ms();
      {
        std::lock_guard<std::mutex> lk(mutex_);
        for (int i = 0; i < kMaxScaleSettings; ++i) {
          // Confirmation sweep runs unconditionally so a confirmed value
          // never keeps resending just because the write window was closed.
          if (retry_value_[i] >= 0 && setting_grace_until_ms_[i] == 0)
            retry_value_[i] = -1;
        }
        if (static_cast<int32_t>(now - next_setting_write_ms_) >= 0) {
          for (int i = 0; i < kMaxScaleSettings && send_idx < 0; ++i) {
            const int v = pending_setting_[i].load();
            if (v >= 0) {
              if (static_cast<int32_t>(now - (pending_since_ms_[i] +
                                              kSettingDebounceMs)) < 0)
                continue;  // still being tapped — only the final value goes out
              pending_setting_[i].store(-1);
              send_idx = i;
              send_val = v;
              retry_value_[i] = v;  // arm the resend cycle
              retry_left_[i] = kSettingResends;
              retry_at_ms_[i] = now + kSettingResendGapMs;
              setting_grace_until_ms_[i] = now + kSettingEchoGraceMs;
            } else if (retry_value_[i] >= 0 && retry_left_[i] > 0 &&
                       static_cast<int32_t>(now - retry_at_ms_[i]) >= 0) {
              send_idx = i;
              send_val = retry_value_[i];
              --retry_left_[i];
              retry_at_ms_[i] = now + kSettingResendGapMs;
              setting_grace_until_ms_[i] = now + kSettingEchoGraceMs;
            }
          }
          if (send_idx >= 0) next_setting_write_ms_ = now + kSettingWriteGapMs;
        }
      }
      if (send_idx >= 0) drv->set_device_setting(ble_, send_idx, send_val);
    }

    // Driver housekeeping: heartbeats, stream watchdogs. false = link is dead.
    if (drv && !drv->tick(ble_)) {
      ble_.disconnect();
      connected = false;
      set_connected(false);
      continue;
    }

    sleep_ms(100);  // weight arrives async via the notify callback
  }
}

bool ScaleLink::do_connect(const std::string& address, AddrType type,
                           uint32_t timeout_ms) {
  // Fresh driver per attempt: per-connection state starts clean, and the name
  // may have changed since the last connect.
  std::shared_ptr<IScaleDriver> drv;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    drv = make_scale_driver(name_.c_str());
    driver_ = drv;
  }
  if (!drv) {
    logf("ScaleLink: no driver for saved scale\n");
    return false;
  }

  // Known type: one long listen. Unknown: both types, random first — the
  // Bookoo (and most scales) advertise a random address, so the first-ever
  // reconnect then succeeds on the first try (was ~30 s: a PUBLIC-first try
  // timed out fully before falling back to RANDOM).
  const bool known = type != AddrType::kUnknown;
  if (!ble_.connect(address, type, /*prefer_random=*/true, timeout_ms)) {
    logf("ScaleLink: connect failed (%s, %lu ms)\n",
         known ? addr_type_name(type) : "random+public",
         static_cast<unsigned long>(timeout_ms));
    return false;
  }
  // Learn the type the connect actually used: persisted by the platform so
  // every later connect is a single known-type try. Compared with the STORED
  // type, not the try's — a finder sighting hands in the advertised type, and
  // comparing against that would never record it.
  const AddrType used = ble_.connected_addr_type();
  bool learned = false;
  if (used != AddrType::kUnknown) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (used != addr_type_) {
      addr_type_ = used;
      learned = true;
    }
  }
  if (learned) {
    logf("ScaleLink: learned address type %s\n", addr_type_name(used));
    if (addr_type_learned_) addr_type_learned_(used);
  }

  const char* notify_uuid = drv->select_notify(ble_);
  if (notify_uuid == nullptr) {
    logf("ScaleLink: expected characteristics not found\n");
    ble_.disconnect();
    return false;
  }

  // The lambda owns a driver reference: a driver replaced mid-notification
  // stays alive until the transport drops the callback.
  ScaleLink* self = this;
  ble_.subscribe(notify_uuid, [self, drv](const uint8_t* data, size_t len) {
    drv->on_notify(data, len, *self);
  });

  if (!drv->start(ble_)) {
    logf("ScaleLink: %s init failed\n", drv->model());
    ble_.disconnect();
    return false;
  }

  logf("ScaleLink: %s connected + subscribed (%s)\n", drv->model(),
       addr_type_name(used));
  return true;
}

bool ScaleLink::link_up() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return connected_;
}

ScaleSnapshot ScaleLink::snapshot() const {
  std::lock_guard<std::mutex> lk(mutex_);
  // "Connected" = USABLE: link up AND a weight received since it came up. A
  // link can come up seconds before the stream (seen on the Umbra and the
  // Lunar), and everything downstream keys on this one bit — Home's status/
  // readout/Tare, Settings, Auto shot arming, the shot detector, the stop
  // logic — so none of them act on a scale that isn't sending weight yet.
  return ScaleSnapshot{
      .name = name_.c_str(),
      .connected = connected_ && weight_streaming_,
      .weight_g = weight_g_,
      .timer_ms = timer_ms_,
      .battery_valid = battery_valid_,
      .battery_pct = battery_pct_,
      .seq = seq_,
  };
}

ScaleFeatures ScaleLink::features() const {
  std::lock_guard<std::mutex> lk(mutex_);
  if (partner_mode_.load())
    return ScaleFeatures{.tare = true,
                         .flow = true,
                         .timer = false,
                         .battery = true,
                         .battery_low_pct = 15,
                         .sleep = true};
  if (driver_) return driver_->features();
  return ScaleFeatures{.tare = true,
                       .flow = true,
                       .timer = true,
                       .battery = true,
                       .battery_low_pct = 15,
                       .sleep = false};
}

void ScaleLink::tare() { pending_tare_.store(true); }

int ScaleLink::device_setting_count() const {
  std::lock_guard<std::mutex> lk(mutex_);
  if (partner_mode_.load()) return kPartnerSettingCount;
  // driver_ is classified eagerly from the saved name (set_name), so the
  // descriptors answer while disconnected — only values need the link.
  return driver_ ? driver_->device_setting_count() : 0;
}

ScaleSettingDesc ScaleLink::device_setting(int index) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if (partner_mode_.load())
    return index >= 0 && index < kPartnerSettingCount ? kPartnerSettings[index]
                                                      : ScaleSettingDesc{};
  if (!driver_ || index < 0 || index >= driver_->device_setting_count())
    return ScaleSettingDesc{};
  return driver_->device_setting(index);
}

int ScaleLink::device_setting_value(int index) const {
  if (index < 0 || index >= kMaxScaleSettings) return -1;
  std::lock_guard<std::mutex> lk(mutex_);
  const int pending = pending_setting_[index].load();
  return pending >= 0 ? pending : setting_value_[index];
}

void ScaleLink::set_device_setting(int index, int option_idx) {
  if (index < 0 || index >= kMaxScaleSettings || option_idx < 0) return;
  std::lock_guard<std::mutex> lk(mutex_);
  if (!connected_) return;  // the scale owns the value; nothing to queue for
  if (partner_mode_.load()) {
    if (index > 1 || option_idx > (index == 0 ? 1 : 4) ||
        kPartnerSettings[index].read_only)
      return;
    setting_value_[index] = option_idx;
    setting_grace_until_ms_[index] = now_ms() + kSettingEchoGraceMs;
    if (partner_setting_writer_) partner_setting_writer_(index, option_idx);
    return;
  }
  const uint32_t now = now_ms();
  setting_value_[index] = option_idx;  // optimistic; see on_device_setting
  setting_grace_until_ms_[index] = now + kSettingEchoGraceMs;
  pending_since_ms_[index] = now;  // restarts the debounce on every tap
  pending_setting_[index].store(option_idx);
}

void ScaleLink::request_scan() {
  scanning_.store(true);  // reflect immediately in the UI
  // The arbiter grants the scan to our loop next and preempts whoever holds
  // the radio — including our own connect attempt, which it cancels so the
  // scan runs now, not in ~10 s.
  arb_.request_user_scan(slot_);
}

bool ScaleLink::scanning() const { return scanning_.load(); }

std::vector<ScanResult> ScaleLink::scan_results() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return scan_results_;
}

void ScaleLink::do_scan() {
  // Runs only under the arbiter's kUserScan grant: no other link is
  // connecting or scanning (the host refuses to scan while any connect is
  // pending, which used to return empty scans whenever the Micra sat in
  // "Connecting").
  std::vector<ScanResult> found;
  // 10 s window: the ESP32's radio splits airtime with the live Micra link
  // (and WiFi) while scanning, and sparse advertisers (Varia Aku) slipped
  // through 5 s scans that a phone caught instantly. Pairing is a one-time
  // flow, so the longer wait costs nothing day to day.
  for (const ScanResult& r : ble_.scan(10000)) {
    if (!scale_name_supported(r.name)) {
      // Logged so an unsupported scale's advertised name can be read off the
      // Log page instead of guessed at — new models keep appearing with
      // names the filter doesn't know yet.
      logf("ScaleLink: scan skipped '%s' (%d dBm)\n", r.name, r.rssi);
      continue;
    }
    found.push_back(r);
  }

  {
    std::lock_guard<std::mutex> lk(mutex_);
    scan_results_ = std::move(found);
  }
  scanning_.store(false);
}

}  // namespace core
