#include "platform_esp32/update_check.h"

#include <Arduino.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <mbedtls/platform.h>

#include <cstdio>
#include <cstring>

#include "core/network.h"
#include "core/system.h"
#include "platform_esp32/board_config.h"
#include "platform_esp32/config.h"
#include "platform_esp32/network.h"
#include "version.h"

namespace platform {
namespace {

constexpr char kSiteBase[] = "https://mlsorensen.github.io/Apollo2";
constexpr size_t kMaxNotesBytes = 4096;  // bound RAM; notes truncate past this

// PREALLOCATED worker task. Both the stack and the TCB are static, so creating
// this task takes NOTHING from the heap and can never fail for want of a
// contiguous block. That is the whole point: internal largest-free falls from
// ~131K just after boot to ~21K in steady state (measured 4.3C 2026-09-09), and
// the old code asked for a fresh 12KB contiguous stack on every check — the
// largest single request in the system, made at the moment it was least likely
// to be satisfiable. Statically reserving it from .bss trades 12KB held always
// for a check that can't be starved out by fragmentation.
//
// The task is created ONCE and then parks on a notification for the life of the
// process. It must never be deleted and re-created: a re-create would race the
// idle task's cleanup over these very buffers. (One UpdateCheck instance
// exists — main's g_update_check — so file scope matches its lifetime.)
// SIZED FROM MEASUREMENT (4.3C, 2026-09-09): a real check reported stack_hw=8252
// of 12288, i.e. a peak use of 4036 bytes. 8KB leaves ~2x headroom over that for
// deeper error/alert paths in the TLS handshake, and still hands 4KB back versus
// the old 12KB. Re-check stack_hw after any change to the fetch path.
// 6K: measured peak 4,036 bytes across completed checks, kept at ~2K margin
// rather than the ~1K the other trims run at. The TLS handshake is the deep
// part here and its depth varies with the SERVER's certificate chain, which is
// outside our control and can change without any change on our side. Re-check
// stack_hw after any change to the fetch path.
constexpr uint32_t kCheckStackBytes = 6 * 1024;
StackType_t s_check_stack[kCheckStackBytes];  // StackType_t is uint8_t here
StaticTask_t s_check_tcb;
TaskHandle_t s_check_task = nullptr;

// Smallest largest-free INTERNAL block a check may start with. On the S3 the
// WiFi driver's RX buffers can ONLY come from that pool (the stock core config
// leaves CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP off), and a check piles its task
// stack plus esp-http-client/esp-tls internals on top. Once the largest
// free block drops below what an RX buffer needs, the driver silently discards
// every INBOUND frame — ARP, DNS, ICMP, TCP SYN alike — so the device falls off
// the network entirely until the check finally gives up (measured 2026-09-09 on
// the 4.3C: a boot check ran the pool to 9236 free / 2804 largest and blacked
// the board out for ~90s). Skipping a check beats taking the radio down with it.
//
// MEASURED (4.3C, 2026-09-09): steady-state largest-free with WiFi + NimBLE up
// is ~21K. Two earlier cuts were wrong in the same direction — 24K then 12K sat
// at or above what the FETCH actually needs, and skipped every check.
//
// With the task stack preallocated (above), this guard no longer has to cover
// it. It only has to cover what the fetch itself allocates, and a real run
// measured that at ~2KB (free 17172 -> 16416, largest 9204 -> 7156). 8K is ~4x
// that, well under the ~21K steady state, and still a floor low enough that a
// fetch can't chew into the WiFi driver's RX pool and take ARP/DNS/ICMP with it.
// Note what this guard is NOT: it cannot see a fetch that fails and retries for
// 85s. That failure mode is handled by the NTP gate (only check a proven
// network) plus the preallocated stack, not by this number.
// SET FROM MEASUREMENT (4.3C, 2026-09-09), after three earlier cuts (24K, 12K,
// 8K) all sat ABOVE the real operating point and silently refused every check
// past the first.
//
// A fetch permanently drops the largest free block from ~12.8K (pristine, first
// boot check) to a ~7.2K plateau, and thereafter jitters 5.6K-7.7K. That plateau
// is NOT a leak: four consecutive fetches left it exactly where one did. And
// fetches SUCCEED from it -- three runs starting at 7412/7668/7156 all completed
// in ~1s, dipping transiently to 3444 at worst, with the device staying
// responsive and on the network throughout.
//
// So this floor only has to reject states genuinely tighter than that plateau.
// 6K admits the normal ~7.2K operating point and still refuses the degraded
// ~5.6K excursions, where the same ~3.7K dip would reach the ~2.8K level that
// collapsed the RX pool in the original bug. Note it was 85 SECONDS at that
// depth that killed the network, not the depth alone; the NTP gate keeps a fetch
// to ~1s.
constexpr size_t kMinInternalLargest = 6 * 1024;

// Parse "v1.2.3" or "1.2.3" into a comparable triple. Returns false on
// anything that isn't strict major.minor.patch — callers treat that as
// "not newer" (fail quiet, try again next round).
bool parse_semver(const char* s, int out[3]) {
  if (*s == 'v') ++s;
  return std::sscanf(s, "%d.%d.%d", &out[0], &out[1], &out[2]) == 3;
}

// True iff a is numerically newer than b (major, then minor, then patch).
// NEVER compare version strings: "0.9.0" > "0.10.0" lexicographically.
bool semver_newer(const char* a, const char* b) {
  int va[3], vb[3];
  if (!parse_semver(a, va) || !parse_semver(b, vb)) return false;
  for (int i = 0; i < 3; ++i) {
    if (va[i] != vb[i]) return va[i] > vb[i];
  }
  return false;
}

// Pull the first "vX.Y.Z" out of releases.json (a JSON array, newest first).
// A full JSON parse is overkill for ["v0.10.0", ...] — scan for the first
// quoted token that parses as a version.
std::string first_version(const std::string& body) {
  int start = -1;
  for (int i = 0; i < static_cast<int>(body.size()); ++i) {
    if (body[i] != '"') continue;
    if (start < 0) {
      start = i + 1;
    } else {
      std::string tok(body.data() + start, i - start);
      int v[3];
      if (parse_semver(tok.c_str(), v)) return tok;
      start = -1;
    }
  }
  return {};
}

// ROOT-CAUSE FIX (measured 2026-09-07): the prebuilt core forces mbedTLS to
// allocate from internal RAM (MBEDTLS_INTERNAL_MEM_ALLOC), so TLS's ~46KB of
// record buffers land in the scarce internal-DMA pool the hosted C6 radio's
// SDIO driver also uses — and starve it (sdio_rx_get_buffer assert). Pointing
// mbedTLS at PSRAM at runtime moves those buffers out, so TLS and BLE coexist
// (handshake DMA-free 29K vs the 6K that used to panic). rc=0 on this core.
void* psram_calloc(size_t n, size_t sz) {
  return heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM);
}
// Point mbedTLS at PSRAM so TLS's ~46KB of record buffers don't land in the
// scarce internal-DMA pool the hosted C6 radio's SDIO driver shares and starve
// it — lets the check's TLS fetch run with BLE up. Idempotent.
void use_psram_for_tls() {
  static bool done = false;
  if (done) return;
  done = true;
  if (mbedtls_platform_set_calloc_free(psram_calloc, free) != 0)
    core::logf("UpdateCheck: WARNING mbedTLS PSRAM allocator refused\n");
}

// Small authenticated GET into a bounded string. Trust = the cert bundle
// embedded in the core libs (only reachable through the IDF client config's
// crt_bundle_attach — the Arduino wrapper can't use the built-in bundle).
bool https_get(const std::string& url, std::string& out, size_t cap) {
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 10000;
  esp_http_client_handle_t h = esp_http_client_init(&cfg);
  if (h == nullptr) return false;
  bool ok = false;
  if (esp_http_client_open(h, 0) == ESP_OK) {
    esp_http_client_fetch_headers(h);
    if (esp_http_client_get_status_code(h) == 200) {
      char buf[512];
      int n;
      while ((n = esp_http_client_read(h, buf, sizeof(buf))) > 0) {
        const size_t room = cap - out.size();
        out.append(buf, std::min(static_cast<size_t>(n), room));
        if (out.size() >= cap) break;
      }
      ok = true;
    }
    esp_http_client_close(h);
  }
  esp_http_client_cleanup(h);
  return ok;
}

// Retry wrapper: a `sock < 0` (Connection failed) from https_get is transient —
// the previous fetch's TCP socket can still be in TIME_WAIT and the small LWIP
// socket pool (shared with BLE, the web server and NTP) briefly has none free.
// A short backoff lets it clear. Fixes intermittent empty release notes and
// "couldn't contact the update server" on the native-WiFi S3 boards.
bool https_get_retry(const std::string& url, std::string& out, size_t cap,
                     int tries) {
  for (int attempt = 1; attempt <= tries; ++attempt) {
    out.clear();
    if (https_get(url, out, cap)) return true;
    if (attempt < tries) {
      core::logf("UpdateCheck: fetch attempt %d/%d failed, retrying\n", attempt,
                 tries);
      delay(700);
    }
  }
  return false;
}

}  // namespace

