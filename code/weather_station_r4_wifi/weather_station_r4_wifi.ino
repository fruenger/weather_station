/*
 * Weather Station - Arduino UNO R4 WiFi
 * Version: 1.6.0
 *
 * Cooperative scheduler: each sensor at its own interval (<= 2 Hz).
 * Ring-buffer snapshot every 500 ms with last-known values for slower sensors.
 * Raw 500 ms wind samples; rolling mean / gusts deferred to server (see TODO.md).
 */

#include <Wire.h>
#include <WiFiS3.h>
#include <WiFiSSLClient.h>

#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "DEV_Config.h"
#include "TSL2591.h"
#include <Adafruit_MLX90614.h>
#include <Adafruit_PM25AQI.h>
#include <DFRobot_UVIndex240370Sensor.h>
#include "Arduino_LED_Matrix.h"

#include "secrets.h"
#include "weather_hmac.h"

#ifndef SECRET_WIFI_SSID
#error "Create secrets.h from secrets.h.example (WiFi + HMAC credentials)."
#endif
#ifndef SECRET_DEVICE_ID
#error "SECRET_DEVICE_ID missing in secrets.h"
#endif
#ifndef SECRET_KEY_ID
#error "SECRET_KEY_ID missing in secrets.h"
#endif
#ifndef SECRET_HMAC_SECRET_HEX
#error "SECRET_HMAC_SECRET_HEX missing in secrets.h"
#endif
#ifndef SECRET_HMAC_PATH
#define SECRET_HMAC_PATH "/weather_station/weather_api/datasets/"
#endif

#define SERIAL_DEBUG 0
#define DISABLE_LED_MATRIX 1

// Snapshot + per-sensor intervals (all <= 2 Hz)
const unsigned long SNAPSHOT_INTERVAL_MS = 500UL;   // ring buffer / wind window: 2 Hz
const unsigned long INTERVAL_BME_MS = 500UL;      // 2 Hz — T, RH, P
const unsigned long INTERVAL_LUX_MS = 1000UL;       // 1 Hz — TSL2591 integration
const unsigned long INTERVAL_MLX_MS = 2000UL;       // 0.5 Hz — IR slow
const unsigned long INTERVAL_UV_MS = 1000UL;        // 1 Hz
const unsigned long INTERVAL_RAIN_MS = 500UL;       // 2 Hz — reed + drop digital
const unsigned long LIVE_UPLOAD_INTERVAL_MS = 1000UL;

// Upload strategy
const unsigned long STARTUP_LIVE_UPLOAD_MS = 300000UL;
const unsigned long UPLOAD_BURST_INTERVAL_MS = 60000UL;
// 60 s burst at 2 Hz = 120 samples; +10 spare (old 75 was only enough for 1 Hz)
const uint16_t RING_BUFFER_SIZE = 130;

const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;
const unsigned long HTTP_RESPONSE_TIMEOUT_MS = 15000UL;
const unsigned long NTP_RESYNC_INTERVAL_MS = 20UL * 60UL * 1000UL;
const unsigned long UPLOAD_BACKOFF_INITIAL_MS = 60000UL;
const unsigned long UPLOAD_BACKOFF_MAX_MS = 30UL * 60UL * 1000UL;
const uint16_t API_PORT = 443;

#if SERIAL_DEBUG
#define DBG_PRINT(...) Serial.print(__VA_ARGS__)
#define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define DBG_PRINT(...) ((void)0)
#define DBG_PRINTLN(...) ((void)0)
#endif

#define RESET_INTERVAL 3600000UL
#define RAIN_MM_PER_TIP 1.25f

#define WIND_SENSOR_PIN 3
#define RESET_PIN 4
#define RAIN_REED_PIN 2
#define RAIN_DROP_ANALOG_PIN A1
#define RAIN_DROP_DIGITAL_PIN 5
#define PMSA003I_SET_PIN 6

const unsigned long PM_MEASUREMENT_INTERVAL = 300000UL;
const unsigned long PM_BOOT_DELAY = 3000UL;
const unsigned long PM_WARMUP_TIME = 30000UL;

#define TCA9548A_ADDRESS 0x70
#define TCA_CHANNEL_0 0x01
#define TCA_CHANNEL_1 0x02
#define TCA_CHANNEL_2 0x04
#define TCA_CHANNEL_3 0x08
#define TCA_CHANNEL_4 0x10

struct Measurement {
  uint32_t cycle;
  float temperature;
  float pressure;
  float humidity;
  float illuminance;
  uint16_t rain_tips;     // tips in this sample interval (500 ms)
  uint16_t wind_revolutions; // pulses in this sample interval (500 ms window)
  uint32_t packet_number;
  float sky_temp;
  float box_temp;
  bool is_raining;
  uint16_t rain_analog;
  uint16_t pm1_0;
  uint16_t pm2_5;
  uint16_t pm10;
  uint16_t uv_index;
};

