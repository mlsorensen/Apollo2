#pragma once

// Firmware-update notification port. The platform side checks the releases
// site once per boot (if enabled) and on demand from the Info button — only
// once NTP has synced, proof the internet is actually reachable. The UI polls
// info() and offers a dismissable modal. Notify-only: installing still happens
// through the web flasher.

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

  // Snapshot of the latest check result. The fetch runs LIVE (mbedTLS is
  // pointed at PSRAM so TLS no longer starves the radio's internal-DMA pool),
  // on its own short-lived task; inert without WiFi + NTP.
  virtual UpdateInfo info() const = 0;

  // Persist "don't offer this version again" (the modal's Skip button).
  virtual void skip_current() = 0;

  // Settings → Apollo → WiFi "Check for updates": check once after each boot.
  virtual bool check_at_startup() const = 0;
  virtual void set_check_at_startup(bool on) = 0;

  // Manual "Check for updates" (Stats → Info): request a live check. Watch
  // checking() and check_seq() (bumped when any check finishes) for the result.
  virtual void request_check() = 0;
  virtual bool checking() const = 0;
  virtual int check_seq() const = 0;

  // Self-install into the inactive OTA slot. DORMANT on the hosted-radio P4
  // boards (the esp-hosted SDIO link asserts under a multi-MB download — see
  // the 2026-09 findings); those notify and hand off to the web flasher. Kept
  // wired for the S3 boards / a future fix. cancel_install() is honored between
  // chunks; a canceled/failed install leaves the running firmware untouched.
  virtual void start_install() = 0;
  virtual void cancel_install() = 0;
  virtual InstallStatus install_status() const = 0;
};

}  // namespace core
