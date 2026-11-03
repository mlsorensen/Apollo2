// Host check of the shot index row format (core/shot_csv.h): the header
// stays fixed while columns are appended, so every reader must take old
// rows, new rows and garbage tails.
//   make test-csv

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/shot_csv.h"

using namespace core;

namespace {
int checks = 0;
#define CHECK(x)                                                        \
  do {                                                                  \
    ++checks;                                                           \
    if (!(x)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
      return 1;                                                         \
    }                                                                   \
  } while (0)
bool near(float a, float b) { return std::fabs(a - b) <= 0.01f; }
}  // namespace

int main() {
  // A v1.3 row round-trips, dose included, and carries exactly ten fields.
  ShotSummary s;
  s.id = 4294967295u;
  s.unix_time = 1781528100;
  s.duration_ms = 27400;
  s.target_g = 36.0f;
  s.final_g = 36.2f;
  s.avg_gps = 1.32f;
  s.mode = ShotMode::kDetect;
  s.wired = false;
  s.dose_g = 18.2f;
  s.dose_src = 2;
  char row[160];
  const int n = format_shot_index_row(row, sizeof(row), s);
  CHECK(n > 0 && n < 100);  // fits both stores' line[160] with room
  int commas = 0;
  for (const char* p = row; *p; ++p) commas += *p == ',';
  CHECK(commas == 10);
  ShotSummary back;
  CHECK(parse_shot_index_row(row, back));
  CHECK(back.id == s.id && back.unix_time == s.unix_time && back.duration_ms == 27400);
  CHECK(near(back.final_g, 36.2f) && back.mode == ShotMode::kDetect && !back.wired);
  CHECK(near(back.dose_g, 18.2f) && back.dose_src == 2);

  // A pre-1.3 row (nine fields): dose defaults to none.
  ShotSummary old;
  CHECK(parse_shot_index_row("7,2026-06-15T06:55:00,1781528100,27400,36.0,36.2,1.32,auto,1\n", old));
  CHECK(old.id == 7 && old.wired && old.mode == ShotMode::kAuto && old.dose_g == 0.0f && old.dose_src == 0);
  // A 1.3 beta row with a dose but no source column: it was weighed (1).
  CHECK(parse_shot_index_row("8,2026-06-15T06:55:00,1781528100,27400,36.0,36.2,1.32,auto,1,18.2\n", old));
  CHECK(near(old.dose_g, 18.2f) && old.dose_src == 1);
  // A garbage source reads as weighed, never as something else.
  CHECK(parse_shot_index_row("8,2026-06-15T06:55:00,1781528100,27400,36.0,36.2,1.32,auto,1,18.2,9\n", old));
  CHECK(old.dose_src == 1);
  // Older still (no mode/wired): still parses.
  CHECK(parse_shot_index_row("3,2026-06-15T06:55:00,1781528100,27400,36.0,36.2,1.32\n", old));
  CHECK(old.id == 3 && old.dose_g == 0.0f);
  // Garbage tails read as "no dose", never as a value.
  const char* bad[] = {
      "7,2026-06-15T06:55:00,1781528100,27400,36.0,36.2,1.32,auto,1,\n",
      "7,2026-06-15T06:55:00,1781528100,27400,36.0,36.2,1.32,auto,1,abc\n",
      "7,2026-06-15T06:55:00,1781528100,27400,36.0,36.2,1.32,auto,1,-5\n",
      "7,2026-06-15T06:55:00,1781528100,27400,36.0,36.2,1.32,auto,1,inf\n",
      "7,2026-06-15T06:55:00,1781528100,27400,36.0,36.2,1.32,auto,1,nan\n",
      "7,2026-06-15T06:55:00,1781528100,27400,36.0,36.2,1.32,auto,1,18.2,extra\n",
  };
  for (const char* line : bad) {
    ShotSummary g;
    CHECK(parse_shot_index_row(line, g));
    CHECK(g.id == 7 && g.wired);
    CHECK(std::strstr(line, ",18.2,") != nullptr ? (near(g.dose_g, 18.2f) && g.dose_src == 1)
                                               : (g.dose_g == 0.0f && g.dose_src == 0));
  }
  // Header and blank lines are rejected, not misread.
  ShotSummary h;
  CHECK(!parse_shot_index_row(kShotIndexHeader, h));
  CHECK(!parse_shot_index_row("\n", h));
  std::printf("shot_csv_test: all %d checks passed\n", checks);
  return 0;
}