struct BufferedSample {
  Measurement m;
  double jd;
};

Adafruit_BME280 bme;
Adafruit_MLX90614 mlx;
Adafruit_PM25AQI aqi;
DFRobot_UVIndex240370Sensor uvSensor(&Wire);
WiFiSSLClient sslClient;

#if DISABLE_LED_MATRIX
ArduinoLEDMatrix ledMatrix;
static const uint32_t ledMatrixBlankFrame[] = {0, 0, 0};
#endif

Measurement sample;
BufferedSample ringBuffer[RING_BUFFER_SIZE];
uint16_t ringCount = 0;

bool bme_sensor_available = false;
bool mlx_sensor_available = false;
bool light_sensor_available = false;
bool pmsa003i_sensor_available = false;
bool uv_sensor_available = false;
bool wifi_module_ok = false;

bool pmsa003i_awake = false;
unsigned long pmsa003i_wake_time = 0;
unsigned long last_pm_measurement = 0;
uint16_t last_pm1_0 = 0;
uint16_t last_pm2_5 = 0;
uint16_t last_pm10 = 0;
uint16_t last_uv_index = 0;
float last_illuminance = 0.0f;
uint16_t luxErrorStreak = 0;
// Consecutive TSL2591 I2C errors before the sensor is re-initialised (~1 min at 1 Hz)
const uint16_t LUX_MAX_ERROR_STREAK = 60;

volatile uint32_t windPulseTotal = 0;
uint32_t windPulseLastSnap = 0;

bool firstLoop = true;
bool lastState = false;
unsigned long lastChanged = 0;
uint16_t rainTipsThisCycle = 0;

uint32_t packetNumber = 0;
uint32_t measureCycle = 0;
uint16_t successfulUploads = 0;
uint16_t failedUploads = 0;

unsigned long bootMillis = 0;
unsigned long lastBurstUploadMs = 0;
unsigned long lastLiveUploadMs = 0;
unsigned long lastSnapshotMs = 0;
unsigned long lastBmeMs = 0;
unsigned long lastLuxMs = 0;
unsigned long lastMlxMs = 0;
unsigned long lastUvMs = 0;
unsigned long lastRainMs = 0;
unsigned long lastDebugMs = 0;
bool uploadSessionOpen = false;
bool startupLiveModeAnnounced = false;
bool burstModeAnnounced = false;

bool timeSynced = false;
unsigned long epochAtSync = 0;
unsigned long millisAtSync = 0;
unsigned long lastNtpSyncMs = 0;
unsigned long uploadBackoffUntilMs = 0;
unsigned long uploadBackoffMs = UPLOAD_BACKOFF_INITIAL_MS;

enum HttpPostResultClass {
  HTTP_POST_SUCCESS = 0,
  HTTP_POST_AUTH_OR_RATE_LIMIT,
  HTTP_POST_OTHER_ERROR,
  HTTP_POST_TIMEOUT
};

struct HttpPostResult {
  HttpPostResultClass resultClass;
  int statusCode;
  unsigned long retryAfterSec;
};

// ---------------------------------------------------------------------------
// WiFi / HTTP helpers
// ---------------------------------------------------------------------------

static double julianDateFromUnix(unsigned long unixUtc) {
  return 2440587.5 + ((double)unixUtc / 86400.0);
}

static double jdAtMillis(unsigned long captureMs) {
  if (!timeSynced) {
    return 0.0;
  }
  unsigned long elapsedSec = (captureMs - millisAtSync) / 1000UL;
  return julianDateFromUnix(epochAtSync + elapsedSec);
}

static double currentJulianDate() {
  return jdAtMillis(millis());
}

static unsigned long currentUnixUtc() {
  if (!timeSynced) {
    return 0;
  }
  return epochAtSync + ((millis() - millisAtSync) / 1000UL);
}

static bool syncTimeFromNtp() {
  for (uint8_t attempt = 0; attempt < 20; attempt++) {
    unsigned long epoch = WiFi.getTime();
    if (epoch > 1000000000UL) {
      epochAtSync = epoch;
      millisAtSync = millis();
      timeSynced = true;
      lastNtpSyncMs = millis();
      DBG_PRINT(F("NTP time synced, epoch="));
      DBG_PRINTLN(epoch);
      return true;
    }
    delay(500);
  }
  DBG_PRINTLN(F("WARNING: NTP time sync failed"));
  return false;
}

static void resetUploadBackoff() {
  uploadBackoffMs = UPLOAD_BACKOFF_INITIAL_MS;
  uploadBackoffUntilMs = 0;
}

