#pragma once

// Compile-time helpers for the release workflow's build asserts. The
// workflow passes what it EXPECTS as plain, unquoted defines on the compiler
// command line (-DAPOLLO_EXPECT_VERSION=1.1.0 -DAPOLLO_EXPECT_SLUG=p4-...),
// and version.h / board_config.h static_assert the compiled constants equal
// them, so a forgotten bump or a slug/matrix mix-up fails the first compile
// with a readable message. Unquoted on purpose: the values are stringised
// here, which sidesteps every layer of shell/CI quoting. Local builds leave
// both undefined and assert nothing.
//
// Why compile-time and not a grep of the finished image (what the workflow
// did until v1.1.0): a scan only works while the string survives the linker
// as written, and it doesn't — the xtensa linker tail-merges short literals,
// so v1.1.0's "1.1.0" vanished into the TZ table's "...,M3.2.0,M11.1.0" on
// the S3 and CI refused a correct build (2026-10-23).

#define APOLLO_STRINGIZE_(x) #x
#define APOLLO_STRINGIZE(x) APOLLO_STRINGIZE_(x)

namespace build_check {

constexpr bool str_eq(const char* a, const char* b) {
  while (*a != '\0' && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

}  // namespace build_check
