#pragma once

#include <cstdio>

// Version comparison, shared by everything that has to decide "is this build
// newer than that one": the update check (releases.json / releases-beta.json)
// and the settings-backup restore (which refuses a file written by newer
// firmware). Kept in ONE place on purpose — the release workflow's pages job
// already carries a second copy of this ordering (order_key()), and a third
// would be a third thing to keep in agreement.

namespace core {

// "0.14.0" / "v0.14.0" / "0.14.0-beta.1" -> out[3] + out_pre (-1 = a release).
// False for any other pre-release form: ignore rather than guess at ordering.
inline bool parse_version(const char* s, int out[3], int& out_pre) {
  if (*s == 'v') ++s;
  int used = 0;
  if (std::sscanf(s, "%d.%d.%d%n", &out[0], &out[1], &out[2], &used) != 3)
    return false;
  const char* tail = s + used;
  if (*tail == '\0') {
    out_pre = -1;  // a release
    return true;
  }
  int pre = 0, pre_used = 0;
  if (std::sscanf(tail, "-beta.%d%n", &pre, &pre_used) == 1 &&
      tail[pre_used] == '\0' && pre >= 0) {
    out_pre = pre;
    return true;
  }
  return false;
}

// True iff a is newer than b: major, then minor, then patch, then pre-release —
// where a release beats any beta of the same number and beta.2 beats beta.1.
// NEVER compare version strings: "0.9.0" > "0.10.0" lexicographically.
inline bool semver_newer(const char* a, const char* b) {
  int va[3], vb[3], pa = -1, pb = -1;
  if (!parse_version(a, va, pa) || !parse_version(b, vb, pb)) return false;
  for (int i = 0; i < 3; ++i) {
    if (va[i] != vb[i]) return va[i] > vb[i];
  }
  if (pa == pb) return false;
  if (pa < 0) return true;   // a is the release, b a beta of the same version
  if (pb < 0) return false;  // b is the release
  return pa > pb;            // both betas of the same version
}

}  // namespace core
