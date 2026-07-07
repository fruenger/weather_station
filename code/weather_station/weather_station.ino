// Weather Station - Sender Arduino
// Version: 2.2.1 (Serial debug gated for field power without USB)
// Features: BME280, MLX90614, TSL2591, PMSA003I, SEN0636 UV, Rain Sensors, Anemometer, nRF24L01
// Data Format: 16 int16_t values (32 bytes) with scaled integers for efficiency
// I2C Multiplexer: TCA9548A for better sensor organization

// I2C and Sensor Libraries
#include <Wire.h>             // I2C communication
#include <Adafruit_Sensor.h>  // Sensor interface
#include <Adafruit_BME280.h>  // BME280 temperature, humidity, pressure sensor
#include "DEV_Config.h"       // TSL2591 light sensor configuration
#include "TSL2591.h"          // TSL2591 light sensor library
#include <Adafruit_MLX90614.h> // MLX90614 infrared temperature sensor
#include <Adafruit_PM25AQI.h> // PMSA003I particulate matter sensor
#include <DFRobot_UVIndex240370Sensor.h> // SEN0636 UV index sensor


// Radio Communication Libraries
#include <SPI.h>              // SPI communication for nRF24L01
#include <nRF24L01.h>         // nRF24L01 radio module
#include <RF24.h>             // RF24 library

// Set to 1 only when debugging over USB Serial Monitor.
// When 0, no Serial output in loop — required for standalone power (no USB reader).
#define SERIAL_DEBUG 0

#if SERIAL_DEBUG
#define DBG_PRINT(...) Serial.print(__VA_ARGS__)
#define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define DBG_PRINT(...) ((void)0)
#define DBG_PRINTLN(...) ((void)0)
#endif

// Configuration Constants
#define WIND_MEASUREMENT_TIME 1000     // Wind measurement duration (ms)
#define RESET_INTERVAL 3600000         // Auto-reset interval (1 hour in ms)

// Pin Definitions
#define WIND_SENSOR_PIN 3     // Anemometer interrupt pin
#define RESET_PIN 4          // Reset pin for watchdog
#define RAIN_REED_PIN 2      // Reed sensor for rain amount
#define RAIN_DROP_ANALOG_PIN A1  // Analog rain drop sensor
#define RAIN_DROP_DIGITAL_PIN 5  // Digital rain drop sensor
#define PMSA003I_SET_PIN 6       // SET pin: LOW = sleep, HIGH = active (saves ~60-120 mA)

// PMSA003I power management (solar/battery operation)
const unsigned long PM_MEASUREMENT_INTERVAL = 300000; // Measure every 5 minutes
const unsigned long PM_BOOT_DELAY = 3000;             // Boot time after wake
const unsigned long PM_WARMUP_TIME = 30000;         // Fan/laser stabilization after wake

// Radio Configuration
#define RADIO_CE_PIN 9       // nRF24L01 CE pin
#define RADIO_CSN_PIN 8      // nRF24L01 CSN pin
#define RADIO_CHANNEL 76     // Radio channel to avoid interference
static const uint8_t RF_ADDRESS[5] = {'9','9','9','9','9'}; // 5-byte pipe address

// I2C Multiplexer Configuration
#define TCA9548A_ADDRESS 0x70  // I2C address of TCA9548A multiplexer
#define TCA_CHANNEL_0 0x01     // Channel 0: PMSA003I
#define TCA_CHANNEL_1 0x02     // Channel 1: BME280
#define TCA_CHANNEL_2 0x04     // Channel 2: SEN0636 UV Index
#define TCA_CHANNEL_3 0x08     // Channel 3: MLX90614
#define TCA_CHANNEL_4 0x10     // Channel 4: TSL2591

// Sensor Objects
Adafruit_BME280 bme;         // BME280 sensor object
Adafruit_MLX90614 mlx;       // MLX90614 infrared sensor object
Adafruit_PM25AQI aqi;                // PMSA003I particulate matter sensor object
DFRobot_UVIndex240370Sensor uvSensor(&Wire); // SEN0636 UV index sensor object
RF24 radio(RADIO_CE_PIN, RADIO_CSN_PIN); // RF24 radio object

// Sensor Availability Flags
bool bme_sensor_available = false;
bool mlx_sensor_available = false;
bool light_sensor_available = false;
bool pmsa003i_sensor_available = false;
bool uv_sensor_available = false;
bool radio_available = false;

// PMSA003I state (sleep/wake cycle for power saving)
bool pmsa003i_awake = false;
unsigned long pmsa003i_wake_time = 0;
unsigned long last_pm_measurement = 0;
int16_t last_pm1_0 = 0;
int16_t last_pm2_5 = 0;
int16_t last_pm10 = 0;
int16_t last_uv_index = 0;

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

  // Initialize serial communication (optional; loop output only when SERIAL_DEBUG)
  Serial.begin(9600);
