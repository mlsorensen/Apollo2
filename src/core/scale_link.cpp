#include "core/scale_link.h"

#include "core/system.h"

namespace {

constexpr uint32_t kReconnectBackoffMs = 3000;
// Connect timeout per address-type attempt. We connect by MAC and don't persist
// the address type, so the transport tries both — a wrong-type attempt at the
// stack's 30 s default would stall the fallback, so cap it low.
constexpr uint32_t kConnectTimeoutMs = 5000;
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

void ScaleLink::set_address(std::string address) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    address_ = std::move(address);
  }
  reconnect_requested_.store(true);
}

void ScaleLink::set_name(std::string name) {
  std::lock_guard<std::mutex> lk(mutex_);
  name_ = name.empty() ? "Scale" : std::move(name);
  // Classify eagerly so features() reflects the saved model before the first
  // connect. do_connect() makes a fresh instance per connection anyway.
  driver_ = make_scale_driver(name_.c_str());
}

void ScaleLink::set_connect_enabled(bool enabled) {
  connect_enabled_.store(enabled);
  reconnect_requested_.store(true);
}

void ScaleLink::set_connected(bool c) {
  std::lock_guard<std::mutex> lk(mutex_);
  connected_ = c;
  if (!c) weight_g_ = 0.0f;
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

void ScaleLink::run() {
  bool connected = false;
  bool retare = false;  // second tare send armed (see the tare block below)
  for (;;) {
    if (scan_requested_.exchange(false)) do_scan();  // works in any state

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

    // Peer link is scanning: hold off on new connect attempts (the host can't
    // scan with a connect pending). Established links are untouched.
    if (!connected && connects_paused_.load()) {
      sleep_ms(200);
      continue;
    }

    if (!connected) {
      retare = false;  // a resend never crosses a link drop: taring whatever
                       // sits on a freshly relinked scale could zero a mid-shot cup
      if (!do_connect(addr)) {
        set_connected(false);
        // Sliced so a scan requested mid-backoff (this link's own Scan button
        // cancels the failed attempt above) runs promptly, not seconds later.
        for (uint32_t waited = 0;
             waited < kReconnectBackoffMs && !scan_requested_.load(); waited += 100) {
          sleep_ms(100);
        }
        continue;
      }
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

bool ScaleLink::do_connect(const std::string& address) {
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

  // Random first: the Bookoo (and most scales) advertise a random address, so the
  // saved-MAC reconnect after reboot then succeeds on the first try (was ~30 s: a
  // PUBLIC-first attempt timed out fully before falling back to RANDOM).
  if (!ble_.connect(address, /*prefer_random=*/true, kConnectTimeoutMs)) {
    logf("ScaleLink: connect failed\n");
    return false;
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

  logf("ScaleLink: %s connected + subscribed\n", drv->model());
  return true;
}

ScaleSnapshot ScaleLink::snapshot() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return ScaleSnapshot{
      .name = name_.c_str(),
      .connected = connected_,
      .weight_g = weight_g_,
      .timer_ms = timer_ms_,
      .battery_valid = battery_valid_,
      .battery_pct = battery_pct_,
      .seq = seq_,
  };
}

ScaleFeatures ScaleLink::features() const {
  std::lock_guard<std::mutex> lk(mutex_);
  if (driver_) return driver_->features();
  return ScaleFeatures{.tare = true,
                       .flow = true,
                       .timer = true,
                       .battery = true,
                       .sleep = false};
}

void ScaleLink::tare() { pending_tare_.store(true); }

int ScaleLink::device_setting_count() const {
  std::lock_guard<std::mutex> lk(mutex_);
  // driver_ is classified eagerly from the saved name (set_name), so the
  // descriptors answer while disconnected — only values need the link.
  return driver_ ? driver_->device_setting_count() : 0;
}

ScaleSettingDesc ScaleLink::device_setting(int index) const {
  std::lock_guard<std::mutex> lk(mutex_);
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
  const uint32_t now = now_ms();
  setting_value_[index] = option_idx;  // optimistic; see on_device_setting
  setting_grace_until_ms_[index] = now + kSettingEchoGraceMs;
  pending_since_ms_[index] = now;  // restarts the debounce on every tap
  pending_setting_[index].store(option_idx);
}

void ScaleLink::request_scan() {
  scanning_.store(true);        // reflect immediately in the UI
  scan_requested_.store(true);  // the loop performs the actual scan
  ble_.cancel_connect();        // if OUR loop is camped in a connect attempt,
                                // unblock it so the scan runs now, not in ~10s
}

bool ScaleLink::scanning() const { return scanning_.load(); }

std::vector<ScanResult> ScaleLink::scan_results() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return scan_results_;
}

void ScaleLink::do_scan() {
  // Take the radio: cancel + pause the peer's (Micra's) connect attempts —
  // the host refuses to scan while any connect is pending, which made scale
  // scans come back empty whenever the Micra sat in "Connecting". The short
  // sleep lets the cancel land before the scan starts.
  if (peer_pause_) { peer_pause_(true); sleep_ms(300); }
  std::vector<ScanResult> found;
  for (const ScanResult& r : ble_.scan(5000)) {
    if (!scale_name_supported(r.name)) {
      // Logged so an unsupported scale's advertised name can be read off the
      // Log page instead of guessed at — new models keep appearing with
      // names the filter doesn't know yet.
      logf("ScaleLink: scan skipped '%s' (%d dBm)\n", r.name, r.rssi);
      continue;
    }
    found.push_back(r);
  }
  if (peer_pause_) peer_pause_(false);

  {
    std::lock_guard<std::mutex> lk(mutex_);
    scan_results_ = std::move(found);
  }
  scanning_.store(false);
}

void ScaleLink::pause_connects(bool on) {
  connects_paused_.store(on);
  // Level-held at the transport: aborts the in-flight attempt AND blocks new
  // ones (incl. the second address-type try inside a cancelled connect()) —
  // a plain cancel left those to grab the radio back before the peer's scan.
  ble_.hold_connects(on);
}

}  // namespace core
