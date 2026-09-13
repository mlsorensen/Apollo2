#include "platform_esp32/crash_record.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_rom_crc.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/system.h"

namespace platform::crash_record {
namespace {

constexpr uint32_t kMagic = 0x4d454d46;  // "MEMF"
constexpr int kUsedSizes = 24;  // distinct exact sizes tracked among used blocks (16 filled on the 4.3C)
constexpr int kFreeTop = 8;     // largest free holes kept
constexpr uint32_t kSmallBlock = 512;  // used blocks below this are lumped together

struct SizeCount {
  uint32_t size;
  uint32_t count;
};

// Everything below survives a software/panic reset (not a power cycle).
struct Failure {  // who asked for what, when
  uint32_t uptime_ms;
  uint32_t req_size;
  uint32_t req_caps;
  char func[24];
  char task[16];
  uint8_t core;
  uint8_t in_isr;  // 1 = interrupt context (no task name, no census)
  uint8_t pad[2];
};

struct Record {
  uint32_t magic;
  uint32_t seq;      // failures seen in the boot that wrote this
  Failure first;     // the one that started it
  Failure last;      // the one closest to the crash; the census below is its
  uint8_t walked;    // 1 = census below is valid
  uint8_t truncated; // 1 = walk stopped at kWalkMaxBlocks (census is partial)
  uint8_t pad[2];
  uint32_t free_bytes;
  uint32_t largest_free;
  uint32_t min_free;    // low-water mark of the pool since boot
  uint32_t used_blocks;
  uint32_t free_blocks;
  uint32_t used_bytes;
  SizeCount used[kUsedSizes];  // used blocks >= kSmallBlock, by exact ALLOCATOR
                               // block size (request rounded up + poison overhead)
  uint32_t used_other_count;   // used blocks < kSmallBlock, or beyond the table
  uint32_t used_other_bytes;
  uint32_t free_top[kFreeTop];  // largest free holes, descending
  uint32_t walked_blocks;
  uint32_t crc;
};

// Bound the time spent inside the heap lock (interrupts are off on this core
// while heap_caps_walk holds it): the S3's internal heap is ~500 blocks, the
// P4's a few thousand, a PSRAM pool could be far more.
constexpr uint32_t kWalkMaxBlocks = 4096;

RTC_NOINIT_ATTR Record g_rec;
static_assert(sizeof(Record) <= 1024, "keep the RTC footprint small (S3 has 8 KB of RTC slow RAM)");

uint32_t crc_of(const Record& r) {
  return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&r),
                          sizeof(Record) - sizeof(r.crc));
}

