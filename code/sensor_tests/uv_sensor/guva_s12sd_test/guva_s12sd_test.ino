/*
 * Adafruit GUVA-S12SD Analog UV Sensor Test
 *
 * This program tests the Adafruit Analog UV Light Sensor Breakout (GUVA-S12SD)
 * and displays raw ADC value, output voltage, estimated UV index, and risk level.
 *
 * Hardware:
 * - Adafruit Analog UV Light Sensor Breakout - GUVA-S12SD (Product ID 1918)
 * - Arduino Uno/Nano
 *
 * Connections:
 * - VCC (3Vo on breakout) -> 3.3V or 5V (sensor supports 2.7-5.5V)
 * - GND -> GND
 * - OUT -> A0 (analog input)
 *
 * No external library required — the sensor provides an analog voltage output.
 *
 * Conversion (per Adafruit datasheet):
 * - Output voltage Vo = 4.3 * photocurrent (uA)
 * - UV Index ≈ Vo / 0.1 V  (e.g. 0.5 V -> UV Index 5)
 *
 * Note: Meaningful measurements require real UV exposure (sunlight).
 * Standard 400 nm UV LEDs are outside the sensor range and won't work for testing.
 * Use sunlight or a UV tanning / reptile lamp instead.
 *
 * Calibration: Individual modules may deviate. Compare readings with a local
 * weather station and adjust UV_INDEX_MV_PER_UNIT if needed (default: 100 mV per UV index step).
 */

// --- Configuration ---
const uint8_t UV_SENSOR_PIN = A0;
const float ADC_REF_V = 5.0;              // Uno/Nano default when Vref = AVcc (5V)
const uint16_t ADC_MAX = 1023;            // 10-bit ADC on ATmega328P
const float OPAMP_GAIN_V_PER_UA = 4.3;    // Vo = 4.3 * I(uA)
const float UV_INDEX_MV_PER_UNIT = 100.0; // 0.1 V per UV index step

const uint8_t SAMPLE_COUNT = 10;          // Samples averaged per reading
const unsigned long MEASUREMENT_INTERVAL = 1000; // ms

unsigned long lastMeasurement = 0;
unsigned long startTime = 0;

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ;
  }

  Serial.println(F("Adafruit GUVA-S12SD UV Sensor Test"));
  Serial.println(F("=================================="));

  printSensorInfo();

  startTime = millis();
  Serial.println(F("Sensor ready for measurements"));
  Serial.println(F("Expose the sensor to sunlight or a UV source"));
  Serial.println();

  delay(1000);
}

void loop() {
  if (millis() - lastMeasurement < MEASUREMENT_INTERVAL) {
    return;
  }
  lastMeasurement = millis();

  uint16_t rawAdc = readAveragedAdc(UV_SENSOR_PIN, SAMPLE_COUNT);
  float voltage = adcToVoltage(rawAdc);
  float photocurrentUa = voltage / OPAMP_GAIN_V_PER_UA;
  float uvIndex = voltageToUvIndex(voltage);
  uint16_t riskLevel = uvIndexToRiskLevel(uvIndex);
  unsigned long runtime = (millis() - startTime) / 1000;

  Serial.println(F("=== GUVA-S12SD Measurement ==="));
  Serial.print(F("Timestamp: "));
  Serial.print(millis());
  Serial.println(F(" ms"));

  Serial.print(F("Runtime: "));
  Serial.print(runtime);
  Serial.println(F(" seconds"));

  Serial.print(F("ADC raw (avg): "));
  Serial.println(rawAdc);

  Serial.print(F("Output voltage: "));
  Serial.print(voltage, 3);
  Serial.println(F(" V"));

  Serial.print(F("Photocurrent (est.): "));
  Serial.print(photocurrentUa, 3);
  Serial.println(F(" uA"));

  Serial.print(F("UV Index (est.): "));
  Serial.println(uvIndex, 1);

  Serial.print(F("Risk Level: "));
  Serial.print(riskLevel);
  Serial.print(F(" ("));
  Serial.print(getRiskLevelText(riskLevel));
  Serial.println(F(")"));

  Serial.print(F("UV Index Quality: "));
  Serial.println(getUvIndexQuality((uint16_t)uvIndex));

  Serial.print(F("Protection Advice: "));
  Serial.println(getProtectionAdvice((uint16_t)uvIndex));

  Serial.println(F("=============================="));
  Serial.println();

  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
}

uint16_t readAveragedAdc(uint8_t pin, uint8_t samples) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  return (uint16_t)(sum / samples);
}

float adcToVoltage(uint16_t rawAdc) {
  return (rawAdc * ADC_REF_V) / ADC_MAX;
}

float voltageToUvIndex(float voltage) {
  if (voltage <= 0.0) {
    return 0.0;
  }
  return (voltage * 1000.0) / UV_INDEX_MV_PER_UNIT;
}

uint16_t uvIndexToRiskLevel(float uvIndex) {
  if (uvIndex < 3.0) {
    return 0;
  }
  if (uvIndex < 6.0) {
    return 1;
  }
  if (uvIndex < 8.0) {
    return 2;
  }
  if (uvIndex < 11.0) {
    return 3;
  }
  return 4;
}

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

void printSensorInfo() {
  Serial.println(F("=== GUVA-S12SD Sensor Information ==="));
  Serial.print(F("Analog pin: A"));
  Serial.println(UV_SENSOR_PIN - A0);
  Serial.print(F("ADC reference: "));
  Serial.print(ADC_REF_V, 1);
  Serial.println(F(" V"));
  Serial.println(F("Wavelength range: 240-370 nm (UVA, UVB, partial UVC)"));
  Serial.println(F("Communication: Analog voltage output"));
  Serial.println(F("====================================="));
}