static void applyUploadBackoff(unsigned long retryAfterSec) {
  unsigned long waitMs = uploadBackoffMs;
  if (retryAfterSec > 0) {
    waitMs = retryAfterSec * 1000UL;
    if (waitMs > UPLOAD_BACKOFF_MAX_MS) {
      waitMs = UPLOAD_BACKOFF_MAX_MS;
    }
  }
  uploadBackoffUntilMs = millis() + waitMs;
  if (uploadBackoffMs < UPLOAD_BACKOFF_MAX_MS / 2UL) {
    uploadBackoffMs *= 2UL;
  } else {
    uploadBackoffMs = UPLOAD_BACKOFF_MAX_MS;
  }
}

static bool uploadBackoffActive() {
  return millis() < uploadBackoffUntilMs;
}

static bool connectWiFi(bool requestNtpSync) {
  if (WiFi.status() == WL_CONNECTED) {
    if (requestNtpSync) {
      syncTimeFromNtp();
    }
    return true;
  }

  DBG_PRINT(F("Connecting to WiFi: "));
  DBG_PRINTLN(SECRET_WIFI_SSID);

  WiFi.disconnect();
  WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    DBG_PRINT('.');
  }
  DBG_PRINTLN();

  if (WiFi.status() != WL_CONNECTED) {
    DBG_PRINTLN(F("WiFi connect failed"));
    return false;
  }

  DBG_PRINT(F("WiFi connected, IP: "));
  DBG_PRINTLN(WiFi.localIP());

  if (requestNtpSync) {
    syncTimeFromNtp();
  }
  return true;
}

static bool ensureTimeSynced(bool forceResync) {
  bool needSync = !timeSynced || forceResync;
  if (!needSync && lastNtpSyncMs > 0 &&
      (millis() - lastNtpSyncMs) >= NTP_RESYNC_INTERVAL_MS) {
    needSync = true;
  }
  if (!needSync) {
    return timeSynced;
  }

  if (!connectWiFi(true)) {
    return timeSynced;
  }
  if (syncTimeFromNtp()) {
    return true;
  }
  return timeSynced;
}

static void disconnectWiFi() {
  WiFi.disconnect();
  DBG_PRINTLN(F("WiFi disconnected (power save)"));
}

static bool inRange(float value, float minValue, float maxValue) {
  return value >= minValue && value <= maxValue;
}

static bool validateMeasurement(const Measurement &m) {
  if (!inRange(m.temperature, -50.0f, 60.0f)) return false;
  if (!inRange(m.pressure, 800.0f, 1200.0f)) return false;
  if (!inRange(m.humidity, 0.0f, 100.0f)) return false;
  if (!inRange(m.illuminance, 0.0f, 200000.0f)) return false;
  if (!inRange(m.sky_temp, -100.0f, 60.0f)) return false;
  if (!inRange(m.box_temp, -40.0f, 80.0f)) return false;
  if (m.wind_revolutions > 500) return false;
  if (m.pm1_0 > 1000 || m.pm2_5 > 1000 || m.pm10 > 1000) return false;
  if (m.uv_index > 20) return false;
  return true;
}

static int buildUploadBody(char *body, size_t bodySize, const Measurement &m, double jd) {
  float rainMm = m.rain_tips * RAIN_MM_PER_TIP;
  // Raw revolutions in SNAPSHOT_INTERVAL_MS window; server converts to m/s (see TODO.md)
  float windRaw = (float)m.wind_revolutions;
  if (windRaw > 500.0f) {
    windRaw = 500.0f;
  }

  return snprintf(
      body, bodySize,
      "jd=%.5f&temperature=%.2f&pressure=%.2f&humidity=%.2f&illuminance=%.3f"
      "&wind_speed=%.0f&rain=%.2f&sky_temp=%.2f&box_temp=%.2f&is_raining=%d"
      "&pm1_0=%u&pm2_5=%u&pm10=%u&uv_index=%u",
      jd, m.temperature, m.pressure, m.humidity, m.illuminance,
      windRaw, rainMm, m.sky_temp, m.box_temp,
      m.is_raining ? 1 : 0, m.pm1_0, m.pm2_5, m.pm10, m.uv_index);
}

static void drainHttpResponse(WiFiSSLClient &client) {
  unsigned long deadline = millis() + 2000UL;
  while (client.connected() && millis() < deadline) {
    while (client.available()) {
      client.read();
      deadline = millis() + 2000UL;
    }
    delay(1);
  }
}

static bool startsWithIgnoreCase(const char *line, const char *prefix) {
  for (size_t i = 0; prefix[i] != '\0'; i++) {
    char a = line[i];
    char b = prefix[i];
    if (a >= 'A' && a <= 'Z') {
      a = (char)(a + ('a' - 'A'));
    }
    if (b >= 'A' && b <= 'Z') {
      b = (char)(b + ('a' - 'A'));
    }
    if (a != b) {
      return false;
    }
  }
  return true;
}

