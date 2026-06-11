/*
 * PMSA003I Air Quality Sensor Test
 *
 * This program tests the Adafruit PMSA003I particulate matter sensor
 * and displays PM1.0, PM2.5, and PM10 concentrations.
 *
 * Hardware:
 * - Adafruit PMSA003I Air Quality Breakout (STEMMA QT / Qwiic)
 * - Arduino Uno/Nano
 *
 * Connections (direct wiring):
 * - VIN -> 3.3V
 * - GND -> GND
 * - SDA -> A4 (SDA)
 * - SCL -> A5 (SCL)
 *
 * Alternatively, connect via STEMMA QT / Qwiic cable.
 *
 * I2C Address: 0x12 (fixed)
 *
 * Required Libraries (Arduino Library Manager):
 * - Adafruit PM25 AQI
 * - Adafruit BusIO
 *
 * Note: The PMSA003I requires a warm-up time of approximately 30 seconds
 * after power-on for stable measurements.
 */

#include <Wire.h>
#include <Adafruit_PM25AQI.h>

// Create sensor object
Adafruit_PM25AQI aqi;

// Timing variables
unsigned long lastMeasurement = 0;
const unsigned long MEASUREMENT_INTERVAL = 1000; // 1 second
unsigned long startTime = 0;
bool sensorReady = false;

void setup() {
  // Start serial communication
  Serial.begin(9600);
  while (!Serial) {
    ; // Wait for serial connection
  }

  Serial.println(F("PMSA003I Air Quality Sensor Test"));
  Serial.println(F("================================"));

  // Start I2C
  Wire.begin();

  // Sensor needs a few seconds after power-on before I2C communication
  Serial.println(F("Waiting for sensor boot-up..."));
  delay(3000);

  // Initialize sensor over I2C
  while (!aqi.begin_I2C()) {
    Serial.println(F("failed to init chip, please check the chip connection"));
    delay(1000);
  }
  sensorReady = true;
  Serial.println(F("PMSA003I Sensor initialized successfully!"));

  startTime = millis();
  Serial.println(F("Sensor starting warm-up phase..."));
  Serial.println(F("Wait 30 seconds for stable measurements"));
  Serial.println();

  // Short pause
  delay(1000);
}

void loop() {
  // Only measure every MEASUREMENT_INTERVAL milliseconds
  if (millis() - lastMeasurement >= MEASUREMENT_INTERVAL) {
    lastMeasurement = millis();

    if (!sensorReady) {
      return;
    }

    PM25_AQI_Data data;

    if (aqi.read(&data)) {
      unsigned long warmupTime = (millis() - startTime) / 1000; // in seconds

      Serial.println(F("=== PMSA003I Measurement ==="));
      Serial.print(F("Timestamp: "));
      Serial.print(millis());
      Serial.println(F(" ms"));

      Serial.print(F("Warm-up time: "));
      Serial.print(warmupTime);
      Serial.println(F(" seconds"));

      Serial.println(F("--- Standard Concentration (ug/m3) ---"));
      Serial.print(F("PM 1.0: "));
      Serial.println(data.pm10_standard);
      Serial.print(F("PM 2.5: "));
      Serial.println(data.pm25_standard);
      Serial.print(F("PM 10: "));
      Serial.println(data.pm100_standard);

      Serial.println(F("--- Environmental Concentration (ug/m3) ---"));
      Serial.print(F("PM 1.0: "));
      Serial.println(data.pm10_env);
      Serial.print(F("PM 2.5: "));
      Serial.println(data.pm25_env);
      Serial.print(F("PM 10: "));
      Serial.println(data.pm100_env);

      Serial.println(F("--- Particle Counts (per 0.1L air) ---"));
      Serial.print(F("> 0.3 um: "));
      Serial.println(data.particles_03um);
      Serial.print(F("> 0.5 um: "));
      Serial.println(data.particles_05um);
      Serial.print(F("> 1.0 um: "));
      Serial.println(data.particles_10um);
      Serial.print(F("> 2.5 um: "));
      Serial.println(data.particles_25um);
      Serial.print(F("> 5.0 um: "));
      Serial.println(data.particles_50um);
      Serial.print(F("> 10  um: "));
      Serial.println(data.particles_100um);

      Serial.println(F("--- Air Quality Index (US EPA) ---"));
      Serial.print(F("PM2.5 AQI: "));
      Serial.println(data.aqi_pm25_us);
      Serial.print(F("PM10  AQI: "));
      Serial.println(data.aqi_pm100_us);

      Serial.print(F("PM2.5 Quality: "));
      Serial.println(getAqiQuality(data.aqi_pm25_us));

      Serial.print(F("PM2.5 Concentration Quality: "));
      Serial.println(getPm25Quality(data.pm25_standard));

      Serial.println(F("====================="));
      Serial.println();

      // LED as status indicator
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

    } else {
      Serial.print(F("Waiting for new data... ("));
      Serial.print((millis() - startTime) / 1000);
      Serial.println(F(" seconds since start)"));
    }
  }
}

/*
 * Additional functions for extended tests:
 */

// US EPA AQI category for PM2.5
const __FlashStringHelper *getAqiQuality(uint16_t aqi) {
  if (aqi <= 50) {
    return F("Good (0-50)");
  } else if (aqi <= 100) {
    return F("Moderate (51-100)");
  } else if (aqi <= 150) {
    return F("Unhealthy for Sensitive Groups (101-150)");
  } else if (aqi <= 200) {
    return F("Unhealthy (151-200)");
  } else if (aqi <= 300) {
    return F("Very Unhealthy (201-300)");
  }
  return F("Hazardous (>300)");
}

// WHO-style PM2.5 concentration assessment (ug/m3, standard units)
const __FlashStringHelper *getPm25Quality(uint16_t pm25) {
  if (pm25 <= 15) {
    return F("Good (<=15 ug/m3)");
  } else if (pm25 <= 25) {
    return F("Moderate (16-25 ug/m3)");
  } else if (pm25 <= 50) {
    return F("Unhealthy for Sensitive Groups (26-50 ug/m3)");
  } else if (pm25 <= 75) {
    return F("Unhealthy (51-75 ug/m3)");
  } else if (pm25 <= 100) {
    return F("Very Unhealthy (76-100 ug/m3)");
  }
  return F("Hazardous (>100 ug/m3)");
}

// Display sensor information
void printSensorInfo() {
  Serial.println(F("=== PMSA003I Sensor Information ==="));
  Serial.print(F("I2C Address: 0x"));
  Serial.println(0x12, HEX);
  Serial.println(F("Interface: I2C (STEMMA QT compatible)"));
  Serial.println(F("==================================="));
}

// Perform I2C scan
void scanI2C() {
  Serial.println(F("=== I2C Scan ==="));
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.print(F("I2C device found at address 0x"));
      if (address < 16) {
        Serial.print(F("0"));
      }
      Serial.println(address, HEX);
    }
  }
  Serial.println(F("================"));
}
