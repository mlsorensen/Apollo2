#include "platform_esp32/config_backup.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "core/semver.h"
#include "core/system.h"
#include "nvs.h"
#include "platform_esp32/board_config.h"
#include "platform_esp32/config.h"
#include "version.h"

namespace platform {

namespace {

constexpr char kPartition[] = "nvs";
constexpr int kFormat = 1;
// Longest value we carry: a WPA2 passphrase is 63, a POSIX TZ string ~40, the
// pairing token ~64. 192 covers those with room; anything longer is skipped
// with a log line rather than silently truncated into a bad setting.
constexpr size_t kMaxValue = 192;
constexpr size_t kMaxLine = 256;  // "str:" + 15-char key + kMaxValue + slack

// Keys that describe THIS UNIT rather than the user's setup: the per-unit
// paddle GPIO repair knobs (they belong to the board with the damaged pad),
// the pending-install boot flag (restoring one would send the machine into
// install mode), the hourly clock seed, and Preferences' own namespace marker.
// Never written to the file, and held back from the erase on restore.
bool is_unit_key(const char* k) {
  static const char* kUnit[] = {"padsense", "paddrive", "otainst", "lastunix",
                                "_init"};
  for (const char* u : kUnit)
    if (std::strcmp(k, u) == 0) return true;
  return false;
}

// The network, in full: without the password the SSID is worse than useless
// (Network::begin joins on ssid non-empty, and an empty PSK is a valid open-
// network attempt that can never succeed), so the enable flag goes too.
bool is_wifi_key(const char* k) {
  return std::strcmp(k, "ssid") == 0 || std::strcmp(k, "wifipass") == 0 ||
         std::strcmp(k, "wifi_en") == 0;
}

// Only the token itself. The saved MAC and name identify the machine, they
// don't open it — a restore without the token still shows the right machine
// and just needs pairing.
bool is_token_key(const char* k) { return std::strcmp(k, "token") == 0; }

const char* type_tag(nvs_type_t t) {
  switch (t) {
    case NVS_TYPE_U8: return "u8";
    case NVS_TYPE_I8: return "i8";
    case NVS_TYPE_U16: return "u16";
    case NVS_TYPE_I16: return "i16";
    case NVS_TYPE_U32: return "u32";
    case NVS_TYPE_I32: return "i32";
    case NVS_TYPE_U64: return "u64";
    case NVS_TYPE_I64: return "i64";
    case NVS_TYPE_STR: return "str";
    case NVS_TYPE_BLOB: return "blob";
    default: return nullptr;  // unknown type: skipped rather than guessed at
  }
}

void hex_encode(const uint8_t* in, size_t n, char* out) {
  static const char* kHex = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i) {
    out[i * 2] = kHex[in[i] >> 4];
    out[i * 2 + 1] = kHex[in[i] & 0x0f];
  }
  out[n * 2] = '\0';
}

int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

size_t hex_decode(const char* in, uint8_t* out, size_t max) {
  size_t n = 0;
  while (in[0] != '\0' && in[1] != '\0' && n < max) {
    const int hi = hex_nibble(in[0]), lo = hex_nibble(in[1]);
    if (hi < 0 || lo < 0) return 0;
    out[n++] = static_cast<uint8_t>((hi << 4) | lo);
    in += 2;
  }
  return n;
}

// One "tag:key=value" line for `key`, or false to skip it (unreadable, an
// unhandled type, or a value longer than we carry).
bool write_entry(FILE* f, nvs_handle_t h, const nvs_entry_info_t& e) {
  const char* tag = type_tag(e.type);
  if (tag == nullptr) return false;
  switch (e.type) {
    case NVS_TYPE_U8: {
      uint8_t v = 0;
      if (nvs_get_u8(h, e.key, &v) != ESP_OK) return false;
      std::fprintf(f, "u8:%s=%u\n", e.key, v);
      return true;
    }
    case NVS_TYPE_I8: {
      int8_t v = 0;
      if (nvs_get_i8(h, e.key, &v) != ESP_OK) return false;
      std::fprintf(f, "i8:%s=%d\n", e.key, v);
      return true;
    }
    case NVS_TYPE_U16: {
      uint16_t v = 0;
      if (nvs_get_u16(h, e.key, &v) != ESP_OK) return false;
      std::fprintf(f, "u16:%s=%u\n", e.key, v);
      return true;
    }
    case NVS_TYPE_I16: {
      int16_t v = 0;
      if (nvs_get_i16(h, e.key, &v) != ESP_OK) return false;
      std::fprintf(f, "i16:%s=%d\n", e.key, v);
      return true;
    }
    case NVS_TYPE_U32: {
      uint32_t v = 0;
      if (nvs_get_u32(h, e.key, &v) != ESP_OK) return false;
      std::fprintf(f, "u32:%s=%lu\n", e.key, static_cast<unsigned long>(v));
      return true;
    }
    case NVS_TYPE_I32: {
      int32_t v = 0;
      if (nvs_get_i32(h, e.key, &v) != ESP_OK) return false;
      std::fprintf(f, "i32:%s=%ld\n", e.key, static_cast<long>(v));
      return true;
    }
    case NVS_TYPE_U64: {
      uint64_t v = 0;
      if (nvs_get_u64(h, e.key, &v) != ESP_OK) return false;
      std::fprintf(f, "u64:%s=%llu\n", e.key, static_cast<unsigned long long>(v));
      return true;
    }
    case NVS_TYPE_I64: {
      int64_t v = 0;
      if (nvs_get_i64(h, e.key, &v) != ESP_OK) return false;
      std::fprintf(f, "i64:%s=%lld\n", e.key, static_cast<long long>(v));
      return true;
    }
    case NVS_TYPE_STR: {
      char buf[kMaxValue];
      size_t len = sizeof(buf);
      if (nvs_get_str(h, e.key, buf, &len) != ESP_OK) return false;
      std::fprintf(f, "str:%s=%s\n", e.key, buf);
      return true;
    }
    default: {  // blob (a float, in practice)
      uint8_t raw[kMaxValue / 2];
      size_t len = sizeof(raw);
      if (nvs_get_blob(h, e.key, raw, &len) != ESP_OK) return false;
      char hex[kMaxValue + 1];
      hex_encode(raw, len, hex);
      std::fprintf(f, "blob:%s=%s\n", e.key, hex);
      return true;
    }
  }
}

// Apply one "tag:key=value" line. False on a malformed line (skipped).
bool apply_line(nvs_handle_t h, char* line) {
  char* colon = std::strchr(line, ':');
  char* eq = std::strchr(line, '=');
  if (colon == nullptr || eq == nullptr || colon > eq) return false;
  *colon = '\0';
  *eq = '\0';
  const char* tag = line;
  const char* key = colon + 1;
  const char* val = eq + 1;
  if (*key == '\0' || std::strlen(key) > 15) return false;  // NVS key limit

  if (std::strcmp(tag, "str") == 0)
    return nvs_set_str(h, key, val) == ESP_OK;
  if (std::strcmp(tag, "blob") == 0) {
    uint8_t raw[kMaxValue / 2];
    const size_t n = hex_decode(val, raw, sizeof(raw));
    return n > 0 && nvs_set_blob(h, key, raw, n) == ESP_OK;
  }
  if (std::strcmp(tag, "u8") == 0)
    return nvs_set_u8(h, key, static_cast<uint8_t>(std::strtoul(val, nullptr, 10))) == ESP_OK;
  if (std::strcmp(tag, "i8") == 0)
    return nvs_set_i8(h, key, static_cast<int8_t>(std::strtol(val, nullptr, 10))) == ESP_OK;
  if (std::strcmp(tag, "u16") == 0)
    return nvs_set_u16(h, key, static_cast<uint16_t>(std::strtoul(val, nullptr, 10))) == ESP_OK;
  if (std::strcmp(tag, "i16") == 0)
    return nvs_set_i16(h, key, static_cast<int16_t>(std::strtol(val, nullptr, 10))) == ESP_OK;
  if (std::strcmp(tag, "u32") == 0)
    return nvs_set_u32(h, key, static_cast<uint32_t>(std::strtoul(val, nullptr, 10))) == ESP_OK;
  if (std::strcmp(tag, "i32") == 0)
    return nvs_set_i32(h, key, static_cast<int32_t>(std::strtol(val, nullptr, 10))) == ESP_OK;
  if (std::strcmp(tag, "u64") == 0)
    return nvs_set_u64(h, key, std::strtoull(val, nullptr, 10)) == ESP_OK;
  if (std::strcmp(tag, "i64") == 0)
    return nvs_set_i64(h, key, std::strtoll(val, nullptr, 10)) == ESP_OK;
  return false;  // unknown tag
}

// The per-unit keys, lifted out before the erase and put back after it.
struct UnitKeys {
  bool has_sense = false, has_drive = false, has_pending = false, has_seed = false;
  int32_t sense = 0, drive = 0;
  int64_t seed = 0;
  char pending[32] = "";
};

void read_unit_keys(nvs_handle_t h, UnitKeys& u) {
  u.has_sense = nvs_get_i32(h, "padsense", &u.sense) == ESP_OK;
  u.has_drive = nvs_get_i32(h, "paddrive", &u.drive) == ESP_OK;
  u.has_seed = nvs_get_i64(h, "lastunix", &u.seed) == ESP_OK;
  size_t len = sizeof(u.pending);
  u.has_pending = nvs_get_str(h, "otainst", u.pending, &len) == ESP_OK;
}

void write_unit_keys(nvs_handle_t h, const UnitKeys& u) {
  if (u.has_sense) nvs_set_i32(h, "padsense", u.sense);
  if (u.has_drive) nvs_set_i32(h, "paddrive", u.drive);
  if (u.has_seed) nvs_set_i64(h, "lastunix", u.seed);
  if (u.has_pending) nvs_set_str(h, "otainst", u.pending);
}

// Strip the trailing newline a fgets() line carries.
void chomp(char* s) {
  size_t n = std::strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
}

}  // namespace

