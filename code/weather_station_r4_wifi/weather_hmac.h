#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define WEATHER_HMAC_PROTOCOL "WEATHER-HMAC-V1"
#define WEATHER_HMAC_METHOD "POST"
#define WEATHER_HMAC_CONTENT_TYPE "application/x-www-form-urlencoded"

#define WEATHER_HMAC_NONCE_BYTES 16
#define WEATHER_HMAC_NONCE_HEX_LEN 32
#define WEATHER_HMAC_SHA256_HEX_LEN 64
#define WEATHER_HMAC_SIG_HEX_LEN 64
#define WEATHER_HMAC_SECRET_BYTES 32
#define WEATHER_HMAC_SECRET_HEX_LEN 64
#define WEATHER_HMAC_CANONICAL_MAX 512

/** Parse 64-char hex secret and initialize hardware-backed RNG. Call once from setup(). */
bool weatherHmacInit(const char *secretHex);

/** Release RNG resources (optional). */
void weatherHmacShutdown(void);

/** Fill 16-byte nonce from Renesas SCE hardware TRNG. No pseudo-random fallback. */
bool weatherHmacFillNonce(uint8_t nonceOut[WEATHER_HMAC_NONCE_BYTES]);

bool weatherHmacNonceToHex(const uint8_t *nonce, char *hexOut, size_t hexOutSize);

bool weatherHmacBodySha256Hex(const uint8_t *body, size_t bodyLen, char *hexOut, size_t hexOutSize);

bool weatherHmacBuildCanonical(
    const char *path,
    const char *deviceId,
    const char *keyId,
    const char *timestamp,
    const char *nonceHex,
    const char *bodyDigestHex,
    char *canonicalOut,
    size_t canonicalOutSize);

bool weatherHmacSignCanonical(const char *canonical, char *signatureHexOut, size_t signatureHexOutSize);

/** Sign exact raw body bytes; writes lowercase hex nonce and signature. */
bool weatherHmacSignBody(
    const char *path,
    const char *deviceId,
    const char *keyId,
    const char *timestamp,
    const uint8_t *body,
    size_t bodyLen,
    char *nonceHexOut,
    size_t nonceHexOutSize,
    char *signatureHexOut,
    size_t signatureHexOutSize);

/** Verify against known test vectors (see tools/hmac_test_vectors.txt). */
bool weatherHmacSelfTest(void);
