#include "core/micra_link.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/system.h"

namespace {

// GATT characteristic UUIDs (from pylamarzocco). The parent service UUID isn't
// published, so we discover characteristics by UUID across all services.
constexpr char kReadUuid[] = "0a0b7847-e12b-09a8-b04b-8e0922a9abab";   // state
constexpr char kWriteUuid[] = "0b0b7847-e12b-09a8-b04b-8e0922a9abab";  // command
constexpr char kAuthUuid[] = "0d0b7847-e12b-09a8-b04b-8e0922a9abab";   // auth
// Present ONLY while the machine is in configuration/pairing mode (where the auth
// char above is absent). Used to tell "machine in config mode, needs a restart"
// apart from an ordinary unreachable/failed connect.
constexpr char kConfigSeedUuid[] = "0c0b7847-e12b-09a8-b04b-8e0922a9abab";

// Standard Device Information Service (0x180A) string characteristics.
constexpr char kDisManufacturer[] = "2a29";
constexpr char kDisModel[] = "2a24";
constexpr char kDisSerial[] = "2a25";
constexpr char kDisFirmware[] = "2a26";
constexpr char kDisSoftware[] = "2a28";

constexpr uint32_t kReconnectBackoffMs = 3000;
// Per address-type attempt. A present, advertising Micra connects in ~1-2s;
// the retry loop covers the rest, so failing fast beats camping on the radio —
// every try holds the shared radio (core::RadioArbiter), and a 30s value here
// made Settings scans unusable while "Connecting".
constexpr uint32_t kConnectTimeoutMs = 8000;

bool write_with_nul(core::ble::ICentral& ble, const char* uuid,
                    const std::string& payload) {
  std::string buf = payload;
  buf.push_back('\0');
  return ble.write(uuid, buf.data(), buf.size(), /*with_response=*/true);
}

}  // namespace

namespace core {

bool MicraLink::read_setting(const char* name, std::string& out) {
  if (!write_with_nul(ble_, kReadUuid, name)) return false;
  sleep_ms(150);  // let the machine replace the characteristic value with its answer
  if (!ble_.read(kReadUuid, out)) return false;
  return !out.empty();
}

void MicraLink::set_address(std::string address, AddrType type) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    address_ = std::move(address);
    addr_type_ = type;
  }
  token_bad_.store(false);  // new machine -> give its token a fresh chance
  reconnect_requested_.store(true);
  arb_.abort_slot(RadioSlot::kMicra);  // a try for the OLD address must not run out
}

void MicraLink::set_token(std::string token) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    token_ = std::move(token);
  }
  token_bad_.store(false);  // a new token deserves a fresh attempt
  reconnect_requested_.store(true);
}

void MicraLink::set_connect_enabled(bool enabled) {
  connect_enabled_.store(enabled);
  reconnect_requested_.store(true);  // nudge the loop to act now (drop or reconnect)
  if (!enabled) arb_.abort_slot(RadioSlot::kMicra);  // Disconnect lands now
}

void MicraLink::set_name(std::string name) {
  std::lock_guard<std::mutex> lk(mutex_);
  name_ = name.empty() ? "Micra" : std::move(name);
}

void MicraLink::set_link(Link link) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (link != link_) {
    // Silent drops matter: the paddle's wake-vs-shot decision needs Connected,
    // and a stale power_ + dropped link reads as "shot path" at the next flip.
    static const char* const kNames[] = {"Unconfigured", "NeedsToken", "Disconnected",
                                         "Connecting", "Connected"};
    logf("MicraLink: link %s -> %s\n", kNames[static_cast<int>(link_)],
         kNames[static_cast<int>(link)]);
  }
  link_ = link;
}

