/*
 * Weather Station - Arduino UNO R4 WiFi
 * Version: 1.9.1
 *
 * Cooperative scheduler: each sensor at its own interval (<= 2 Hz).
 * One snapshot per second (1 Hz) with last-known values for slower sensors;
 * wind and rain are counted per snapshot (pulses / tips per second).
 * After the 5 min startup live mode, all snapshots of a minute are sent in one
 * bulk request (CSV, <api path>bulk/). WiFi stays connected; a timer-driven
 * software watchdog, an upload stall reset and I2C bus recovery keep the station
 * running unattended.
 */

#include <Wire.h>
#include <WiFiS3.h>
#include <WiFiSSLClient.h>

#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "DEV_Config.h"
#include "TSL2591.h"
#include "tsl2591_logic.h"
#include <Adafruit_MLX90614.h>
#include <Adafruit_PM25AQI.h>
#include <DFRobot_UVIndex240370Sensor.h>
#include "Arduino_LED_Matrix.h"
#include "FspTimer.h"
#include "bulk_csv.h"
#include "sha256.h"

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
// Bulk endpoint next to the single-sample endpoint (string literal concatenation)
#define BULK_API_PATH SECRET_API_PATH "bulk/"
#define BULK_HMAC_PATH SECRET_HMAC_PATH "bulk/"

#define SERIAL_DEBUG 0
#define DISABLE_LED_MATRIX 1
// 115200 baud: the sensor report (~600 characters every DEBUG_INTERVAL_MS) would
// block the scheduler for ~0.6 s at 9600 baud
const unsigned long SERIAL_BAUD = 115200UL;
const unsigned long DEBUG_INTERVAL_MS = 2000UL;
// Sensors not found at boot are searched again at this interval (power glitches,
// loose connectors); only I2C transactions, no blocking waits
const unsigned long SENSOR_RETRY_INTERVAL_MS = 60000UL;

// Snapshot + per-sensor intervals (all <= 2 Hz)
const unsigned long SNAPSHOT_INTERVAL_MS = 1000UL;  // ring buffer / wind window: 1 Hz
const unsigned long INTERVAL_BME_MS = 1000UL;     // 1 Hz — T, RH, P
const unsigned long INTERVAL_LUX_MS = 1000UL;       // 1 Hz — TSL2591 integration
const unsigned long INTERVAL_MLX_MS = 2000UL;       // 0.5 Hz — IR slow
const unsigned long INTERVAL_UV_MS = 1000UL;        // 1 Hz
const unsigned long INTERVAL_RAIN_MS = 500UL;       // 2 Hz — reed + drop digital
const unsigned long LIVE_UPLOAD_INTERVAL_MS = 1000UL;

// Upload strategy
const unsigned long STARTUP_LIVE_UPLOAD_MS = 300000UL;
const unsigned long UPLOAD_BURST_INTERVAL_MS = 60000UL;
// Rows per bulk request; a backlog is sent as several requests in one burst
const uint16_t BULK_MAX_ROWS = 120;
// 3 min at 1 Hz: one missed burst (60 s) plus margin
const uint16_t RING_BUFFER_SIZE = 180;

const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;
// WiFi.begin() itself waits this long; the rest of WIFI_CONNECT_TIMEOUT_MS is
// polled in our own loop so the watchdog keeps being fed
const unsigned long WIFI_BEGIN_TIMEOUT_MS = 4000UL;
const int SSL_CONNECT_TIMEOUT_MS = 8000;
const unsigned long HTTP_RESPONSE_TIMEOUT_MS = 15000UL;
// A burst returns to the main loop after this time; the rest follows next burst
const unsigned long BURST_MAX_DURATION_MS = 45000UL;
// Live mode: pause after a failed upload instead of retrying every second
const unsigned long LIVE_RETRY_DELAY_MS = 10000UL;
// Software watchdog: reset when the main loop has not checked in for this long.
// The RA4M1 hardware watchdog allows at most ~5.6 s, shorter than single
// WiFiS3 modem commands (up to 10 s), so a timer interrupt is used instead.
const unsigned long WATCHDOG_TIMEOUT_MS = 120000UL;
// Reset when nothing could be uploaded for this long (catches every other
// failure that leaves the main loop running, e.g. a stuck WiFi module)
const unsigned long UPLOAD_STALL_RESET_MS = 10UL * 60UL * 1000UL;
// I2C: a sensor counts as failed after this many implausible readings in a row;
// the bus is recovered at most once per I2C_RECOVERY_MIN_INTERVAL_MS
const uint8_t SENSOR_FAIL_LIMIT = 3;
const unsigned long I2C_RECOVERY_MIN_INTERVAL_MS = 10000UL;
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

// Periodic self-reset through D4 -> RESET. Off since v1.9.1: the software
// watchdog and the upload stall reset catch hangs, and every reset cost ~1 min of
// data plus 5 min live mode. Set to 1 to re-enable.
#define AUTO_RESET_ENABLED 0
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
// Give up a measurement if the sensor does not deliver within this time after warm-up
const unsigned long PM_READ_TIMEOUT = 30000UL;

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
  uint16_t rain_tips;     // tips in this sample interval (1 s)
  uint16_t wind_revolutions; // pulses in this sample interval (1 s window)
  uint32_t packet_number;
  float sky_temp;
  float box_temp;
  bool is_raining;
  bool bme_valid;         // false: BME280 reading failed, sample is not uploaded
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
uint32_t rainTipsTotal = 0;  // since boot, for the debug report
uint32_t windPulseDebugLast = 0;
unsigned long windDebugLastMs = 0;

uint32_t packetNumber = 0;
uint32_t measureCycle = 0;
uint32_t successfulUploads = 0;   // samples stored by the server
uint16_t failedUploads = 0;       // failed requests
uint32_t discardedSamples = 0;    // invalid samples not sent / rejected by the server
unsigned long lastSuccessfulUploadMs = 0;

uint8_t bmeFailStreak = 0;
uint8_t mlxFailStreak = 0;
uint8_t uvFailStreak = 0;
uint8_t muxFailStreak = 0;
uint16_t i2cRecoveries = 0;
unsigned long lastI2cRecoveryMs = 0;
// Grows while recoveries do not help (e.g. multiplexer missing), max 5 min
unsigned long i2cRecoveryIntervalMs = I2C_RECOVERY_MIN_INTERVAL_MS;

