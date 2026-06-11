/*
 * Gravity UV Index Sensor Test (SEN0636)
 *
 * This program tests the DFRobot Gravity UV Index Sensor (240-370nm)
 * and displays UV voltage, UV index, and risk level.
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
 * Note: Meaningful measurements require UV exposure (e.g. sunlight).
 */

#include <Wire.h>
#include <DFRobot_UVIndex240370Sensor.h>

// Create sensor object (I2C mode)
DFRobot_UVIndex240370Sensor uvSensor(&Wire);

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

  Serial.println(F("Gravity UV Index Sensor Test (SEN0636)"));
  Serial.println(F("======================================"));

  // Start I2C
  Wire.begin();

  // Initialize sensor
  while (uvSensor.begin() != true) {
    Serial.println(F("failed to init chip, please check the chip connection"));
    Serial.println(F("Ensure the mode switch is set to I2C"));
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