UpdateCheck::UpdateCheck(Config& config, Network& network)
    : config_(config), network_(network) {}

// "Is the internet actually reachable?" — the gate every check goes through.
// It must key off a REAL SNTP reply, not ntp_synced(): on the RTC boards
// (4.3B/4.3C PCF85063) the coin-cell clock hands the system a plausible year at
// boot, which satisfies ntp_synced()'s "year looks real" test about a second
// after DHCP — long before DNS works. The boot check then fired into a half-up
// network, and its retry storm ran the internal heap down far enough to starve
// the WiFi driver's RX pool, taking ARP/DNS/ICMP/TCP down with it for ~90s.
// v0.11.3 fixed this same false positive for the Info page's readout but left
// this gate on the old signal; the P4 boards have no discrete RTC, which is the
// only reason they never showed the bug. A landed SNTP sync is proof that DNS
// and a UDP round-trip both work.
bool UpdateCheck::network_ready() const {
  return network_.status() == core::NetState::Connected &&
         network_.ntp_seconds_since_sync() >= 0;
}

bool UpdateCheck::take_check_request() { return check_req_.exchange(false); }

void UpdateCheck::begin_check() {
  if (in_flight_.load()) return;
  if (!network_ready()) {
    core::logf("UpdateCheck: no WiFi/NTP; check skipped\n");
    return;  // no network -> the whole feature is inert
  }
  // NOTE (2026-09-09): this reads plain INTERNAL, which also counts 32-bit-only
  // IRAM that no ordinary allocation can use — it reads ~5.6KB HIGH versus the
  // 8BIT figure the telltale reports (13812 vs 8180, same moment). So this gate
  // is more permissive than it looks and should move to MALLOC_CAP_INTERNAL |
  // MALLOC_CAP_8BIT once the logged pairs give a threshold to re-derive from.
  // Left on the old mask for now so the verified boot-check path is unchanged
  // while we gather the numbers.
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  if (largest < kMinInternalLargest) {
    core::logf("UpdateCheck: internal RAM tight (largest=%u < %u); check skipped\n",
               static_cast<unsigned>(largest),
               static_cast<unsigned>(kMinInternalLargest));
    return;  // a fetch here would starve the WiFi driver's RX pool
  }
  // First call brings the worker up; it then lives forever, parked on its
  // notification. Static buffers -> this allocates nothing and won't fail.
  if (s_check_task == nullptr) {
    s_check_task = xTaskCreateStaticPinnedToCore(check_task_entry, "updchk",
                                                 kCheckStackBytes, this, 1,
                                                 s_check_stack, &s_check_tcb, 0);
    if (s_check_task == nullptr) {
      core::logf("UpdateCheck: worker task create failed\n");
      return;
    }
  }
  in_flight_.store(true);
  xTaskNotifyGive(s_check_task);  // hand the request to the parked worker
}

