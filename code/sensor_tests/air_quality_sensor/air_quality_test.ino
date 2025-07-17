/*
 * CCS811 Air Quality Sensor Test
 * 
 * This program tests the CCS811 air quality sensor
 * and displays CO2 and TVOC values.
 * 
 * Hardware:
 * - CCS811 Air Quality Sensor
 * - Arduino Uno/Nano
 * 
 * Connections:
 * - VCC -> 3.3V
 * - GND -> GND
 * - SDA -> A4 (SDA)
 * - SCL -> A5 (SCL)
 * - WAK -> GND (Wake-up Pin)
 * 
 * I2C Address: 0x5B
 * 
 * Note: The CCS811 requires a warm-up time of approximately 20 minutes
 * for accurate measurements!
 */

#include <Wire.h>
#include <SparkFunCCS811.h>

// Create sensor object
CCS811 ccs811;

// Timing variables
unsigned long lastMeasurement = 0;
const unsigned long MEASUREMENT_INTERVAL = 1000; // 1 second
unsigned long startTime = 0;
bool sensorReady = false;

// Measurement values
uint16_t co2 = 0;
uint16_t tvoc = 0;
uint16_t baseline = 0;

void setup() {
  // Start serial communication
  Serial.begin(9600);
  while (!Serial) {
    ; // Wait for serial connection
  }
  
  Serial.println(F("CCS811 Air Quality Sensor Test"));
  Serial.println(F("============================="));
  
  // Start I2C
  Wire.begin();
  
  // Initialize sensor
  if (ccs811.begin()) {
    Serial.println(F("CCS811 Sensor initialized successfully!"));
    
    // Set Drive Mode (1 = 1 second measurements)
    ccs811.setDriveMode(1);
    Serial.println(F("Drive Mode set to 1 second"));
    
    startTime = millis();
    Serial.println(F("Sensor starting warm-up phase..."));
    Serial.println(F("Wait 20 minutes for accurate measurements"));
    Serial.println();
    
  } else {
    Serial.println(F("ERROR: CCS811 Sensor not found!"));
    Serial.println(F("Please check wiring and I2C address."));
    while (1); // Infinite loop on error
  }
  
  // Short pause
  delay(1000);
}

void loop() {
  // Only measure every MEASUREMENT_INTERVAL milliseconds
  if (millis() - lastMeasurement >= MEASUREMENT_INTERVAL) {
    lastMeasurement = millis();
    
    // Check if new data is available
    if (ccs811.dataAvailable()) {
      // Read algorithm results
      ccs811.readAlgorithmResults();
      
      // Read values
      co2 = ccs811.getCO2();
      tvoc = ccs811.getTVOC();
      baseline = ccs811.getBaseline();
      
      // Calculate warm-up time
      unsigned long warmupTime = (millis() - startTime) / 1000; // in seconds
      
      // Output results
      Serial.println(F("=== CCS811 Measurement ==="));
      Serial.print(F("Timestamp: "));
      Serial.print(millis());
      Serial.println(F(" ms"));
      
      Serial.print(F("Warm-up time: "));
      Serial.print(warmupTime);
      Serial.println(F(" seconds"));
      
      Serial.print(F("CO2: "));
      Serial.print(co2);
      Serial.println(F(" ppm"));
      
      Serial.print(F("TVOC: "));
      Serial.print(tvoc);
      Serial.println(F(" ppb"));
      
      Serial.print(F("Baseline: 0x"));
      Serial.println(baseline, HEX);
      
      // CO2 quality assessment
      Serial.print(F("CO2 Quality: "));
      if (co2 < 400) {
        Serial.println(F("Below Normal (400 ppm)"));
      } else if (co2 < 1000) {
        Serial.println(F("Normal (400-1000 ppm)"));
      } else if (co2 < 2000) {
        Serial.println(F("Elevated (1000-2000 ppm)"));
      } else if (co2 < 5000) {
        Serial.println(F("High (2000-5000 ppm)"));
      } else {
        Serial.println(F("Very High (>5000 ppm)"));
      }
      
      // TVOC quality assessment
      Serial.print(F("TVOC Quality: "));
      if (tvoc < 100) {
        Serial.println(F("Good (<100 ppb)"));
      } else if (tvoc < 220) {
        Serial.println(F("Moderate (100-220 ppb)"));
      } else if (tvoc < 660) {
        Serial.println(F("Poor (220-660 ppb)"));
      } else {
        Serial.println(F("Very Poor (>660 ppb)"));
      }
      
      Serial.println(F("====================="));
      Serial.println();
      
      // LED as status indicator
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      
    } else {
      // No new data available
      Serial.print(F("Waiting for new data... ("));
      Serial.print((millis() - startTime) / 1000);
      Serial.println(F(" seconds since start)"));
    }
  }
}

/*
 * Additional functions for extended tests:
 */

// Set Environmental Data (for better accuracy)
void setEnvironmentalData(float humidity, float temperature) {
  ccs811.setEnvironmentalData(humidity, temperature);
  Serial.print(F("Environmental Data set - Humidity: "));
  Serial.print(humidity);
  Serial.print(F("%, Temperature: "));
  Serial.print(temperature);
  Serial.println(F("°C"));
}

// Change Drive Mode
void setDriveMode(uint8_t mode) {
  if (mode >= 0 && mode <= 4) {
    ccs811.setDriveMode(mode);
    Serial.print(F("Drive Mode set to "));
    Serial.print(mode);
    Serial.println(F(""));
  } else {
    Serial.println(F("ERROR: Drive Mode must be between 0 and 4"));
  }
}

// Display sensor information
void printSensorInfo() {
  Serial.println(F("=== CCS811 Sensor Information ==="));
  Serial.print(F("I2C Address: 0x"));
  Serial.println(0x5B, HEX);
  Serial.print(F("Current Baseline: 0x"));
  Serial.println(ccs811.getBaseline(), HEX);
  Serial.println(F("================================="));
}

/*
 * Drive Mode Explanation:
 * 0: Idle (no measurements)
 * 1: Constant power mode, IAQ measurement every second
 * 2: Pulse heating mode IAQ measurement every 10 seconds
 * 3: Low power pulse heating mode IAQ measurement every 60 seconds
 * 4: Constant power mode, sensor measurement every 250ms
 */ 