void MicraLink::run() {
  bool connected = false;
  uint32_t last_refresh = 0;
  // The arbiter tracks the raw link so it knows when this slot wants the
  // radio; every edge of `connected` goes through here.
  auto mark = [&](bool up) {
    connected = up;
    arb_.set_connected(RadioSlot::kMicra, up);
  };

  for (;;) {
    std::string addr;
    std::string token;
    AddrType addr_type;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      addr = address_;
      token = token_;
      addr_type = addr_type_;
    }
    arb_.set_target(RadioSlot::kMicra, addr, addr_type);
    arb_.set_wanted(RadioSlot::kMicra, !addr.empty() && !token.empty() &&
                                           !token_bad_.load() && connect_enabled_.load());

    // The user's Settings scan: granted by the arbiter (it preempts whoever
    // holds the radio, including this link's own connect attempt), run here
    // in any link state.
    if (arb_.user_scan_pending(RadioSlot::kMicra)) {
      const RadioGrant g = arb_.poll(RadioSlot::kMicra, now_ms());
      if (g.work == RadioWork::kUserScan) {
        do_scan();
        arb_.release(RadioSlot::kMicra, g.ticket, ReleaseResult::kScanDone, now_ms());
      } else {
        sleep_ms(100);
        continue;
      }
    }

    // Not fully provisioned: idle in the matching state (don't spin on connect).
    if (addr.empty()) {
      if (connected) { ble_.disconnect(); mark(false); }
      set_link(Link::Unconfigured);
      sleep_ms(500);
      continue;
    }
    if (token.empty()) {
      if (connected) { ble_.disconnect(); mark(false); }
      // No token: the machine can't be read for one (the cloud-issued token is
      // obtained out-of-band, e.g. via lmtoken). Idle in NeedsToken until one is
      // entered (set_token clears this).
      set_link(Link::NeedsToken);
      sleep_ms(500);
      continue;
    }

    // Address changed (e.g. user saved a new MAC): drop the old connection.
    // Token authed at the BLE level but reads were rejected: it's a bad token,
    // not a flaky link. Idle in NeedsToken (don't hammer the machine) until a new
    // token arrives (set_token / set_address clear this).
    if (token_bad_.load()) {
      if (connected) { ble_.disconnect(); mark(false); }
      set_link(Link::NeedsToken);
      sleep_ms(500);
      continue;
    }

    // User disconnected: drop the link and idle (don't auto-reconnect) so another
    // remote can take the Micra's single BLE slot.
    if (!connect_enabled_.load()) {
      if (connected) { ble_.disconnect(); mark(false); }
      set_link(Link::Disconnected);
      sleep_ms(500);
      continue;
    }

    if (reconnect_requested_.exchange(false) && connected) {
      ble_.disconnect();
      mark(false);
    }

    if (!connected) {
      set_link(Link::Connecting);  // still trying, even while yielding the radio
      // Ask the arbiter: another link may hold the radio (a scale's connect
      // or finder chunk, a Settings scan), or the setup portal may have
      // parked us. Established connections are never touched by any of it.
      const RadioGrant g = arb_.poll(RadioSlot::kMicra, now_ms());
      if (g.work != RadioWork::kDirectConnect) {
        sleep_ms(100);
        continue;
      }
      const bool ok = do_connect(addr, token, g.addr_type);
      const bool preempted = arb_.was_preempted(RadioSlot::kMicra, g.ticket);
      arb_.release(RadioSlot::kMicra, g.ticket,
                   ok ? ReleaseResult::kConnected
                      : preempted ? ReleaseResult::kPreempted : ReleaseResult::kFailed,
                   now_ms());
      if (!ok) {
        set_link(Link::Disconnected);  // unreachable -> back off and retry
        if (preempted) continue;       // a yielded try is not a failed one
        // Sliced so a Scan tapped mid-backoff runs promptly, not seconds later.
        for (uint32_t waited = 0; waited < kReconnectBackoffMs &&
                                  !arb_.user_scan_pending(RadioSlot::kMicra);
             waited += 100) {
          sleep_ms(100);
        }
        continue;
      }
      // Connected over BLE; the auth WRITE always "succeeds", so verify the token
      // really works with a read. Retry a few times to ride out a transient glitch
      // before concluding the token is bad.
      bool verified = false;
      for (int i = 0; i < 3 && !verified; ++i) {
        if (do_refresh()) verified = true;  // also seeds temps before Connected
        else sleep_ms(300);
      }
      if (!verified) {
        logf("MicraLink: token rejected (reads failed) -> NeedsToken\n");
        token_bad_.store(true);
        ble_.disconnect();
        mark(false);
        set_link(Link::NeedsToken);
        continue;
      }
      mark(true);
      set_link(Link::Connected);
      last_refresh = now_ms();
    }

    if (!ble_.connected()) {
      mark(false);
      set_link(Link::Disconnected);
      continue;
    }

    const int p = pending_power_.exchange(-1);
    if (p >= 0) do_set_power(p == 1);
    const int b = pending_brew_tenths_.exchange(-1);
    if (b >= 0) {
      char v[8];
      std::snprintf(v, sizeof(v), "%.1f", b / 10.0f);
      do_set_boiler_target("CoffeeBoiler1", v);
    }
    const int st = pending_steam_whole_.exchange(-1);
    if (st >= 0) {
      char v[8];
      std::snprintf(v, sizeof(v), "%d", st);
      do_set_boiler_target("SteamBoiler", v);
    }
    const int se = pending_steam_enable_.exchange(-1);
    if (se >= 0) { do_set_steam_enabled(se == 1); }
    // No instant read-back after a command: the machine's machineMode answer
    // lags the command by a beat, so an immediate read reliably returns the
    // OLD state (observed on HW). The regular cadence picks up the settled
    // answer within ~one interval instead.
    if (now_ms() - last_refresh > poll_interval_ms_.load()) {
      do_refresh();  // a failed read just means we re-detect the drop next loop
      last_refresh = now_ms();
    }

    sleep_ms(100);
  }
}

