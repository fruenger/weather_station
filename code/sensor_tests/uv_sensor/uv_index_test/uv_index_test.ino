/*
 * Gravity UV Index Sensor Test (SEN0636)
 *
 * This program tests the DFRobot Gravity UV Index Sensor (240-370nm)
 * and displays UV voltage, UV index, and risk level.
 *
 * NOT for Adafruit GUVA-S12SD (analog sensor) — use guva_s12sd_test.ino for that.
 *
 * Hardware:
 * - Gravity: UV Index Sensor (SKU SEN0636)
 * - Arduino Uno/Nano
 *
 * Connections (I2C mode):
 * - VCC -> 3.3V (or 5V, sensor supports 3.3-5V)
 * - GND -> GND
 * - D/R -> A4 (SDA)
 * - C/T -> A5 (SCL)
 *
 * Important: Set the communication mode switch on the sensor to I2C.
 * Power off the sensor before changing the switch position!
 *
 * I2C Address: 0x23
 *
 * Required Libraries (download from DFRobot GitHub):
 * - DFRobot_UVIndex240370Sensor
 * - DFRobot RTU
 *
 * If connected via TCA9548A I2C multiplexer, uncomment USE_I2C_MULTIPLEXER below
 * and set TCA_CHANNEL_UV to the correct channel.
 *
 * Note: Meaningful measurements require UV exposure (e.g. sunlight).
 */

#include <Wire.h>
#include <DFRobot_UVIndex240370Sensor.h>

// Uncomment if the sensor is behind a TCA9548A multiplexer (e.g. weather station)
// #define USE_I2C_MULTIPLEXER
#ifdef USE_I2C_MULTIPLEXER
#define TCA9548A_ADDRESS 0x70
#define TCA_CHANNEL_UV 0x20  // Channel 5 example — adjust to your wiring
#endif

// Create sensor object (I2C mode)
DFRobot_UVIndex240370Sensor uvSensor(&Wire);

// Timing variables
unsigned long lastMeasurement = 0;
const unsigned long MEASUREMENT_INTERVAL = 1000; // 1 second
unsigned long startTime = 0;
bool sensorReady = false;

void selectI2CChannel(uint8_t channel) {
#ifdef USE_I2C_MULTIPLEXER
  Wire.beginTransmission(TCA9548A_ADDRESS);
  Wire.write(channel);
  Wire.endTransmission();
  delay(1);
#else
  (void)channel;
#endif
}

void scanI2C();

const __FlashStringHelper *i2cErrorText(uint8_t error) {
  switch (error) {
    case 0: return F("OK");
    case 1: return F("data too long");
    case 2: return F("NACK on address (device not found)");
    case 3: return F("NACK on data");
    case 4: return F("other error");
    case 5: return F("timeout");
    default: return F("unknown");
  }
}

bool probeSensorAddress(uint8_t address) {
  Wire.beginTransmission(address);
  uint8_t error = Wire.endTransmission();
  Serial.print(F("  Probe 0x"));
  if (address < 16) Serial.print(F("0"));
  Serial.print(address, HEX);
  Serial.print(F(": "));
  Serial.println(i2cErrorText(error));
  return error == 0;
}

