# Sensor Tests for Weather Station

This directory contains test programs for the individual sensors of the weather station to verify their functionality before integration.

## Available Tests

### 1. IR-Sensor Test
**File:** `ir_sensor/ir_sensor_test.ino`

**Purpose:** Tests the MLX90614 infrared temperature sensor

**Features:**
- Object temperature (infrared)
- Ambient temperature (internal sensor)
- Temperature difference
- Emissivity display
- LED status indicator

**Connections:**
- VCC → 3.3V
- GND → GND
- SDA → A4 (SDA)
- SCL → A5 (SCL)

**I2C Address:** 0x5A

### 2. Air Quality Sensor Test
**File:** `air_quality_sensor/air_quality_test.ino`

**Purpose:** Tests the CCS811 air quality sensor

**Features:**
- CO2 measurement (ppm)
- TVOC measurement (ppb)
- Baseline values
- Quality assessment
- Warm-up time display

**Connections:**
- VCC → 3.3V
- GND → GND
- SDA → A4 (SDA)
- SCL → A5 (SCL)
- WAK → GND (Wake-up Pin)

**I2C Address:** 0x5B

**Important:** The CCS811 requires 20 minutes warm-up time for accurate measurements!

### 3. Air Quality Sensor with Environmental Compensation
**File:** `air_quality_sensor/air_quality_with_compensation_test.ino`

**Purpose:** Tests the CCS811 with BME280 Environmental Compensation

**Features:**
- All features of the basic CCS811 test
- BME280 temperature/humidity/pressure
- Environmental compensation for more accurate measurements
- Combined data output

**Connections:**
- CCS811: as above
- BME280: VCC→3.3V, GND→GND, SDA→A4, SCL→A5

**I2C Addresses:**
- CCS811: 0x5B
- BME280: 0x76

## Test Sequence

1. **IR-Sensor Test** - Simple test for infrared temperature measurement
2. **Air Quality Test** - Basic test for air quality sensor
3. **Air Quality with Compensation** - Extended test with BME280

## Usage

1. **Open Arduino IDE**
2. **Open test program** (e.g., `ir_sensor_test.ino`)
3. **Select Arduino board** (Uno/Nano)
4. **Select port**
5. **Upload program**
6. **Open Serial Monitor** (9600 baud)
7. **Check output**

## Expected Outputs

### IR-Sensor Test
```
MLX90614 Infrared Temperature Sensor Test
=========================================
MLX90614 Sensor initialized successfully!
Emissivity: 0.95

=== MLX90614 Measurement ===
Timestamp: 1234 ms
Object Temperature (IR): 23.45 °C
Ambient Temperature: 22.10 °C
Temperature Difference: 1.35 °C
Emissivity: 0.950
===========================
```

### Air Quality Test
```
CCS811 Air Quality Sensor Test
=============================
CCS811 Sensor initialized successfully!
Drive Mode set to 1 second
Sensor starting warm-up phase...
Wait 20 minutes for accurate measurements

=== CCS811 Measurement ===
Timestamp: 1234 ms
Warm-up time: 65 seconds
CO2: 450 ppm
TVOC: 25 ppb
Baseline: 0x8473
CO2 Quality: Normal (400-1000 ppm)
TVOC Quality: Good (<100 ppb)
=====================
```

## Troubleshooting

### Sensor not found
- **Check wiring** (VCC, GND, SDA, SCL)
- **Verify I2C address**
- **Pull-up resistors** (4.7kΩ on SDA/SCL)
- **Power supply** (3.3V for sensors)

### CCS811 Issues
- **Observe warm-up time** (20 minutes)
- **WAK pin** must be connected to GND
- **Drive Mode** set correctly
- **Environmental Data** for better accuracy

### IR-Sensor Issues
- **Adjust emissivity** for the material to be measured
- **Consider distance** to object
- **Ambient temperature** for reference

## Next Steps

After successful tests, the sensors can be integrated into the main weather station:

1. **Wiring** according to `wiring/pin_connections.txt`
2. **I2C Multiplexer** for multiple sensors
3. **Main program** `weather_station.ino` use
4. **Environmental Compensation** activate

## Additional Tests

Further sensor tests are available in other directories:
- `light_sensor/` - TSL2591 light sensor
- `PressureSensor/` - BME280 pressure sensor
- `anemometer/` - wind speed sensor
- `RainSensor/` - rain sensors 