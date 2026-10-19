// The DEVICE's meta.json parser (core/release_meta.h), run on the host by the
// release workflow against the meta.json it is about to publish:
//   meta_epoch <meta.json> <slug> -> prints the epoch the device would read
// Reads only the first core::kMaxMetaBytes, exactly like the device's fetch.

#include <cstdio>
#include <string>

#include "core/release_meta.h"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: meta_epoch <meta.json> <slug>\n");
    return 2;
  }
  std::FILE* f = std::fopen(argv[1], "rb");
  if (f == nullptr) return 2;
  char buf[core::kMaxMetaBytes];
  const std::size_t n = std::fread(buf, 1, sizeof(buf), f);
  std::fclose(f);
  std::printf("%d\n", core::meta_epoch(std::string(buf, n), argv[2]));
  return 0;
}
