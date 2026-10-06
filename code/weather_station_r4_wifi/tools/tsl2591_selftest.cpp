/*
 * Host-side check of the TSL2591 lux formula and auto-ranging (tsl2591_logic.h).
 *
 * Build (from this directory):
 *   g++ -std=c++17 -Wall -Wextra -o tsl2591_selftest tsl2591_selftest.cpp
 * Run:
 *   ./tsl2591_selftest
 */

#include <cmath>
#include <cstdio>

#include "../tsl2591_logic.h"

using namespace tsl2591;

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) {
    failures++;
  }
}

bool near(float a, float b, float rel = 1e-4f) {
  return std::fabs(a - b) <= rel * std::fmax(std::fabs(b), 1.0f);
}

// Simulated sensor: counts produced by a light level on a stage, clipped at full scale
void simulate(float lux, float ir_fraction, const Stage &s, uint16_t &ch0, uint16_t &ch1) {
  // Invert computeLux: lux = (ch0 - 2 ch1) / cpl with ch1 = ir_fraction * ch0
  float c0 = lux * countsPerLux(s) / (1.0f - 2.0f * ir_fraction);
  float c1 = ir_fraction * c0;
  float limit = (float)maxCount(s);
  ch0 = (uint16_t)std::fmin(c0, limit);
  ch1 = (uint16_t)std::fmin(c1, limit);
}

// Runs the ranging loop until the stage is stable; returns the reported lux
float settle(float lux, float ir_fraction, uint8_t &stage, int &steps) {
  float reported = -1.0f;
  for (steps = 0; steps < 20; steps++) {
    uint16_t ch0, ch1;
    simulate(lux, ir_fraction, STAGES[stage], ch0, ch1);
    if (isReportable(stage, ch0, ch1)) {
      reported = computeLux(ch0, ch1, STAGES[stage]);
    }
    uint8_t next = nextStage(stage, ch0, ch1);
    if (next == stage) {
      break;
    }
    stage = next;
  }
  return reported;
}

}  // namespace

int main() {
  // Lux formula
  const Stage &s25 = STAGES[DEFAULT_STAGE];  // 25x, 100 ms
  check(near(countsPerLux(s25), 2500.0f / 762.0f), "counts per lux 25x/100ms");
  check(near(computeLux(1000, 100, s25), 800.0f / (2500.0f / 762.0f)), "lux from counts");
  check(computeLux(100, 60, s25) == 0.0f, "infrared-rich light clamps to 0 (no uint16 wrap)");
  check(computeLux(36863, 0, STAGES[0]) == LUX_MAX, "lux clamps to LUX_MAX");

  // Saturation and ranging decisions
  check(isSaturated(34000, 100, s25), "ch0 near full scale is saturated (100 ms limit 36863)");
  check(!isSaturated(30000, 100, STAGES[3]), "30000 counts below 65535 limit");
  check(nextStage(DEFAULT_STAGE, 36000, 5000) == DEFAULT_STAGE - 1, "saturated -> less sensitive");
  check(nextStage(0, 36863, 20000) == 0, "least sensitive stage cannot step down");
  check(nextStage(DEFAULT_STAGE, 100, 10) == DEFAULT_STAGE + 1, "dark -> more sensitive");
  check(nextStage(NUM_STAGES - 1, 5, 1) == NUM_STAGES - 1, "most sensitive stage cannot step up");
  check(!isReportable(DEFAULT_STAGE, 36863, 100), "saturated reading discarded");
  check(isReportable(0, 36863, 100), "saturated reading on stage 0 reported as lower bound");

  // No oscillation: after stepping up, the next stage must not be saturated
  for (uint8_t i = 0; i + 1 < NUM_STAGES; i++) {
    const Stage &lo = STAGES[i];
    const Stage &hi = STAGES[i + 1];
    float ratio = (hi.again * hi.atime_ms) / (lo.again * lo.atime_ms);
    uint16_t ch0 = (uint16_t)(0.5f * maxCount(hi) / ratio) - 1;
    bool up = nextStage(i, ch0, 0) == i + 1;
    bool not_saturated_after = !isSaturated((uint16_t)(ch0 * ratio), 0, hi);
    char what[80];
    std::snprintf(what, sizeof what, "stage %u -> %u steps up without saturating", i, i + 1);
    check(up && not_saturated_after, what);
  }

  // End-to-end: daylight to night sky, starting from the power-up stage
  struct Case { float lux; const char *name; };
  const Case cases[] = {
      {150000.0f, "full sun behind window"}, {30000.0f, "overcast day"}, {500.0f, "dusk"},
      {5.0f, "late twilight"}, {0.05f, "light-polluted night sky"}};
  for (const Case &c : cases) {
    uint8_t stage = DEFAULT_STAGE;
    int steps = 0;
    float lux = settle(c.lux, 0.2f, stage, steps);
    char what[120];
    std::snprintf(what, sizeof what, "%s: %.3g lx -> %.3g lx on stage %u after %d steps",
                  c.name, c.lux, lux, stage, steps);
    check(near(lux, c.lux, 0.05f) || (c.lux > 100000.0f && lux >= 100000.0f), what);
  }

  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