bool walker(walker_heap_into_t, walker_block_info_t b, void* user) {
  Record& r = *static_cast<Record*>(user);
  if (++r.walked_blocks > kWalkMaxBlocks) {
    r.truncated = 1;
    return false;  // stop the walk
  }
  const uint32_t size = static_cast<uint32_t>(b.size);
  if (b.used) {
    if (size >= kSmallBlock) {
      // Exact-size table, kept as the LARGEST sizes seen: those are the
      // fixed-size buffers a size signature can name. A new larger size
      // evicts the smallest tracked one into "other".
      int free_slot = -1, smallest = 0;
      for (int i = 0; i < kUsedSizes; ++i) {
        if (r.used[i].count == 0) {
          if (free_slot < 0) free_slot = i;
          continue;
        }
        if (r.used[i].size == size) {
          ++r.used[i].count;
          return true;
        }
        if (r.used[i].size < r.used[smallest].size) smallest = i;
      }
      if (free_slot >= 0) {
        r.used[free_slot] = {size, 1};
        return true;
      }
      if (size > r.used[smallest].size) {
        r.used_other_count += r.used[smallest].count;
        r.used_other_bytes += r.used[smallest].size * r.used[smallest].count;
        r.used[smallest] = {size, 1};
        return true;
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

void log_census(const Record& r, const char* tag) {
  core::logf("%s: pool free=%u largest=%u minfree=%u | used %u blocks / %u B | "
             "free %u blocks%s\n",
             tag, static_cast<unsigned>(r.free_bytes), static_cast<unsigned>(r.largest_free),
             static_cast<unsigned>(r.min_free), static_cast<unsigned>(r.used_blocks),
             static_cast<unsigned>(r.used_bytes), static_cast<unsigned>(r.free_blocks),
             r.truncated ? " (census truncated at 4096 blocks)" : "");
  char line[256];
  int n = std::snprintf(line, sizeof(line), "%s: used >=%uB by block size:", tag,
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
  n = std::snprintf(line, sizeof(line), "%s: largest free holes:", tag);
  for (int i = 0; i < kFreeTop && r.free_top[i] != 0; ++i) {
    n += std::snprintf(line + n, sizeof(line) - n, " %u",
                       static_cast<unsigned>(r.free_top[i]));
  }
  std::snprintf(line + n, sizeof(line) - n, "\n");
  core::logf("%s", line);
}

void census(Record& r, uint32_t caps) {
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

void fill_failure(Failure& f, size_t size, uint32_t caps, const char* function) {
  std::memset(&f, 0, sizeof(f));
  f.uptime_ms = core::now_ms();
  f.req_size = static_cast<uint32_t>(size);
  f.req_caps = caps;
  if (function != nullptr) std::strncpy(f.func, function, sizeof(f.func) - 1);
  f.in_isr = xPortInIsrContext() ? 1 : 0;
  f.core = static_cast<uint8_t>(xPortGetCoreID());
  if (!f.in_isr) {
    const char* name = pcTaskGetName(nullptr);
    if (name != nullptr) std::strncpy(f.task, name, sizeof(f.task) - 1);
  }
}

void on_alloc_failed(size_t size, uint32_t caps, const char* function) {
  if (g_busy) return;
  g_busy = true;
  ++g_seq;
  Record& r = g_rec;
  Failure first;
  if (g_seq == 1) {
    fill_failure(first, size, caps, function);
  } else {
    first = r.first;  // keep the boot's first failure across rewrites
  }
  std::memset(&r, 0, sizeof(r));
  r.seq = g_seq;
  r.first = first;
  fill_failure(r.last, size, caps, function);
  // Walk the pool that actually refused the request. heap_caps_walk takes
  // each heap's lock; the failed allocation has already released them.
  if (!r.last.in_isr) census(r, caps);
  r.magic = kMagic;   // covered by the CRC, so set it BEFORE computing
  r.crc = crc_of(r);  // (a torn write fails the CRC check instead)
  // One live line, rate-limited (a starving pool can fail many times a second
  // and the record already holds the latest) — and ONLY with stack to spare:
  // this runs on the failing task's stack, and logf's formatting frame is
  // ~1.5 KB, which would overflow the 1-4 KB radio/ipc tasks exactly the way
  // the rgb_resync canary did. The RTC record is written regardless.
  const bool room = !r.last.in_isr && uxTaskGetStackHighWaterMark(nullptr) > 2048;
  if (room && (g_seq == 1 || r.last.uptime_ms - g_last_log_ms >= 5000u)) {
    g_last_log_ms = r.last.uptime_ms;
    core::logf("memfail: %s (task %s) wanted %u B (caps 0x%x) — pool free=%u largest=%u; "
               "record #%u saved for next boot\n",
               r.last.func, r.last.task, static_cast<unsigned>(r.last.req_size),
               static_cast<unsigned>(r.last.req_caps), static_cast<unsigned>(r.free_bytes),
               static_cast<unsigned>(r.largest_free), static_cast<unsigned>(r.seq));
  }
  g_busy = false;
}

}  // namespace

// Low-water-mark tripwire. A 2 ms esp_timer reads the internal-DMA pool's
// minimum-free (one counter under the heap lock — microseconds); when it has
// dropped by >= 4 KB since the last trip, the census runs RIGHT THERE in the
// esp_timer task (8 KB stack; the walk itself is small) so the transient
// consumer is still holding its memory. The result goes into a second RTC
// record — so it also survives if that dip is the one that crashes the board
// — and loop() prints it (logf's frame stays off the timer task). The 60 s
// telltale only shows "lowest" stepping down after the fact; on the 4.3C the
// dips last less than one main-loop iteration.
struct WatchRecord {
  uint32_t magic;
  uint32_t uptime_ms;
  uint32_t new_low;
  uint8_t pending;    // 1 = loop() has not printed this yet
  uint8_t pad[3];
  Record census;
  uint32_t crc;
};
RTC_NOINIT_ATTR WatchRecord g_watch;
static_assert(sizeof(WatchRecord) <= 1024, "keep the RTC footprint small");
uint32_t g_watch_last_min = 0;
esp_timer_handle_t g_watch_timer = nullptr;

uint32_t watch_crc(const WatchRecord& w) {
  return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&w),
                          sizeof(WatchRecord) - sizeof(w.crc));
}

void watch_tick(void*) {
  constexpr uint32_t kCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
  constexpr uint32_t kStep = 4096;
  const uint32_t m = heap_caps_get_minimum_free_size(kCaps);
  if (g_watch_last_min == 0) {
    g_watch_last_min = m;
    return;
  }
  if (m + kStep > g_watch_last_min) return;
  g_watch_last_min = m;
  if (g_busy) return;  // a failed-alloc census is in flight; it wins
  WatchRecord& w = g_watch;
  std::memset(&w, 0, sizeof(w));
  w.uptime_ms = core::now_ms();
  w.new_low = m;
  census(w.census, kCaps);
  w.pending = 1;
  w.magic = kMagic;
  w.crc = watch_crc(w);
}

void watch_start() {
  const esp_timer_create_args_t args = {
      .callback = watch_tick,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "memwatch",
      .skip_unhandled_events = true,
  };
  if (esp_timer_create(&args, &g_watch_timer) == ESP_OK) {
    esp_timer_start_periodic(g_watch_timer, 2000);  // 2 ms
  }
}

void install() {
  heap_caps_register_failed_alloc_callback(on_alloc_failed);
}

void log_failure(const char* which, const Failure& f) {
  core::logf("memfail: %s at +%u.%us: %s wanted %u B (caps 0x%x)%s%s%s core %u\n",
             which, static_cast<unsigned>(f.uptime_ms / 1000),
             static_cast<unsigned>(f.uptime_ms % 1000 / 100), f.func,
             static_cast<unsigned>(f.req_size), static_cast<unsigned>(f.req_caps),
             f.in_isr ? " from ISR context" : " in task ", f.in_isr ? "" : f.task,
             f.in_isr ? "" : ",", static_cast<unsigned>(f.core));
}

void report_at_boot() {
  watch_start();
  {
    WatchRecord& w = g_watch;
    if (w.magic == kMagic && w.crc == watch_crc(w)) {
      core::logf("memwatch: last new-low census from the previous boot (+%u.%us, %u B):\n",
                 static_cast<unsigned>(w.uptime_ms / 1000),
                 static_cast<unsigned>(w.uptime_ms % 1000 / 100),
                 static_cast<unsigned>(w.new_low));
      log_census(w.census, "memwatch");
    }
    w.magic = 0;
  }
  Record& r = g_rec;
  if (r.magic != kMagic || r.crc != crc_of(r)) {
    r.magic = 0;
    return;
  }
  core::logf("memfail: previous boot had %u failed allocation(s)\n",
             static_cast<unsigned>(r.seq));
  if (r.seq > 1) log_failure("first", r.first);
  log_failure(r.seq > 1 ? "last" : "only one", r.last);
  if (r.walked) log_census(r, "memfail");
  r.magic = 0;  // reported once
}

void watch() {
  WatchRecord& w = g_watch;
  if (w.magic != kMagic || !w.pending || w.crc != watch_crc(w)) return;
  w.pending = 0;
  w.crc = watch_crc(w);
  core::logf("memwatch: DMA pool new low %u B at +%u.%us\n", static_cast<unsigned>(w.new_low),
             static_cast<unsigned>(w.uptime_ms / 1000),
             static_cast<unsigned>(w.uptime_ms % 1000 / 100));
  log_census(w.census, "memwatch");
}

void self_test() {
  // An impossible internal-DMA request: exercises the hook end to end without
  // touching anything real. Reboot afterwards to see the boot-time report.
  void* p = heap_caps_malloc(64u * 1024u * 1024u, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (p != nullptr) heap_caps_free(p);  // cannot happen; keep the compiler honest
}

}  // namespace platform::crash_record
