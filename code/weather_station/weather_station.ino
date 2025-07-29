// Weather Station - Sender Arduino
// Version: 2.0 (Updated with I2C Multiplexer and CCS811 Air Quality Sensor)
// Features: BME280, MLX90614, TSL2591, CCS811, Rain Sensors, Anemometer, nRF24L01
// Data Format: 16 int16_t values (32 bytes) with scaled integers for efficiency
// I2C Multiplexer: TCA9548A for better sensor organization

// I2C and Sensor Libraries
#include <Wire.h>             // I2C communication
#include <Adafruit_Sensor.h>  // Sensor interface
#include <Adafruit_BME280.h>  // BME280 temperature, humidity, pressure sensor
#include "DEV_Config.h"       // TSL2591 light sensor configuration
#include "TSL2591.h"          // TSL2591 light sensor library
#include <Adafruit_MLX90614.h> // MLX90614 infrared temperature sensor
#include <DFRobot_CCS811.h> // CCS811 air quality sensor


// Radio Communication Libraries
#include <SPI.h>              // SPI communication for nRF24L01
#include <nRF24L01.h>         // nRF24L01 radio module
#include <RF24.h>             // RF24 library

// Configuration Constants
#define WIND_MEASUREMENT_TIME 1000     // Wind measurement duration (ms)
#define RESET_INTERVAL 3600000         // Auto-reset interval (1 hour in ms)

// Pin Definitions
#define WIND_SENSOR_PIN 3     // Anemometer interrupt pin
#define RESET_PIN 4          // Reset pin for watchdog
#define RAIN_REED_PIN 2      // Reed sensor for rain amount
#define RAIN_DROP_ANALOG_PIN A1  // Analog rain drop sensor
#define RAIN_DROP_DIGITAL_PIN 5  // Digital rain drop sensor

// Radio Configuration
#define RADIO_CE_PIN 9       // nRF24L01 CE pin
#define RADIO_CSN_PIN 8      // nRF24L01 CSN pin
#define RADIO_CHANNEL 76     // Radio channel to avoid interference

// I2C Multiplexer Configuration
#define TCA9548A_ADDRESS 0x70  // I2C address of TCA9548A multiplexer
#define TCA_CHANNEL_0 0x01     // Channel 0: BME280
#define TCA_CHANNEL_1 0x02     // Channel 1: MLX90614
#define TCA_CHANNEL_2 0x04     // Channel 2: TSL2591
#define TCA_CHANNEL_3 0x08     // Channel 3: CCS811

// Sensor Objects
Adafruit_BME280 bme;         // BME280 sensor object
Adafruit_MLX90614 mlx;       // MLX90614 infrared sensor object
DFRobot_CCS811 ccs811;               // CCS811 air quality sensor object
RF24 radio(RADIO_CE_PIN, RADIO_CSN_PIN); // RF24 radio object

// Sensor Availability Flags
bool bme_sensor_available = false;
bool mlx_sensor_available = false;
bool light_sensor_available = false;
bool ccs811_sensor_available = false;
bool radio_available = false;

// Anemometer Variables
int InterruptCounter = 0;    // Wind revolution counter
uint16_t wind_revolutions_this_cycle = 0; // Revolutions in current cycle

// Rain Sensor Variables
bool firstLoop = true;        // First loop flag for reed sensor
bool lastState = false;       // Previous reed sensor state
bool currentState = false;    // Current reed sensor state
unsigned long lastChanged = 0; // Last reed sensor change time
uint16_t accumulated_rain_tips = 0; // Accumulated tips for packet loss recovery
bool rain_data_pending = false; // Flag for pending rain data

// Data Transmission Variables
int16_t results[16];         // 16 int16_t values = 32 bytes total payload (nRF24L01 limit)
uint16_t packetNumber = 0;   // Packet counter for tracking
unsigned long lastTransmissionTime = 0; // Last transmission duration
uint16_t successfulTransmissions = 0;   // Success counter
uint16_t failedTransmissions = 0;       // Failure counter

// Timing Variables
float time_stamp = 0;        // Measurement timestamp

// SETUP Function
void setup() {
  // Initialize reset pin
  pinMode(RESET_PIN, OUTPUT);
  digitalWrite(RESET_PIN, HIGH);

  // Initialize serial communication
  Serial.begin(9600);
  while (!Serial) {
    ; // Wait for serial connection
  }
  Serial.println(F("Weather Station Sender Starting..."));
  Serial.println(F("Version 2.0"));

  // Initialize rain sensor pins
  pinMode(RAIN_REED_PIN, INPUT);
  pinMode(RAIN_DROP_DIGITAL_PIN, INPUT);

  // Initialize I2C multiplexer
  initializeI2CMultiplexer();

  // Initialize sensors through multiplexer
  initializeBME280();
  initializeMLX90614();
  initializeTSL2591();
  initializeCCS811();

  // Initialize nRF24L01 radio
  initializeRadio();

  Serial.println(F("Weather Station initialization complete!"));
}

