# Weather Station — Arduino UNO R4 WiFi

All-in-one variant: **one board** reads all sensors and uploads directly to the OST Django API over WiFi. No nRF24 radio, no receiver Arduino, no `receive.py` on a PC.

## Architecture

```
[Sensors + TCA9548A] ──I2C/GPIO──► [UNO R4 WiFi] ──HTTPS──► [Django API / Website]
```

Compared to the classic setup:

| Classic | R4 WiFi |
|---------|---------|
| Sender Arduino + nRF24 | **not needed** |
| Receiver Arduino + nRF24 | **not needed** |
| PC running `receive.py` | **not needed** |
| USB cable for data path | **not needed** |

## Hardware

### Board

- **Arduino UNO R4 WiFi** (not the UNO R4 Minima — Minima has no WiFi)
- Power: 6–24 V on `VIN` (solar/battery) or USB for bench testing
- **2.4 GHz WiFi** only (no 5 GHz)

### Sensors (unchanged)

Same sensors and TCA9548A multiplexer as the classic sender. Pin mapping is identical to `weather_station.ino`.

| Pin | Function |
|-----|----------|
| A4 (SDA), A5 (SCL) | I2C → TCA9548A |
| D2 | Rain reed (tipping bucket) |
| D3 | Anemometer (interrupt) |
| D4 | Watchdog reset output |
| D5 | Rain drop digital |
| D6 | PMSA003I SET (sleep/wake) |
| A1 | Rain drop analog |

**Removed:** D8–D12 (formerly nRF24L01 SPI) — no wiring needed.

See `../../wiring/pin_connections_r4_wifi.txt` for the full pin table.

### Power notes (solar / battery)

- R4 WiFi draws more current than a Nano, especially during WiFi transmit.
- Keep **PMSA003I sleep/wake** (already in firmware).
- Firmware sets `WIFI_DISCONNECT_AFTER_UPLOAD 1` — WiFi is only on during upload.
- Use a solid 5 V regulator and local capacitors on the board and sensors.
- `SERIAL_DEBUG` must be `0` in the field (no USB Serial reader).

## Software setup

### 1. Arduino IDE

1. Install board package: **Arduino UNO R4 Boards** (Boards Manager).
2. Select board: **Arduino UNO R4 WiFi**.
3. **Tools → Firmware Updater** — update the WiFi module firmware once (required for HTTPS).

### 2. Libraries

Same as the classic sender:

- Adafruit BME280, MLX90614, PM25 AQI, BusIO, Unified Sensor
- DFRobot UVIndex240370Sensor + DFRobot RTU
- TSL2591 support files are included in this folder (`DEV_Config.*`, `TSL2591.*`)

No RF24 library needed.

### 3. Credentials

```bash
cd weather_station/code/weather_station_r4_wifi
cp secrets.h.example secrets.h
# Edit secrets.h — WiFi SSID/password + API user/password
```

Use the same API user as `receive.py` / `weather_station_config.json`.

### 4. Upload sketch

Open `weather_station_r4_wifi.ino` in the Arduino IDE and upload.

For first tests, set `#define SERIAL_DEBUG 1` in the sketch, open Serial Monitor at **9600 baud**, and verify:

- All sensors initialize
- WiFi connects
- `NTP time synced`
- `Packet N uploaded`

For deployment, set `SERIAL_DEBUG 0` and re-flash.

## API upload format

The sketch POSTs `application/x-www-form-urlencoded` data to:

`https://<SECRET_API_HOST><SECRET_API_PATH>`

Fields match `receive.py` / `DatasetSerializer` (physical units, no integer scaling):

`jd`, `temperature` (°C), `pressure` (hPa), `humidity` (%), `illuminance` (lux), `wind_speed` (revolutions per sample), `rain` (mm collector depth), `sky_temp`, `box_temp`, `is_raining`, `pm1_0`, `pm2_5`, `pm10`, `uv_index`

Unit conversions applied in firmware:

- **Rain:** tipping-bucket count × 1.25 → mm collector depth (same as classic `receive.py`)
- **Wind:** raw anemometer revolutions per 1 s sample (website converts to m/s)

## Configuration defines (in .ino)

| Define | Default | Purpose |
|--------|---------|---------|
| `SERIAL_DEBUG` | `0` | USB debug output |
| `WIFI_DISCONNECT_AFTER_UPLOAD` | `1` | Save power between uploads |
| `WIFI_CONNECT_TIMEOUT_MS` | `20000` | WiFi join timeout |
| `PM_MEASUREMENT_INTERVAL` | `300000` | PMSA003I wake every 5 min |

## Troubleshooting

| Symptom | Check |
|---------|--------|
| `WiFi module not found` | Firmware updater, board selection (R4 **WiFi**) |
| `SSL connect failed` | WiFi firmware version, host reachable on 443 |
| `NTP time sync failed` | Internet access from observatory WiFi |
| `Upload skipped: validation failed` | Sensor wiring / bogus readings |
| HTTP 401 | `SECRET_API_USER` / `SECRET_API_PASS` in secrets.h |
| HTTP 400 | Field out of range — check Serial debug values |

### HTTPS / certificates

The R4 WiFi firmware embeds a CA bundle that works with most public HTTPS sites (including Let's Encrypt). If SSL fails only for your server, update the WiFi firmware or add a custom root CA via `sslClient.setCACert()` (see [Arduino SSL docs](https://docs.arduino.cc/tutorials/uno-r4-wifi/wifi-examples)).

## Migration from classic setup

1. Move the sensor shield/cabling from the old sender Uno/Nano to the R4 WiFi (same pins).
2. Remove both nRF24 modules.
3. Configure `secrets.h` and flash `weather_station_r4_wifi.ino`.
4. Run in parallel with the old receiver for a few days if desired.
5. Decommission receiver Arduino + `receive.py` when stable.

## File layout

```
weather_station_r4_wifi/
├── weather_station_r4_wifi.ino   # Main sketch
├── secrets.h.example             # Template credentials
├── secrets.h                     # Your credentials (gitignored, create locally)
├── DEV_Config.* / TSL2591.*      # Light sensor support
└── README.md                     # This file
```
