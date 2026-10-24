#include "core/radio_arbiter.h"

#include <cstring>

namespace core {

namespace {

constexpr int kSlots = static_cast<int>(RadioSlot::kCount);

inline int idx(RadioSlot s) { return static_cast<int>(s); }
inline bool is_scale(int i) {
  return i == idx(RadioSlot::kScaleEspresso) || i == idx(RadioSlot::kScaleBench);
}
// Wrap-safe "a is at or after b".
inline bool reached(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) >= 0; }

}  // namespace

// ---------------------------------------------------------------- feeding

void RadioArbiter::set_target(RadioSlot s, const std::string& addr, AddrType type) {
  std::lock_guard<std::mutex> lk(m_);
  Slot& x = slots_[idx(s)];
  if (x.addr == addr && x.type == type) return;
  x.addr = addr;
  x.type = type;
  x.has_sighting = false;  // a sighting of the old address is worthless
  touch_locked();
}

void RadioArbiter::set_wanted(RadioSlot s, bool wanted) {
  HookCall call;
  {
    std::lock_guard<std::mutex> lk(m_);
    Slot& x = slots_[idx(s)];
    if (x.wanted == wanted) return;
    x.wanted = wanted;
    // No longer wanted while mid-attempt (Disconnect, Forget): abort it now,
    // not when its timeout runs out.
    if (!wanted && holder_ == idx(s) && holder_work_ == RadioWork::kDirectConnect)
      preempt_holder_locked(call);
    touch_locked();
  }
  run_hook(call);
}

void RadioArbiter::set_connected(RadioSlot s, bool connected) {
  std::lock_guard<std::mutex> lk(m_);
  Slot& x = slots_[idx(s)];
  if (x.connected == connected) return;
  x.connected = connected;
  if (connected) x.has_sighting = false;
  touch_locked();
}

void RadioArbiter::set_partner_mode(RadioSlot s, bool partner) {
  std::lock_guard<std::mutex> lk(m_);
  Slot& x = slots_[idx(s)];
  if (x.partner == partner) return;
  x.partner = partner;
  touch_locked();
}

void RadioArbiter::set_parked(RadioSlot s, ParkSource src, bool parked) {
  HookCall call;
  {
    std::lock_guard<std::mutex> lk(m_);
    Slot& x = slots_[idx(s)];
    const uint8_t bit = static_cast<uint8_t>(src);
    const uint8_t next = parked ? static_cast<uint8_t>(x.parked | bit)
                                : static_cast<uint8_t>(x.parked & ~bit);
    if (next == x.parked) return;
    x.parked = next;
    // Parking the slot that is mid-attempt: yield the radio now.
    if (x.parked != 0 && holder_ == idx(s) && holder_work_ == RadioWork::kDirectConnect)
      preempt_holder_locked(call);
    touch_locked();
  }
  run_hook(call);
}

void RadioArbiter::request_user_scan(RadioSlot s) {
  HookCall call;
  {
    std::lock_guard<std::mutex> lk(m_);
    slots_[idx(s)].user_scan = true;
    // Preempt whoever holds the radio — including this slot's OWN connect
    // attempt, which is blocked in the transport until cancelled.
    if (holder_ >= 0 && holder_work_ != RadioWork::kUserScan) preempt_holder_locked(call);
    touch_locked();
  }
  run_hook(call);
}

void RadioArbiter::abort_slot(RadioSlot s) {
  HookCall call;
  {
    std::lock_guard<std::mutex> lk(m_);
    if (holder_ == idx(s) && holder_work_ == RadioWork::kDirectConnect)
      preempt_holder_locked(call);
  }
  run_hook(call);
}

void RadioArbiter::set_hold_hook(RadioSlot s, std::function<void(bool)> hook) {
  std::lock_guard<std::mutex> lk(m_);
  slots_[idx(s)].hold = std::move(hook);
}

void RadioArbiter::set_scan_cancel_hook(RadioSlot s, std::function<void()> hook) {
  std::lock_guard<std::mutex> lk(m_);
  slots_[idx(s)].cancel_scan = std::move(hook);
}

// ---------------------------------------------------------------- protocol

