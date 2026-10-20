# OST Observatory Weather Station

Arduino weather station on the roof of the OST observatory. An **Arduino UNO R4 WiFi** reads all sensors once per second and uploads the data over HTTPS to the observatory website (Django, repository `ost_weather_station_website`), which stores, plots and evaluates it, including cloud detection from the sky sensors.

```
[Sensors + PCA9548 I2C multiplexer] ──I2C/GPIO──► [UNO R4 WiFi] ──HTTPS (WEATHER-HMAC-V1)──► [Website API]
```

Setup, configuration, upload protocol and troubleshooting: **[code/weather_station_r4_wifi/README.md](code/weather_station_r4_wifi/README.md)**.

## Sensors

| Sensor | Measures | Connection |
|---|---|---|
| BME280 | Temperature, humidity, pressure | I2C, multiplexer channel 1 |
| TSL2591 | Illuminance (sky-facing) | I2C, channel 4 |
| MLX90614 | Sky temperature (IR) and box temperature | I2C, channel 3 |
| DFRobot SEN0636 | UV index (240–370 nm) | I2C, channel 2 |
| PMSA003I | Particulate matter PM1.0 / PM2.5 / PM10, sleeps between measurements | I2C, channel 0; SET on D6 |
| Tipping-bucket rain gauge | Rainfall (reed contact) | D2 |
| Rain drop module | Rain detection | D5 digital, A1 analog |
| Anemometer | Wind speed (reed contact, RC debounce) | D3 |

The multiplexer is an Adafruit PCA9548 STEMMA QT board (TCA9548A compatible, 0x70) on A4/A5. The station runs on a 30 W solar panel with a 33 Ah lead-acid battery.

## Repository layout

```
weather_station/
├── code/
│   ├── weather_station_r4_wifi/   # Station firmware (Arduino sketch), README, TODO, host self-tests in tools/
│   └── sensor_tests/              # Standalone bench tests for single sensors
├── wiring/
│   ├── pin_connections_r4_wifi.txt  # Pin table, power budget, wiring notes
│   ├── wiring_diagram.py            # Generates the diagram below from one description
│   ├── wiring_r4_wifi.svg / .html   # Wiring diagram (static / interactive)
│   └── wiring_r4_wifi.png           # Preview (generated, gitignored)
├── legacy/nrf24/                  # Archive: former two-board setup with nRF24 radio and receive.py
├── .github/workflows/             # CI: host self-tests of the firmware (HMAC, bulk CSV, lux formula)
├── LICENSE
└── README.md
```

After wiring changes, regenerate the diagram with `python3 wiring/wiring_diagram.py` and keep `pin_connections_r4_wifi.txt` in sync.

## Quick start

1. Arduino IDE: install the board package *Arduino UNO R4 Boards* and the libraries listed in the firmware README.
2. Copy `code/weather_station_r4_wifi/secrets.h.example` to `secrets.h` and fill in WiFi and the upload key from the website (`provision_upload_device`).
3. Flash `code/weather_station_r4_wifi/weather_station_r4_wifi.ino`. For the field, set `SERIAL_DEBUG 0`.

## License

GPL, see `LICENSE`.