// Long-lived worker. Parks on a notification between checks — never deleted, so
// its static stack/TCB are never re-entered while the kernel is still tearing a
// previous instance down.
void UpdateCheck::check_task_entry(void* arg) {
  auto* self = static_cast<UpdateCheck*>(arg);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // wait for begin_check()
    // Bracket the check with internal-heap figures: the fetch is the heaviest
    // transient load on that pool, and every starvation bug here has surfaced
    // as a SILENT downstream failure (see kMinInternalLargest). The start/done
    // delta is exactly what that threshold should be derived from.
    core::logf(
        "UpdateCheck: check start (8bit free=%u largest=%u | anycap largest=%u)\n",
        static_cast<unsigned>(
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    self->run_check();
    // stack_hw = bytes of this stack never touched; feeds kCheckStackBytes.
    core::logf(
        "UpdateCheck: check done (8bit free=%u largest=%u | anycap largest=%u | "
        "stack_hw=%u)\n",
        static_cast<unsigned>(
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    self->in_flight_.store(false);
    self->seq_.fetch_add(1);  // wake the UI: a check finished
  }
}

void UpdateCheck::run_check() {
  last_ok_.store(false);  // set true only once we've actually read a version
  use_psram_for_tls();
  std::string body;
  if (!https_get_retry(std::string(kSiteBase) + "/releases.json", body, 2048,
                       3)) {
    core::logf("UpdateCheck: releases.json fetch failed\n");
    return;
  }
  const std::string latest = first_version(body);
  if (latest.empty()) {
    core::logf("UpdateCheck: no version in releases.json\n");
    return;
  }

  std::string notes;
  bool got_notes = false;
  if (semver_newer(latest.c_str(), fw::kVersion)) {
    // Release notes are best-effort (older releases predate notes.txt), but
    // retry so a transient socket failure doesn't leave the notice blank.
    got_notes = https_get_retry(
        std::string(kSiteBase) + "/" + latest + "/notes.txt", notes,
        kMaxNotesBytes, 3);
    core::logf("UpdateCheck: v%s running, %s available\n", fw::kVersion,
               latest.c_str());
  }

  {
    std::lock_guard<std::mutex> lock(mu_);
    const bool same_version = (latest_ == latest);
    latest_ = latest;
    // Don't let a transient notes-fetch failure blank out notes we already have
    // for this same version; a newer version always replaces them.
    if (got_notes || !same_version) notes_ = notes;
  }
  last_ok_.store(true);  // reached the server and read a version
}

core::UpdateInfo UpdateCheck::info() const {
  core::UpdateInfo out;
  std::lock_guard<std::mutex> lock(mu_);
  if (latest_.empty()) return out;
  out.version = latest_;
  out.notes = notes_;
  out.available = semver_newer(latest_.c_str(), fw::kVersion) &&
                  latest_ != config_.skipped_update();
  return out;
}

void UpdateCheck::skip_current() {
  std::string v;
  {
    std::lock_guard<std::mutex> lock(mu_);
    v = latest_;
  }
  if (!v.empty()) config_.set_skipped_update(v);
}

int UpdateCheck::check_cadence() const { return config_.update_check_mode(); }

void UpdateCheck::set_check_cadence(int mode) {
  config_.set_update_check_mode(mode);
}

}  // namespace platform