void setup() {
  // Start serial communication
  Serial.begin(115200);
  while (!Serial) {
    ; // Wait for serial connection
  }

  Serial.println(F("Gravity UV Index Sensor Test (SEN0636)"));
  Serial.println(F("======================================"));
  Serial.println(F("DFRobot I2C sensor — NOT the analog GUVA-S12SD!"));
  printSensorInfo();

  // Start I2C
  Wire.begin();
  delay(100);

#ifdef USE_I2C_MULTIPLEXER
  Serial.println(F("I2C multiplexer mode enabled"));
  selectI2CChannel(TCA_CHANNEL_UV);
#endif

  // Initialize sensor
  uint8_t attempts = 0;
  const uint8_t maxAttempts = 5;
  while (uvSensor.begin() != true) {
    attempts++;
    Serial.print(F("Init failed (attempt "));
    Serial.print(attempts);
    Serial.print(F("/"));
    Serial.print(maxAttempts);
    Serial.println(F(")"));

    probeSensorAddress(0x23);

    Serial.println(F("Checklist:"));
    Serial.println(F("  1. Mode switch on I2C (power off before switching!)"));
    Serial.println(F("  2. D/R -> A4 (SDA), C/T -> A5 (SCL)"));
    Serial.println(F("  3. VCC + GND connected"));
    Serial.println(F("  4. DFRobot_RTU library installed"));
#ifdef USE_I2C_MULTIPLEXER
    Serial.println(F("  5. Correct TCA9548A channel selected"));
#else
    Serial.println(F("  5. If using multiplexer: enable USE_I2C_MULTIPLEXER"));
#endif

    if (attempts == 1) {
      Serial.println();
      scanI2C();
      Serial.println();
    }

    if (attempts >= maxAttempts) {
      Serial.println(F("Giving up. Fix wiring/switch and reset Arduino."));
      while (true) {
        delay(5000);
      }
    }
    delay(1000);
  }
  sensorReady = true;
  Serial.println(F("UV Index Sensor initialized successfully!"));

  startTime = millis();
  Serial.println(F("Sensor ready for measurements"));
  Serial.println(F("Expose the sensor to sunlight or a UV source"));
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

#ifdef USE_I2C_MULTIPLEXER
    selectI2CChannel(TCA_CHANNEL_UV);
#endif

    uint16_t voltage = uvSensor.readUvOriginalData();
    uint16_t uvIndex = uvSensor.readUvIndexData();
    uint16_t riskLevel = uvSensor.readRiskLevelData();
    unsigned long runtime = (millis() - startTime) / 1000; // in seconds

    Serial.println(F("=== UV Index Measurement ==="));
    Serial.print(F("Timestamp: "));
    Serial.print(millis());
    Serial.println(F(" ms"));

    Serial.print(F("Runtime: "));
    Serial.print(runtime);
    Serial.println(F(" seconds"));

    Serial.print(F("UV Voltage (raw): "));
    Serial.print(voltage);
    Serial.println(F(" mV"));

    Serial.print(F("UV Index: "));
    Serial.println(uvIndex);

    Serial.print(F("Risk Level: "));
    Serial.print(riskLevel);
    Serial.print(F(" ("));
    Serial.print(getRiskLevelText(riskLevel));
    Serial.println(F(")"));

    Serial.print(F("UV Index Quality: "));
    Serial.println(getUvIndexQuality(uvIndex));

    Serial.print(F("Protection Advice: "));
    Serial.println(getProtectionAdvice(uvIndex));

    Serial.println(F("====================="));
    Serial.println();

    // LED as status indicator
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
}

/*
 * Additional functions for extended tests:
 */

// Sensor risk level (0-4)
const __FlashStringHelper *getRiskLevelText(uint16_t level) {
  switch (level) {
    case 0:
      return F("Low Risk");
    case 1:
      return F("Moderate Risk");
    case 2:
      return F("High Risk");
    case 3:
      return F("Very High Risk");
    case 4:
      return F("Extreme Risk");
    default:
      return F("Unknown");
  }
}

// WHO UV index scale (0-11+)
const __FlashStringHelper *getUvIndexQuality(uint16_t uvIndex) {
  if (uvIndex <= 2) {
    return F("Low (0-2)");
  } else if (uvIndex <= 5) {
    return F("Moderate (3-5)");
  } else if (uvIndex <= 7) {
    return F("High (6-7)");
  } else if (uvIndex <= 10) {
    return F("Very High (8-10)");
  }
  return F("Extreme (11+)");
}

// WHO protection recommendations
const __FlashStringHelper *getProtectionAdvice(uint16_t uvIndex) {
  if (uvIndex <= 2) {
    return F("No protection required");
  } else if (uvIndex <= 5) {
    return F("Wear sunglasses; use sunscreen if outside for extended periods");
  } else if (uvIndex <= 7) {
    return F("Reduce sun exposure between 10am and 4pm; wear protection");
  } else if (uvIndex <= 10) {
    return F("Minimize sun exposure; extra protection required");
  }
  return F("Avoid sun exposure; full protection required");
}

// Display sensor information
void printSensorInfo() {
  Serial.println(F("=== UV Index Sensor Information ==="));
  Serial.print(F("I2C Address: 0x"));
  Serial.println(0x23, HEX);
  Serial.println(F("Wavelength Range: 240-370 nm (UVA, UVB, UVC)"));
  Serial.println(F("Communication: I2C"));
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
