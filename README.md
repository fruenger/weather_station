# Arduino Weather Station with Wireless Data Transmission for the OST Observatory

A comprehensive weather monitoring system built with Arduino microcontrollers that collects environmental data from multiple sensors and transmits it wirelessly to a receiver for data logging and remote upload.

## Project Overview

This weather station consists of two main components:

- **Sender Arduino**: Collects data from various environmental sensors
- **Receiver Arduino**: Receives data wirelessly and forwards it to a Python script for processing and server upload

The system uses nRF24L01 radio modules for wireless communication and supports real-time monitoring of temperature, humidity, pressure, light levels, wind speed, rainfall, and air quality.

## Hardware Components

### Sensors

- **BME280**: Temperature, humidity, and barometric pressure sensor
- **TSL2591**: High-precision light sensor for illuminance measurement
- **MLX90614**: Infrared temperature sensor for sky and enclosure temperature
- **Rain Reed Switch**: Tipping bucket rain gauge for rainfall measurement
- **Rain Drop Sensor**: Analog rain detection sensor
- **Anemometer**: Wind speed measurement via reed switch
- **PMSA003I**: Particulate matter sensor (PM1.0, PM2.5, PM10) with sleep/wake power management

### Microcontrollers & Communication

- **2x Arduino Nano/Uno**: One for sensor data collection, one for data reception
- **2x nRF24L01+**: 2.4GHz radio modules for wireless data transmission
- **TCA9548A I2C Multiplexer**: Allows multiple I2C sensors on a single bus

### Power & Connectivity

- **USB Connection**: For receiver Arduino to computer communication
- **5V Power Supply**: For sender Arduino and sensors

## Data Format

The system transmits 16 int16_t values (32 bytes total - nRF24L01 hardware limit):

```
[timestamp, temperature*100, pressure*10, humidity*100, illuminance, rain_tips, wind_revolutions, packet_number, sky_temp*100, box_temp*100, rain_detection, rain_analog, pm1_0, pm2_5, pm10, reserved]
```

### Data Scaling

- Temperature: Scaled by 100 (e.g., 23.45°C → 2345)
- Pressure: Scaled by 10 (e.g., 1013.25 hPa → 10132)
- Humidity: Scaled by 100 (e.g., 45.67% → 4567)
- Infrared temperatures: Scaled by 100

## Software Components

### Arduino Code

- **weather_station.ino**: Main sender code with all sensor integrations
- **receive.ino**: Receiver code for wireless data reception
- **TSL2591 Library**: Light sensor interface
- **BME280 Library**: Environmental sensor interface
- **MLX90614 Library**: Infrared temperature sensor interface
- **Adafruit PM25 AQI Library**: PMSA003I particulate matter sensor interface

### Python Processing

- **receive.py**: Main data processing and server upload script
- **config.py**: Configuration management for server credentials
- **wiring_diagram.py**: Wiring diagram generation (does not work currently)

## Setup Instructions

### 1. Hardware Assembly

1. Connect sensors to the sender Arduino according to the pin connections:
   
   - BME280: I2C (SDA/SCL)
   - TSL2591: I2C via TCA9548A multiplexer
   - MLX90614: I2C via TCA9548A multiplexer
   - PMSA003I: I2C via TCA9548A multiplexer (Channel 0), SET pin on D6
   - Rain reed switch: Digital pin
   - Rain drop sensor: Analog + Digital pins
   - Anemometer: Digital pin

2. Connect nRF24L01 modules:
   
   - Sender: CE, CSN, MOSI, MISO, SCK, VCC, GND
   - Receiver: Same connections

3. Power the sender Arduino with 5V supply

### 2. Software Installation

1. Install required Arduino libraries:
   
   - Adafruit BME280 Library
   - Adafruit MLX90614 Library
   - Adafruit PM25 AQI Library
   - Adafruit BusIO
   - RF24 Library

2. Install Python dependencies:
   
   ```bash
   pip install pyserial numpy requests astropy
   ```

### 3. Configuration

1. Create `weather_station_config.json` in the receive directory:
   
   ```json
   {
     "server": {
       "url": "your_server_url",
       "username": "your_username",
       "password": "your_password"
     },
     "arduino": {
       "port_search": "ttyUSB"
     }
   }
   ```

### 4. Upload Code

1. Upload `weather_station.ino` to the sender Arduino
2. Upload `receive.ino` to the receiver Arduino
3. Connect receiver Arduino to computer via USB

### 5. Start Data Collection

1. Run the Python receiver script:
   
   ```bash
   cd weather_station_code/receive
   python receive.py
   ```

## Features

### Data Validation

- Range checking for all sensor values
- Automatic error detection and logging
- Invalid data filtering

### Robust Communication

- Packet numbering for data integrity
- Retry mechanisms for failed transmissions
- Error handling for network issues

### Data Storage

- Local CSV backup for failed uploads
- Automatic retry of failed server uploads
- Timestamp-based data organization

### Real-time Monitoring

- Live data display
- Status indicators for all sensors
- Debug logging for troubleshooting

## Data Processing

The Python script performs the following operations:

1. **Data Reception**: Receives binary data from Arduino
2. **Validation**: Checks sensor values against reasonable ranges
3. **Conversion**: Converts scaled integers back to physical units
4. **Upload**: Sends data to remote server asynchronously
5. **Backup**: Stores failed uploads locally for retry

### Calculated Values & Server Upload

- **Rain (`rain` field)**: `rain_tips × 1.25` → depth in the **collector** (mm per tip). The website converts collector mm → **mm/m²** when plotting (`× 0.07534`, gauge Ø 130 mm).
- **Wind (`wind_speed` field)**: anemometer **revolutions** per sample (not m/s). The website converts to m/s for display (`× 0.14`).
- **Air quality**: CO2 and TVOC (ppb); TVOC must be ≤ 10000 to match the API.

Upload endpoint: `POST …/weather_api/datasets/` with HTTP Basic Auth (see `weather_station_config.json`). Production URLs must include the `/weather_station/` prefix. On upload failure the server response body is logged.

## Troubleshooting

### Common Issues

1. **No data received**: Check nRF24L01 connections and power
2. **Invalid sensor readings**: Verify sensor wiring and I2C addresses
3. **Upload failures**: Check network connection and server credentials
4. **Serial port issues**: Verify Arduino port detection in config

### Debug Features

- Serial monitor output for Arduino debugging
- Python logging for data processing issues
- Status indicators for all system components

## File Structure

```
weather_station/
├── code/
│   ├── weather_station/             # Main sender code
│   ├── receive/                     # Receiver and processing
│   ├── sensor_tests/                # Sensor tests
│   └── ...                          # 
├── wiring                           # Wiring & circuit diagrams
├── LICENSE                          # License 
└── README.md                        # This file
```

## Future Enhancements

- Additional sensor support (UV, wind direction etc.)
- Solar power integration for standalone operation

## License

This project is open source and available under the GPL License.