bool settings_backup_write(const char* path, bool include_wifi, bool include_token,
                           int* out_count, std::string* err) {
  nvs_handle_t h;
  esp_err_t rc = nvs_open(kConfigNamespace, NVS_READONLY, &h);
  if (rc != ESP_OK) {
    if (err) *err = "No settings to back up yet.";
    return false;
  }
  FILE* f = std::fopen(path, "w");
  if (f == nullptr) {
    nvs_close(h);
    if (err) *err = "The card could not be written.";
    return false;
  }

  std::fprintf(f, "# Apollo settings backup\nformat=%d\nversion=%s\nboard=%s\n",
               kFormat, fw::kVersion, board::kUpdateSlug);
  std::fprintf(f, "saved=%lld\nwifi=%d\ntoken=%d\n",
               static_cast<long long>(time(nullptr)), include_wifi ? 1 : 0,
               include_token ? 1 : 0);

  int n = 0;
  nvs_iterator_t it = nullptr;
  esp_err_t res = nvs_entry_find(kPartition, kConfigNamespace, NVS_TYPE_ANY, &it);
  while (res == ESP_OK && it != nullptr) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    const bool skip = is_unit_key(info.key) ||
                      (!include_wifi && is_wifi_key(info.key)) ||
                      (!include_token && is_token_key(info.key));
    if (!skip) {
      if (write_entry(f, h, info)) ++n;
      // Only a value longer than kMaxValue (or a type we don't handle) lands
      // here. Say so rather than quietly shipping an incomplete backup.
      else core::logf("Settings: key '%s' skipped (unreadable or too long)\n", info.key);
    }
    res = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  nvs_close(h);

  const bool ok = std::fflush(f) == 0;
  std::fclose(f);
  if (!ok) {
    if (err) *err = "The card ran out of space.";
    return false;
  }
  if (out_count) *out_count = n;
  core::logf("Settings: backed up %d keys to %s (wifi=%d token=%d)\n", n, path,
             include_wifi ? 1 : 0, include_token ? 1 : 0);
  return true;
}