#if SERIAL_DEBUG
  {
    unsigned long serialWaitStart = millis();
    while (!Serial && (millis() - serialWaitStart < 3000UL)) {
      ;
    }
  }
#endif
  DBG_PRINTLN(F("Weather Station Sender Starting..."));
  DBG_PRINTLN(F("Version 2.2.1"));

  // Initialize rain drop sensor pins
  pinMode(RAIN_REED_PIN, INPUT);
  pinMode(RAIN_DROP_DIGITAL_PIN, INPUT);

  // Initialize I2C multiplexer
  initializeI2CMultiplexer();

  // Initialize sensors through multiplexer
  initializeBME280();
  initializeMLX90614();
  initializeTSL2591();
  initializePMSA003I();
  initializeUVSensor();

  // Initialize nRF24L01 radio
  initializeRadio();

  DBG_PRINTLN(F("Weather Station initialization complete!"));
}

// Initialize I2C multiplexer
void initializeI2CMultiplexer() {
  Wire.begin();
  
  // Test multiplexer communication
  Wire.beginTransmission(TCA9548A_ADDRESS);
  if (Wire.endTransmission() == 0) {
    DBG_PRINTLN(F("I2C Multiplexer (TCA9548A) initialized successfully"));
  } else {
    DBG_PRINTLN(F("ERROR: I2C Multiplexer (TCA9548A) not found!"));
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
  selectI2CChannel(TCA_CHANNEL_1);
  
  unsigned status = bme.begin(0x76);
  
  if (!status) {
    DBG_PRINTLN(F("Could not find a valid BME280 sensor, check wiring, address, sensor ID!"));
    DBG_PRINT(F("SensorID was: 0x")); DBG_PRINTLN(bme.sensorID(), 16);
    DBG_PRINTLN(F("Using default values for BME280 sensor data"));
    bme_sensor_available = false;
  } else {
    bme_sensor_available = true;
    DBG_PRINTLN(F("BME280 sensor initialized successfully (Channel 1)"));
  }
}

// Initialize MLX90614 infrared temperature sensor
void initializeMLX90614() {
  selectI2CChannel(TCA_CHANNEL_3);
  
  if (mlx.begin()) {
    mlx_sensor_available = true;
    DBG_PRINTLN(F("MLX90614 infrared temperature sensor initialized successfully (Channel 3)"));
    DBG_PRINT(F("Emissivity = ")); DBG_PRINTLN(mlx.readEmissivity());
  } else {
    mlx_sensor_available = false;
    DBG_PRINTLN(F("Could not find a valid MLX90614 sensor, check wiring!"));
    DBG_PRINTLN(F("Using default values for MLX90614 sensor data"));
  }
}

// Initialize TSL2591 light sensor
void initializeTSL2591() {
  selectI2CChannel(TCA_CHANNEL_4);
  
  DEV_ModuleInit();
  if (TSL2591_Init() == 0) {
    light_sensor_available = true;
    DBG_PRINTLN(F("TSL2591 light sensor initialized successfully (Channel 4)"));
  } else {
    light_sensor_available = false;
    DBG_PRINTLN(F("Could not initialize TSL2591 light sensor, using default values"));
  }
}

// Initialize PMSA003I particulate matter sensor (SET pin + deferred I2C init)
void initializePMSA003I() {
  pinMode(PMSA003I_SET_PIN, OUTPUT);
  digitalWrite(PMSA003I_SET_PIN, LOW); // Start in sleep mode to save power
  pmsa003i_sensor_available = false;
  pmsa003i_awake = false;
  DBG_PRINTLN(F("PMSA003I configured (sleep mode, SET pin on D6)"));
  DBG_PRINTLN(F("PM measurements every 5 minutes with 30s warm-up"));
}

void wakePMSA003I() {
  digitalWrite(PMSA003I_SET_PIN, HIGH);
  pmsa003i_awake = true;
  pmsa003i_wake_time = millis();
}

void sleepPMSA003I() {
  digitalWrite(PMSA003I_SET_PIN, LOW);
  pmsa003i_awake = false;
  pmsa003i_sensor_available = false; // Re-init I2C on next wake
}

// Initialize DFRobot SEN0636 UV index sensor (I2C mode, switch must be on I2C)
void initializeUVSensor() {
  selectI2CChannel(TCA_CHANNEL_2);

  int attempts = 0;
  const int maxAttempts = 10;

  while (uvSensor.begin() != true && attempts < maxAttempts) {
    attempts++;
    DBG_PRINT(F("UV sensor init attempt "));
    DBG_PRINT(attempts);
    DBG_PRINT(F("/"));
    DBG_PRINT(maxAttempts);
    DBG_PRINTLN(F(" failed"));
    delay(1000);
  }

  if (attempts >= maxAttempts) {
    uv_sensor_available = false;
    DBG_PRINTLN(F("SEN0636 UV sensor init failed — check I2C wiring and mode switch"));
  } else {
    uv_sensor_available = true;
    DBG_PRINTLN(F("SEN0636 UV sensor initialized successfully (Channel 2)"));
  }
}

// Initialize nRF24L01 radio module
void initializeRadio() {
  if (radio.begin()) {
    // Align with receiver
    radio.setAddressWidth(5);
    radio.setCRCLength(RF24_CRC_16);
    radio.setAutoAck(true);
    radio.setDataRate(RF24_1MBPS);
    radio.setPayloadSize(32);
    radio.setChannel(RADIO_CHANNEL);
    radio.setPALevel(RF24_PA_HIGH);
    radio.setRetries(5, 15);

    radio.openWritingPipe(RF_ADDRESS); // 5-byte address
    radio.flush_tx();
    radio.stopListening();
    
    radio_available = true;
    DBG_PRINTLN(F("RF24 radio initialized successfully"));
    if (!radio.isChipConnected()) {
      DBG_PRINTLN(F("WARNING: RF24 chip not detected — check CE/CSN wiring and 3.3V power"));
    }
  } else {
    radio_available = false;
    DBG_PRINTLN(F("Could not initialize RF24 radio, data transmission disabled"));
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

  // Read PMSA003I particulate matter sensor data
  readPMSA003IData();

  // Read SEN0636 UV index sensor data
  readUVIndexData();

  // Read rain sensor data (reed sensor for amount)
  readRainReedData();

  // Read rain drop sensor data (binary detection)
  readRainDropData();

  // Read anemometer data
  readAnemometerData();

  // Add packet number for tracking
  results[7] = (int16_t)packetNumber;

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
    selectI2CChannel(TCA_CHANNEL_1);
    
    float temp = bme.readTemperature();
    float pressure_pa = bme.readPressure();      // Pa from library
    float pressure_hpa = pressure_pa / 100.0f;   // convert to hPa
    float humidity = bme.readHumidity();
    
    // Scale temperature by 100 (e.g., 23.45°C -> 2345)
    results[1] = (int16_t)(temp * 100.0);
    
    // Scale pressure (hPa) by 10 (e.g., 1013.25 hPa -> 10132)
    results[2] = (int16_t)(pressure_hpa * 10.0f);
    
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
    selectI2CChannel(TCA_CHANNEL_3);
    
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
    selectI2CChannel(TCA_CHANNEL_4);
    
    uint32_t lux_raw = TSL2591_Read_Lux();
    // Scale light by dividing by 100 to fit in int16_t range (max 655.35 lux)
    // This prevents overflow while maintaining reasonable precision
    uint32_t lux_scaled = lux_raw / 100;
    results[4] = (int16_t)min(lux_scaled, 65535UL);
  } else {
    // Use default value if sensor is not available
    results[4] = 0; // illuminance default
  }
}

// Read PMSA003I particulate matter sensor data (with sleep/wake power management)
void readPMSA003IData() {
  // Always transmit last known values between measurement cycles
  results[12] = last_pm1_0;
  results[13] = last_pm2_5;
  results[14] = last_pm10;

  unsigned long now = millis();

  if (!pmsa003i_awake) {
    if (now - last_pm_measurement >= PM_MEASUREMENT_INTERVAL) {
      DBG_PRINTLN(F("PMSA003I: waking sensor for measurement"));
      wakePMSA003I();
    }
    return;
  }

  unsigned long awake_time = now - pmsa003i_wake_time;

  // Initialize I2C after boot delay
  if (!pmsa003i_sensor_available && awake_time >= PM_BOOT_DELAY) {
    selectI2CChannel(TCA_CHANNEL_0);
    if (aqi.begin_I2C()) {
      pmsa003i_sensor_available = true;
      DBG_PRINTLN(F("PMSA003I initialized successfully (Channel 0)"));
    } else {
      DBG_PRINTLN(F("PMSA003I I2C init failed, will retry next cycle"));
      sleepPMSA003I();
      last_pm_measurement = now;
      return;
    }
  }

  // Wait for fan/laser warm-up before reading
  if (!pmsa003i_sensor_available || awake_time < (PM_BOOT_DELAY + PM_WARMUP_TIME)) {
    return;
  }

  selectI2CChannel(TCA_CHANNEL_0);
  PM25_AQI_Data data;
  if (aqi.read(&data)) {
    last_pm1_0 = (int16_t)data.pm10_standard;  // PM 1.0 ug/m3
    last_pm2_5 = (int16_t)data.pm25_standard;  // PM 2.5 ug/m3
    last_pm10 = (int16_t)data.pm100_standard;  // PM 10 ug/m3
    results[12] = last_pm1_0;
    results[13] = last_pm2_5;
    results[14] = last_pm10;

    DBG_PRINT(F("PMSA003I - PM1.0: "));
    DBG_PRINT(last_pm1_0);
    DBG_PRINT(F(" PM2.5: "));
    DBG_PRINT(last_pm2_5);
    DBG_PRINT(F(" PM10: "));
    DBG_PRINTLN(last_pm10);

    sleepPMSA003I();
    last_pm_measurement = now;
  }
}

// Read SEN0636 UV index sensor data
void readUVIndexData() {
  results[15] = last_uv_index;

  if (!uv_sensor_available) {
    return;
  }

  selectI2CChannel(TCA_CHANNEL_2);
  uint16_t uvIndex = uvSensor.readUvIndexData();
  last_uv_index = (int16_t)uvIndex;
  results[15] = last_uv_index;
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
      accumulated_rain_tips++;
      rain_data_pending = true;
      
      DBG_PRINT(F("Rain tip detected! Accumulated: "));
      DBG_PRINTLN(accumulated_rain_tips);
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
        DBG_PRINT(F("Rain tips successfully transmitted: "));
        DBG_PRINT(accumulated_rain_tips);
        DBG_PRINTLN(F(" tips"));
        accumulated_rain_tips = 0;
        rain_data_pending = false;
      }
      
      DBG_PRINT(F("Packet "));
      DBG_PRINT(packetNumber);
      DBG_PRINT(F(" transmitted successfully in "));
      DBG_PRINT(transmissionTime);
      DBG_PRINTLN(F(" ms"));
    } else {
      failedTransmissions++;
      DBG_PRINT(F("Failed to transmit packet "));
      DBG_PRINT(packetNumber);
      DBG_PRINTLN(F(" after 3 retries"));
      
      // Keep rain data accumulated if transmission failed
      if (rain_data_pending) {
        DBG_PRINT(F("Rain tips preserved for next transmission. Total accumulated: "));
        DBG_PRINT(accumulated_rain_tips);
        DBG_PRINTLN(F(" tips"));
      }
    }
    
    // Increment packet number for next transmission
    packetNumber++;
    
    // Print transmission statistics every 100 packets
    if (packetNumber % 100 == 0) {
      printTransmissionStats();
    }
  } else {
    DBG_PRINTLN(F("Radio not available, skipping transmission"));
  }
}