static unsigned long parseRetryAfterSeconds(const char *value) {
  while (*value == ' ' || *value == '\t') {
    value++;
  }
  char *end = NULL;
  unsigned long sec = strtoul(value, &end, 10);
  if (end != value && sec > 0) {
    return sec;
  }
  return 0;
}

static HttpPostResult readHttpPostResult(WiFiSSLClient &client) {
  HttpPostResult result = {HTTP_POST_TIMEOUT, 0, 0};
  unsigned long deadline = millis() + HTTP_RESPONSE_TIMEOUT_MS;
  char line[128];
  uint8_t lineIdx = 0;
  bool statusParsed = false;

  while (millis() < deadline) {
    while (client.available()) {
      char c = client.read();
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        line[lineIdx] = '\0';
        if (lineIdx == 0) {
          drainHttpResponse(client);
          return result;
        }

        if (!statusParsed && strncmp(line, "HTTP/", 5) == 0) {
          const char *sp = strchr(line, ' ');
          if (sp != NULL) {
            result.statusCode = atoi(sp + 1);
            if (result.statusCode == 200 || result.statusCode == 201) {
              result.resultClass = HTTP_POST_SUCCESS;
            } else if (result.statusCode == 401 || result.statusCode == 403 ||
                       result.statusCode == 429) {
              result.resultClass = HTTP_POST_AUTH_OR_RATE_LIMIT;
            } else {
              result.resultClass = HTTP_POST_OTHER_ERROR;
            }
            DBG_PRINT(F("HTTP response: "));
            DBG_PRINTLN(line);
          }
          statusParsed = true;
        } else if (startsWithIgnoreCase(line, "Retry-After:")) {
          const char *value = strchr(line, ':');
          if (value != NULL) {
            result.retryAfterSec = parseRetryAfterSeconds(value + 1);
          }
        }

        lineIdx = 0;
        continue;
      }
      if (lineIdx < sizeof(line) - 1) {
        line[lineIdx++] = c;
      }
    }
    if (!client.connected() && !client.available()) {
      break;
    }
    delay(10);
  }

  drainHttpResponse(client);
  return result;
}

static bool beginUploadSession(bool requestNtpSync) {
  if (uploadBackoffActive()) {
    DBG_PRINTLN(F("Upload deferred (backoff)"));
    return false;
  }
  if (!ensureTimeSynced(requestNtpSync)) {
    DBG_PRINTLN(F("Upload skipped: time not synced"));
    return false;
  }
  if (!connectWiFi(requestNtpSync)) {
    return false;
  }
  if (!sslClient.connected()) {
    if (!sslClient.connect(SECRET_API_HOST, API_PORT)) {
      DBG_PRINTLN(F("SSL connect failed"));
      disconnectWiFi();
      return false;
    }
  }
  uploadSessionOpen = true;
  return true;
}

static void endUploadSession() {
  if (uploadSessionOpen) {
    sslClient.stop();
    uploadSessionOpen = false;
  }
  if (WiFi.status() == WL_CONNECTED) {
    disconnectWiFi();
  }
}

static HttpPostResult postSample(const Measurement &m, double jd, bool keepAlive) {
  HttpPostResult fail = {HTTP_POST_OTHER_ERROR, 0, 0};

  if (!validateMeasurement(m)) {
    DBG_PRINTLN(F("Upload skipped: validation failed"));
    return fail;
  }
  if (jd <= 0.0) {
    DBG_PRINTLN(F("Upload skipped: invalid jd"));
    return fail;
  }
  if (!timeSynced) {
    DBG_PRINTLN(F("Upload skipped: time not synced"));
    return fail;
  }

  char body[512];
  int bodyLen = buildUploadBody(body, sizeof(body), m, jd);
  if (bodyLen <= 0 || bodyLen >= (int)sizeof(body)) {
    DBG_PRINTLN(F("Upload body build failed"));
    return fail;
  }

  unsigned long nowEpoch = currentUnixUtc();
  if (nowEpoch < 1000000000UL) {
    DBG_PRINTLN(F("Upload skipped: invalid timestamp"));
    return fail;
  }

  char timestamp[12];
  snprintf(timestamp, sizeof(timestamp), "%lu", nowEpoch);

  char nonceHex[WEATHER_HMAC_NONCE_HEX_LEN + 1];
  char signatureHex[WEATHER_HMAC_SIG_HEX_LEN + 1];
  if (!weatherHmacSignBody(
          SECRET_HMAC_PATH,
          SECRET_DEVICE_ID,
          SECRET_KEY_ID,
          timestamp,
          (const uint8_t *)body,
          (size_t)bodyLen,
          nonceHex,
          sizeof(nonceHex),
          signatureHex,
          sizeof(signatureHex))) {
    DBG_PRINTLN(F("Upload skipped: HMAC signing failed"));
    return fail;
  }

  if (!sslClient.connected()) {
    if (!sslClient.connect(SECRET_API_HOST, API_PORT)) {
      DBG_PRINTLN(F("SSL reconnect failed"));
      return fail;
    }
    uploadSessionOpen = true;
  }

  sslClient.print(F("POST "));
  sslClient.print(SECRET_API_PATH);
  sslClient.println(F(" HTTP/1.1"));
  sslClient.print(F("Host: "));
  sslClient.println(SECRET_API_HOST);
  sslClient.println(F("Content-Type: application/x-www-form-urlencoded"));
  sslClient.print(F("X-Weather-Device: "));
  sslClient.println(SECRET_DEVICE_ID);
  sslClient.print(F("X-Weather-Key-Id: "));
  sslClient.println(SECRET_KEY_ID);
  sslClient.print(F("X-Weather-Timestamp: "));
  sslClient.println(timestamp);
  sslClient.print(F("X-Weather-Nonce: "));
  sslClient.println(nonceHex);
  sslClient.print(F("X-Weather-Signature: "));
  sslClient.println(signatureHex);
  if (keepAlive) {
    sslClient.println(F("Connection: keep-alive"));
  } else {
    sslClient.println(F("Connection: close"));
  }
  sslClient.print(F("Content-Length: "));
  sslClient.println(bodyLen);
  sslClient.println();
  sslClient.print(body);

  return readHttpPostResult(sslClient);
}