bool MicraLink::do_connect(const std::string& address, const std::string& token,
                           AddrType type) {
  // A learned type is one try; unknown tries both, public first (Micras
  // advertise a public address more often than not).
  if (!ble_.connect(address, type, /*prefer_random=*/false, kConnectTimeoutMs)) {
    logf("MicraLink: connect failed (%s)\n",
         type != AddrType::kUnknown ? addr_type_name(type) : "public+random");
    return false;
  }
  // Learn the type the connect used so later connects are a single try (a
  // two-type try holds the shared radio for 16 s). Compared with the stored
  // type, persisted by the platform next to the MAC.
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
    logf("MicraLink: learned address type %s\n", addr_type_name(used));
    if (addr_type_learned_) addr_type_learned_(used);
  }

  if (!ble_.has_characteristic(kReadUuid) || !ble_.has_characteristic(kWriteUuid) ||
      !ble_.has_characteristic(kAuthUuid)) {
    // Configuration/pairing mode presents the seed char (0c) but NOT the operative
    // auth char (0d): flag it so the UI can tell the user to restart the machine,
    // instead of looping on an opaque "Disconnected".
    if (ble_.has_characteristic(kConfigSeedUuid)) {
      logf("MicraLink: machine in configuration mode (needs restart)\n");
      config_mode_seq_.fetch_add(1, std::memory_order_relaxed);
    } else {
      logf("MicraLink: missing characteristics\n");
    }
    ble_.disconnect();
    return false;
  }

  // Authenticate: token as raw UTF-8, NO trailing NUL, write-with-response.
  if (!ble_.write(kAuthUuid, token.data(), token.size(), /*with_response=*/true)) {
    logf("MicraLink: auth write failed\n");
    ble_.disconnect();
    return false;
  }

  // Read the Device Information Service strings once (read-only standard chars).
  {
    std::lock_guard<std::mutex> lk(mutex_);
    ble_.read(kDisManufacturer, dis_manufacturer_);
    ble_.read(kDisModel, dis_model_);
    ble_.read(kDisSerial, dis_serial_);
    ble_.read(kDisFirmware, dis_firmware_);
    ble_.read(kDisSoftware, dis_software_);
  }

  logf("MicraLink: connected + authenticated\n");
  return true;
}

bool MicraLink::do_refresh() {
  std::string mode_json;
  std::string boilers_json;
  if (!read_setting("machineMode", mode_json)) return false;
  if (!read_setting("boilers", boilers_json)) return false;

  // An authenticated machine answers with parseable JSON; a rejected token
  // yields a 1-byte NUL / garbage. Unparseable reads => NOT really authenticated,
  // so this doubles as the token check that gates Connected.
  JsonDocument mode_doc;
  if (deserializeJson(mode_doc, mode_json)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, boilers_json) || !doc.is<JsonArray>()) return false;

  const char* mode = mode_doc.as<const char*>();
  const Power power = (mode != nullptr && std::strcmp(mode, "BrewingMode") == 0)
                          ? Power::On
                          : Power::Standby;

  float brew_c = 0, brew_t = 0, boiler_c = 0, boiler_t = 0;
  bool steam_en = true;
  for (JsonObject b : doc.as<JsonArray>()) {
    const char* id = b["id"] | "";
    const float current = b["current"] | 0.0f;
    const float target = b["target"] | 0.0f;
    if (std::strcmp(id, "CoffeeBoiler1") == 0) { brew_c = current; brew_t = target; }
    else if (std::strcmp(id, "SteamBoiler") == 0) {
      boiler_c = current;
      boiler_t = target;
      steam_en = b["isEnabled"] | true;
    }
  }

  // Commit the parsed values to the shared cache under lock.
  std::lock_guard<std::mutex> lk(mutex_);
  if (power != power_) {
    // Power transitions are load-bearing for the paddle's wake-vs-shot
    // decision (BrewController standby provider) — log the raw mode string.
    logf("MicraLink: mode '%s' -> power %s\n", mode != nullptr ? mode : "(null)",
         power == Power::On ? "On" : "Standby");
  }
  power_ = power;
  brew_temp_c_ = brew_c;
  brew_target_c_ = brew_t;
  boiler_temp_c_ = boiler_c;
  boiler_target_c_ = boiler_t;
  steam_enabled_ = steam_en;
  return true;
}

