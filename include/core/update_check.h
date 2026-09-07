#pragma once

// Firmware-update notification port. The platform side checks the releases
// site (at most daily, and only once NTP has synced — proof the internet is
// actually reachable); the UI polls info() and offers a dismissable modal.
// Notify-only: installing still happens through the web flasher.

#include <string>

namespace core {

struct UpdateInfo {
  bool available = false;  // newer than the running firmware and not skipped
  std::string version;     // e.g. "v0.11.0" (empty until a check succeeded)
  std::string notes;       // that release's changelog section (may be empty)
};

// Self-install ("Install now") progress. kReady means the new image is
// written and set to boot — the device restarts moments later.
enum class InstallState { kIdle, kDownloading, kVerifying, kReady, kError };

struct InstallStatus {
  InstallState state = InstallState::kIdle;
  int percent = 0;         // download progress, 0-100
  std::string error;       // short reason when state == kError
};

class IUpdateSource {
 public:
  virtual ~IUpdateSource() = default;

  // Snapshot of the latest check result (thread-safe on the device — the
  // fetch runs on its own short-lived task).
  virtual UpdateInfo info() const = 0;

  // Persist "don't offer this version again" (the modal's Skip button).
  virtual void skip_current() = 0;

  // The Settings toggle: whether the daily check runs at all. Persisted;
  // default on (the check is already implicitly opt-in via WiFi + NTP).
  virtual bool enabled() const = 0;
  virtual void set_enabled(bool on) = 0;

  // Self-install: download the offered version into the inactive OTA slot.
  // start_install() is a no-op while a check/install is already running;
  // cancel_install() is honored between download chunks (a canceled or
  // failed install leaves the running firmware untouched).
  virtual void start_install() = 0;
  virtual void cancel_install() = 0;
  virtual InstallStatus install_status() const = 0;
};

}  // namespace core