static bool isStartupLiveMode() {
  return (millis() - bootMillis) < STARTUP_LIVE_UPLOAD_MS;
}

static void pushToRingBuffer(const Measurement &m, double jd) {
  if (ringCount >= RING_BUFFER_SIZE) {
    memmove(&ringBuffer[0], &ringBuffer[1], (RING_BUFFER_SIZE - 1) * sizeof(BufferedSample));
    ringCount = RING_BUFFER_SIZE - 1;
    DBG_PRINTLN(F("Ring buffer full — dropped oldest sample"));
  }
  ringBuffer[ringCount].m = m;
  ringBuffer[ringCount].jd = jd;
  ringCount++;
}

static bool flushRingBuffer() {
  if (ringCount == 0) {
    return true;
  }

  DBG_PRINT(F("Burst upload: "));
  DBG_PRINT(ringCount);
  DBG_PRINTLN(F(" samples"));

  if (!beginUploadSession(true)) {
    return false;
  }

  uint16_t uploaded = 0;
  for (uint16_t i = 0; i < ringCount; i++) {
    bool keepAlive = (i + 1 < ringCount);
    HttpPostResult result = postSample(ringBuffer[i].m, ringBuffer[i].jd, keepAlive);
    if (result.resultClass == HTTP_POST_SUCCESS) {
      uploaded++;
      successfulUploads++;
      packetNumber++;
      resetUploadBackoff();
    } else {
      failedUploads++;
      if (result.resultClass == HTTP_POST_AUTH_OR_RATE_LIMIT) {
        applyUploadBackoff(result.retryAfterSec);
        DBG_PRINT(F("Burst auth/rate-limit HTTP "));
        DBG_PRINTLN(result.statusCode);
      } else {
        DBG_PRINT(F("Burst failed at index "));
        DBG_PRINTLN(i);
      }
      break;
    }
  }

  endUploadSession();

  if (uploaded > 0) {
    uint16_t remaining = ringCount - uploaded;
    if (remaining > 0) {
      memmove(&ringBuffer[0], &ringBuffer[uploaded], remaining * sizeof(BufferedSample));
    }
    ringCount = remaining;
    lastBurstUploadMs = millis();
  }

  return ringCount == 0;
}

static bool uploadLiveSample(const Measurement &m, double jd) {
  if (!uploadSessionOpen) {
    if (!beginUploadSession(true)) {
      return false;
    }
    if (!startupLiveModeAnnounced) {
      DBG_PRINTLN(F("Upload mode: startup live (1 Hz)"));
      startupLiveModeAnnounced = true;
    }
  }

  double useJd = jd;
  if (useJd <= 0.0) {
    useJd = currentJulianDate();
  }

  bool ok = false;
  HttpPostResult result = postSample(m, useJd, true);
  if (result.resultClass == HTTP_POST_SUCCESS) {
    ok = true;
    successfulUploads++;
    packetNumber++;
    resetUploadBackoff();
  } else {
    failedUploads++;
    if (result.resultClass == HTTP_POST_AUTH_OR_RATE_LIMIT) {
      applyUploadBackoff(result.retryAfterSec);
      DBG_PRINT(F("Live upload auth/rate-limit HTTP "));
      DBG_PRINTLN(result.statusCode);
    }
    endUploadSession();
  }
  return ok;
}

