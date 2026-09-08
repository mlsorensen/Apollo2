#pragma once

// Firmware-update notification port. The platform side checks the releases
// site (off / on boot / daily, per Settings) and on demand from the Info
// button — only once NTP has synced, proof the internet is reachable. The UI
// polls info() and offers a dismissable modal; "Install now" hands the version
// to device main, which reboots into the early-boot install mode (see
// platform_esp32/install_mode.h). This port only checks + reports.

#include <string>

namespace core {

struct UpdateInfo {
  bool available = false;  // newer than the running firmware and not skipped
  std::string version;     // e.g. "v0.11.0" (empty until a check succeeded)
  std::string notes;       // that release's changelog section (may be empty)
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

  // Settings → Apollo → WiFi "Check for updates" cadence:
  // 0 = Off, 1 = On boot, 2 = Daily (on boot + every ~24 h while running).
  virtual int check_cadence() const = 0;
  virtual void set_check_cadence(int mode) = 0;

  // Manual "Check for updates" (Stats → Info): request a live check. Watch
  // checking() and check_seq() (bumped when any check finishes) for the result.
  virtual void request_check() = 0;
  virtual bool checking() const = 0;
  virtual int check_seq() const = 0;

  // Did the most recently completed check actually reach the releases server
  // (and read a version)? False after a network/DNS/TLS failure — so the UI can
  // say "couldn't check" instead of mistaking a failed check for "up to date".
  virtual bool last_check_ok() const = 0;
};

}  // namespace core
