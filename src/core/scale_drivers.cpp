#include "core/scale_driver.h"

#include <cctype>
#include <cstring>

// Name-based model classification + the driver factory. Individual drivers
// live in scale_driver_<model>.cpp and expose a make_*_driver() creator.

namespace core {

std::shared_ptr<IScaleDriver> make_bookoo_driver();  // scale_driver_bookoo.cpp
std::shared_ptr<IScaleDriver> make_acaia_driver();   // scale_driver_acaia.cpp

namespace {

bool has_prefix_ci(const char* name, const char* prefix) {
  const size_t n = std::strlen(prefix);
  if (std::strlen(name) < n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (std::toupper(static_cast<unsigned char>(name[i])) != prefix[i]) return false;
  }
  return true;
}

bool is_bookoo(const char* name) { return has_prefix_ci(name, "BOOKOO"); }

bool is_acaia(const char* name) {
  static constexpr const char* kPrefixes[] = {"UMBRA", "LUNAR", "ACAIA", "PYXIS",
                                              "PROCH"};
  for (const char* p : kPrefixes) {
    if (has_prefix_ci(name, p)) return true;
  }
  return false;
}

}  // namespace

bool scale_name_supported(const char* name) {
  return name != nullptr && (is_bookoo(name) || is_acaia(name));
}

std::shared_ptr<IScaleDriver> make_scale_driver(const char* name) {
  if (name == nullptr) return nullptr;
  if (is_bookoo(name)) return make_bookoo_driver();
  if (is_acaia(name)) return make_acaia_driver();
  return nullptr;
}

}  // namespace core