static void handleUploads(unsigned long captureMs) {
  if (!wifi_module_ok) {
    return;
  }
  if (uploadBackoffActive()) {
    return;
  }

  if (isStartupLiveMode()) {
    if (captureMs - lastLiveUploadMs >= LIVE_UPLOAD_INTERVAL_MS) {
      uploadLiveSample(sample, jdAtMillis(captureMs));
      lastLiveUploadMs = captureMs;
    }
    return;
  }

  if (uploadSessionOpen) {
    endUploadSession();
    if (!burstModeAnnounced) {
      DBG_PRINTLN(F("Upload mode: burst (buffered)"));
      burstModeAnnounced = true;
    }
  }

  double jd = jdAtMillis(captureMs);
  if (jd > 0.0) {
    pushToRingBuffer(sample, jd);
  }

  if (ringCount > 0 && (captureMs - lastBurstUploadMs) >= UPLOAD_BURST_INTERVAL_MS) {
    flushRingBuffer();
  } else if (ringCount >= (RING_BUFFER_SIZE - 4)) {
    DBG_PRINTLN(F("Ring buffer nearly full — early burst"));
    flushRingBuffer();
  }
}

// ---------------------------------------------------------------------------
// Sensor init / read
// ---------------------------------------------------------------------------

void initializeI2CMultiplexer() {
  Wire.begin();
  Wire.beginTransmission(TCA9548A_ADDRESS);
  if (Wire.endTransmission() == 0) {
    DBG_PRINTLN(F("TCA9548A multiplexer OK"));
  } else {
    DBG_PRINTLN(F("ERROR: TCA9548A not found"));
  }
}

void selectI2CChannel(uint8_t channel) {
  Wire.beginTransmission(TCA9548A_ADDRESS);
  Wire.write(channel);
  Wire.endTransmission();
  delay(1);
}

void initializeBME280() {
  selectI2CChannel(TCA_CHANNEL_1);
  if (!bme.begin(0x76)) {
    bme_sensor_available = false;
    DBG_PRINTLN(F("BME280 not found"));
  } else {
    bme_sensor_available = true;
    DBG_PRINTLN(F("BME280 OK (ch1)"));
  }
}

void initializeMLX90614() {
  selectI2CChannel(TCA_CHANNEL_3);
  if (mlx.begin()) {
    mlx_sensor_available = true;
    DBG_PRINTLN(F("MLX90614 OK (ch3)"));
  } else {
    mlx_sensor_available = false;
    DBG_PRINTLN(F("MLX90614 not found"));
  }
}

void initializeTSL2591() {
  selectI2CChannel(TCA_CHANNEL_4);
  DEV_ModuleInit();
  if (TSL2591_Init() == 0) {
    light_sensor_available = true;
    DBG_PRINTLN(F("TSL2591 OK (ch4)"));
  } else {
    light_sensor_available = false;
    DBG_PRINTLN(F("TSL2591 init failed"));
  }
}

void initializePMSA003I() {
  pinMode(PMSA003I_SET_PIN, OUTPUT);
  digitalWrite(PMSA003I_SET_PIN, LOW);
  pmsa003i_sensor_available = false;
  pmsa003i_awake = false;
  DBG_PRINTLN(F("PMSA003I sleep mode (D6 SET pin)"));
}

void wakePMSA003I() {
  digitalWrite(PMSA003I_SET_PIN, HIGH);
  pmsa003i_awake = true;
  pmsa003i_wake_time = millis();
}

void sleepPMSA003I() {
  digitalWrite(PMSA003I_SET_PIN, LOW);
  pmsa003i_awake = false;
  pmsa003i_sensor_available = false;
}

void initializeUVSensor() {
  selectI2CChannel(TCA_CHANNEL_2);
  int attempts = 0;
  while (uvSensor.begin() != true && attempts < 10) {
    attempts++;
    delay(1000);
  }
  uv_sensor_available = (attempts < 10);
  DBG_PRINTLN(uv_sensor_available ? F("SEN0636 UV OK (ch2)") : F("SEN0636 UV failed"));
}

void initializeWiFiModule() {
  if (WiFi.status() == WL_NO_MODULE) {
    wifi_module_ok = false;
    DBG_PRINTLN(F("ERROR: WiFi module not found"));
    return;
  }

  String fv = WiFi.firmwareVersion();
  DBG_PRINT(F("WiFi firmware: "));
  DBG_PRINTLN(fv);

  wifi_module_ok = true;
  WiFi.disconnect();
}

#if DISABLE_LED_MATRIX
void disableLedMatrix() {
  ledMatrix.begin();
  ledMatrix.loadFrame(ledMatrixBlankFrame);
  DBG_PRINTLN(F("Onboard LED matrix disabled (blank frame)"));
}
#endif

void beginMeasurementCycle() {
  rainTipsThisCycle = 0;
}

void readBME280Data() {
  if (bme_sensor_available) {
    selectI2CChannel(TCA_CHANNEL_1);
    sample.temperature = bme.readTemperature();
    sample.pressure = bme.readPressure() / 100.0f;
    sample.humidity = bme.readHumidity();
  } else {
    sample.temperature = 0.0f;
    sample.pressure = 0.0f;
    sample.humidity = 0.0f;
  }
}