// Print transmission statistics
void printTransmissionStats() {
  DBG_PRINT(F("Transmission stats - Success: "));
  DBG_PRINT(successfulTransmissions);
  DBG_PRINT(F(", Failed: "));
  DBG_PRINT(failedTransmissions);
  DBG_PRINT(F(", Success rate: "));
  DBG_PRINT((float)successfulTransmissions / (successfulTransmissions + failedTransmissions) * 100);
  DBG_PRINTLN(F("%"));
  
  // Show pending rain data
  if (rain_data_pending) {
    DBG_PRINT(F("Pending rain tips: "));
    DBG_PRINT(accumulated_rain_tips);
    DBG_PRINTLN(F(" tips"));
  }
}

// Print debug information
void printDebugInfo() {
  // Print scaled values
  DBG_PRINT(F("Scaled values: "));
  for (int i = 0; i < 16; i++) {
    DBG_PRINT(results[i]);
    DBG_PRINT(F(" "));
  }
  DBG_PRINTLN();
  
  // Print rain detection debug info
  int rainDropAnalogVal = analogRead(RAIN_DROP_ANALOG_PIN);
  int rainDropDigitalVal = digitalRead(RAIN_DROP_DIGITAL_PIN);
  bool isRaining = (results[10] == 1);
  
  DBG_PRINT(F("Rain Detection - Digital: "));
  DBG_PRINT(rainDropDigitalVal);
  DBG_PRINT(F(", Analog: "));
  DBG_PRINT(rainDropAnalogVal);
  DBG_PRINT(F(", Is Raining: "));
  DBG_PRINTLN(isRaining ? F("YES") : F("NO"));
  
  // Print particulate matter debug info
  if (last_pm2_5 > 0) {
    DBG_PRINT(F("Particulate Matter - PM1.0: "));
    DBG_PRINT(results[12]);
    DBG_PRINT(F(" PM2.5: "));
    DBG_PRINT(results[13]);
    DBG_PRINT(F(" PM10: "));
    DBG_PRINTLN(results[14]);
  }

  if (last_uv_index > 0) {
    DBG_PRINT(F("UV Index: "));
    DBG_PRINTLN(results[15]);
  }
}

// Check for auto-reset condition
void checkAutoReset() {
  if (millis() > RESET_INTERVAL) {
    DBG_PRINTLN(F("Auto-reset triggered"));
    digitalWrite(RESET_PIN, LOW);
  }
}

// Interrupt handler for anemometer
void countup() {
  InterruptCounter++;
}
