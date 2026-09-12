#pragma once

#include <string>

#include "core/settings_backup.h"

// Settings backup file: the whole `micra` NVS namespace as plain text on the
// SD card. These three functions do the NVS<->file work and nothing else — the
// card is mounted, and these are called, by the shot store's writer task (the
// one thing that owns the medium). Nothing here touches LVGL or blocks the UI.
//
// FORMAT (v1), one header block then one line per key:
//
//   # Apollo settings backup
//   format=1
//   version=0.14.0
//   board=p4-wifi6-touch-lcd-5
//   saved=1789214040
//   wifi=1
//   token=1
//   i32:bright=100
//   u8:clock24=1
//   str:tz=CST6CDT,M3.2.0,M11.1.0
//   blob:tgtg=00001042
//
// The type tag is the NVS type, so a value round-trips exactly (floats are
// 4-byte blobs in Preferences, hence hex). Keys are dumped by ENUMERATING the
// namespace, never from a hand-kept list: a setting added later is carried
// without anyone remembering to add it here, and a key this firmware doesn't
// know is still written back on restore.

namespace platform {

// Write every key in the namespace to `path`. Per-unit keys (paddle GPIO
// overrides, the pending-install flag, the clock seed) are left out: they
// describe the board, not the user. The two credentials are opt-out.
bool settings_backup_write(const char* path, bool include_wifi, bool include_token,
                           int* out_count, std::string* err);

// Read just the header into `out` (file_present/saved/version/board/has_*,
// and newer_firmware against the running build). False if there is no
// readable backup at `path`.
bool settings_backup_read(const char* path, core::BackupInfo* out);

// REPLACE the namespace with the file's contents: the per-unit keys are held
// back, everything else is erased, then the file is applied. A key the file
// doesn't carry is therefore absent afterwards, and reads as its compiled
// default — which is what makes restoring an older backup behave exactly like
// upgrading into the defaults. Refuses a file written by newer firmware.
bool settings_backup_restore(const char* path, int* out_count, std::string* err);

}  // namespace platform
