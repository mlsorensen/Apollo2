#pragma once

#include <cstdint>
#include <string>

// Settings backup/restore port. The device's settings live in NVS, which does
// NOT travel with the removable card the shot history is on — so replacing the
// hardware keeps every shot and loses every setting. This port copies the whole
// settings namespace to a plain-text file on the same card, and reads it back.
//
// Explicit both ways: nothing is written or restored without the user asking.
// A restore is a REPLACE, not a merge — the file is ground truth, and a setting
// the file doesn't carry goes back to its compiled default (so an older
// backup's missing keys behave exactly like a fresh upgrade). Per-unit keys
// (the paddle GPIO overrides, the pending-install boot flag) belong to the board
// rather than the user and survive a restore untouched.
//
// Requests are non-blocking: the implementation owns the medium on its own task
// (on-device, the shot store's writer task, which already owns the mount) and
// the UI polls state().

namespace core {

struct BackupInfo {
  bool medium_ready = false;  // a card is mounted right now
  bool file_present = false;  // ...and it carries a settings backup
  int64_t saved_unix = 0;     // when that backup was written
  std::string version;        // firmware that wrote it ("0.14.0")
  std::string board;          // board slug it was written on
  bool has_wifi = false;      // it carries the WiFi network (SSID + password)
  bool has_token = false;     // it carries the Micra pairing token
  // Written by FIRMWARE NEWER than what's running: restore is refused. An
  // older build indexes stored enums straight into fixed arrays, so a value
  // from a newer build can be an out-of-bounds read (the NVS rule).
  bool newer_firmware = false;
};

enum class BackupOp : uint8_t { kNone, kBackup, kRestore };
enum class BackupState : uint8_t { kIdle, kBusy, kDone, kFailed };

class ISettingsBackup {
 public:
  virtual ~ISettingsBackup() = default;

  // Cached snapshot — safe to call from the UI thread every refresh.
  virtual BackupInfo info() const = 0;

  // The two credentials in the settings namespace are opt-out, because the file
  // is plain text on a card that leaves the machine. `include_wifi` false omits
  // the network entirely (SSID + password + the WiFi-enabled flag);
  // `include_token` false omits the Micra pairing token (the saved MAC stays —
  // it identifies the machine, it doesn't open it). Everything else, timezone
  // and NTP included, is not a credential and always travels.
  virtual void request_backup(bool include_wifi, bool include_token) = 0;
  virtual void request_restore() = 0;

  virtual BackupState state() const = 0;
  virtual BackupOp last_op() const = 0;
  virtual std::string message() const = 0;  // UI-ready detail / failure reason
};

}  // namespace core