bool settings_backup_read(const char* path, core::BackupInfo* out) {
  FILE* f = std::fopen(path, "r");
  if (f == nullptr) return false;
  char line[kMaxLine];
  int format = 0;
  core::BackupInfo info;
  info.file_present = true;
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    chomp(line);
    if (std::strncmp(line, "format=", 7) == 0) format = std::atoi(line + 7);
    else if (std::strncmp(line, "version=", 8) == 0) info.version = line + 8;
    else if (std::strncmp(line, "board=", 6) == 0) info.board = line + 6;
    else if (std::strncmp(line, "saved=", 6) == 0) info.saved_unix = std::atoll(line + 6);
    else if (std::strncmp(line, "wifi=", 5) == 0) info.has_wifi = line[5] == '1';
    else if (std::strncmp(line, "token=", 6) == 0) info.has_token = line[6] == '1';
    else if (std::strchr(line, ':') != nullptr) break;  // header done
  }
  std::fclose(f);
  if (format != kFormat || info.version.empty()) return false;
  // A backup from a newer build may hold values this firmware's fixed arrays
  // can't index (the NVS rule) — surfaced here, refused in restore.
  info.newer_firmware = core::semver_newer(info.version.c_str(), fw::kVersion);
  if (out) *out = info;
  return true;
}

bool settings_backup_restore(const char* path, int* out_count, std::string* err) {
  core::BackupInfo info;
  if (!settings_backup_read(path, &info)) {
    if (err) *err = "That file isn't an Apollo settings backup.";
    return false;
  }
  if (info.newer_firmware) {
    if (err) *err = "The backup is newer than this firmware.";
    return false;
  }
  FILE* f = std::fopen(path, "r");
  if (f == nullptr) {
    if (err) *err = "The card could not be read.";
    return false;
  }
  nvs_handle_t h;
  if (nvs_open(kConfigNamespace, NVS_READWRITE, &h) != ESP_OK) {
    std::fclose(f);
    if (err) *err = "Settings storage is unavailable.";
    return false;
  }

  // REPLACE, not merge: hold the per-unit keys, wipe the rest, apply the file.
  // A key the file doesn't carry is then absent, and every getter reads its
  // compiled default — so an older backup lands exactly as an upgrade would.
  UnitKeys unit;
  read_unit_keys(h, unit);
  nvs_erase_all(h);
  write_unit_keys(h, unit);

  int n = 0, bad = 0;
  char line[kMaxLine];
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    chomp(line);
    if (line[0] == '\0' || line[0] == '#') continue;
    if (std::strchr(line, ':') == nullptr) continue;  // header line
    // A key this firmware doesn't know is written back anyway: unknown keys are
    // inert here, and carrying them keeps a round trip through an older build
    // from quietly dropping a newer build's settings.
    if (apply_line(h, line)) ++n; else ++bad;
  }
  std::fclose(f);
  const esp_err_t rc = nvs_commit(h);
  nvs_close(h);
  if (rc != ESP_OK) {
    if (err) *err = "Settings could not be written.";
    return false;
  }
  if (out_count) *out_count = n;
  core::logf("Settings: restored %d keys from %s (%d skipped)\n", n, path, bad);
  return true;
}

}  // namespace platform
