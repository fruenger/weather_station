/* sha256.h — FIPS 180-4 SHA-256
 *
 * Based on Brad Conte's public-domain crypto-algorithms
 * (https://github.com/B-Con/crypto-algorithms). Public domain.
 * Vendored for Arduino UNO R4 WiFi (mbedtls headers are not on the public include path).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t data[64];
  uint32_t datalen;
  uint64_t bitlen;
  uint32_t state[8];
} SHA256_CTX;

void sha256_init(SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len);
void sha256_final(SHA256_CTX *ctx, uint8_t hash[32]);

/** One-shot SHA-256. */
void sha256(const uint8_t *data, size_t len, uint8_t hash[32]);

/** HMAC-SHA256 (RFC 2104). keyLen may be any size; for WEATHER-HMAC-V1 it is 32. */
void hmac_sha256(
    const uint8_t *key,
    size_t keyLen,
    const uint8_t *msg,
    size_t msgLen,
    uint8_t out[32]);

#ifdef __cplusplus
}
#endif
