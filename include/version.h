#pragma once

// This remote's own firmware version — the software running on the ESP32, NOT the
// Micra. Shown in Stats > Info ("Remote FW") so a flashed device is identifiable.
// Bump kVersion on releases; the build also stamps the git revision (FW_GIT_REV,
// see tools/pio_version.py) so dev builds are traceable to a commit.

namespace fw {

inline constexpr const char* kVersion = "0.1.0";

#ifdef FW_GIT_REV
inline constexpr const char* kGitRev = FW_GIT_REV;
#else
inline constexpr const char* kGitRev = "";
#endif

}  // namespace fw
