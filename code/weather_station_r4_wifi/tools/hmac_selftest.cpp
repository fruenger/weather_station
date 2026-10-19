/*
 * Host-side WEATHER-HMAC-V1 vector check (OpenSSL libcrypto).
 * Firmware uses mbedtls; vectors in hmac_test_vectors.txt must match both.
 *
 * Build (from this directory):
 *   g++ -std=c++17 -Wall -Wextra -o hmac_selftest hmac_selftest.cpp -lcrypto
 * Run:
 *   ./hmac_selftest
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace {

constexpr const char *kPath = "/weather_station/weather_api/datasets/";
constexpr const char *kDevice = "test-device";
constexpr const char *kKeyId = "key1";
constexpr const char *kTimestamp = "1700000000";
constexpr const char *kNonceHex = "00112233445566778899aabbccddeeff";
constexpr const char *kBody = "jd=2460000.00000&temperature=20.00";
constexpr const char *kExpectedBodyDigest =
    "272add28edef85bb137dd1dec6f7c69d5a74f13f2032f00507465a8e564fb7f9";
constexpr const char *kExpectedSignature =
    "17dcbd5904c2dbf8a7780e2e90a8b63cb91fda199810fa18a24b1a3fb9a87c3c";

std::string sha256Hex(const uint8_t *data, size_t len) {
  uint8_t digest[EVP_MAX_MD_SIZE];
  unsigned int digestLen = 0;
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  if (!ctx) {
    return {};
  }
  if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(ctx, data, len) != 1 ||
      EVP_DigestFinal_ex(ctx, digest, &digestLen) != 1) {
    EVP_MD_CTX_free(ctx);
    return {};
  }
  EVP_MD_CTX_free(ctx);

  static const char *hex = "0123456789abcdef";
  std::string out;
  out.reserve(digestLen * 2);
  for (unsigned int i = 0; i < digestLen; i++) {
    out.push_back(hex[(digest[i] >> 4) & 0x0F]);
    out.push_back(hex[digest[i] & 0x0F]);
  }
  return out;
}

std::string hmacSha256Hex(const uint8_t *key, size_t keyLen, const std::string &message) {
  uint8_t digest[EVP_MAX_MD_SIZE];
  unsigned int digestLen = 0;
  if (!HMAC(
          EVP_sha256(),
          key,
          static_cast<int>(keyLen),
          reinterpret_cast<const uint8_t *>(message.data()),
          message.size(),
          digest,
          &digestLen)) {
    return {};
  }

  static const char *hex = "0123456789abcdef";
  std::string out;
  out.reserve(digestLen * 2);
  for (unsigned int i = 0; i < digestLen; i++) {
    out.push_back(hex[(digest[i] >> 4) & 0x0F]);
    out.push_back(hex[digest[i] & 0x0F]);
  }
  return out;
}

std::string buildCanonical(
    const char *path,
    const char *contentType,
    const char *deviceId,
    const char *keyId,
    const char *timestamp,
    const char *nonceHex,
    const char *bodyDigestHex) {
  std::string canonical;
  canonical.reserve(256);
  canonical.append("WEATHER-HMAC-V1\n");
  canonical.append("POST\n");
  canonical.append(path);
  canonical.append("\n");
  canonical.append(contentType);
  canonical.append("\n");
  canonical.append(deviceId);
  canonical.append("\n");
  canonical.append(keyId);
  canonical.append("\n");
  canonical.append(timestamp);
  canonical.append("\n");
  canonical.append(nonceHex);
  canonical.append("\n");
  canonical.append(bodyDigestHex);
  return canonical;
}

}  // namespace

int main() {
  const uint8_t secret[32] = {};
  const uint8_t *body = reinterpret_cast<const uint8_t *>(kBody);
  const size_t bodyLen = std::strlen(kBody);

  std::string bodyDigest = sha256Hex(body, bodyLen);
  if (bodyDigest != kExpectedBodyDigest) {
    std::fprintf(stderr, "body digest mismatch\n  got: %s\n  exp: %s\n", bodyDigest.c_str(), kExpectedBodyDigest);
    return 1;
  }

  std::string canonical = buildCanonical(
      kPath, "application/x-www-form-urlencoded", kDevice, kKeyId, kTimestamp, kNonceHex,
      bodyDigest.c_str());
  std::string signature = hmacSha256Hex(secret, sizeof(secret), canonical);
  if (signature != kExpectedSignature) {
    std::fprintf(stderr, "signature mismatch\n  got: %s\n  exp: %s\n", signature.c_str(), kExpectedSignature);
    return 1;
  }

  // Bulk endpoint vector (CSV body, own path and content type)
  const char *bulkBody =
      "jd,temperature,pressure,humidity,illuminance,wind_speed,rain,sky_temp,box_temp,"
      "is_raining,pm1_0,pm2_5,pm10,uv_index\n"
      "2460000.0000000,20.00,1013.00,50.00,100.000,0,0.00,10.00,15.00,0,1,2,3,0\n";
  std::string bulkDigest = sha256Hex(reinterpret_cast<const uint8_t *>(bulkBody), std::strlen(bulkBody));
  std::string bulkSignature = hmacSha256Hex(
      secret, sizeof(secret),
      buildCanonical("/weather_station/weather_api/datasets/bulk/", "text/csv", kDevice, kKeyId,
                     kTimestamp, kNonceHex, bulkDigest.c_str()));
  if (bulkDigest != "012f723a4f081f2da62da1e2d7d3fcf7bcd4bb5a1405257ab8ffb33fd166f2df" ||
      bulkSignature != "1fc948e3deaaee6ea8f66087dc3e9e96a09c3f07efcb09e1141e3b9d7218b2e8") {
    std::fprintf(stderr, "bulk vector mismatch\n  digest: %s\n  signature: %s\n",
                 bulkDigest.c_str(), bulkSignature.c_str());
    return 1;
  }

  std::printf("hmac_selftest: OK\n");
  std::printf("  body_sha256_hex=%s\n", bodyDigest.c_str());
  std::printf("  signature_hex=%s\n", signature.c_str());
  std::printf("  bulk signature_hex=%s\n", bulkSignature.c_str());
  return 0;
}
