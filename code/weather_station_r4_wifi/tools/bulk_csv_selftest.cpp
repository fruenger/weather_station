/*
 * Host-side check of the bulk upload body (bulk_csv.h) and of hashing it line by
 * line with the vendored sha256.c, against the server's bulk test vector
 * (website docs/upload-hmac-v1.md, BulkUploadTests.test_bulk_signing_vector).
 *
 * Build (from this directory):
 *   gcc -Wall -c ../sha256.c -o sha256.o
 *   g++ -std=c++17 -Wall -Wextra bulk_csv_selftest.cpp sha256.o -o bulk_csv_selftest
 * Run:
 *   ./bulk_csv_selftest
 */

#include <cstdio>
#include <cstring>
#include <string>

#include "../bulk_csv.h"
#include "../sha256.h"

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) {
    failures++;
  }
}

std::string hex(const uint8_t *d, size_t n) {
  static const char *h = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; i++) {
    s += h[d[i] >> 4];
    s += h[d[i] & 15];
  }
  return s;
}

}  // namespace

int main() {
  // Sample of the server vector
  BulkCsvSample s = {2460000.0, 20.0f, 1013.0f, 50.0f, 100.0f, 0, 0.0f,
                     10.0f, 15.0f, false, 1, 2, 3, 0};
  char line[BULK_CSV_LINE_MAX];
  int len = formatBulkCsvLine(line, sizeof line, s);
  check(len > 0 && std::strcmp(line, "2460000.0000000,20.00,1013.00,50.00,100.000,0,0.00,10.00,"
                                     "15.00,0,1,2,3,0\n") == 0,
        "CSV line matches the server vector");

  // Body hashed line by line, as the firmware does
  SHA256_CTX ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, (const uint8_t *)BULK_CSV_HEADER, std::strlen(BULK_CSV_HEADER));
  sha256_update(&ctx, (const uint8_t *)line, (size_t)len);
  uint8_t digest[32];
  sha256_final(&ctx, digest);
  check(hex(digest, 32) == "012f723a4f081f2da62da1e2d7d3fcf7bcd4bb5a1405257ab8ffb33fd166f2df",
        "incremental SHA-256 matches the server vector");

  // Same result when hashed in one piece
  std::string body = std::string(BULK_CSV_HEADER) + line;
  uint8_t oneShot[32];
  sha256((const uint8_t *)body.data(), body.size(), oneShot);
  check(std::memcmp(digest, oneShot, 32) == 0, "incremental == one-shot hash");

  // Realistic values: precision of jd and negative temperatures
  BulkCsvSample r = {2461333.1234567, -3.25f, 1029.8f, 99.5f, 13623.131f, 12, 1.25f,
                     -17.31f, 8.27f, true, 5, 8, 11, 2};
  len = formatBulkCsvLine(line, sizeof line, r);
  check(len > 0 && std::strcmp(line, "2461333.1234567,-3.25,1029.80,99.50,13623.131,12,1.25,"
                                     "-17.31,8.27,1,5,8,11,2\n") == 0,
        "CSV line with negative values and 7-digit jd");
  check(len < (int)BULK_CSV_LINE_MAX - 40, "line length leaves margin");

  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