void MicraLink::do_set_power(bool on) {
  const std::string json =
      on ? R"({"name":"MachineChangeMode","parameter":{"mode":"BrewingMode"}})"
         : R"({"name":"MachineChangeMode","parameter":{"mode":"StandBy"}})";
  write_with_nul(ble_, kWriteUuid, json);
}

MachineSnapshot MicraLink::snapshot() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return MachineSnapshot{
      .name = name_.c_str(),
      .manufacturer = dis_manufacturer_.c_str(),
      .model = dis_model_.c_str(),
      .serial = dis_serial_.c_str(),
      .firmware = dis_firmware_.c_str(),
      .software = dis_software_.c_str(),
      .link = link_,
      .power = power_,
      .brew_temp_c = brew_temp_c_,
      .brew_target_c = brew_target_c_,
      .boiler_temp_c = boiler_temp_c_,
      .boiler_target_c = boiler_target_c_,
      .steam_enabled = steam_enabled_,
      .brewing = brewing_,
      .config_mode_seq = config_mode_seq_.load(std::memory_order_relaxed),
  };
}

void MicraLink::set_power(bool on) { pending_power_.store(on ? 1 : 0); }

void MicraLink::set_brew_target(float celsius) {
  pending_brew_tenths_.store(static_cast<int>(lroundf(celsius * 10.0f)));
}

void MicraLink::set_steam_target(float celsius) {
  pending_steam_whole_.store(static_cast<int>(lroundf(celsius)));
}

void MicraLink::set_steam_enabled(bool enabled) {
  pending_steam_enable_.store(enabled ? 1 : 0);
}

void MicraLink::do_set_boiler_target(const char* identifier, const char* value) {
  std::string json =
      std::string(R"({"name":"SettingBoilerTarget","parameter":{"identifier":")") +
      identifier + R"(","value":)" + value + "}}";
  write_with_nul(ble_, kWriteUuid, json);
}

void MicraLink::do_set_steam_enabled(bool enabled) {
  std::string json =
      std::string(R"({"name":"SettingBoilerEnable","parameter":{"identifier":"SteamBoiler","state":)") +
      (enabled ? "true" : "false") + "}}";
  write_with_nul(ble_, kWriteUuid, json);
}

void MicraLink::request_scan() {
  scanning_.store(true);  // reflect immediately in the UI
  // The arbiter grants the scan to our loop next and preempts whoever holds
  // the radio — including our own connect attempt, which it cancels.
  arb_.request_user_scan(RadioSlot::kMicra);
}

bool MicraLink::scanning() const { return scanning_.load(); }

std::vector<ScanResult> MicraLink::scan_results() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return scan_results_;
}

void MicraLink::do_scan() {
  // Runs only under the arbiter's kUserScan grant: no other link is
  // connecting or scanning (the host refuses to scan while a connect is
  // pending — a scan during a scale's connect window used to return nothing).
  std::vector<ScanResult> found;
  // 10 s, matching the scale's window and for the same reason (see
  // ScaleLink::do_scan): the radio splits airtime with any LIVE link and with
  // WiFi while scanning, so a 5 s window can miss advertisers a phone finds
  // instantly. peer_pause_ above cancels the peer's pending CONNECTS but
  // deliberately leaves an established link up, so its airtime cost remains.
  // Pairing is a one-time flow; the extra 5 s costs nothing day to day.
  //
  // NOT evidence-backed: raised on symmetry with the scale, not on a measured
  // miss. The 2026-09-10 "can't see the Micra" report that prompted it turned
  // out to be unrelated -- another remote had taken the machine's SINGLE BLE
  // slot, so it had stopped advertising and no scan length would have found it.
  for (const ScanResult& r : ble_.scan(10000)) {
    if (std::strncmp(r.name, "MICRA", 5) != 0) continue;  // La Marzocco name prefix
    found.push_back(r);
  }

  {
    std::lock_guard<std::mutex> lk(mutex_);
    scan_results_ = std::move(found);
  }
  scanning_.store(false);
}

}  // namespace core
