/*
 * CCS811 Air Quality Sensor Test with Environmental Compensation
 * 
 * This program tests the CCS811 air quality sensor
 * with BME280 Environmental Compensation for more accurate measurements.
 * 
 * Hardware:
 * - CCS811 Air Quality Sensor
 * - BME280 Temperature/Humidity Sensor
 * - Arduino Uno/Nano
 * 
 * Connections:
 * CCS811:
 * - VCC -> 3.3V
 * - GND -> GND
 * - SDA -> A4 (SDA)
 * - SCL -> A5 (SCL)
 * - WAK -> GND (Wake-up Pin)
 * 
 * BME280:
 * - VCC -> 3.3V
 * - GND -> GND
 * - SDA -> A4 (SDA)
 * - SCL -> A5 (SCL)
 * 
 * I2C Addresses:
 * - CCS811: 0x5B
 * - BME280: 0x76
 * 
 * Note: The CCS811 requires a warm-up time of approximately 20 minutes
 * for accurate measurements!
 */

#include <Wire.h>
#include <DFRobot_CCS811.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// Create sensor objects
DFRobot_CCS811 ccs811;
Adafruit_BME280 bme;

// Timing variables
unsigned long lastMeasurement = 0;
const unsigned long MEASUREMENT_INTERVAL = 1000; // 1 second
unsigned long startTime = 0;
bool ccs811Ready = false;
bool bmeReady = false;

// Measurement values
uint16_t co2 = 0;
uint16_t tvoc = 0;
uint16_t baseline = 0;
float temperature = 0;
float humidity = 0;
float pressure = 0;

void setup() {
  // Start serial communication
  Serial.begin(9600);
  while (!Serial) {
    ; // Wait for serial connection
  }
  
  Serial.println(F("CCS811 + BME280 Environmental Compensation Test"));
  Serial.println(F("==============================================="));
  
  // Start I2C
  Wire.begin();
  
  // Initialize BME280
  if (bme.begin(0x76)) {
    bmeReady = true;
    Serial.println(F("BME280 Sensor initialized successfully!"));
  } else {
    Serial.println(F("WARNING: BME280 Sensor not found!"));
    Serial.println(F("Environmental Compensation will be disabled."));
  }
  
  // Initialize CCS811
  while(ccs811.begin() != 0) {
    Serial.println(F("failed to init chip, please check the chip connection"));
    delay(1000);
  }
  ccs811Ready = true;
  Serial.println(F("CCS811 Sensor initialized successfully!"));
  
  startTime = millis();
  Serial.println(F("Sensor starting warm-up phase..."));
  Serial.println(F("Wait 20 minutes for accurate measurements"));
  Serial.println();
  
  // Short pause
  delay(1000);
}

void loop() {
  // Only measure every MEASUREMENT_INTERVAL milliseconds
  if (millis() - lastMeasurement >= MEASUREMENT_INTERVAL) {
    lastMeasurement = millis();
    
    // Read BME280 data (if available)
    if (bmeReady) {
      temperature = bme.readTemperature();
      humidity = bme.readHumidity();
      pressure = bme.readPressure() / 100.0; // in hPa
    }
    
    // Read CCS811 data (if available)
    if (ccs811Ready && ccs811.checkDataReady()) {
      // Environmental Compensation (if BME280 available)
      if (bmeReady) {
        ccs811.setInTempHum(temperature, humidity);
      }
      
      // Read values
      co2 = ccs811.getCO2PPM();
      tvoc = ccs811.getTVOCPPB();
      baseline = ccs811.readBaseLine();
      
      // Calculate warm-up time
      unsigned long warmupTime = (millis() - startTime) / 1000; // in seconds
      
      // Output results
      Serial.println(F("=== Combined Measurement ==="));
      Serial.print(F("Timestamp: "));
      Serial.print(millis());
      Serial.println(F(" ms"));
      
      Serial.print(F("Warm-up time: "));
      Serial.print(warmupTime);
      Serial.println(F(" seconds"));
      
      // BME280 data
      if (bmeReady) {
        Serial.println(F("--- BME280 Data ---"));
        Serial.print(F("Temperature: "));
        Serial.print(temperature, 2);
        Serial.println(F(" °C"));
        
        Serial.print(F("Humidity: "));
        Serial.print(humidity, 2);
        Serial.println(F(" %"));
        
        Serial.print(F("Pressure: "));
        Serial.print(pressure, 2);
        Serial.println(F(" hPa"));
      }
      
      // CCS811 data
      Serial.println(F("--- CCS811 Data ---"));
      Serial.print(F("CO2: "));
      Serial.print(co2);
      Serial.println(F(" ppm"));
      
      Serial.print(F("TVOC: "));
      Serial.print(tvoc);
      Serial.println(F(" ppb"));
      
      Serial.print(F("Baseline: 0x"));
      Serial.println(baseline, HEX);
      
      // Environmental Compensation Status
      if (bmeReady) {
        Serial.println(F("Environmental Compensation: ACTIVE"));
      } else {
        Serial.println(F("Environmental Compensation: INACTIVE"));
      }
      
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
      
      Serial.println(F("======================="));
      Serial.println();
      
      // LED as status indicator
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      
    } else if (ccs811Ready) {
      // No new CCS811 data available
      Serial.print(F("Waiting for CCS811 data... ("));
      Serial.print((millis() - startTime) / 1000);
      Serial.println(F(" seconds since start)"));
    }
  }
}

/*
 * Additional functions for extended tests:
 */

// Display sensor status
void printSensorStatus() {
  Serial.println(F("=== Sensor Status ==="));
  Serial.print(F("BME280: "));
  Serial.println(bmeReady ? F("OK") : F("ERROR"));
  Serial.print(F("CCS811: "));
  Serial.println(ccs811Ready ? F("OK") : F("ERROR"));
  Serial.println(F("==================="));
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
        Serial.print("0");
      }
      Serial.println(address, HEX);
    }
  }
  Serial.println(F("================"));
} 