// Initialize I2C multiplexer
void initializeI2CMultiplexer() {
  Wire.begin();
  
  // Test multiplexer communication
  Wire.beginTransmission(TCA9548A_ADDRESS);
  if (Wire.endTransmission() == 0) {
    Serial.println(F("I2C Multiplexer (TCA9548A) initialized successfully"));
  } else {
    Serial.println(F("ERROR: I2C Multiplexer (TCA9548A) not found!"));
  }
}

// Select I2C multiplexer channel
void selectI2CChannel(uint8_t channel) {
  Wire.beginTransmission(TCA9548A_ADDRESS);
  Wire.write(channel);
  Wire.endTransmission();
  delay(1); // Small delay for channel switching
}

// Initialize BME280 temperature, humidity, pressure sensor
void initializeBME280() {
  selectI2CChannel(TCA_CHANNEL_0);
  
  unsigned status = bme.begin(0x76);
  
  if (!status) {
    Serial.println(F("Could not find a valid BME280 sensor, check wiring, address, sensor ID!"));
    Serial.print(F("SensorID was: 0x")); Serial.println(bme.sensorID(), 16);
    Serial.println(F("Using default values for BME280 sensor data"));
    bme_sensor_available = false;
  } else {
    bme_sensor_available = true;
    Serial.println(F("BME280 sensor initialized successfully (Channel 0)"));
  }
}

// Initialize MLX90614 infrared temperature sensor
void initializeMLX90614() {
  selectI2CChannel(TCA_CHANNEL_1);
  
  if (mlx.begin()) {
    mlx_sensor_available = true;
    Serial.println(F("MLX90614 infrared temperature sensor initialized successfully (Channel 1)"));
    Serial.print(F("Emissivity = ")); Serial.println(mlx.readEmissivity());
  } else {
    mlx_sensor_available = false;
    Serial.println(F("Could not find a valid MLX90614 sensor, check wiring!"));
    Serial.println(F("Using default values for MLX90614 sensor data"));
  }
}

// Initialize TSL2591 light sensor
void initializeTSL2591() {
  selectI2CChannel(TCA_CHANNEL_2);
  
  DEV_ModuleInit();
  if (TSL2591_Init() == 0) {
    light_sensor_available = true;
    Serial.println(F("TSL2591 light sensor initialized successfully (Channel 2)"));
  } else {
    light_sensor_available = false;
    Serial.println(F("Could not initialize TSL2591 light sensor, using default values"));
  }
}

// Initialize CCS811 air quality sensor
void initializeCCS811() {
  selectI2CChannel(TCA_CHANNEL_3);
  
  while(ccs811.begin() != 0) {
    Serial.println(F("failed to init chip, please check the chip connection"));
    delay(1000);
  }
  ccs811_sensor_available = true;
  Serial.println(F("CCS811 air quality sensor initialized successfully (Channel 3)"));
  Serial.println(F("CCS811 configured for default measurements"));
}

// Initialize nRF24L01 radio module
void initializeRadio() {
  if (radio.begin()) {
    radio.openWritingPipe((const uint8_t*)"99999");
    radio.stopListening();
    
    // Configure RF24 for reliable transmission
    radio.setRetries(3, 15);        // 3 retries, 15ms delay between retries
    radio.setPayloadSize(32);       // nRF24L01 hardware limit: 32 bytes max
    radio.setChannel(RADIO_CHANNEL); // Use channel 76 to avoid interference
    radio.setPALevel(RF24_PA_HIGH); // High power for better range
    
    radio_available = true;
    Serial.println(F("RF24 radio initialized successfully"));
  } else {
    radio_available = false;
    Serial.println(F("Could not initialize RF24 radio, data transmission disabled"));
  }
}

// MAIN LOOP
void loop() {
  // Increment timestamp
  time_stamp++;
  results[0] = (int16_t)time_stamp;

  // Read BME280 sensor data
  readBME280Data();

  // Read MLX90614 infrared sensor data
  readMLX90614Data();

  // Read TSL2591 light sensor data
  readTSL2591Data();

  // Read CCS811 air quality sensor data
  readCCS811Data();

  // Read rain sensor data (reed sensor for amount)
  readRainReedData();

  // Read rain drop sensor data (binary detection)
  readRainDropData();

  // Read anemometer data
  readAnemometerData();

  // Add packet number for tracking
  results[7] = (int16_t)packetNumber;

  // Clear remaining data slots for future expansion
  for (int i = 15; i < 16; i++) {
    results[i] = 0;
  }

  // Transmit data via radio
  transmitData();

  // Print debug information
  printDebugInfo();

  // Check for auto-reset
  checkAutoReset();
}