unsigned long bootMillis = 0;
unsigned long lastBurstAttemptMs = 0;
bool lastBurstOk = true;
unsigned long lastLiveUploadMs = 0;
unsigned long liveRetryUntilMs = 0;
uint16_t droppedDuringUpload = 0;
unsigned long lastSnapshotMs = 0;
unsigned long lastBmeMs = 0;
unsigned long lastLuxMs = 0;
unsigned long lastMlxMs = 0;
unsigned long lastUvMs = 0;
unsigned long lastRainMs = 0;
unsigned long lastDebugMs = 0;
unsigned long lastSensorRetryMs = 0;
bool uploadSessionOpen = false;
bool startupLiveModeAnnounced = false;
bool burstModeAnnounced = false;

bool timeSynced = false;
unsigned long epochAtSync = 0;
unsigned long millisAtSync = 0;
unsigned long lastNtpSyncMs = 0;
unsigned long uploadBackoffUntilMs = 0;
unsigned long uploadBackoffMs = UPLOAD_BACKOFF_INITIAL_MS;

FspTimer watchdogTimer;
volatile unsigned long watchdogKickMs = 0;
bool watchdogRunning = false;

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
// Software watchdog
// ---------------------------------------------------------------------------

static inline void feedWatchdog() {
  watchdogKickMs = millis();
}

// Runs at 50 Hz in interrupt context, also while the main code waits inside a
// WiFi / TLS library call.
static void watchdogIsr(timer_callback_args_t *args) {
  (void)args;
  if (millis() - watchdogKickMs > WATCHDOG_TIMEOUT_MS) {
    NVIC_SystemReset();
  }
}

static void startWatchdog() {
  uint8_t type = 0;
  int8_t channel = FspTimer::get_available_timer(type);
  feedWatchdog();
  // 50 Hz: also reachable with the 16 bit timers of the RA4M1
  if (channel < 0 ||
      !watchdogTimer.begin(TIMER_MODE_PERIODIC, type, (uint8_t)channel, 50.0f, 0.0f, watchdogIsr) ||
      !watchdogTimer.setup_overflow_irq() || !watchdogTimer.open() || !watchdogTimer.start()) {
    DBG_PRINTLN(F("WARNING: no timer for the software watchdog"));
    return;
  }
  watchdogRunning = true;
  DBG_PRINTLN(F("Software watchdog started"));
}

// ---------------------------------------------------------------------------
// WiFi / HTTP helpers
// ---------------------------------------------------------------------------

static double julianDateFromUnix(unsigned long unixUtc) {
  return 2440587.5 + ((double)unixUtc / 86400.0);
}

// Millisecond resolution: two snapshots per second must not share a timestamp
static double jdAtMillis(unsigned long captureMs) {
  if (!timeSynced) {
    return 0.0;
  }
  double unixUtc = (double)epochAtSync + (double)(long)(captureMs - millisAtSync) / 1000.0;
  return 2440587.5 + unixUtc / 86400.0;
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
  for (uint8_t attempt = 0; attempt < 10; attempt++) {
    feedWatchdog();
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

// WiFi stays connected between uploads; this only (re)connects when needed.
static bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  DBG_PRINT(F("Connecting to WiFi: "));
  DBG_PRINTLN(SECRET_WIFI_SSID);

  feedWatchdog();
  WiFi.disconnect();
  WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASS);  // waits up to WIFI_BEGIN_TIMEOUT_MS

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    feedWatchdog();
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
  return true;
}

// NTP only at boot and every NTP_RESYNC_INTERVAL_MS (not on every upload)
static bool ensureTimeSynced() {
  bool needSync = !timeSynced ||
                  (millis() - lastNtpSyncMs) >= NTP_RESYNC_INTERVAL_MS;
  if (needSync && connectWiFi()) {
    syncTimeFromNtp();
  }
  return timeSynced;
}

// Only after errors: forces a fresh association on the next connectWiFi()
static void disconnectWiFi() {
  WiFi.disconnect();
  DBG_PRINTLN(F("WiFi disconnected (after error)"));
}

static bool inRange(float value, float minValue, float maxValue) {
  return value >= minValue && value <= maxValue;
}

// Same ranges as the server. Invalid samples are not sent (a dead sensor must not
// block the upload of everything after it).
static bool validateMeasurement(const Measurement &m) {
  if (!m.bme_valid) return false;
  if (!inRange(m.temperature, -50.0f, 60.0f)) return false;
  if (!inRange(m.pressure, 800.0f, 1200.0f)) return false;
  if (!inRange(m.humidity, 0.0f, 100.0f)) return false;
  if (!inRange(m.illuminance, 0.0f, 200000.0f)) return false;
  if (!inRange(m.sky_temp, -100.0f, 60.0f)) return false;
  if (!inRange(m.box_temp, -40.0f, 80.0f)) return false;
  if (m.uv_index > 20) return false;
  return true;  // wind and PM are capped when formatted (capU16), not rejected
}

static uint16_t capU16(uint16_t value, uint16_t maxValue) {
  return value > maxValue ? maxValue : value;
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
      "jd=%.7f&temperature=%.2f&pressure=%.2f&humidity=%.2f&illuminance=%.3f"
      "&wind_speed=%.0f&rain=%.2f&sky_temp=%.2f&box_temp=%.2f&is_raining=%d"
      "&pm1_0=%u&pm2_5=%u&pm10=%u&uv_index=%u",
      jd, m.temperature, m.pressure, m.humidity, m.illuminance,
      windRaw, rainMm, m.sky_temp, m.box_temp,
      m.is_raining ? 1 : 0, capU16(m.pm1_0, 1000), capU16(m.pm2_5, 1000), capU16(m.pm10, 1000),
      m.uv_index);
}

