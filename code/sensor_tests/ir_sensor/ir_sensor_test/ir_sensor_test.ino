/*
 * MLX90614 Infrared Temperature Sensor Test
 * 
 * This program tests the MLX90614 infrared temperature sensor
 * and displays both object and ambient temperature.
 * 
 * Hardware:
 * - MLX90614 Infrared Temperature Sensor
 * - Arduino Uno/Nano
 * 
 * Connections:
 * - VCC -> 3.3V
 * - GND -> GND
 * - SDA -> A4 (SDA)
 * - SCL -> A5 (SCL)
 * 
 * I2C Address: 0x5A
 */

#include <Wire.h>
#include <Adafruit_MLX90614.h>

// Create sensor object
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

// Timing variables
unsigned long lastMeasurement = 0;
const unsigned long MEASUREMENT_INTERVAL = 1000; // 1 second

void setup() {
  // Start serial communication
  Serial.begin(9600);
  while (!Serial) {
    ; // Wait for serial connection
  }
  
  Serial.println(F("MLX90614 Infrared Temperature Sensor Test"));
  Serial.println(F("========================================="));
  
  // Start I2C
  Wire.begin();
  
  // Initialize sensor
  if (mlx.begin()) {
    Serial.println(F("MLX90614 Sensor initialized successfully!"));
    Serial.print(F("Emissivity: "));
    Serial.println(mlx.readEmissivity());
    Serial.println();
  } else {
    Serial.println(F("ERROR: MLX90614 Sensor not found!"));
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
    
    // Read temperatures
    float objectTemp = mlx.readObjectTempC();    // Object temperature (infrared)
    float ambientTemp = mlx.readAmbientTempC();  // Ambient temperature (internal sensor)
    
    // Output results
    Serial.println(F("=== MLX90614 Measurement ==="));
    Serial.print(F("Timestamp: "));
    Serial.print(millis());
    Serial.println(F(" ms"));
    
    Serial.print(F("Object Temperature (IR): "));
    Serial.print(objectTemp, 2);
    Serial.println(F(" °C"));
    
    Serial.print(F("Ambient Temperature: "));
    Serial.print(ambientTemp, 2);
    Serial.println(F(" °C"));
    
    // Calculate temperature difference
    float tempDiff = objectTemp - ambientTemp;
    Serial.print(F("Temperature Difference: "));
    Serial.print(tempDiff, 2);
    Serial.println(F(" °C"));
    
    // Display emissivity (useful for calibration)
    Serial.print(F("Emissivity: "));
    Serial.println(mlx.readEmissivity(), 3);
    
    Serial.println(F("==========================="));
    Serial.println();
    
    // LED as status indicator (if available)
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
}

/*
 * Additional functions for extended tests:
 */

// Change emissivity (for calibration)
void setEmissivity(float emissivity) {
  if (emissivity >= 0.1 && emissivity <= 1.0) {
    mlx.writeEmissivity(emissivity);
    Serial.print(F("Emissivity set to "));
    Serial.print(emissivity, 3);
    Serial.println(F(""));
  } else {
    Serial.println(F("ERROR: Emissivity must be between 0.1 and 1.0"));
  }
}

// Display sensor information
void printSensorInfo() {
  Serial.println(F("=== MLX90614 Sensor Information ==="));
  Serial.print(F("Emissivity: "));
  Serial.println(mlx.readEmissivity(), 3);
  Serial.print(F("I2C Address: 0x"));
  Serial.println(0x5A, HEX);
  Serial.println(F("==================================="));
} 