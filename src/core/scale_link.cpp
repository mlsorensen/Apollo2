#include "core/scale_link.h"

#include <cctype>
#include <cstring>

#include "core/system.h"

namespace {

// Bookoo Themis Mini. Characteristics are discovered by UUID across all services
// (like MicraLink), so the parent service UUID doesn't matter. Notify = weight
// stream, write = commands.
constexpr char kNotifyUuid[] = "ff11";
constexpr char kWriteUuid[] = "ff12";
constexpr uint8_t kTareCmd[] = {0x03, 0x0a, 0x01, 0x00, 0x00, 0x08};

constexpr uint32_t kReconnectBackoffMs = 3000;
// Connect timeout per address-type attempt. We connect by MAC and don't persist
// the address type, so the transport tries both — a wrong-type attempt at the
// stack's 30 s default would stall the fallback, so cap it low.
constexpr uint32_t kConnectTimeoutMs = 5000;

bool name_has_prefix(const char* name, const char* prefix) {
  const size_t n = std::strlen(prefix);
  if (std::strlen(name) < n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (std::toupper(static_cast<unsigned char>(name[i])) != prefix[i]) return false;
  }
  return true;
}

// Decode a 20-byte Bookoo Themis notification (per goscale themis/comms):
//   [2..4] ms timer (24-bit BE); [6] sign ('-'=neg); [7..9] weight 24-bit BE /100;
//   [13] battery %. (The UI derives flow rate from the weight stream, so we don't
//   decode any flow field here.)
void decode_and_publish(core::ScaleLink* link, const uint8_t* d, size_t len) {
  if (d == nullptr || len < 20) return;
  const uint32_t ms =
      (static_cast<uint32_t>(d[2]) << 16) | (static_cast<uint32_t>(d[3]) << 8) | d[4];
  const int sign = (d[6] == 0x2D) ? -1 : 1;  // 0x2D == '-'
  const uint32_t raw_w =
      (static_cast<uint32_t>(d[7]) << 16) | (static_cast<uint32_t>(d[8]) << 8) | d[9];
  const float weight = sign * static_cast<float>(raw_w) / 100.0f;
  const int batt = d[13];
  link->publish_sample(weight, ms, batt);
}

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
}

void ScaleLink::set_connect_enabled(bool enabled) {
  connect_enabled_.store(enabled);
  reconnect_requested_.store(true);
}

void ScaleLink::set_connected(bool c) {
  std::lock_guard<std::mutex> lk(mutex_);
  connected_ = c;
  if (!c) weight_g_ = 0.0f;
}

void ScaleLink::publish_sample(float weight_g, uint32_t timer_ms, int battery_pct) {
  std::lock_guard<std::mutex> lk(mutex_);
  weight_g_ = weight_g;
  timer_ms_ = timer_ms;
  battery_pct_ = battery_pct;
  battery_valid_ = true;
}

void ScaleLink::run() {
  bool connected = false;
  for (;;) {
    if (scan_requested_.exchange(false)) do_scan();  // works in any state

    std::string addr;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      addr = address_;
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
      if (!do_connect(addr)) {
        set_connected(false);
        sleep_ms(kReconnectBackoffMs);
        continue;
      }
      connected = true;
      set_connected(true);
    }

    if (!ble_.connected()) {
      connected = false;
      set_connected(false);
      continue;
    }

    if (pending_tare_.exchange(false)) {
      ble_.write(kWriteUuid, kTareCmd, sizeof(kTareCmd), /*with_response=*/true);
    }

    sleep_ms(100);  // weight arrives async via the notify callback
  }
}

bool ScaleLink::do_connect(const std::string& address) {
  // Random first: the Bookoo (and most scales) advertise a random address, so the
  // saved-MAC reconnect after reboot then succeeds on the first try (was ~30 s: a
  // PUBLIC-first attempt timed out fully before falling back to RANDOM).
  if (!ble_.connect(address, /*prefer_random=*/true, kConnectTimeoutMs)) {
    logf("ScaleLink: connect failed\n");
    return false;
  }

  if (!ble_.has_characteristic(kNotifyUuid)) {
    logf("ScaleLink: notify characteristic not found\n");
    ble_.disconnect();
    return false;
  }

  ScaleLink* self = this;
  ble_.subscribe(kNotifyUuid, [self](const uint8_t* data, size_t len) {
    decode_and_publish(self, data, len);
  });

  logf("ScaleLink: connected + subscribed\n");
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
  };
}

ScaleFeatures ScaleLink::features() const {
  return ScaleFeatures{
      .tare = true, .flow = true, .timer = true, .battery = true, .beep = false};
}

void ScaleLink::tare() { pending_tare_.store(true); }

void ScaleLink::request_scan() {
  scanning_.store(true);        // reflect immediately in the UI
  scan_requested_.store(true);  // the loop performs the actual scan
}

bool ScaleLink::scanning() const { return scanning_.load(); }

std::vector<ScanResult> ScaleLink::scan_results() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return scan_results_;
}

void ScaleLink::do_scan() {
  std::vector<ScanResult> found;
  for (const ScanResult& r : ble_.scan(5000)) {
    if (!name_has_prefix(r.name, "BOOKOO")) continue;  // Themis advertises "BOOKOO..."
    found.push_back(r);
  }

  {
    std::lock_guard<std::mutex> lk(mutex_);
    scan_results_ = std::move(found);
  }
  scanning_.store(false);
}

}  // namespace core
