#include "weather_hmac.h"

#include "sha256.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#if defined(ARDUINO_UNOR4_WIFI) || defined(ARDUINO_UNOR4_MINIMA)
extern "C" {
fsp_err_t HW_SCE_McuSpecificInit(void);
fsp_err_t HW_SCE_RNG_Read(uint32_t *OutData_Text);
}
#else
#error "weather_hmac requires Arduino UNO R4 WiFi/Minima SCE TRNG (HW_SCE_RNG_Read)."
#endif

static uint8_t g_secret[WEATHER_HMAC_SECRET_BYTES];
static bool g_secretReady = false;
static bool g_rngReady = false;

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

static bool parseSecretHex(const char *hex, uint8_t secretOut[WEATHER_HMAC_SECRET_BYTES]) {
  if (hex == NULL) {
    return false;
  }
  size_t len = strlen(hex);
  if (len != WEATHER_HMAC_SECRET_HEX_LEN) {
    return false;
  }
  for (size_t i = 0; i < WEATHER_HMAC_SECRET_BYTES; i++) {
    int hi = hexNibble(hex[i * 2]);
    int lo = hexNibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    secretOut[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

static void bytesToHexLower(const uint8_t *bytes, size_t len, char *hexOut) {
  static const char *hex = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    hexOut[i * 2] = hex[(bytes[i] >> 4) & 0x0F];
    hexOut[i * 2 + 1] = hex[bytes[i] & 0x0F];
  }
  hexOut[len * 2] = '\0';
}

static bool initRng(void) {
  if (g_rngReady) {
    return true;
  }
  // Same SCE TRNG path as ArduinoCore-renesas WMath.cpp for UNO R4.
  if (HW_SCE_McuSpecificInit() != FSP_SUCCESS) {
    return false;
  }
  g_rngReady = true;
  return true;
}

bool weatherHmacInit(const char *secretHex) {
  if (!parseSecretHex(secretHex, g_secret)) {
    return false;
  }
  g_secretReady = true;
  return initRng();
}

void weatherHmacShutdown(void) {
  g_rngReady = false;
  g_secretReady = false;
  memset(g_secret, 0, sizeof(g_secret));
}

bool weatherHmacFillNonce(uint8_t nonceOut[WEATHER_HMAC_NONCE_BYTES]) {
  if (!g_rngReady || nonceOut == NULL) {
    return false;
  }
  // HW_SCE_RNG_Read fills four uint32_t words (16 bytes) — exact nonce size.
  uint32_t words[4];
  if (HW_SCE_RNG_Read(words) != FSP_SUCCESS) {
    return false;
  }
  memcpy(nonceOut, words, WEATHER_HMAC_NONCE_BYTES);
  memset(words, 0, sizeof(words));
  return true;
}

bool weatherHmacNonceToHex(const uint8_t *nonce, char *hexOut, size_t hexOutSize) {
  if (nonce == NULL || hexOut == NULL || hexOutSize < (WEATHER_HMAC_NONCE_HEX_LEN + 1)) {
    return false;
  }
  bytesToHexLower(nonce, WEATHER_HMAC_NONCE_BYTES, hexOut);
  return true;
}

bool weatherHmacBodySha256Hex(const uint8_t *body, size_t bodyLen, char *hexOut, size_t hexOutSize) {
  if (body == NULL || hexOut == NULL || hexOutSize < (WEATHER_HMAC_SHA256_HEX_LEN + 1)) {
    return false;
  }

  uint8_t digest[32];
  sha256(body, bodyLen, digest);
  bytesToHexLower(digest, sizeof(digest), hexOut);
  return true;
}

bool weatherHmacBuildCanonical(
    const char *path,
    const char *deviceId,
    const char *keyId,
    const char *timestamp,
    const char *nonceHex,
    const char *bodyDigestHex,
    char *canonicalOut,
    size_t canonicalOutSize) {
  if (path == NULL || deviceId == NULL || keyId == NULL || timestamp == NULL ||
      nonceHex == NULL || bodyDigestHex == NULL || canonicalOut == NULL) {
    return false;
  }

  int written = snprintf(
      canonicalOut,
      canonicalOutSize,
      "%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s",
      WEATHER_HMAC_PROTOCOL,
      WEATHER_HMAC_METHOD,
      path,
      WEATHER_HMAC_CONTENT_TYPE,
      deviceId,
      keyId,
      timestamp,
      nonceHex,
      bodyDigestHex);

  if (written <= 0 || (size_t)written >= canonicalOutSize) {
    return false;
  }
  return true;
}

bool weatherHmacSignCanonical(const char *canonical, char *signatureHexOut, size_t signatureHexOutSize) {
  if (!g_secretReady || canonical == NULL || signatureHexOut == NULL ||
      signatureHexOutSize < (WEATHER_HMAC_SIG_HEX_LEN + 1)) {
    return false;
  }

  uint8_t hmacOut[32];
  hmac_sha256(
      g_secret,
      sizeof(g_secret),
      (const uint8_t *)canonical,
      strlen(canonical),
      hmacOut);
  bytesToHexLower(hmacOut, sizeof(hmacOut), signatureHexOut);
  return true;
}

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
    size_t signatureHexOutSize) {
  if (!g_secretReady || !g_rngReady) {
    return false;
  }

  char bodyDigestHex[WEATHER_HMAC_SHA256_HEX_LEN + 1];
  if (!weatherHmacBodySha256Hex(body, bodyLen, bodyDigestHex, sizeof(bodyDigestHex))) {
    return false;
  }

  uint8_t nonce[WEATHER_HMAC_NONCE_BYTES];
  if (!weatherHmacFillNonce(nonce)) {
    return false;
  }
  if (!weatherHmacNonceToHex(nonce, nonceHexOut, nonceHexOutSize)) {
    return false;
  }

  char canonical[WEATHER_HMAC_CANONICAL_MAX];
  if (!weatherHmacBuildCanonical(
          path,
          deviceId,
          keyId,
          timestamp,
          nonceHexOut,
          bodyDigestHex,
          canonical,
          sizeof(canonical))) {
    return false;
  }

  return weatherHmacSignCanonical(canonical, signatureHexOut, signatureHexOutSize);
}

bool weatherHmacSelfTest(void) {
  static const char kPath[] = "/weather_station/weather_api/datasets/";
  static const char kDevice[] = "test-device";
  static const char kKeyId[] = "key1";
  static const char kTimestamp[] = "1700000000";
  static const char kNonceHex[] = "00112233445566778899aabbccddeeff";
  static const char kBody[] = "jd=2460000.00000&temperature=20.00";
  static const char kExpectedBodyDigest[] =
      "272add28edef85bb137dd1dec6f7c69d5a74f13f2032f00507465a8e564fb7f9";
  static const char kExpectedSignature[] =
      "17dcbd5904c2dbf8a7780e2e90a8b63cb91fda199810fa18a24b1a3fb9a87c3c";
  static const char kZeroSecretHex[] =
      "0000000000000000000000000000000000000000000000000000000000000000";

  uint8_t savedSecret[WEATHER_HMAC_SECRET_BYTES];
  bool savedSecretReady = g_secretReady;
  bool savedRngReady = g_rngReady;
  if (savedSecretReady) {
    memcpy(savedSecret, g_secret, sizeof(savedSecret));
  }

  if (!weatherHmacInit(kZeroSecretHex)) {
    return false;
  }

  char bodyDigestHex[WEATHER_HMAC_SHA256_HEX_LEN + 1];
  if (!weatherHmacBodySha256Hex(
          (const uint8_t *)kBody,
          strlen(kBody),
          bodyDigestHex,
          sizeof(bodyDigestHex))) {
    weatherHmacShutdown();
    return false;
  }
  if (strcmp(bodyDigestHex, kExpectedBodyDigest) != 0) {
    weatherHmacShutdown();
    return false;
  }

  char canonical[WEATHER_HMAC_CANONICAL_MAX];
  if (!weatherHmacBuildCanonical(
          kPath,
          kDevice,
          kKeyId,
          kTimestamp,
          kNonceHex,
          bodyDigestHex,
          canonical,
          sizeof(canonical))) {
    weatherHmacShutdown();
    return false;
  }

  char signatureHex[WEATHER_HMAC_SIG_HEX_LEN + 1];
  if (!weatherHmacSignCanonical(canonical, signatureHex, sizeof(signatureHex))) {
    weatherHmacShutdown();
    return false;
  }
  if (strcmp(signatureHex, kExpectedSignature) != 0) {
    weatherHmacShutdown();
    return false;
  }

  weatherHmacShutdown();

  if (savedSecretReady) {
    memcpy(g_secret, savedSecret, sizeof(g_secret));
    g_secretReady = true;
    g_rngReady = savedRngReady;
    if (savedRngReady && !initRng()) {
      g_rngReady = false;
    }
  }

  return true;
}