// Read BME280 temperature, humidity, pressure data
void readBME280Data() {
  if (bme_sensor_available) {
    selectI2CChannel(TCA_CHANNEL_0);
    
    float temp = bme.readTemperature();
    float pressure = bme.readPressure();
    float humidity = bme.readHumidity();
    
    // Scale temperature by 100 (e.g., 23.45°C -> 2345)
    results[1] = (int16_t)(temp * 100.0);
    
    // Scale pressure by 10 (e.g., 1013.25 hPa -> 10132)
    results[2] = (int16_t)(pressure * 10.0);
    
    // Scale humidity by 100 (e.g., 45.67% -> 4567)
    results[3] = (int16_t)(humidity * 100.0);
  } else {
    // Use default values if sensor is not available
    results[1] = 0; // temperature default
    results[2] = 0; // pressure default
    results[3] = 0; // humidity default
  }
}

// Read MLX90614 infrared temperature sensor data
void readMLX90614Data() {
  if (mlx_sensor_available) {
    selectI2CChannel(TCA_CHANNEL_1);
    
    float sky_temp = mlx.readObjectTempC();  // Sky temperature (infrared)
    float box_temp = mlx.readAmbientTempC(); // Temperature of weather station box (internal sensor)
    
    // Scale temperatures by 100 (e.g., 25.67°C -> 2567)
    results[8] = (int16_t)(sky_temp * 100.0);
    results[9] = (int16_t)(box_temp * 100.0);
  } else {
    // Use default values if sensor is not available
    results[8] = 0; // sky temperature default
    results[9] = 0; // box temperature default
  }
}

// Read TSL2591 light sensor data
void readTSL2591Data() {
  if (light_sensor_available) {
    selectI2CChannel(TCA_CHANNEL_2);
    
    uint32_t lux_raw = TSL2591_Read_Lux();
    // Scale light by 1 (direct lux value, max 65535)
    results[4] = (int16_t)min(lux_raw, 65535UL);
  } else {
    // Use default value if sensor is not available
    results[4] = 0; // illuminance default
  }
}

// Read CCS811 air quality sensor data
void readCCS811Data() {
  if (ccs811_sensor_available) {
    selectI2CChannel(TCA_CHANNEL_3);
    
    if (ccs811.checkDataReady()) {
      // Environmental compensation using BME280 data
      if (bme_sensor_available) {
        // Get temperature and humidity from BME280 for compensation
        selectI2CChannel(TCA_CHANNEL_0);
        float temp = bme.readTemperature();
        float humidity = bme.readHumidity();
        
        // Set environmental data for CCS811 compensation
        selectI2CChannel(TCA_CHANNEL_3);
        ccs811.setInTempHum(temp, humidity);
        
        Serial.print(F("CCS811 Environmental Compensation - Temp: "));
        Serial.print(temp);
        Serial.print(F("°C, Humidity: "));
        Serial.print(humidity);
        Serial.println(F("%"));
      }
      
      // Get CO2 and TVOC values (now with environmental compensation)
      uint16_t co2 = ccs811.getCO2PPM();
      uint16_t tvoc = ccs811.getTVOCPPB();
      
      // Store values directly (CCS811 provides ppm values)
      results[12] = (int16_t)co2;   // CO2 in ppm
      results[13] = (int16_t)tvoc;  // TVOC in ppb
      
      // Get baseline values for calibration
      uint16_t baseline = ccs811.readBaseLine();
      results[14] = (int16_t)baseline; // Baseline for CO2 calibration
      
    } else {
      // Use previous values if no new data available
      results[12] = 0; // CO2 default
      results[13] = 0; // TVOC default
      results[14] = 0; // Baseline default
    }
  } else {
    // Use default values if sensor is not available
    results[12] = 0; // CO2 default
    results[13] = 0; // TVOC default
    results[14] = 0; // Baseline default
  }
}

// Read rain reed sensor data (for rain amount measurement)
void readRainReedData() {
  // Handle first loop initialization
  int currentState = digitalRead(RAIN_REED_PIN);
  if (firstLoop) {
    lastState = currentState;
    firstLoop = false;
  }

  // Check for state change (rain tip occurred)
  if (currentState != lastState) {
    // Debounce: minimum 250ms between changes to prevent false triggers
    if ((millis() - lastChanged) > 250) {
      if (currentState == HIGH && lastState == LOW) {
        // Rising edge detected - rain tip occurred
        accumulated_rain_tips++;
        rain_data_pending = true;
        
        Serial.print(F("Rain tip detected! Accumulated: "));
        Serial.println(accumulated_rain_tips);
      }
      lastState = currentState;
      lastChanged = millis();
    }
  }

  // Set rain tips for transmission
  results[5] = (int16_t)accumulated_rain_tips;
}