RadioGrant RadioArbiter::poll(RadioSlot slot, uint32_t now_ms) {
  const int s = idx(slot);
  RadioGrant grant;
  HookCall call;
  {
    std::lock_guard<std::mutex> lk(m_);
    Slot& me = slots_[s];

    if (holder_ >= 0 && holder_ != s) {
      // Someone else has the radio. A pending user scan here preempts them —
      // re-issued on EVERY poll until they release, so a cancel that lands
      // before the transport's attempt actually started is not lost.
      if (me.user_scan && holder_work_ != RadioWork::kUserScan) preempt_holder_locked(call);
    } else if (holder_ == s) {
      grant.work = holder_work_;
      grant.ticket = ticket_;
      grant.addr_type = holder_type_;
      grant.from_sighting = holder_from_sighting_;
    } else {
      RadioWork work;
      if (ready_locked(s, now_ms, work)) {
        // Fairness among the slots that are ready right now: the one served
        // longest ago goes first (index breaks ties). Everyone else answers
        // kIdle once and the winner takes the lease on its next poll.
        bool mine = true;
        for (int i = 0; i < kSlots && mine; ++i) {
          if (i == s) continue;
          RadioWork other;
          if (!ready_locked(i, now_ms, other)) continue;
          const int32_t d = static_cast<int32_t>(slots_[i].last_served_ms - me.last_served_ms);
          if (d < 0 || (d == 0 && i < s)) mine = false;
        }
        if (mine) {
          holder_ = s;
          holder_work_ = work;
          holder_preempted_ = false;
          ++ticket_;
          if (ticket_ == 0) ++ticket_;
          me.last_served_ms = now_ms;
          holder_from_sighting_ = false;
          holder_type_ = me.type;
          if (work == RadioWork::kDirectConnect && fresh_sighting_locked(me, now_ms)) {
            holder_from_sighting_ = true;
            holder_type_ = me.sighting_type;
            me.has_sighting = false;  // consumed
          }
          if (work == RadioWork::kFinderChunk) {
            sighting_in_chunk_ = false;
            // A chunk listens for EVERY missing scale, so it counts as serving
            // all of them — otherwise the slot that didn't run it would jump
            // ahead of the Micra as "never served".
            for (int i = 0; i < kSlots; ++i)
              if (is_scale(i) && missing_locked(slots_[i])) slots_[i].last_served_ms = now_ms;
          }
          grant.work = work;
          grant.ticket = ticket_;
          grant.addr_type = holder_type_;
          grant.from_sighting = holder_from_sighting_;
          // A stale hold level from an earlier preemption must not refuse
          // this attempt.
          if (me.hold) {
            call.slot = s;
            call.is_hold = true;
            call.hold_level = false;
          }
        }
      }
    }
  }
  run_hook(call);
  return grant;
}

void RadioArbiter::release(RadioSlot slot, uint32_t ticket, ReleaseResult result,
                           uint32_t now_ms) {
  std::lock_guard<std::mutex> lk(m_);
  const int s = idx(slot);
  if (holder_ != s || ticket != ticket_) return;  // stale
  const RadioWork work = holder_work_;
  holder_ = -1;
  holder_work_ = RadioWork::kIdle;
  holder_preempted_ = false;
  Slot& me = slots_[s];
  if (work == RadioWork::kUserScan) me.user_scan = false;
  if (work == RadioWork::kFinderChunk) {
    // Empty chunks back off; a sighting, a preemption or any state change
    // (touch_locked) brings the gap straight back to the minimum.
    const bool productive = sighting_in_chunk_ || result == ReleaseResult::kPreempted;
    if (productive) chunk_gap_ms_ = kGapMinMs;
    next_chunk_ok_ms_ = now_ms + chunk_gap_ms_;  // this gap: 1 s, 5 s, 20 s, 20 s...
    if (!productive) {
      const uint32_t grown = chunk_gap_ms_ * 5;
      chunk_gap_ms_ = grown > kGapMaxMs ? kGapMaxMs : grown;  // the NEXT empty one
    }
  }
}

bool RadioArbiter::was_preempted(RadioSlot s, uint32_t ticket) const {
  std::lock_guard<std::mutex> lk(m_);
  return holder_ == idx(s) && ticket == ticket_ && holder_preempted_;
}

bool RadioArbiter::user_scan_pending(RadioSlot s) const {
  std::lock_guard<std::mutex> lk(m_);
  return slots_[idx(s)].user_scan;
}

