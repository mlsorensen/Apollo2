#pragma once

#include <atomic>

#include "core/scale.h"
#include "core/scale_provisioner.h"  // core::ScaleSlot

// The two scale ROLES (espresso, bench) as stable objects that forward to
// whichever scale LINK currently holds the role.
//
// Every consumer of "the espresso scale" — the brew controller, the Home
// cards, the Device settings page — holds a RoleScale, never a link. A link
// object is bound to its radio slot, its task and (for a paired Umbra) the
// partner GATT service, none of which can move at runtime; a role is just a
// pointer. "Swap roles" therefore swaps the two pointers (and the saved
// keys, so a rolled-back build still finds its espresso scale under the
// original keys) while both links keep their connections — no reconnect, no
// restart. At boot the mapping is identity: link A is seeded from the
// espresso keys and holds the espresso role, link B the bench.

namespace core {

class RoleScale : public IScale {
 public:
  explicit RoleScale(IScale* target) : target_(target) {}
  IScale* target() const { return target_.load(std::memory_order_acquire); }
  void set_target(IScale* t) { target_.store(t, std::memory_order_release); }

  ScaleSnapshot snapshot() const override { return target()->snapshot(); }
  ScaleFeatures features() const override { return target()->features(); }
  void tare() override { target()->tare(); }
  int device_setting_count() const override { return target()->device_setting_count(); }
  ScaleSettingDesc device_setting(int index) const override {
    return target()->device_setting(index);
  }
  int device_setting_value(int index) const override {
    return target()->device_setting_value(index);
  }
  void set_device_setting(int index, int option_idx) override {
    target()->set_device_setting(index, option_idx);
  }

 private:
  std::atomic<IScale*> target_;
};

class ScaleRoles {
 public:
  // `a` holds the espresso role at boot, `b` the bench role.
  ScaleRoles(IScale& a, IScale& b) : a_(&a), b_(&b), espresso_(&a), bench_(&b) {}

  RoleScale& espresso() { return espresso_; }
  RoleScale& bench() { return bench_; }
  bool swapped() const { return espresso_.target() == b_; }

  // Exchange the roles. The links are untouched: whichever was espresso is
  // now bench and vice versa. Callers swap the saved keys alongside.
  void swap() {
    const bool s = swapped();
    espresso_.set_target(s ? a_ : b_);
    bench_.set_target(s ? b_ : a_);
  }

  // The link currently holding a role, and the role a link currently holds.
  IScale& link_for(ScaleSlot slot) {
    return slot == ScaleSlot::kBench ? *bench_.target() : *espresso_.target();
  }
  ScaleSlot slot_of(const IScale& link) const {
    return &link == bench_.target() ? ScaleSlot::kBench : ScaleSlot::kEspresso;
  }
  // Link A by boot identity (seeded from the espresso keys; its task always
  // runs, so the user's Settings scan is dispatched to it).
  IScale& link_a() { return *a_; }

 private:
  IScale* a_;
  IScale* b_;
  RoleScale espresso_;
  RoleScale bench_;
};

}  // namespace core
