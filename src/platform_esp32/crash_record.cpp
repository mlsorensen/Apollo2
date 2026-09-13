#include "platform_esp32/crash_record.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_rom_crc.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/system.h"

namespace platform::crash_record {
namespace {

constexpr uint32_t kMagic = 0x4d454d46;  // "MEMF"
constexpr int kUsedSizes = 16;  // distinct exact sizes tracked among used blocks
constexpr int kFreeTop = 8;     // largest free holes kept
constexpr uint32_t kSmallBlock = 512;  // used blocks below this are lumped together

struct SizeCount {
  uint32_t size;
  uint32_t count;
};

// Everything below survives a software/panic reset (not a power cycle).
struct Record {
  uint32_t magic;
  uint32_t seq;         // failures seen in the boot that wrote this
  uint32_t uptime_ms;   // when the LAST failure happened
  uint32_t req_size;
  uint32_t req_caps;
  char func[24];
  uint8_t in_isr;       // 1 = failure from interrupt context (no census taken)
  uint8_t walked;       // 1 = census below is valid
  uint8_t pad[2];
  uint32_t free_bytes;
  uint32_t largest_free;
  uint32_t min_free;    // low-water mark of the pool since boot
  uint32_t used_blocks;
  uint32_t free_blocks;
  uint32_t used_bytes;
  SizeCount used[kUsedSizes];  // used blocks >= kSmallBlock, by exact size
  uint32_t used_other_count;   // used blocks < kSmallBlock, or beyond the table
  uint32_t used_other_bytes;
  uint32_t free_top[kFreeTop];  // largest free holes, descending
  uint32_t crc;
};

RTC_NOINIT_ATTR Record g_rec;

uint32_t crc_of(const Record& r) {
  return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&r),
                          sizeof(Record) - sizeof(r.crc));
}

bool walker(walker_heap_into_t, walker_block_info_t b, void* user) {
  Record& r = *static_cast<Record*>(user);
  const uint32_t size = static_cast<uint32_t>(b.size);
  if (b.used) {
    if (size >= kSmallBlock) {
      for (int i = 0; i < kUsedSizes; ++i) {
        if (r.used[i].count == 0) {
          r.used[i] = {size, 1};
          return true;
        }
        if (r.used[i].size == size) {
          ++r.used[i].count;
          return true;
        }
      }
    }
    ++r.used_other_count;
    r.used_other_bytes += size;
  } else {
    // Keep the top-N free holes sorted descending.
    for (int i = 0; i < kFreeTop; ++i) {
      if (size > r.free_top[i]) {
        for (int j = kFreeTop - 1; j > i; --j) r.free_top[j] = r.free_top[j - 1];
        r.free_top[i] = size;
        break;
      }
    }
  }
  return true;
}

uint32_t g_seq = 0;            // failures this boot
uint32_t g_last_log_ms = 0;    // rate limit for the live log line
volatile bool g_busy = false;  // the hook must never re-enter itself

void on_alloc_failed(size_t size, uint32_t caps, const char* function) {
  if (g_busy) return;
  g_busy = true;
  ++g_seq;
  Record& r = g_rec;
  r.magic = 0;  // invalid while being written
  std::memset(&r, 0, sizeof(r));
  r.seq = g_seq;
  r.uptime_ms = core::now_ms();
  r.req_size = static_cast<uint32_t>(size);
  r.req_caps = caps;
  if (function != nullptr) std::strncpy(r.func, function, sizeof(r.func) - 1);
  r.in_isr = xPortInIsrContext() ? 1 : 0;
  if (!r.in_isr) {
    // Walk the pool that actually refused the request. heap_caps_walk takes
    // each heap's lock; the failed allocation has already released them.
    multi_heap_info_t info = {};
    heap_caps_get_info(&info, caps);
    r.free_bytes = info.total_free_bytes;
    r.largest_free = info.largest_free_block;
    r.min_free = info.minimum_free_bytes;
    r.used_blocks = info.allocated_blocks;
    r.free_blocks = info.free_blocks;
    r.used_bytes = info.total_allocated_bytes;
    heap_caps_walk(caps, walker, &r);
    r.walked = 1;
  }
  r.crc = crc_of(r);
  r.magic = kMagic;
  // One live line, rate-limited: a starving pool can fail many times a second
  // and the record already holds the latest.
  if (!r.in_isr && (g_seq == 1 || r.uptime_ms - g_last_log_ms >= 5000u)) {
    g_last_log_ms = r.uptime_ms;
    core::logf("memfail: %s wanted %u B (caps 0x%x) — pool free=%u largest=%u; "
               "record #%u saved for next boot\n",
               r.func, static_cast<unsigned>(r.req_size),
               static_cast<unsigned>(r.req_caps), static_cast<unsigned>(r.free_bytes),
               static_cast<unsigned>(r.largest_free), static_cast<unsigned>(r.seq));
  }
  g_busy = false;
}

}  // namespace

void install() {
  heap_caps_register_failed_alloc_callback(on_alloc_failed);
}

void report_at_boot() {
  Record& r = g_rec;
  if (r.magic != kMagic || r.crc != crc_of(r)) {
    r.magic = 0;
    return;
  }
  core::logf("memfail: previous boot had %u failed allocation(s); last at +%u.%us: "
             "%s wanted %u B (caps 0x%x)%s\n",
             static_cast<unsigned>(r.seq), static_cast<unsigned>(r.uptime_ms / 1000),
             static_cast<unsigned>(r.uptime_ms % 1000 / 100), r.func,
             static_cast<unsigned>(r.req_size), static_cast<unsigned>(r.req_caps),
             r.in_isr ? " from ISR context (no census)" : "");
  if (r.walked) {
    core::logf("memfail: pool free=%u largest=%u minfree=%u | used %u blocks / %u B | "
               "free %u blocks\n",
               static_cast<unsigned>(r.free_bytes), static_cast<unsigned>(r.largest_free),
               static_cast<unsigned>(r.min_free), static_cast<unsigned>(r.used_blocks),
               static_cast<unsigned>(r.used_bytes), static_cast<unsigned>(r.free_blocks));
    char line[256];
    int n = std::snprintf(line, sizeof(line), "memfail: used >=%uB by size:",
                          static_cast<unsigned>(kSmallBlock));
    for (int i = 0; i < kUsedSizes && r.used[i].count != 0 && n < 200; ++i) {
      n += std::snprintf(line + n, sizeof(line) - n, " %ux%u",
                         static_cast<unsigned>(r.used[i].size),
                         static_cast<unsigned>(r.used[i].count));
    }
    std::snprintf(line + n, sizeof(line) - n, "; other %u blocks / %u B\n",
                  static_cast<unsigned>(r.used_other_count),
                  static_cast<unsigned>(r.used_other_bytes));
    core::logf("%s", line);
    n = std::snprintf(line, sizeof(line), "memfail: largest free holes:");
    for (int i = 0; i < kFreeTop && r.free_top[i] != 0; ++i) {
      n += std::snprintf(line + n, sizeof(line) - n, " %u",
                         static_cast<unsigned>(r.free_top[i]));
    }
    std::snprintf(line + n, sizeof(line) - n, "\n");
    core::logf("%s", line);
  }
  r.magic = 0;  // reported once
}

void self_test() {
  // An impossible internal-DMA request: exercises the hook end to end without
  // touching anything real. Reboot afterwards to see the boot-time report.
  void* p = heap_caps_malloc(64u * 1024u * 1024u, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (p != nullptr) heap_caps_free(p);  // cannot happen; keep the compiler honest
}

}  // namespace platform::crash_record
