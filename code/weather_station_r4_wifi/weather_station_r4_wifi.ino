/*
 * Weather Station - Arduino UNO R4 WiFi
 * Version: 1.1.0
 *
 * All-in-one outdoor station: sensors + direct HTTPS upload to the OST Django API.
 * Values are stored in physical units (no int16 scaling — that was for the 32-byte nRF24 payload).
 *
 * Board: Arduino UNO R4 WiFi (Renesas RA4M1 + ESP32-S3 WiFi coprocessor)
 * Sensors: BME280, MLX90614, TSL2591, PMSA003I, SEN0636, rain, anemometer
 * I2C: TCA9548A multiplexer on Wire (SDA=A4, SCL=A5)
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

#include "secrets.h"

#ifndef SECRET_WIFI_SSID
#error "Create secrets.h from secrets.h.example (WiFi + API credentials)."
#endif

#define SERIAL_DEBUG 0
#define WIFI_DISCONNECT_AFTER_UPLOAD 1

const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;
const unsigned long HTTP_RESPONSE_TIMEOUT_MS = 15000UL;
const uint16_t API_PORT = 443;

#if SERIAL_DEBUG
#define DBG_PRINT(...) Serial.print(__VA_ARGS__)
#define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define DBG_PRINT(...) ((void)0)
#define DBG_PRINTLN(...) ((void)0)
#endif

#define WIND_MEASUREMENT_TIME 1000
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

// Physical units — matches Django DatasetSerializer / receive.py upload fields
struct Measurement {
  uint32_t cycle;
  float temperature;      // °C
  float pressure;         // hPa
  float humidity;         // %
  float illuminance;      // lux
  uint16_t rain_tips;     // tipping-bucket count since last successful upload
  uint16_t wind_revolutions;
  uint32_t packet_number;
  float sky_temp;         // °C
  float box_temp;         // °C
  bool is_raining;
  uint16_t rain_analog;   // debug only, not uploaded
  uint16_t pm1_0;         // µg/m³
  uint16_t pm2_5;
  uint16_t pm10;
  uint16_t uv_index;
};

Adafruit_BME280 bme;
Adafruit_MLX90614 mlx;
Adafruit_PM25AQI aqi;
DFRobot_UVIndex240370Sensor uvSensor(&Wire);
WiFiSSLClient sslClient;

Measurement sample;

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

int InterruptCounter = 0;

bool firstLoop = true;
bool lastState = false;
unsigned long lastChanged = 0;
uint16_t accumulated_rain_tips = 0;
bool rain_data_pending = false;

uint32_t packetNumber = 0;
uint32_t measureCycle = 0;
uint16_t successfulUploads = 0;
uint16_t failedUploads = 0;

bool timeSynced = false;
unsigned long epochAtSync = 0;
unsigned long millisAtSync = 0;

// ---------------------------------------------------------------------------
// WiFi / HTTP helpers
// ---------------------------------------------------------------------------

static void base64Encode(const char *input, char *output, size_t outputSize) {
  static const char *b64 =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t inLen = strlen(input);
  size_t outIdx = 0;

  for (size_t i = 0; i < inLen; i += 3) {
    uint32_t octetA = (uint8_t)input[i];
    uint32_t octetB = (i + 1 < inLen) ? (uint8_t)input[i + 1] : 0;
    uint32_t octetC = (i + 2 < inLen) ? (uint8_t)input[i + 2] : 0;
    uint32_t triple = (octetA << 16) | (octetB << 8) | octetC;

    if (outIdx + 4 >= outputSize) {
      break;
    }
    output[outIdx++] = b64[(triple >> 18) & 0x3F];
    output[outIdx++] = b64[(triple >> 12) & 0x3F];
    output[outIdx++] = (i + 1 < inLen) ? b64[(triple >> 6) & 0x3F] : '=';
    output[outIdx++] = (i + 2 < inLen) ? b64[triple & 0x3F] : '=';
  }
  output[outIdx] = '\0';
}

static double julianDateFromUnix(unsigned long unixUtc) {
  return 2440587.5 + ((double)unixUtc / 86400.0);
}

static double currentJulianDate() {
  if (!timeSynced) {
    return 0.0;
  }
  unsigned long elapsedSec = (millis() - millisAtSync) / 1000UL;
  return julianDateFromUnix(epochAtSync + elapsedSec);
}

static bool syncTimeFromNtp() {
  unsigned long epoch = 0;
  for (uint8_t attempt = 0; attempt < 20; attempt++) {
    if (WiFi.getTime(epoch) == 0 && epoch > 1000000000UL) {
      epochAtSync = epoch;
      millisAtSync = millis();
      timeSynced = true;
      DBG_PRINT(F("NTP time synced, epoch="));
      DBG_PRINTLN(epoch);
      return true;
    }
    delay(500);
  }
  DBG_PRINTLN(F("WARNING: NTP time sync failed"));
  return false;
}

static bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
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
  syncTimeFromNtp();
  return true;
}

static void disconnectWiFi() {
#if WIFI_DISCONNECT_AFTER_UPLOAD
  WiFi.disconnect();
  DBG_PRINTLN(F("WiFi disconnected (power save)"));
#endif
}

static bool inRange(float value, float minValue, float maxValue) {
  return value >= minValue && value <= maxValue;
}

// Ranges aligned with datasets/api/serializers.py (DatasetSerializer)
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

  return snprintf(
      body, bodySize,
      "jd=%.5f&temperature=%.2f&pressure=%.2f&humidity=%.2f&illuminance=%.1f"
      "&wind_speed=%.0f&rain=%.2f&sky_temp=%.2f&box_temp=%.2f&is_raining=%d"
      "&pm1_0=%u&pm2_5=%u&pm10=%u&uv_index=%u",
      jd, m.temperature, m.pressure, m.humidity, m.illuminance,
      (float)m.wind_revolutions, rainMm, m.sky_temp, m.box_temp,
      m.is_raining ? 1 : 0, m.pm1_0, m.pm2_5, m.pm10, m.uv_index);
}

static bool readHttpSuccess(WiFiSSLClient &client) {
  unsigned long deadline = millis() + HTTP_RESPONSE_TIMEOUT_MS;
  char line[96];
  uint8_t lineIdx = 0;

  while (millis() < deadline) {
    while (client.available()) {
      char c = client.read();
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        line[lineIdx] = '\0';
        if (lineIdx > 0) {
          if (strstr(line, "HTTP/1.1 200") != NULL ||
              strstr(line, "HTTP/1.1 201") != NULL ||
              strstr(line, "HTTP/2 200") != NULL ||
              strstr(line, "HTTP/2 201") != NULL) {
            return true;
          }
          if (strncmp(line, "HTTP/", 5) == 0) {
            DBG_PRINT(F("HTTP response: "));
            DBG_PRINTLN(line);
            return false;
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
  return false;
}

static bool uploadToApi(const Measurement &m) {
  if (!validateMeasurement(m)) {
    DBG_PRINTLN(F("Upload skipped: validation failed"));
    return false;
  }

  double jd = currentJulianDate();
  if (jd <= 0.0) {
    DBG_PRINTLN(F("Upload skipped: no valid Julian date (NTP failed)"));
    return false;
  }

  if (!connectWiFi()) {
    return false;
  }

  char body[512];
  int bodyLen = buildUploadBody(body, sizeof(body), m, jd);
  if (bodyLen <= 0 || bodyLen >= (int)sizeof(body)) {
    DBG_PRINTLN(F("Upload body build failed"));
    return false;
  }

  char credentials[96];
  snprintf(credentials, sizeof(credentials), "%s:%s", SECRET_API_USER, SECRET_API_PASS);
  char authB64[140];
  base64Encode(credentials, authB64, sizeof(authB64));

  DBG_PRINT(F("HTTPS POST to "));
  DBG_PRINT(SECRET_API_HOST);
  DBG_PRINTLN(SECRET_API_PATH);

  if (!sslClient.connect(SECRET_API_HOST, API_PORT)) {
    DBG_PRINTLN(F("SSL connect failed — check firmware + host certificate"));
    disconnectWiFi();
    return false;
  }

  sslClient.print(F("POST "));
  sslClient.print(SECRET_API_PATH);
  sslClient.println(F(" HTTP/1.1"));
  sslClient.print(F("Host: "));
  sslClient.println(SECRET_API_HOST);
  sslClient.print(F("Authorization: Basic "));
  sslClient.println(authB64);
  sslClient.println(F("Content-Type: application/x-www-form-urlencoded"));
  sslClient.println(F("Connection: close"));
  sslClient.print(F("Content-Length: "));
  sslClient.println(bodyLen);
  sslClient.println();
  sslClient.print(body);

  bool ok = readHttpSuccess(sslClient);
  sslClient.stop();
  disconnectWiFi();
  return ok;
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

void readTSL2591Data() {
  if (light_sensor_available) {
    selectI2CChannel(TCA_CHANNEL_4);
    sample.illuminance = (float)TSL2591_Read_Lux();
  } else {
    sample.illuminance = 0.0f;
  }
}

void readPMSA003IData() {
  sample.pm1_0 = last_pm1_0;
  sample.pm2_5 = last_pm2_5;
  sample.pm10 = last_pm10;

  unsigned long now = millis();
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

void readUVIndexData() {
  sample.uv_index = last_uv_index;
  if (!uv_sensor_available) {
    return;
  }
  selectI2CChannel(TCA_CHANNEL_2);
  last_uv_index = (uint16_t)uvSensor.readUvIndexData();
  sample.uv_index = last_uv_index;
}

void readRainReedData() {
  int reedState = digitalRead(RAIN_REED_PIN);
  if (firstLoop) {
    lastState = reedState;
    firstLoop = false;
  }
  if (reedState != lastState && (millis() - lastChanged) > 250) {
    accumulated_rain_tips++;
    rain_data_pending = true;
    lastState = reedState;
    lastChanged = millis();
  }
  sample.rain_tips = accumulated_rain_tips;
}

void readRainDropData() {
  sample.is_raining = (digitalRead(RAIN_DROP_DIGITAL_PIN) == LOW);
  sample.rain_analog = (uint16_t)analogRead(RAIN_DROP_ANALOG_PIN);
}

void readAnemometerData() {
  InterruptCounter = 0;
  attachInterrupt(digitalPinToInterrupt(WIND_SENSOR_PIN), countup, RISING);
  delay(WIND_MEASUREMENT_TIME);
  detachInterrupt(digitalPinToInterrupt(WIND_SENSOR_PIN));
  sample.wind_revolutions = (uint16_t)InterruptCounter;
}

void uploadMeasurement() {
  if (!wifi_module_ok) {
    DBG_PRINTLN(F("WiFi module unavailable, skipping upload"));
    return;
  }

  if (uploadToApi(sample)) {
    successfulUploads++;
    if (rain_data_pending) {
      accumulated_rain_tips = 0;
      rain_data_pending = false;
    }
    DBG_PRINT(F("Packet "));
    DBG_PRINT(packetNumber);
    DBG_PRINTLN(F(" uploaded"));
  } else {
    failedUploads++;
    DBG_PRINT(F("Packet "));
    DBG_PRINT(packetNumber);
    DBG_PRINTLN(F(" upload failed"));
  }

  packetNumber++;
  if (packetNumber % 100 == 0) {
    DBG_PRINT(F("Upload stats OK="));
    DBG_PRINT(successfulUploads);
    DBG_PRINT(F(" FAIL="));
    DBG_PRINTLN(failedUploads);
  }
}

void printDebugInfo() {
  DBG_PRINT(F("T="));
  DBG_PRINT(sample.temperature, 1);
  DBG_PRINT(F("C P="));
  DBG_PRINT(sample.pressure, 1);
  DBG_PRINT(F("hPa H="));
  DBG_PRINT(sample.humidity, 0);
  DBG_PRINT(F("% Lux="));
  DBG_PRINT(sample.illuminance, 0);
  DBG_PRINT(F(" RainTips="));
  DBG_PRINT(sample.rain_tips);
  DBG_PRINT(F(" Wind="));
  DBG_PRINT(sample.wind_revolutions);
  DBG_PRINT(F(" PM2.5="));
  DBG_PRINT(sample.pm2_5);
  DBG_PRINT(F(" UV="));
  DBG_PRINTLN(sample.uv_index);
}

void checkAutoReset() {
  if (millis() > RESET_INTERVAL) {
    digitalWrite(RESET_PIN, LOW);
  }
}

void countup() {
  InterruptCounter++;
}

void setup() {
  pinMode(RESET_PIN, OUTPUT);
  digitalWrite(RESET_PIN, HIGH);
  pinMode(RAIN_REED_PIN, INPUT);
  pinMode(RAIN_DROP_DIGITAL_PIN, INPUT);

  Serial.begin(9600);
#if SERIAL_DEBUG
  {
    unsigned long t0 = millis();
    while (!Serial && (millis() - t0 < 3000UL)) {
      ;
    }
  }
#endif

  DBG_PRINTLN(F("OST Weather Station — UNO R4 WiFi v1.1.0"));

  initializeI2CMultiplexer();
  initializeBME280();
  initializeMLX90614();
  initializeTSL2591();
  initializePMSA003I();
  initializeUVSensor();
  initializeWiFiModule();

  DBG_PRINTLN(F("Initialization complete"));
}

void loop() {
  measureCycle++;
  sample.cycle = measureCycle;
  sample.packet_number = packetNumber;

  readBME280Data();
  readMLX90614Data();
  readTSL2591Data();
  readPMSA003IData();
  readUVIndexData();
  readRainReedData();
  readRainDropData();
  readAnemometerData();

  uploadMeasurement();
  printDebugInfo();
  checkAutoReset();
}
