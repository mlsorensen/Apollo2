#include "core/log_ring.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "core/clock.h"
#include "core/system.h"

namespace core {

void LogRing::init(char* buf, size_t capacity) {
  std::lock_guard<std::mutex> lock(mu_);
  if (buf == nullptr || capacity == 0) return;
  buf_ = buf;
  cap_ = capacity;
  head_ = 0;
  used_ = 0;
}

void LogRing::make_stamp(char* out, size_t n) const {
  if (clock_ != nullptr) {
    const std::time_t u = clock_->now_unix();
    if (u > 0) {
      struct tm tm;
      localtime_r(&u, &tm);
      if (tm.tm_year + 1900 >= kClockBaseYear) {
        std::snprintf(out, n, "[%02d:%02d:%02d] ", tm.tm_hour, tm.tm_min,
                      tm.tm_sec);
        return;
      }
    }
  }
  const uint32_t ms = now_ms();
  std::snprintf(out, n, "[+%lu.%03lu] ", static_cast<unsigned long>(ms / 1000u),
                static_cast<unsigned long>(ms % 1000u));
}

void LogRing::push(const char* p, size_t n) {
  if (buf_ == nullptr || n == 0) return;
  if (n >= cap_) {  // pathological: keep only the newest cap_ bytes
    p += n - cap_;
    n = cap_;
  }
  const size_t first = cap_ - head_ < n ? cap_ - head_ : n;
  std::memcpy(buf_ + head_, p, first);
  if (n > first) std::memcpy(buf_, p + first, n - first);
  head_ = (head_ + n) % cap_;
  used_ = used_ + n > cap_ ? cap_ : used_ + n;
}

void LogRing::write(const char* text, bool echo) {
  if (text == nullptr || text[0] == '\0') return;
  // Stamp BEFORE taking the lock: the clock read must never nest inside mu_
  // (a clock impl that ever logs would deadlock otherwise).
  char stamp[20];
  make_stamp(stamp, sizeof(stamp));
  const size_t stamp_len = std::strlen(stamp);

  std::lock_guard<std::mutex> lock(mu_);
  const char* p = text;
  while (*p != '\0') {
    if (at_line_start_) {
      push(stamp, stamp_len);
      if (echo && sink_ != nullptr) sink_(stamp, stamp_len);
      at_line_start_ = false;
    }
    const char* nl = std::strchr(p, '\n');
    const size_t n = nl != nullptr ? static_cast<size_t>(nl - p) + 1
                                   : std::strlen(p);
    push(p, n);
    if (echo && sink_ != nullptr) sink_(p, n);
    if (nl != nullptr) at_line_start_ = true;
    p += n;
  }
}

size_t LogRing::snapshot_tail(char* out, size_t max_len) const {
  if (out == nullptr || max_len == 0) return 0;
  std::lock_guard<std::mutex> lock(mu_);
  size_t n = used_ < max_len - 1 ? used_ : max_len - 1;
  if (n == 0) {
    out[0] = '\0';
    return 0;
  }
  const size_t start = (head_ + cap_ - n) % cap_;
  const size_t first = cap_ - start < n ? cap_ - start : n;
  std::memcpy(out, buf_ + start, first);
  if (n > first) std::memcpy(out + first, buf_, n - first);
  out[n] = '\0';
  if (n < used_) {
    // Window starts mid-line: drop the partial first line.
    char* nl = std::strchr(out, '\n');
    if (nl != nullptr) {
      const size_t keep = n - static_cast<size_t>(nl + 1 - out);
      std::memmove(out, nl + 1, keep + 1);  // +1 carries the NUL
      n = keep;
    }
  }
  return n;
}

LogRing& log_ring() {
  static LogRing ring;
  return ring;
}

}  // namespace core