FinderTargets RadioArbiter::finder_targets() const {
  std::lock_guard<std::mutex> lk(m_);
  FinderTargets out;
  for (int i = 0; i < kSlots && out.count < 2; ++i) {
    if (!is_scale(i) || !missing_locked(slots_[i])) continue;
    FinderTarget& t = out.t[out.count++];
    std::strncpy(t.addr, slots_[i].addr.c_str(), sizeof(t.addr) - 1);
    t.addr[sizeof(t.addr) - 1] = '\0';
    t.type = slots_[i].type;
    t.slot = static_cast<RadioSlot>(i);
  }
  return out;
}

void RadioArbiter::report_sighting(RadioSlot seen, AddrType type, uint32_t now_ms) {
  std::lock_guard<std::mutex> lk(m_);
  Slot& x = slots_[idx(seen)];
  x.has_sighting = true;
  x.sighting_ms = now_ms;
  x.sighting_type = type;
  sighting_in_chunk_ = true;
}

RadioSlot RadioArbiter::holder() const {
  std::lock_guard<std::mutex> lk(m_);
  return holder_ < 0 ? RadioSlot::kCount : static_cast<RadioSlot>(holder_);
}

RadioWork RadioArbiter::holder_work() const {
  std::lock_guard<std::mutex> lk(m_);
  return holder_work_;
}

// ---------------------------------------------------------------- internals

bool RadioArbiter::missing_locked(const Slot& x) const {
  return x.wanted && !x.connected && !x.partner && x.parked == 0 && !x.addr.empty();
}

int RadioArbiter::missing_count_locked() const {
  int n = 0;
  for (int i = 0; i < kSlots; ++i)
    if (is_scale(i) && missing_locked(slots_[i])) ++n;
  return n;
}

bool RadioArbiter::fresh_sighting_locked(const Slot& x, uint32_t now_ms) const {
  return x.has_sighting && !reached(now_ms, x.sighting_ms + kSightingWindowMs);
}

// Is slot i ready for a grant right now, and for what? Three priority tiers:
// a pending user scan beats everything; a fresh sighting beats the rest
// (the scale is advertising NOW — nothing else may take the radio first);
// then the ordinary reconnects.
bool RadioArbiter::ready_locked(int i, uint32_t now_ms, RadioWork& work) const {
  const Slot& x = slots_[i];
  bool any_user_scan = false, any_sighting = false;
  for (int k = 0; k < kSlots; ++k) {
    if (slots_[k].user_scan) any_user_scan = true;
    if (is_scale(k) && missing_locked(slots_[k]) && fresh_sighting_locked(slots_[k], now_ms))
      any_sighting = true;
  }
  if (any_user_scan) {
    if (!x.user_scan) return false;
    work = RadioWork::kUserScan;
    return true;
  }
  if (any_sighting) {
    if (!is_scale(i) || !missing_locked(x) || !fresh_sighting_locked(x, now_ms)) return false;
    work = RadioWork::kDirectConnect;
    return true;
  }
  if (!missing_locked(x)) return false;
  if (is_scale(i)) {
    if (missing_count_locked() == 1) {
      work = RadioWork::kDirectConnect;
      return true;
    }
    if (!reached(now_ms, next_chunk_ok_ms_)) return false;
    work = RadioWork::kFinderChunk;
    return true;
  }
  work = RadioWork::kDirectConnect;  // the Micra
  return true;
}

void RadioArbiter::preempt_holder_locked(HookCall& call) {
  if (holder_ < 0) return;
  holder_preempted_ = true;
  call.slot = holder_;
  if (holder_work_ == RadioWork::kDirectConnect) {
    call.is_hold = true;
    call.hold_level = true;
  } else {
    call.is_hold = false;  // a scan chunk / user scan: stop the scan
  }
}

void RadioArbiter::touch_locked() {
  chunk_gap_ms_ = kGapMinMs;
  next_chunk_ok_ms_ = 0;  // due now
}

void RadioArbiter::run_hook(const HookCall& call) {
  if (call.slot < 0) return;
  // Hooks are stored under the mutex but called outside it: a transport
  // cancel is a round trip into the BLE host (and over SDIO on the P4).
  std::function<void(bool)> hold;
  std::function<void()> cancel;
  {
    std::lock_guard<std::mutex> lk(m_);
    if (call.is_hold) hold = slots_[call.slot].hold;
    else cancel = slots_[call.slot].cancel_scan;
  }
  if (call.is_hold) {
    if (hold) hold(call.hold_level);
  } else {
    if (cancel) cancel();
  }
}

}  // namespace core
