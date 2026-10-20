# Sensor tests

Small standalone sketches to check one sensor at a time on the bench, before or after it goes into the station. They are not used by the station firmware (`../weather_station_r4_wifi/`).

For a quick check of the assembled station, flash the firmware with `SERIAL_DEBUG 1` instead: at boot it scans all multiplexer channels, and during operation it prints the sensor readings and upload status.

## Sketches

| Sketch | Sensor | Connection | Baud |
|---|---|---|---|
| `i2c_scanner/` | Any I2C device behind the PCA9548/TCA9548A (0x70) | Multiplexer on A4/A5; lists the addresses found on channels 0–7 | 115200 |
| `bme280/bme280_test/` | BME280 temperature / humidity / pressure (0x76) | I2C directly on A4/A5 | 9600 |
| `ir_sensor/ir_sensor_test/` | MLX90614 sky / box temperature (0x5A) | I2C directly on A4/A5 | 9600 |
| `air_quality_sensor/pmsa003i_test/` | PMSA003I particulate matter (0x12) | I2C directly on A4/A5, VIN on 5 V | 9600 |
| `uv_sensor/uv_index_test/` | DFRobot SEN0636 UV index (0x23), the sensor in the station | I2C, optionally through the multiplexer (`TCA_CHANNEL_UV`) | 115200 |
| `uv_sensor/guva_s12sd_test/` | Adafruit GUVA-S12SD analog UV sensor (alternative, not installed) | OUT → A0 | 9600 |
| `anemometer/` | Anemometer reed contact | D3 (interrupt) with the RC debounce from `wiring/pin_connections_r4_wifi.txt` | 9600 |
| `rain_gauge/rain_gauge_test/` | Tipping-bucket rain gauge (reed module) | D2 | 9600 |
| `rain_drop/rain_drop_test/` | Rain drop module | A1 analog, D2 digital (the station uses D5 for digital) | 9600 |

The TSL2591 light sensor has no separate sketch. Its lux formula and auto-ranging are checked on the host by `../weather_station_r4_wifi/tools/tsl2591_selftest.cpp`, and the firmware debug output shows the raw counts.

## Station I2C layout

The tests talk to the sensor directly on A4/A5. In the station the sensors sit behind the multiplexer (0x70); see `../../wiring/pin_connections_r4_wifi.txt`:

| Channel | Sensor | Address |
|---|---|---|
| 0 | PMSA003I | 0x12 |
| 1 | BME280 | 0x76 |
| 2 | SEN0636 UV | 0x23 |
| 3 | MLX90614 | 0x5A |
| 4 | TSL2591 | 0x29 |

## Troubleshooting

- **Sensor not found:** check VCC/GND/SDA/SCL and the address with `i2c_scanner`. The multiplexer must be powered even if only one channel is used.
- **MLX90614 reads odd values:** the emissivity is stored in the sensor's EEPROM; the test prints it.
- **PMSA003I:** needs ~30 s after power-up before the values settle.
