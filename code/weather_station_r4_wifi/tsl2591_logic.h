/*
 * TSL2591 lux computation and auto-ranging logic (no Arduino dependencies,
 * unit-tested on the host by tools/tsl2591_selftest.cpp).
 *
 * The sensor sensitivity is set by a ladder of (gain, integration time) stages.
 * After every reading the next stage is chosen:
 *   - a channel at >= 90 % of full scale  -> one stage less sensitive
 *   - ch0 so low that even the next stage stays below 50 % of full scale
 *                                         -> one stage more sensitive
 * A reading taken in a saturated state on the least sensitive stage is still
 * returned (as a lower bound); all other saturated readings are discarded.
 */
#ifndef TSL2591_LOGIC_H
#define TSL2591_LOGIC_H

#include <stdint.h>

namespace tsl2591 {

// Register values (CONTROL register bits 5:4 = gain, 2:0 = integration time)
const uint8_t GAIN_LOW = 0x00;    // 1x
const uint8_t GAIN_MED = 0x10;    // 25x
const uint8_t GAIN_HIGH = 0x20;   // 428x
const uint8_t GAIN_MAX = 0x30;    // 9876x
const uint8_t ATIME_100 = 0x00;
const uint8_t ATIME_200 = 0x01;
const uint8_t ATIME_300 = 0x02;
const uint8_t ATIME_400 = 0x03;
const uint8_t ATIME_600 = 0x05;

// Lux scaling as in the original Waveshare driver ("GA * 53", GA = glass
// attenuation); kept so that new values stay comparable with the archive.
const float LUX_DF = 762.0f;
// Upper bound accepted by the firmware validation and the server
const float LUX_MAX = 200000.0f;

struct Stage {
  uint8_t gain_bits;
  uint8_t atime_bits;
  float again;
  uint16_t atime_ms;
};

// Ordered from least to most sensitive; neighbouring stages differ by 4-11x.
const Stage STAGES[] = {
    {GAIN_LOW, ATIME_100, 1.0f, 100},
    {GAIN_LOW, ATIME_400, 1.0f, 400},
    {GAIN_MED, ATIME_100, 25.0f, 100},
    {GAIN_MED, ATIME_400, 25.0f, 400},
    {GAIN_HIGH, ATIME_200, 428.0f, 200},
    {GAIN_HIGH, ATIME_600, 428.0f, 600},
    {GAIN_MAX, ATIME_300, 9876.0f, 300},
    {GAIN_MAX, ATIME_600, 9876.0f, 600},
};
const uint8_t NUM_STAGES = sizeof(STAGES) / sizeof(STAGES[0]);
// Stage used after power-up: 25x / 100 ms covers roughly 0.4 .. 10000 lx
const uint8_t DEFAULT_STAGE = 2;

inline uint32_t maxCount(const Stage &s) {
  // 100 ms integration saturates at 36863 counts, longer ones at 65535
  return s.atime_ms <= 100 ? 36863u : 65535u;
}

inline float countsPerLux(const Stage &s) {
  return (s.atime_ms * s.again) / LUX_DF;
}

// Lux from raw channel counts (ch0 = full spectrum, ch1 = infrared).
// Negative results (infrared-rich light) are clamped to 0, large ones to LUX_MAX.
inline float computeLux(uint16_t ch0, uint16_t ch1, const Stage &s) {
  float lux = ((float)ch0 - 2.0f * (float)ch1) / countsPerLux(s);
  if (lux < 0.0f) {
    return 0.0f;
  }
  if (lux > LUX_MAX) {
    return LUX_MAX;
  }
  return lux;
}

inline bool isSaturated(uint16_t ch0, uint16_t ch1, const Stage &s) {
  uint32_t limit = (uint32_t)(0.9f * (float)maxCount(s));
  return ch0 >= limit || ch1 >= limit;
}

// Stage to use for the next integration.
inline uint8_t nextStage(uint8_t stage, uint16_t ch0, uint16_t ch1) {
  const Stage &s = STAGES[stage];
  if (isSaturated(ch0, ch1, s)) {
    return stage > 0 ? (uint8_t)(stage - 1) : stage;
  }
  if (stage + 1 < NUM_STAGES) {
    const Stage &up = STAGES[stage + 1];
    float ratio = (up.again * up.atime_ms) / (s.again * s.atime_ms);
    if ((float)ch0 * ratio < 0.5f * (float)maxCount(up)) {
      return (uint8_t)(stage + 1);
    }
  }
  return stage;
}

// Whether a reading on this stage may be reported.
inline bool isReportable(uint8_t stage, uint16_t ch0, uint16_t ch1) {
  return stage == 0 || !isSaturated(ch0, ch1, STAGES[stage]);
}

}  // namespace tsl2591

#endif