// Read rain drop sensor data (binary detection)
void readRainDropData() {
  int rainDropAnalogVal = analogRead(RAIN_DROP_ANALOG_PIN);
  int rainDropDigitalVal = digitalRead(RAIN_DROP_DIGITAL_PIN);
  
  // Binary rain detection (1 = raining, 0 = not raining)
  bool isRaining = (rainDropDigitalVal == LOW);
  results[10] = (int16_t)(isRaining ? 1 : 0);
  
  // Store analog value for debugging (0-1023)
  results[11] = (int16_t)rainDropAnalogVal;
}

// Read anemometer data
void readAnemometerData() {
  // Count revolutions during measurement period
  InterruptCounter = 0;
  attachInterrupt(digitalPinToInterrupt(WIND_SENSOR_PIN), countup, RISING);
  delay(WIND_MEASUREMENT_TIME);
  detachInterrupt(digitalPinToInterrupt(WIND_SENSOR_PIN));
  
  wind_revolutions_this_cycle = InterruptCounter;
  
  // Set wind revolutions for transmission (raw count, will be converted on receiver side)
  results[6] = (int16_t)wind_revolutions_this_cycle;
}

// Transmit data via nRF24L01 radio
void transmitData() {
  if (radio_available) {
    unsigned long transmissionStart = millis();
    
    if (radio.write(&results, sizeof(results))) {
      successfulTransmissions++;
      unsigned long transmissionTime = millis() - transmissionStart;
      lastTransmissionTime = transmissionTime;
      
      // Clear accumulated rain data if transmission successful
      if (rain_data_pending) {
        Serial.print(F("Rain tips successfully transmitted: "));
        Serial.print(accumulated_rain_tips);
        Serial.println(F(" tips"));
        accumulated_rain_tips = 0;
        rain_data_pending = false;
      }
      
      Serial.print(F("Packet "));
      Serial.print(packetNumber);
      Serial.print(F(" transmitted successfully in "));
      Serial.print(transmissionTime);
      Serial.println(F(" ms"));
    } else {
      failedTransmissions++;
      Serial.print(F("Failed to transmit packet "));
      Serial.print(packetNumber);
      Serial.println(F(" after 3 retries"));
      
      // Keep rain data accumulated if transmission failed
      if (rain_data_pending) {
        Serial.print(F("Rain tips preserved for next transmission. Total accumulated: "));
        Serial.print(accumulated_rain_tips);
        Serial.println(F(" tips"));
      }
    }
    
    // Increment packet number for next transmission
    packetNumber++;
    
    // Print transmission statistics every 100 packets
    if (packetNumber % 100 == 0) {
      printTransmissionStats();
    }
  } else {
    Serial.println(F("Radio not available, skipping transmission"));
  }
}

// Print transmission statistics
void printTransmissionStats() {
  Serial.print(F("Transmission stats - Success: "));
  Serial.print(successfulTransmissions);
  Serial.print(F(", Failed: "));
  Serial.print(failedTransmissions);
  Serial.print(F(", Success rate: "));
  Serial.print((float)successfulTransmissions / (successfulTransmissions + failedTransmissions) * 100);
  Serial.println(F("%"));
  
  // Show pending rain data
  if (rain_data_pending) {
    Serial.print(F("Pending rain tips: "));
    Serial.print(accumulated_rain_tips);
    Serial.println(F(" tips"));
  }
}

// Print debug information
void printDebugInfo() {
  // Print scaled values
  Serial.print(F("Scaled values: "));
  for (int i = 0; i < 15; i++) {
    Serial.print(results[i]);
    Serial.print(F(" "));
  }
  Serial.println();
  
  // Print rain detection debug info
  int rainDropAnalogVal = analogRead(RAIN_DROP_ANALOG_PIN);
  int rainDropDigitalVal = digitalRead(RAIN_DROP_DIGITAL_PIN);
  bool isRaining = (results[10] == 1);
  
  Serial.print(F("Rain Detection - Digital: "));
  Serial.print(rainDropDigitalVal);
  Serial.print(F(", Analog: "));
  Serial.print(rainDropAnalogVal);
  Serial.print(F(", Is Raining: "));
  Serial.println(isRaining ? F("YES") : F("NO"));
  
  // Print air quality debug info
  if (ccs811_sensor_available) {
    Serial.print(F("Air Quality - CO2: "));
    Serial.print(results[12]);
    Serial.print(F(" ppm, TVOC: "));
    Serial.print(results[13]);
    Serial.print(F(" ppb, Baseline: "));
    Serial.print(results[14]);
    Serial.println();
  }
}

// Check for auto-reset condition
void checkAutoReset() {
  if (millis() > RESET_INTERVAL) {
    Serial.println(F("Auto-reset triggered"));
    digitalWrite(RESET_PIN, LOW);
  }
}

// Interrupt handler for anemometer
void countup() {
  InterruptCounter++;
}
