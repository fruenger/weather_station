/*
 * CSV format of the bulk upload (POST <api path>bulk/, Content-Type: text/csv).
 * No Arduino dependencies: tested on the host by tools/bulk_csv_selftest.cpp.
 *
 * The body is BULK_CSV_HEADER followed by one formatBulkCsvLine() line per
 * sample. The firmware formats every line twice (once for the SHA-256 of the
 * signature, once for sending), so the output must depend only on the sample.
 */
#ifndef BULK_CSV_H
#define BULK_CSV_H

#include <stdint.h>
#include <stdio.h>

static const char BULK_CSV_HEADER[] =
    "jd,temperature,pressure,humidity,illuminance,wind_speed,rain,sky_temp,box_temp,"
    "is_raining,pm1_0,pm2_5,pm10,uv_index\n";

// Longest line: 14 fields, well below this
const size_t BULK_CSV_LINE_MAX = 192;

struct BulkCsvSample {
  double jd;
  float temperature;  // degC
  float pressure;     // hPa
  float humidity;     // %
  float illuminance;  // lx
  uint16_t wind;      // anemometer pulses in the sample interval
  float rain_mm;      // collector depth
  float sky_temp;     // degC
  float box_temp;     // degC
  bool is_raining;
  uint16_t pm1_0;
  uint16_t pm2_5;
  uint16_t pm10;
  uint16_t uv_index;
};

// Returns the line length (without terminator), or -1 if it does not fit.
inline int formatBulkCsvLine(char *out, size_t size, const BulkCsvSample &s) {
  int n = snprintf(out, size, "%.7f,%.2f,%.2f,%.2f,%.3f,%u,%.2f,%.2f,%.2f,%d,%u,%u,%u,%u\n",
                   s.jd, (double)s.temperature, (double)s.pressure, (double)s.humidity,
                   (double)s.illuminance, (unsigned)s.wind, (double)s.rain_mm,
                   (double)s.sky_temp, (double)s.box_temp, s.is_raining ? 1 : 0,
                   (unsigned)s.pm1_0, (unsigned)s.pm2_5, (unsigned)s.pm10, (unsigned)s.uv_index);
  return (n > 0 && (size_t)n < size) ? n : -1;
}

#endif
