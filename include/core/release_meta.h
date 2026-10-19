#pragma once

// A release's <site>/<tag>/meta.json, as the device reads it. Written by the
// release workflow (tools/release_meta.py):
//   {"epoch": {"<slug>": N, ...}, "layout": {...}}
// The device needs only its own board's flash epoch. The epoch map comes FIRST
// and the workflow fails a release whose map would not fit kMaxMetaBytes, so a
// small bounded fetch always holds it. The workflow also compiles this header
// (tools/meta_epoch.cpp) and checks it against every slug it published, so the
// parser here and the file there can't drift apart unnoticed.

#include <cstddef>
#include <string>

namespace core {

inline constexpr std::size_t kMaxMetaBytes = 512;

// The epoch listed for `slug`, or -1 if it isn't there. The key is matched
// QUOTED: slugs nest (p4-wifi6-touch-lcd-5 is a prefix of ...-5-rev3).
inline int meta_epoch(const std::string& body, const char* slug) {
  const std::size_t map = body.find("\"epoch\"");
  if (map == std::string::npos) return -1;
  const std::size_t close = body.find('}', map);
  const std::string key = std::string("\"") + slug + "\"";
  const std::size_t k = body.find(key, map);
  if (k == std::string::npos || k > close) return -1;
  std::size_t i = k + key.size();
  while (i < body.size() && (body[i] == ' ' || body[i] == '\n' || body[i] == ':')) ++i;
  int epoch = 0;
  bool digits = false;
  for (; i < body.size() && body[i] >= '0' && body[i] <= '9'; ++i) {
    epoch = epoch * 10 + (body[i] - '0');
    digits = true;
    if (epoch > 9999) return -1;
  }
  return digits ? epoch : -1;
}

}  // namespace core