// Reads `count` body bytes, or until the server closes when count < 0.
// Returns false if the body could not be read completely.
static bool readHttpBody(WiFiSSLClient &client, long count, unsigned long deadline) {
  uint8_t buf[64];
  unsigned long idleSince = millis();
  while (count != 0 && millis() < deadline) {
    int avail = client.available();
    if (avail > 0) {
      int chunk = avail < (int)sizeof(buf) ? avail : (int)sizeof(buf);
      if (count > 0 && chunk > count) {
        chunk = (int)count;
      }
      int n = client.read(buf, (size_t)chunk);
      if (n > 0 && count > 0) {
        count -= n;
      }
      idleSince = millis();
      continue;
    }
    if (!client.connected()) {
      return count < 0;
    }
    if (count < 0 && millis() - idleSince > 500UL) {
      return true;  // unknown length: stop after 0.5 s without data
    }
    feedWatchdog();
    delay(2);
  }
  return count == 0;
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

// Reads status line, headers and exactly Content-Length body bytes, then returns
// at once and keeps the connection open for the next request. (Waiting for the
// server to close a keep-alive connection cost >= 2 s per upload before v1.8.)
// The client is stopped if the server closes or the response is incomplete.
static HttpPostResult readHttpPostResult(WiFiSSLClient &client) {
  HttpPostResult result = {HTTP_POST_TIMEOUT, 0, 0};
  unsigned long deadline = millis() + HTTP_RESPONSE_TIMEOUT_MS;
  char line[128];
  uint8_t lineIdx = 0;
  bool statusParsed = false;
  bool headersDone = false;
  long contentLength = -1;
  bool serverCloses = false;

  while (!headersDone && millis() < deadline) {
    if (!client.available()) {
      if (!client.connected()) {
        break;
      }
      feedWatchdog();
      delay(2);
      continue;
    }
    char c = client.read();
    if (c == '\r') {
      continue;
    }
    if (c != '\n') {
      if (lineIdx < sizeof(line) - 1) {
        line[lineIdx++] = c;
      }
      continue;
    }
    line[lineIdx] = '\0';
    if (lineIdx == 0) {
      headersDone = true;
    } else if (!statusParsed && strncmp(line, "HTTP/", 5) == 0) {
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
      result.retryAfterSec = parseRetryAfterSeconds(line + 12);
    } else if (startsWithIgnoreCase(line, "Content-Length:")) {
      contentLength = strtol(line + 15, NULL, 10);
    } else if (startsWithIgnoreCase(line, "Connection:") && strstr(line + 11, "close") != NULL) {
      serverCloses = true;
    }
    lineIdx = 0;
  }

  if (!headersDone) {
    if (!statusParsed) {
      result.resultClass = HTTP_POST_TIMEOUT;
    }
    client.stop();
    return result;
  }
  if (!readHttpBody(client, contentLength, deadline) || contentLength < 0 || serverCloses) {
    client.stop();
  }
  return result;
}

static bool beginUploadSession() {
  if (uploadBackoffActive()) {
    DBG_PRINTLN(F("Upload deferred (backoff)"));
    return false;
  }
  if (!connectWiFi()) {
    return false;
  }
  if (!ensureTimeSynced()) {
    DBG_PRINTLN(F("Upload skipped: time not synced"));
    return false;
  }
  if (!sslClient.connected()) {
    feedWatchdog();
    if (!sslClient.connect(SECRET_API_HOST, API_PORT)) {
      DBG_PRINTLN(F("SSL connect failed"));
      sslClient.stop();
      disconnectWiFi();
      return false;
    }
  }
  uploadSessionOpen = true;
  return true;
}

// Closes the TLS connection; WiFi stays associated for the next upload.
static void endUploadSession() {
  if (uploadSessionOpen) {
    sslClient.stop();
    uploadSessionOpen = false;
  }
}

static bool ensureSslConnected() {
  if (sslClient.connected()) {
    return true;
  }
  feedWatchdog();
  if (!sslClient.connect(SECRET_API_HOST, API_PORT)) {
    DBG_PRINTLN(F("SSL reconnect failed"));
    sslClient.stop();
    return false;
  }
  uploadSessionOpen = true;
  return true;
}

static void sendRequestHead(const char *path, const char *contentType, const char *timestamp,
                            const char *nonceHex, const char *signatureHex,
                            unsigned long contentLength, bool keepAlive) {
  sslClient.print(F("POST "));
  sslClient.print(path);
  sslClient.println(F(" HTTP/1.1"));
  sslClient.print(F("Host: "));
  sslClient.println(SECRET_API_HOST);
  sslClient.print(F("Content-Type: "));
  sslClient.println(contentType);
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
  sslClient.println(keepAlive ? F("Connection: keep-alive") : F("Connection: close"));
  sslClient.print(F("Content-Length: "));
  sslClient.println(contentLength);
  sslClient.println();
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

  if (!ensureSslConnected()) {
    return fail;
  }
  sendRequestHead(SECRET_API_PATH, WEATHER_HMAC_CONTENT_TYPE, timestamp, nonceHex, signatureHex,
                  (unsigned long)bodyLen, keepAlive);
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

void takeSnapshot(unsigned long now, bool allowUpload);
void runScheduledSensors(unsigned long now);

// Keeps measuring at 1 Hz while a burst is being uploaded. New snapshots are
// only appended to the ring buffer (no upload is started from here).
void checkI2cHealth(unsigned long now);

static void serviceDuringUpload() {
  feedWatchdog();
  unsigned long now = millis();
  runScheduledSensors(now);
  checkI2cHealth(now);
  if (now - lastSnapshotMs >= SNAPSHOT_INTERVAL_MS) {
    takeSnapshot(now, false);
  }
}

// One CSV line for a buffered sample; false if the sample is invalid (not sent).
// Depends only on the sample, so both passes of postBulk() produce identical bytes.
static int formatBufferedRow(const BufferedSample &s, char *line, size_t size) {
  if (s.jd <= 0.0 || !validateMeasurement(s.m)) {
    return -1;
  }
  BulkCsvSample row;
  row.jd = s.jd;
  row.temperature = s.m.temperature;
  row.pressure = s.m.pressure;
  row.humidity = s.m.humidity;
  row.illuminance = s.m.illuminance;
  row.wind = capU16(s.m.wind_revolutions, 500);  // server limit
  row.rain_mm = s.m.rain_tips * RAIN_MM_PER_TIP;
  row.sky_temp = s.m.sky_temp;
  row.box_temp = s.m.box_temp;
  row.is_raining = s.m.is_raining;
  row.pm1_0 = capU16(s.m.pm1_0, 1000);
  row.pm2_5 = capU16(s.m.pm2_5, 1000);
  row.pm10 = capU16(s.m.pm10, 1000);
  row.uv_index = s.m.uv_index;
  return formatBulkCsvLine(line, size, row);
}

// Sends ringBuffer[0..n) as one bulk request. The body is formatted twice: first
// to compute its length and SHA-256 for the signature, then to send it in
// ~512 byte pieces, so it never has to be held in RAM as a whole. `rows` returns
// the number of valid samples in the request (0: nothing to send).
static HttpPostResult postBulk(uint16_t n, uint16_t &rows) {
  HttpPostResult fail = {HTTP_POST_OTHER_ERROR, 0, 0};
  char line[BULK_CSV_LINE_MAX];
  const size_t headerLen = strlen(BULK_CSV_HEADER);

  SHA256_CTX ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, (const uint8_t *)BULK_CSV_HEADER, headerLen);
  unsigned long bodyLen = headerLen;
  rows = 0;
  for (uint16_t i = 0; i < n; i++) {
    int len = formatBufferedRow(ringBuffer[i], line, sizeof(line));
    if (len > 0) {
      sha256_update(&ctx, (const uint8_t *)line, (size_t)len);
      bodyLen += (unsigned long)len;
      rows++;
    }
  }
  if (rows == 0) {
    return fail;
  }
  uint8_t digest[32];
  sha256_final(&ctx, digest);
  char digestHex[WEATHER_HMAC_SHA256_HEX_LEN + 1];
  weatherHmacDigestToHex(digest, digestHex, sizeof(digestHex));

  unsigned long nowEpoch = currentUnixUtc();
  if (nowEpoch < 1000000000UL) {
    DBG_PRINTLN(F("Upload skipped: invalid timestamp"));
    return fail;
  }
  char timestamp[12];
  snprintf(timestamp, sizeof(timestamp), "%lu", nowEpoch);
  char nonceHex[WEATHER_HMAC_NONCE_HEX_LEN + 1];
  char signatureHex[WEATHER_HMAC_SIG_HEX_LEN + 1];
  if (!weatherHmacSignDigestHex(BULK_HMAC_PATH, WEATHER_HMAC_BULK_CONTENT_TYPE, SECRET_DEVICE_ID,
                                SECRET_KEY_ID, timestamp, digestHex, nonceHex, sizeof(nonceHex),
                                signatureHex, sizeof(signatureHex))) {
    DBG_PRINTLN(F("Upload skipped: HMAC signing failed"));
    return fail;
  }

  if (!ensureSslConnected()) {
    return fail;
  }
  sendRequestHead(BULK_API_PATH, WEATHER_HMAC_BULK_CONTENT_TYPE, timestamp, nonceHex,
                  signatureHex, bodyLen, true);

  char out[512];
  size_t used = headerLen;
  memcpy(out, BULK_CSV_HEADER, headerLen);
  for (uint16_t i = 0; i < n; i++) {
    int len = formatBufferedRow(ringBuffer[i], line, sizeof(line));
    if (len <= 0) {
      continue;
    }
    if (used + (size_t)len > sizeof(out)) {
      if (sslClient.write((const uint8_t *)out, used) != used) {
        sslClient.stop();
        return fail;
      }
      used = 0;
      serviceDuringUpload();
    }
    memcpy(out + used, line, (size_t)len);
    used += (size_t)len;
  }
  if (used > 0 && sslClient.write((const uint8_t *)out, used) != used) {
    sslClient.stop();
    return fail;
  }
  return readHttpPostResult(sslClient);
}

static void removeFromRingBuffer(uint16_t n) {
  if (n >= ringCount) {
    ringCount = 0;
    return;
  }
  memmove(&ringBuffer[0], &ringBuffer[n], (ringCount - n) * sizeof(BufferedSample));
  ringCount -= n;
}

// Uploads the ring buffer as bulk requests of at most BULK_MAX_ROWS samples, oldest
// first. Every attempt (also a failed one) restarts the UPLOAD_BURST_INTERVAL_MS
// timer, so a broken connection is retried once per interval instead of on every
// snapshot. A burst returns after BURST_MAX_DURATION_MS; the rest follows later.
static bool flushRingBuffer() {
  if (ringCount == 0) {
    return true;
  }

  DBG_PRINT(F("Burst upload: "));
  DBG_PRINT(ringCount);
  DBG_PRINTLN(F(" samples"));

  unsigned long start = millis();
  lastBurstAttemptMs = start;
  lastBurstOk = false;
  if (!beginUploadSession()) {
    return false;
  }

  bool failed = false;
  // Only the samples present now; snapshots taken during the upload are appended
  // behind them and follow with the next burst
  uint16_t pending = ringCount;
  while (pending > 0) {
    if (millis() - start >= BURST_MAX_DURATION_MS) {
      DBG_PRINTLN(F("Burst time limit reached"));
      break;
    }
    uint16_t n = pending < BULK_MAX_ROWS ? pending : BULK_MAX_ROWS;
    uint16_t rows = 0;
    HttpPostResult result = postBulk(n, rows);
    if (rows > 0 && result.resultClass == HTTP_POST_TIMEOUT && result.statusCode == 0) {
      // No response at all: the server may have closed the keep-alive connection
      // unnoticed. Retry once on a fresh connection; the server skips samples it
      // already stored, so a lost response cannot create duplicates.
      sslClient.stop();
      result = postBulk(n, rows);
    }

    if (rows == 0) {
      discardedSamples += n;  // only invalid samples in this block
      removeFromRingBuffer(n);
      pending -= n;
    } else if (result.resultClass == HTTP_POST_SUCCESS) {
      discardedSamples += n - rows;
      successfulUploads += rows;
      packetNumber += rows;
      lastSuccessfulUploadMs = millis();
      resetUploadBackoff();
      removeFromRingBuffer(n);
      pending -= n;
      DBG_PRINT(F("Bulk upload OK: "));
      DBG_PRINT(rows);
      DBG_PRINTLN(F(" samples"));
    } else if (result.statusCode == 400) {
      // Permanent error: retrying the same block would block the buffer forever
      discardedSamples += n;
      failedUploads++;
      removeFromRingBuffer(n);
      pending -= n;
      DBG_PRINTLN(F("Bulk upload rejected (HTTP 400), block discarded"));
    } else {
      failed = true;
      failedUploads++;
      if (result.resultClass == HTTP_POST_AUTH_OR_RATE_LIMIT) {
        applyUploadBackoff(result.retryAfterSec);
        DBG_PRINT(F("Burst auth/rate-limit HTTP "));
      } else {
        DBG_PRINT(F("Bulk upload failed, HTTP "));
      }
      DBG_PRINTLN(result.statusCode);
      break;
    }
    serviceDuringUpload();
  }

  endUploadSession();
  lastBurstOk = !failed;
  if (droppedDuringUpload > 0) {
    DBG_PRINT(F("Ring buffer full during upload, dropped "));
    DBG_PRINT(droppedDuringUpload);
    DBG_PRINTLN(F(" new samples"));
    droppedDuringUpload = 0;
  }
  return ringCount == 0;
}

static bool uploadLiveSample(const Measurement &m, double jd) {
  if (!uploadSessionOpen) {
    if (!beginUploadSession()) {
      liveRetryUntilMs = millis() + LIVE_RETRY_DELAY_MS;
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
  if (!validateMeasurement(m)) {
    discardedSamples++;  // invalid sample: skip it, keep the session
    return false;
  }

  bool ok = false;
  HttpPostResult result = postSample(m, useJd, true);
  if (result.resultClass == HTTP_POST_SUCCESS) {
    ok = true;
    successfulUploads++;
    packetNumber++;
    lastSuccessfulUploadMs = millis();
    resetUploadBackoff();
  } else {
    failedUploads++;
    if (result.resultClass == HTTP_POST_AUTH_OR_RATE_LIMIT) {
      applyUploadBackoff(result.retryAfterSec);
      DBG_PRINT(F("Live upload auth/rate-limit HTTP "));
      DBG_PRINTLN(result.statusCode);
    }
    endUploadSession();
    liveRetryUntilMs = millis() + LIVE_RETRY_DELAY_MS;
  }
  return ok;
}

static void handleUploads(const Measurement &m, unsigned long captureMs) {
  if (!wifi_module_ok) {
    return;
  }

  if (isStartupLiveMode()) {
    if (uploadBackoffActive() || (long)(millis() - liveRetryUntilMs) < 0) {
      return;
    }
    if (captureMs - lastLiveUploadMs >= LIVE_UPLOAD_INTERVAL_MS) {
      uploadLiveSample(m, jdAtMillis(captureMs));
      lastLiveUploadMs = captureMs;
    }
    return;
  }

  if (!burstModeAnnounced) {
    endUploadSession();
    DBG_PRINTLN(F("Upload mode: burst (buffered)"));
    burstModeAnnounced = true;
  }

  double jd = jdAtMillis(captureMs);
  if (jd > 0.0) {
    pushToRingBuffer(m, jd);
  }
  if (uploadBackoffActive()) {
    return;
  }

  bool due = (captureMs - lastBurstAttemptMs) >= UPLOAD_BURST_INTERVAL_MS;
  bool nearlyFull = ringCount >= (RING_BUFFER_SIZE - 4);
  if (ringCount > 0 && (due || (nearlyFull && lastBurstOk))) {
    if (!due) {
      DBG_PRINTLN(F("Ring buffer nearly full — early burst"));
    }
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
    DBG_PRINTLN(F("I2C multiplexer (PCA9548/TCA9548A, 0x70) OK"));
  } else {
    DBG_PRINTLN(F("ERROR: I2C multiplexer (0x70) not found"));
  }
}

#if SERIAL_DEBUG
bool selectI2CChannel(uint8_t channel);

static bool i2cDevicePresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static const __FlashStringHelper *i2cDeviceName(uint8_t addr) {
  switch (addr) {
    case 0x12: return F("PMSA003I");
    case 0x23: return F("SEN0636");
    case 0x29: return F("TSL2591");
    case 0x5A: return F("MLX90614");
    case 0x70: return F("multiplexer");
    case 0x76: return F("BME280");
    case 0x77: return F("BME280 (address 0x77, firmware expects 0x76)");
    default: return F("unknown");
  }
}

static void printI2CAddress(uint8_t addr) {
  Serial.print(F(" 0x"));
  if (addr < 0x10) {
    Serial.print('0');
  }
  Serial.print(addr, HEX);
  Serial.print(F(" ("));
  Serial.print(i2cDeviceName(addr));
  Serial.print(')');
}

// Lists the devices on the main bus and on every multiplexer channel. Devices on
// the main bus answer on every channel as well, so they are only listed once.
void scanI2CBus() {
  static const uint8_t EXPECTED[8] = {0x12, 0x76, 0x23, 0x5A, 0x29, 0, 0, 0};
  bool onMainBus[128] = {false};

  Serial.println(F("I2C scan"));
  Wire.beginTransmission(TCA9548A_ADDRESS);
  Wire.write((uint8_t)0);  // all channels off
  Wire.endTransmission();
  Serial.print(F("  main bus:"));
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    if (i2cDevicePresent(addr)) {
      onMainBus[addr] = true;
      printI2CAddress(addr);
    }
  }
  Serial.println();

  for (uint8_t ch = 0; ch < 8; ch++) {
    selectI2CChannel((uint8_t)(1 << ch));
    Serial.print(F("  channel "));
    Serial.print(ch);
    Serial.print(':');
    bool any = false;
    bool expectedFound = false;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
      if (onMainBus[addr] || !i2cDevicePresent(addr)) {
        continue;
      }
      any = true;
      expectedFound |= (addr == EXPECTED[ch]);
      printI2CAddress(addr);
    }
    if (!any) {
      Serial.print(F(" -"));
    }
    if (EXPECTED[ch] != 0 && !expectedFound) {
      Serial.print(F("   <-- expected"));
      printI2CAddress(EXPECTED[ch]);
      if (EXPECTED[ch] == 0x12) {
        Serial.print(F(", answers only while awake"));
      }
    }
    Serial.println();
  }
  Wire.beginTransmission(TCA9548A_ADDRESS);
  Wire.write((uint8_t)0);
  Wire.endTransmission();
}
#endif

// Returns false if the multiplexer did not acknowledge (bus stuck or mux missing).
// channel = 0 switches all channels off.
bool selectI2CChannel(uint8_t channel) {
  Wire.beginTransmission(TCA9548A_ADDRESS);
  Wire.write(channel);
  bool ok = Wire.endTransmission() == 0;
  if (ok) {
    muxFailStreak = 0;
  } else if (muxFailStreak < 255) {
    muxFailStreak++;
  }
  delay(1);
  return ok;
}

// Counts implausible readings; after SENSOR_FAIL_LIMIT in a row the sensor is
// marked unavailable and re-initialised by retryMissingSensors().
static void noteSensorResult(bool ok, uint8_t &streak, bool &available,
                             const __FlashStringHelper *name) {
  if (ok) {
    streak = 0;
    return;
  }
  if (streak < 255) {
    streak++;
  }
  if (streak == SENSOR_FAIL_LIMIT) {
    available = false;
    DBG_PRINT(name);
    DBG_PRINTLN(F(" failed repeatedly, will be re-initialised"));
  }
  (void)name;
}

static uint8_t failingSensorCount() {
  return (uint8_t)((bmeFailStreak >= SENSOR_FAIL_LIMIT) + (mlxFailStreak >= SENSOR_FAIL_LIMIT) +
                   (uvFailStreak >= SENSOR_FAIL_LIMIT) + (luxErrorStreak >= SENSOR_FAIL_LIMIT));
}

// Frees a bus that a device holds low (e.g. after a brown-out in the middle of a
// transfer): nine SCL pulses with SDA released, a STOP condition, then a fresh
// Wire.begin(). All sensors are re-initialised right after.
void recoverI2CBus(unsigned long now) {
  DBG_PRINTLN(F("I2C bus recovery"));
  Wire.end();
  pinMode(A4, INPUT_PULLUP);  // SDA released
  for (uint8_t i = 0; i < 9; i++) {
    pinMode(A5, OUTPUT);  // SCL low (pinMode(OUTPUT) drives LOW on the R4 core)
    digitalWrite(A5, LOW);
    delayMicroseconds(10);
    pinMode(A5, INPUT_PULLUP);  // SCL released
    delayMicroseconds(10);
  }
  pinMode(A4, OUTPUT);  // SDA low while SCL is high ...
  digitalWrite(A4, LOW);
  delayMicroseconds(10);
  pinMode(A4, INPUT_PULLUP);  // ... then high: STOP
  delayMicroseconds(10);
  Wire.begin();
  selectI2CChannel(0);

  bme_sensor_available = false;
  mlx_sensor_available = false;
  light_sensor_available = false;
  uv_sensor_available = false;
  pmsa003i_sensor_available = false;
  bmeFailStreak = mlxFailStreak = uvFailStreak = 0;
  luxErrorStreak = 0;
  i2cRecoveries++;
  lastI2cRecoveryMs = now;
  lastSensorRetryMs = now - SENSOR_RETRY_INTERVAL_MS;  // re-initialise on the next loop
}

// Recovers the bus when the multiplexer stops answering or several sensors fail
// at the same time (one failing sensor is just re-initialised).
void checkI2cHealth(unsigned long now) {
  bool stuck = muxFailStreak >= SENSOR_FAIL_LIMIT || failingSensorCount() >= 2;
  if (!stuck) {
    return;
  }
  if (lastI2cRecoveryMs != 0 && now - lastI2cRecoveryMs < i2cRecoveryIntervalMs) {
    return;
  }
  if (lastI2cRecoveryMs != 0 && i2cRecoveryIntervalMs < 300000UL) {
    i2cRecoveryIntervalMs *= 2;  // the last recovery did not help
  }
  recoverI2CBus(now);
}

void initializeBME280() {
  selectI2CChannel(TCA_CHANNEL_1);
  if (!bme.begin(0x76)) {
    bme_sensor_available = false;
    DBG_PRINTLN(F("BME280 not found"));
  } else {
    bme_sensor_available = true;
    bmeFailStreak = 0;
    DBG_PRINTLN(F("BME280 OK (ch1)"));
  }
}

void initializeMLX90614() {
  selectI2CChannel(TCA_CHANNEL_3);
  if (mlx.begin()) {
    mlx_sensor_available = true;
    mlxFailStreak = 0;
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

void initializeUVSensor(uint8_t attempts) {
  selectI2CChannel(TCA_CHANNEL_2);
  uv_sensor_available = false;
  for (uint8_t i = 0; i < attempts; i++) {
    if (uvSensor.begin()) {
      uv_sensor_available = true;
      uvFailStreak = 0;
      break;
    }
    if (i + 1 < attempts) {
      delay(1000);
    }
  }
  DBG_PRINTLN(uv_sensor_available ? F("SEN0636 UV OK (ch2)") : F("SEN0636 UV failed"));
}

// Search again for sensors that were not found (single attempt each, no waits).
// The PMSA003I is not included: it is initialised on every wake-up anyway.
void retryMissingSensors(unsigned long now) {
  if (now - lastSensorRetryMs < SENSOR_RETRY_INTERVAL_MS) {
    return;
  }
  lastSensorRetryMs = now;
  if (!bme_sensor_available) {
    initializeBME280();
  }
  if (!mlx_sensor_available) {
    initializeMLX90614();
  }
  if (!light_sensor_available) {
    initializeTSL2591();
  }
  if (!uv_sensor_available) {
    initializeUVSensor(1);
  }
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
  WiFi.setTimeout(WIFI_BEGIN_TIMEOUT_MS);
  sslClient.setConnectionTimeout(SSL_CONNECT_TIMEOUT_MS);
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

// Only plausible readings are stored; bme_valid = false keeps the sample from
// being uploaded (temperature, pressure and humidity are required by the server).
void readBME280Data() {
  sample.bme_valid = false;
  if (!bme_sensor_available) {
    return;
  }
  bool ok = selectI2CChannel(TCA_CHANNEL_1);
  float t = ok ? bme.readTemperature() : NAN;
  float p = ok ? bme.readPressure() / 100.0f : NAN;
  float h = ok ? bme.readHumidity() : NAN;
  ok = ok && inRange(t, -50.0f, 60.0f) && inRange(p, 800.0f, 1200.0f) && inRange(h, 0.0f, 100.0f);
  noteSensorResult(ok, bmeFailStreak, bme_sensor_available, F("BME280"));
  if (ok) {
    sample.temperature = t;
    sample.pressure = p;
    sample.humidity = h;
    sample.bme_valid = true;
    i2cRecoveryIntervalMs = I2C_RECOVERY_MIN_INTERVAL_MS;  // bus works again
  }
}

// 0/0 marks "not available"; the cloud detection on the website ignores it.
void readMLX90614Data() {
  if (!mlx_sensor_available) {
    sample.sky_temp = 0.0f;
    sample.box_temp = 0.0f;
    return;
  }
  bool ok = selectI2CChannel(TCA_CHANNEL_3);
  float sky = ok ? (float)mlx.readObjectTempC() : NAN;
  float box = ok ? (float)mlx.readAmbientTempC() : NAN;
  ok = ok && inRange(sky, -70.0f, 60.0f) && inRange(box, -40.0f, 80.0f);
  noteSensorResult(ok, mlxFailStreak, mlx_sensor_available, F("MLX90614"));
  sample.sky_temp = ok ? sky : 0.0f;
  sample.box_temp = ok ? box : 0.0f;
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
  bool ok = selectI2CChannel(TCA_CHANNEL_2);
  // A failed read returns 65535 (0xFFFF from Wire.read() = -1)
  uint16_t value = ok ? (uint16_t)uvSensor.readUvIndexData() : 0xFFFF;
  ok = ok && value <= 20;
  noteSensorResult(ok, uvFailStreak, uv_sensor_available, F("SEN0636"));
  last_uv_index = ok ? value : 0;
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
    rainTipsTotal++;
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

  // Channel 0 is switched off again after every access, before the sensor is put
  // to sleep: a sleeping PMSA003I must not stay connected to the main bus (a stuck
  // bus after its first measurement stopped all uploads in v1.8).
  unsigned long awakeTime = now - pmsa003i_wake_time;
  if (!pmsa003i_sensor_available && awakeTime >= PM_BOOT_DELAY) {
    selectI2CChannel(TCA_CHANNEL_0);
    bool found = aqi.begin_I2C();
    selectI2CChannel(0);
    if (found) {
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
  bool ok = aqi.read(&data);
  selectI2CChannel(0);
  if (ok) {
    last_pm1_0 = (uint16_t)data.pm10_standard;
    last_pm2_5 = (uint16_t)data.pm25_standard;
    last_pm10 = (uint16_t)data.pm100_standard;
    sample.pm1_0 = last_pm1_0;
    sample.pm2_5 = last_pm2_5;
    sample.pm10 = last_pm10;
  }
  if (ok || awakeTime >= PM_BOOT_DELAY + PM_WARMUP_TIME + PM_READ_TIMEOUT) {
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

// allowUpload = false while a burst is running (see serviceDuringUpload()):
// the snapshot is only appended to the ring buffer if there is room, because
// the burst is reading the buffer by index.
void takeSnapshot(unsigned long now, bool allowUpload) {
  updateWindReading();
  sample.rain_tips = rainTipsThisCycle;
  measureCycle++;
  sample.cycle = measureCycle;
  sample.packet_number = packetNumber;
  beginMeasurementCycle();
  lastSnapshotMs = now;
  Measurement snap = sample;  // sensors keep updating `sample` during an upload

  if (allowUpload) {
    handleUploads(snap, now);
    return;
  }
  double jd = jdAtMillis(now);
  if (jd <= 0.0) {
    return;
  }
  if (ringCount < RING_BUFFER_SIZE) {
    ringBuffer[ringCount].m = snap;
    ringBuffer[ringCount].jd = jd;
    ringCount++;
  } else {
    droppedDuringUpload++;
  }
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

// Sensor report for hardware tests (SERIAL_DEBUG 1): current reading of every
// sensor plus raw pin states, every DEBUG_INTERVAL_MS.
void printDebugInfo(unsigned long now) {
#if SERIAL_DEBUG
  Serial.println();
  Serial.print(F("--- t="));
  Serial.print(now / 1000UL);
  Serial.print(F(" s "));
  Serial.print(isStartupLiveMode() ? F("[live]") : F("[burst]"));
  Serial.print(F("  buffer "));
  Serial.print(ringCount);
  Serial.print('/');
  Serial.print(RING_BUFFER_SIZE);
  Serial.print(F("  pkt "));
  Serial.print(packetNumber);
  Serial.print(F("  uploads ok/failed "));
  Serial.print(successfulUploads);
  Serial.print('/');
  Serial.print(failedUploads);
  Serial.print(WiFi.status() == WL_CONNECTED ? F("  WiFi on") : F("  WiFi off"));
  Serial.println(watchdogRunning ? F("  watchdog on") : F("  watchdog OFF"));
  Serial.print(F("          last upload "));
  Serial.print((now - lastSuccessfulUploadMs) / 1000UL);
  Serial.print(F(" s ago  discarded "));
  Serial.print(discardedSamples);
  Serial.print(F("  I2C recoveries "));
  Serial.print(i2cRecoveries);
  Serial.print(F("  mux errors "));
  Serial.println(muxFailStreak);

  Serial.print(F("BME280    "));
  if (bme_sensor_available && !sample.bme_valid) {
    Serial.println(F("reading failed (sample not uploaded)"));
  } else if (bme_sensor_available) {
    Serial.print(F("T "));
    Serial.print(sample.temperature, 2);
    Serial.print(F(" C   RH "));
    Serial.print(sample.humidity, 1);
    Serial.print(F(" %   p "));
    Serial.print(sample.pressure, 1);
    Serial.println(F(" hPa"));
  } else {
    Serial.println(F("not available"));
  }

  Serial.print(F("MLX90614  "));
  if (mlx_sensor_available) {
    Serial.print(F("sky "));
    Serial.print(sample.sky_temp, 2);
    Serial.print(F(" C   box "));
    Serial.print(sample.box_temp, 2);
    Serial.print(F(" C   sky-box "));
    Serial.print(sample.sky_temp - sample.box_temp, 2);
    Serial.print(F(" K"));
    if (bme_sensor_available) {
      Serial.print(F("   sky-air "));
      Serial.print(sample.sky_temp - sample.temperature, 2);
      Serial.print(F(" K"));
    }
    Serial.println();
  } else {
    Serial.println(F("not available"));
  }

  Serial.print(F("TSL2591   "));
  if (light_sensor_available) {
    UWORD ch0, ch1;
    TSL2591_LastCounts(&ch0, &ch1);
    const tsl2591::Stage &st = tsl2591::STAGES[TSL2591_Stage()];
    Serial.print(sample.illuminance, 3);
    Serial.print(F(" lx   ch0 "));
    Serial.print(ch0);
    Serial.print(F("  ch1 "));
    Serial.print(ch1);
    Serial.print(F("   stage "));
    Serial.print(TSL2591_Stage());
    Serial.print(F(" ("));
    Serial.print((unsigned long)st.again);
    Serial.print(F("x "));
    Serial.print(st.atime_ms);
    Serial.print(F(" ms)"));
    if (luxErrorStreak > 0) {
      Serial.print(F("   I2C errors "));
      Serial.print(luxErrorStreak);
    }
    Serial.println();
  } else {
    Serial.println(F("not available"));
  }

  Serial.print(F("SEN0636   "));
  if (uv_sensor_available) {
    selectI2CChannel(TCA_CHANNEL_2);
    Serial.print(F("UV index "));
    Serial.print(sample.uv_index);
    Serial.print(F("   raw "));
    Serial.println(uvSensor.readUvOriginalData());
  } else {
    Serial.println(F("not available"));
  }

  Serial.print(F("PMSA003I  "));
  Serial.print(F("PM1.0 "));
  Serial.print(sample.pm1_0);
  Serial.print(F("  PM2.5 "));
  Serial.print(sample.pm2_5);
  Serial.print(F("  PM10 "));
  Serial.print(sample.pm10);
  Serial.print(F(" ug/m3   "));
  if (pmsa003i_awake) {
    Serial.print(F("awake "));
    Serial.print((now - pmsa003i_wake_time) / 1000UL);
    Serial.print(F(" s"));
    Serial.println(pmsa003i_sensor_available ? F(" (found)") : F(" (booting)"));
  } else {
    unsigned long since = now - last_pm_measurement;
    Serial.print(F("asleep, next in "));
    Serial.print(since < PM_MEASUREMENT_INTERVAL ? (PM_MEASUREMENT_INTERVAL - since) / 1000UL : 0UL);
    Serial.println(F(" s"));
  }

  Serial.print(F("Rain      tips "));
  Serial.print(rainTipsTotal);
  Serial.print(F(" since boot   D2 "));
  Serial.print(digitalRead(RAIN_REED_PIN) == HIGH ? F("HIGH") : F("LOW"));
  Serial.print(F("   drop sensor "));
  Serial.print(sample.is_raining ? F("wet") : F("dry"));
  Serial.print(F(" (D5 "));
  Serial.print(digitalRead(RAIN_DROP_DIGITAL_PIN) == HIGH ? F("HIGH") : F("LOW"));
  Serial.print(F(", A1 "));
  Serial.print(sample.rain_analog);
  Serial.println(')');

  uint32_t total = windPulseTotal;
  unsigned long dt = now - windDebugLastMs;
  Serial.print(F("Wind      pulses "));
  Serial.print(sample.wind_revolutions);
  Serial.print(F(" per s   "));
  Serial.print(dt > 0 ? (total - windPulseDebugLast) * 1000.0f / dt : 0.0f, 1);
  Serial.print(F(" /s (last "));
  Serial.print(dt / 1000.0f, 1);
  Serial.print(F(" s)   total "));
  Serial.print(total);
  Serial.print(F("   D3 "));
  Serial.println(digitalRead(WIND_SENSOR_PIN) == HIGH ? F("HIGH") : F("LOW"));
  windPulseDebugLast = total;
  windDebugLastMs = now;
#else
  (void)now;
#endif
}

// RESET_PIN (D4) is wired to the board's RESET pin. It stays a high-impedance
// input during operation and is only switched to output LOW for the reset:
// on the R4 core pinMode(OUTPUT) drives LOW immediately, so driving it HIGH at
// boot would pulse RESET and make the board reset in a loop.
void checkAutoReset() {
  if (AUTO_RESET_ENABLED && millis() > RESET_INTERVAL) {
    digitalWrite(RESET_PIN, LOW);
    pinMode(RESET_PIN, OUTPUT);
  }
}

void windIsr() {
  windPulseTotal++;
}

void setup() {
  pinMode(RESET_PIN, INPUT);  // released; see checkAutoReset()
  pinMode(RAIN_REED_PIN, INPUT);
  pinMode(RAIN_DROP_DIGITAL_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(WIND_SENSOR_PIN), windIsr, RISING);

  Serial.begin(SERIAL_BAUD);
#if SERIAL_DEBUG
  {
    unsigned long t0 = millis();
    while (!Serial && (millis() - t0 < 3000UL)) {
      ;
    }
  }
#endif

  bootMillis = millis();
  lastBurstAttemptMs = bootMillis;
  unsigned long t0 = millis();
  lastSnapshotMs = t0;
  lastBmeMs = t0;
  lastLuxMs = t0;
  lastMlxMs = t0;
  lastUvMs = t0;
  lastRainMs = t0;
  lastDebugMs = t0;
  lastSensorRetryMs = t0;
  windDebugLastMs = t0;
  lastSuccessfulUploadMs = t0;

  DBG_PRINTLN(F("OST Weather Station — UNO R4 WiFi v1.9.1"));

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
#if SERIAL_DEBUG
  scanI2CBus();
#endif
  initializeBME280();
  initializeMLX90614();
  initializeTSL2591();
  initializePMSA003I();
  initializeUVSensor(10);
  initializeWiFiModule();

  readBME280Data();
  readMLX90614Data();
  readTSL2591Data();
  readUVIndexData();
  processRainReed();
  readRainDropData();
  // First PM measurement right after boot (values after ~35 s instead of 5 min;
  // until then 0 is sent)
  last_pm_measurement = t0 - PM_MEASUREMENT_INTERVAL;
  servicePmsa003I(t0);

  DBG_PRINTLN(F("Initialization complete"));
  DBG_PRINTLN(F("Scheduler: snapshot 1 Hz, per-sensor timers (see README)"));
  DBG_PRINT(F("Startup live upload for "));
  DBG_PRINT(STARTUP_LIVE_UPLOAD_MS / 1000UL);
  DBG_PRINTLN(F(" s, then burst mode"));

  startWatchdog();
}

void loop() {
  feedWatchdog();
  unsigned long now = millis();

  runScheduledSensors(now);
  checkI2cHealth(now);
  retryMissingSensors(now);

  if (now - lastSnapshotMs >= SNAPSHOT_INTERVAL_MS) {
    takeSnapshot(now, true);
  }

  // Last resort for anything else that stops uploads while the loop keeps running
  if (wifi_module_ok && millis() - lastSuccessfulUploadMs > UPLOAD_STALL_RESET_MS) {
    DBG_PRINTLN(F("No successful upload for 10 min, resetting"));
    delay(100);
    NVIC_SystemReset();
  }

  if (now - lastDebugMs >= DEBUG_INTERVAL_MS) {
    printDebugInfo(now);
    lastDebugMs = now;
  }

  checkAutoReset();

  unsigned long sleepMs = millisUntilNextEvent(now);
  if (sleepMs > 0) {
    delay(sleepMs);
  }
}