void readMLX90614Data() {
  if (mlx_sensor_available) {
    selectI2CChannel(TCA_CHANNEL_3);
    sample.sky_temp = mlx.readObjectTempC();
    sample.box_temp = mlx.readAmbientTempC();
  } else {
    sample.sky_temp = 0.0f;
    sample.box_temp = 0.0f;
  }
}

// Non-blocking: the TSL2591 integrates continuously and auto-ranges; between
// finished integrations (and on transient I2C errors) the last value is held.
void readTSL2591Data() {
  if (!light_sensor_available) {
    sample.illuminance = 0.0f;
    return;
  }
  selectI2CChannel(TCA_CHANNEL_4);
  float lux;
  int status = TSL2591_Poll(&lux);
  if (status == TSL2591_OK) {
    last_illuminance = lux;
    luxErrorStreak = 0;
  } else if (status == TSL2591_ERROR) {
    if (++luxErrorStreak >= LUX_MAX_ERROR_STREAK) {
      DBG_PRINTLN(F("TSL2591 not responding, re-initialising"));
      TSL2591_Init();
      luxErrorStreak = 0;
    }
  } else {
    luxErrorStreak = 0;
  }
  sample.illuminance = last_illuminance;
}

void readUVIndexData() {
  sample.uv_index = last_uv_index;
  if (!uv_sensor_available) {
    return;
  }
  selectI2CChannel(TCA_CHANNEL_2);
  last_uv_index = (uint16_t)uvSensor.readUvIndexData();
  sample.uv_index = last_uv_index;
}

void processRainReed() {
  int reedState = digitalRead(RAIN_REED_PIN);
  if (firstLoop) {
    lastState = reedState;
    firstLoop = false;
  }
  if (reedState != lastState && (millis() - lastChanged) > 250) {
    rainTipsThisCycle++;
    lastState = reedState;
    lastChanged = millis();
  }
}

void readRainDropData() {
  sample.is_raining = (digitalRead(RAIN_DROP_DIGITAL_PIN) == LOW);
  sample.rain_analog = (uint16_t)analogRead(RAIN_DROP_ANALOG_PIN);
}

void servicePmsa003I(unsigned long now) {
  sample.pm1_0 = last_pm1_0;
  sample.pm2_5 = last_pm2_5;
  sample.pm10 = last_pm10;

  if (!pmsa003i_awake) {
    if (now - last_pm_measurement >= PM_MEASUREMENT_INTERVAL) {
      wakePMSA003I();
    }
    return;
  }

  unsigned long awakeTime = now - pmsa003i_wake_time;
  if (!pmsa003i_sensor_available && awakeTime >= PM_BOOT_DELAY) {
    selectI2CChannel(TCA_CHANNEL_0);
    if (aqi.begin_I2C()) {
      pmsa003i_sensor_available = true;
    } else {
      sleepPMSA003I();
      last_pm_measurement = now;
      return;
    }
  }

  if (!pmsa003i_sensor_available || awakeTime < (PM_BOOT_DELAY + PM_WARMUP_TIME)) {
    return;
  }

  selectI2CChannel(TCA_CHANNEL_0);
  PM25_AQI_Data data;
  if (aqi.read(&data)) {
    last_pm1_0 = (uint16_t)data.pm10_standard;
    last_pm2_5 = (uint16_t)data.pm25_standard;
    last_pm10 = (uint16_t)data.pm100_standard;
    sample.pm1_0 = last_pm1_0;
    sample.pm2_5 = last_pm2_5;
    sample.pm10 = last_pm10;
    sleepPMSA003I();
    last_pm_measurement = now;
  }
}

void runScheduledSensors(unsigned long now) {
  if (now - lastBmeMs >= INTERVAL_BME_MS) {
    readBME280Data();
    lastBmeMs = now;
  }
  if (now - lastLuxMs >= INTERVAL_LUX_MS) {
    readTSL2591Data();
    lastLuxMs = now;
  }
  if (now - lastMlxMs >= INTERVAL_MLX_MS) {
    readMLX90614Data();
    lastMlxMs = now;
  }
  if (now - lastUvMs >= INTERVAL_UV_MS) {
    readUVIndexData();
    lastUvMs = now;
  }
  if (now - lastRainMs >= INTERVAL_RAIN_MS) {
    processRainReed();
    readRainDropData();
    lastRainMs = now;
  }
  servicePmsa003I(now);
}

void takeSnapshot(unsigned long now) {
  updateWindReading();
  sample.rain_tips = rainTipsThisCycle;
  measureCycle++;
  sample.cycle = measureCycle;
  sample.packet_number = packetNumber;
  handleUploads(now);
  beginMeasurementCycle();
  lastSnapshotMs = now;
}

static unsigned long millisUntilNextEvent(unsigned long now) {
  unsigned long nextDue = now + 50UL;

  unsigned long due = lastSnapshotMs + SNAPSHOT_INTERVAL_MS;
  if (due < nextDue) {
    nextDue = due;
  }
  due = lastBmeMs + INTERVAL_BME_MS;
  if (due < nextDue) {
    nextDue = due;
  }
  due = lastLuxMs + INTERVAL_LUX_MS;
  if (due < nextDue) {
    nextDue = due;
  }
  due = lastMlxMs + INTERVAL_MLX_MS;
  if (due < nextDue) {
    nextDue = due;
  }
  due = lastUvMs + INTERVAL_UV_MS;
  if (due < nextDue) {
    nextDue = due;
  }
  due = lastRainMs + INTERVAL_RAIN_MS;
  if (due < nextDue) {
    nextDue = due;
  }

  if (nextDue > now) {
    return nextDue - now;
  }
  return 0;
}

void updateWindReading() {
  uint32_t total = windPulseTotal;
  uint32_t delta = total - windPulseLastSnap;
  windPulseLastSnap = total;
  if (delta > 65535UL) {
    delta = 65535UL;
  }
  sample.wind_revolutions = (uint16_t)delta;
}

void printDebugInfo() {
  DBG_PRINT(isStartupLiveMode() ? F("[live] ") : F("[burst] "));
  DBG_PRINT(F("T="));
  DBG_PRINT(sample.temperature, 1);
  DBG_PRINT(F("C buf="));
  DBG_PRINT(ringCount);
  DBG_PRINT(F("/"));
  DBG_PRINT(RING_BUFFER_SIZE);
  DBG_PRINT(F(" wind="));
  DBG_PRINT(sample.wind_revolutions);
  DBG_PRINT(F(" pkt="));
  DBG_PRINTLN(packetNumber);
}

void checkAutoReset() {
  if (millis() > RESET_INTERVAL) {
    digitalWrite(RESET_PIN, LOW);
  }
}

void windIsr() {
  windPulseTotal++;
}

void setup() {
  pinMode(RESET_PIN, OUTPUT);
  digitalWrite(RESET_PIN, HIGH);
  pinMode(RAIN_REED_PIN, INPUT);
  pinMode(RAIN_DROP_DIGITAL_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(WIND_SENSOR_PIN), windIsr, RISING);

  Serial.begin(9600);
#if SERIAL_DEBUG
  {
    unsigned long t0 = millis();
    while (!Serial && (millis() - t0 < 3000UL)) {
      ;
    }
  }
#endif

  bootMillis = millis();
  lastBurstUploadMs = bootMillis;
  unsigned long t0 = millis();
  lastSnapshotMs = t0;
  lastBmeMs = t0;
  lastLuxMs = t0;
  lastMlxMs = t0;
  lastUvMs = t0;
  lastRainMs = t0;
  lastDebugMs = t0;

  DBG_PRINTLN(F("OST Weather Station — UNO R4 WiFi v1.6.0"));

  if (!weatherHmacInit(SECRET_HMAC_SECRET_HEX)) {
    DBG_PRINTLN(F("ERROR: weatherHmacInit failed"));
  }
#if SERIAL_DEBUG
  if (!weatherHmacSelfTest()) {
    DBG_PRINTLN(F("WARNING: weatherHmacSelfTest failed"));
  } else {
    DBG_PRINTLN(F("weatherHmacSelfTest OK"));
  }
#endif

#if DISABLE_LED_MATRIX
  disableLedMatrix();
#endif

  initializeI2CMultiplexer();
  initializeBME280();
  initializeMLX90614();
  initializeTSL2591();
  initializePMSA003I();
  initializeUVSensor();
  initializeWiFiModule();

  readBME280Data();
  readMLX90614Data();
  readTSL2591Data();
  readUVIndexData();
  processRainReed();
  readRainDropData();
  servicePmsa003I(t0);

  DBG_PRINTLN(F("Initialization complete"));
  DBG_PRINTLN(F("Scheduler: snapshot 2 Hz, per-sensor timers (see README)"));
  DBG_PRINT(F("Startup live upload for "));
  DBG_PRINT(STARTUP_LIVE_UPLOAD_MS / 1000UL);
  DBG_PRINTLN(F(" s, then burst mode"));
}

void loop() {
  unsigned long now = millis();

  runScheduledSensors(now);

  if (now - lastSnapshotMs >= SNAPSHOT_INTERVAL_MS) {
    takeSnapshot(now);
  }

  if (now - lastDebugMs >= 2000UL) {
    printDebugInfo();
    lastDebugMs = now;
  }

  checkAutoReset();

  unsigned long sleepMs = millisUntilNextEvent(now);
  if (sleepMs > 0) {
    delay(sleepMs);
  }
}
