# Weather Station — Arduino UNO R4 WiFi

All-in-one variant: **one board** reads all sensors and uploads directly to the OST Django API over WiFi. No nRF24 radio, no receiver Arduino, no `receive.py` on a PC.

## Architecture

```
[Sensors + PCA9548] ──I2C/GPIO──► [UNO R4 WiFi] ──HTTPS──► [Django API / Website]
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

Same sensors and channel assignment as the classic sender; pin mapping is identical to `weather_station.ino`. The multiplexer is an Adafruit PCA9548 STEMMA QT board (TCA9548A compatible, same address 0x70, no firmware change) on the header I2C bus with V+ = 5 V and its Vlogic switch on 3.3 V, so it also supplies the I2C sensors with 3.3 V. Exception: the PMSA003I takes VIN from the 5 V rail (its fan current would overheat the multiplexer's 3.3 V regulator); only GND/SDA/SCL come from port 0, never connect the red STEMMA QT wire. Do not use the R4's Qwiic port for it: that is a separate 3.3 V-only bus (`Wire1`).

| Pin | Function |
|-----|----------|
| A4 (SDA), A5 (SCL) | I2C → PCA9548 (header bus `Wire`, 5 V logic) |
| D2 | Rain reed (tipping bucket) |
| D3 | Anemometer reed contact (interrupt), external RC debounce: R1 10 kΩ to 5 V, C1 100 nF to GND, R2 1 kΩ to the contact |
| D4 | → RESET pin: hourly self-reset (input during operation, output LOW to reset) |
| D5 | Rain drop digital |
| D6 | PMSA003I SET (sleep/wake) |
| A1 | Rain drop analog |

**Removed:** D8–D12 (formerly nRF24L01 SPI) — no wiring needed.

See `../../wiring/pin_connections_r4_wifi.txt` for the full pin table.

### Power notes (solar / battery)

- R4 WiFi draws more current than a Nano, especially during WiFi transmit.
- Keep **PMSA003I sleep/wake** (already in firmware).
- **Burst upload:** WiFi is off between 60 s upload windows (~5–15 s active per minute).
- Use a solid 5 V regulator and local capacitors on the board and sensors.
- `SERIAL_DEBUG` must be `0` in the field (no USB Serial reader).

## Upload strategy (v1.4 — cooperative scheduler)

| Setting | Value | Meaning |
|---------|-------|---------|
| Snapshot | **2 Hz** (500 ms) | Ring buffer + wind pulse window |
| BME280 | **2 Hz** | Temperature, humidity, pressure |
| TSL2591 | **1 Hz** | Lux, non-blocking poll with auto-ranging (see below) |
| MLX90614 | **0.5 Hz** | Sky / box IR temperature |
| UV | **1 Hz** | UV index |
| Rain | **2 Hz** | Reed tips + drop sensor |
| PM | **5 min** | Sleep/wake cycle (unchanged) |
| Ring buffer | **130 samples** | 60 s × 2 Hz = 120 + margin |
| Startup live | **5 min @ 1 Hz** | Test uploads (snapshots still 2 Hz) |
| Burst | **every 60 s** | One WiFi/TLS session, HTTP keep-alive |

### Slower sensors between snapshots

Values for sensors polled less often than 500 ms are **held** (last known reading), not averaged. Example: MLX90614 updates every 2 s; the four snapshots in between repeat the previous sky/box temperature. This keeps each buffer row self-contained without inventing synthetic averages.

### Light sensor (TSL2591, since v1.6; lux formula since v1.7)

The driver (`TSL2591.cpp`, logic in `tsl2591_logic.h`) integrates continuously and is polled without blocking: `TSL2591_Poll()` returns immediately when no integration has finished, so the 2 Hz snapshot and rain polling are not delayed (the old driver blocked for ≥ 300 ms per read).

- **Auto-ranging in both directions** over eight gain × integration-time stages, from 1×/100 ms (direct sun) to 9876×/600 ms (night sky). A channel at ≥ 90 % of full scale steps one stage down, a reading that would stay below 50 % of full scale on the next stage steps one up. Readings in a saturated state are discarded, except on the least sensitive stage, where they are reported as a lower bound.
- **Lux as float** with the formula of the Adafruit TSL2591 library (since v1.7): `(ch0 − ch1)·(1 − ch1/ch0) / (t·gain / 408)`, capped at 200 000 lx. Unlike the Waveshare formula `(ch0 − 2·ch1) / (t·gain / 762)` used up to v1.6, it stays positive for infrared-rich light (street lighting, light-polluted night sky), so the night sky brightness is measured instead of reading 0 (older drivers wrapped those cases to values such as 53 000 lx).
- **Scale change in v1.7:** values are not comparable with earlier firmware. Daylight (infrared fraction `ch1/ch0` ≈ 0.15–0.3) reads about 40 % lower than with v1.6; at `ch1/ch0` ≥ 0.5 v1.6 reported 0. When flashing v1.7, add a **Calibration epoch** in the website admin so the cloud detection recalibrates.
- In direct sun even the least sensitive stage (1×/100 ms) can saturate. Only `ch0` clips, so the reported value is a lower bound well below the true illuminance; it still marks the interval as very bright.
- **I2C errors** (no ACK, short read) keep the last valid value; after 60 consecutive errors the sensor is re-initialised. `TSL2591_Init()` checks the chip ID, so a missing sensor is reported as unavailable.
- The upload sends `illuminance` with three decimals to resolve the night sky brightness.

Host test of the logic: `cd tools && g++ -std=c++17 -Wall -Wextra -o tsl2591_selftest tsl2591_selftest.cpp && ./tsl2591_selftest`

### Wind

Firmware uploads **raw revolutions per 500 ms**. Rolling 10 s mean and gust detection belong on the server — see `TODO.md`.

**Buffer sizing:** `RING_BUFFER_SIZE` must be ≥ `UPLOAD_BURST_INTERVAL_MS / SNAPSHOT_INTERVAL_MS` + spare. At 60 s / 500 ms → 120 samples; buffer = 130.

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

### 3. Credentials (WEATHER-HMAC-V1)

```bash
cd weather_station/code/weather_station_r4_wifi
cp secrets.h.example secrets.h
# Edit secrets.h — WiFi + device_id / key_id / HMAC secret
```

Provision the device on the Django server (from the website project):

```bash
python manage.py provision_upload_device --device-id <your-device-id> --key-id key1
```

Copy the printed `device_id`, `key_id`, and 64-char hex `secret` into `secrets.h` as `SECRET_DEVICE_ID`, `SECRET_KEY_ID`, and `SECRET_HMAC_SECRET_HEX`.

| Define | Purpose |
|--------|---------|
| `SECRET_WIFI_SSID` / `SECRET_WIFI_PASS` | Observatory 2.4 GHz WiFi |
| `SECRET_API_HOST` / `SECRET_API_PATH` | HTTPS POST target |
| `SECRET_HMAC_PATH` | Canonical signing path (default: `/weather_station/weather_api/datasets/`) |
| `SECRET_DEVICE_ID` | Provisioned device id |
| `SECRET_KEY_ID` | Active signing key id |
| `SECRET_HMAC_SECRET_HEX` | 32-byte secret as 64 hex chars |

**Never log or commit** `SECRET_HMAC_SECRET_HEX` or signatures in the field. Use `SERIAL_DEBUG` only on the bench.

`SECRET_HMAC_PATH` is used only for the canonical string. `SECRET_API_PATH` may differ for local testing, but production should keep both equal.

### 4. Upload sketch

Open `weather_station_r4_wifi.ino` in the Arduino IDE and upload.

For first tests, set `#define SERIAL_DEBUG 1` in the sketch, open Serial Monitor at **115200 baud** (`SERIAL_BAUD`), and verify:

- All sensors initialize
- `weatherHmacSelfTest OK` (known test vectors)
- `NTP time synced` before any upload
- `Upload mode: startup live (1 Hz)` then uploads each second
- After 5 min: `Upload mode: burst (buffered)` and `Burst upload: N samples`

At boot (debug only) an **I2C scan** lists the devices on the main bus and on every multiplexer channel, and marks missing expected sensors:

```
I2C scan
  main bus: 0x70 (multiplexer)
  channel 0: -   <-- expected 0x12 (PMSA003I), answers only while awake
  channel 1: 0x76 (BME280)
  channel 2: 0x23 (SEN0636)
  channel 3: 0x5A (MLX90614)
  channel 4: -   <-- expected 0x29 (TSL2591)
  channel 5: -
  ...
```

Nothing on a channel means no power or no I2C connection on that port (check wire order: black GND, red 3.3 V, blue SDA, yellow SCL); a device at an unexpected address points to a different module or address jumper. Sensors not found at boot are searched again every `SENSOR_RETRY_INTERVAL_MS` (60 s), so a sensor plugged in or repaired later is picked up without a reset.

Every `DEBUG_INTERVAL_MS` (2 s) a sensor report shows the current reading of each sensor (`not available` if it was not found at boot):

```
--- t=125 s [live]  buffer 0/130  pkt 118  uploads ok/failed 118/0
BME280    T 21.34 C   RH 45.2 %   p 1013.2 hPa
MLX90614  sky 18.20 C   box 22.10 C   sky-box -3.90 K   sky-air -3.14 K
TSL2591   1234.567 lx   ch0 2861  ch1 512   stage 3 (25x 400 ms)
SEN0636   UV index 1   raw 412
PMSA003I  PM1.0 3  PM2.5 5  PM10 6 ug/m3   asleep, next in 212 s
Rain      tips 2 since boot   D2 HIGH   drop sensor dry (D5 HIGH, A1 1015)
Wind      pulses 3 per 500 ms   6.0 /s (last 2.0 s)   total 412   D3 HIGH
```

Hardware checks with the report:

- **TSL2591:** cover the sensor and shine a lamp on it; the stage should step up in the dark and down in bright light, `ch0` should stay below ~90 % of full scale (36863 at 100 ms, 65535 otherwise) except in direct sun on stage 0.
- **SEN0636:** `raw` resolves much finer than the integer UV index, useful to compare windows (plexiglass vs. quartz).
- **Rain gauge:** tip the bucket slowly by hand and count; `tips` should increase by exactly one per tip. The firmware counts every change of D2, so a reed that only closes briefly while the bucket passes would count two per tip.
- **Anemometer:** turn the cups slowly by hand, one revolution at a time; the pulse total should increase by the number of contact closures per revolution, not by more (otherwise check the RC debounce on D3).
- **Rain drop sensor:** a wet finger on the plate switches it to `wet` and lowers `A1`.

For deployment, set `SERIAL_DEBUG 0` and re-flash.

## API upload format

The sketch POSTs `application/x-www-form-urlencoded` data to:

`https://<SECRET_API_HOST><SECRET_API_PATH>`

### WEATHER-HMAC-V1 authentication

Each POST is signed over the **exact raw body bytes** sent on the wire. Headers:

| Header | Value |
|--------|-------|
| `X-Weather-Device` | `SECRET_DEVICE_ID` |
| `X-Weather-Key-Id` | `SECRET_KEY_ID` |
| `X-Weather-Timestamp` | Unix UTC seconds (requires NTP) |
| `X-Weather-Nonce` | 32 lowercase hex chars (16 random bytes from hardware TRNG) |
| `X-Weather-Signature` | lowercase hex HMAC-SHA256 |

Canonical string (UTF-8, no trailing newline), lines joined by `\n`:

```
WEATHER-HMAC-V1
POST
<SECRET_HMAC_PATH>
application/x-www-form-urlencoded
<device_id>
<key_id>
<timestamp>
<nonce>
<sha256_hex_of_exact_raw_body>
```

Signing uses vendored **SHA-256 / HMAC-SHA256** (`sha256.c`, based on Brad Conte's public-domain crypto-algorithms). Nonces come from the Renesas **SCE hardware TRNG** (`HW_SCE_RNG_Read`, same path as the ArduinoCore `WMath` TRNG). No mbedtls Library Manager package and no `random()` fallback — if TRNG or signing fails, the upload is skipped and samples stay buffered.


NTP is refreshed about every **20 minutes** while uploading. Uploads require `timeSynced`.

On HTTP **401**, **403**, or **429**, buffered samples are **not** dropped. The firmware parses `Retry-After` when present and applies exponential backoff (60 s initial, up to 30 min).

### Payload fields

`jd`, `temperature` (°C), `pressure` (hPa), `humidity` (%), `illuminance` (lux), `wind_speed` (revolutions per sample), `rain` (mm collector depth), `sky_temp`, `box_temp`, `is_raining`, `pm1_0`, `pm2_5`, `pm10`, `uv_index`

Unit conversions applied in firmware:

- **Rain:** tipping-bucket count × 1.25 → mm collector depth (same as classic `receive.py`)
- **Wind:** raw anemometer revolutions per 500 ms snapshot (server converts to m/s — see `TODO.md`)

## Configuration (in .ino)

| Constant | Default | Purpose |
|----------|---------|---------|
| `SERIAL_DEBUG` | `0` | USB debug output |
| `SNAPSHOT_INTERVAL_MS` | `500` | Ring buffer / wind window (2 Hz) |
| `INTERVAL_BME_MS` | `500` | BME280 poll rate |
| `INTERVAL_LUX_MS` | `1000` | TSL2591 poll rate |
| `INTERVAL_MLX_MS` | `2000` | MLX90614 poll rate |
| `INTERVAL_UV_MS` | `1000` | UV sensor poll rate |
| `INTERVAL_RAIN_MS` | `500` | Rain reed + drop poll rate |
| `LIVE_UPLOAD_INTERVAL_MS` | `1000` | Startup live upload rate (1 Hz) |
| `STARTUP_LIVE_UPLOAD_MS` | `300000` | Live upload period after boot (5 min) |
| `UPLOAD_BURST_INTERVAL_MS` | `60000` | Burst upload interval in field mode |
| `RING_BUFFER_SIZE` | `130` | Max buffered samples (60 s @ 2 Hz + margin) |
| `WIFI_CONNECT_TIMEOUT_MS` | `20000` | WiFi join timeout |
| `NTP_RESYNC_INTERVAL_MS` | `1200000` | Re-sync NTP every 20 min |
| `UPLOAD_BACKOFF_INITIAL_MS` | `60000` | Initial auth/rate-limit backoff |
| `UPLOAD_BACKOFF_MAX_MS` | `1800000` | Max backoff (30 min) |
| `PM_MEASUREMENT_INTERVAL` | `300000` | PMSA003I wake every 5 min |
| `DISABLE_LED_MATRIX` | `1` | Blank onboard 12×8 LED matrix |

## Power saving

- **Burst upload:** snapshot 2 Hz, WiFi only during ~60 s burst windows
- **Cooperative scheduler:** each sensor at its own rate (≤ 2 Hz); short `delay()` until next timer
- **Non-blocking wind:** permanent interrupt, 500 ms pulse windows
- **Startup live:** 5 min persistent WiFi for easy commissioning, then auto switch to burst
- **`DISABLE_LED_MATRIX`**: all onboard LEDs off after boot
- **`SERIAL_DEBUG 0`**: no blocking Serial in the field
- **PMSA003I sleep/wake**: fan/laser off between PM cycles
- **Rain per interval:** tips per 500 ms sample window

## Troubleshooting

| Symptom | Check |
|---------|--------|
| `WiFi module not found` | Firmware updater, board selection (R4 **WiFi**) |
| `SSL connect failed` | WiFi firmware version, host reachable on 443 |
| `NTP time sync failed` | Internet access from observatory WiFi |
| `Upload skipped: time not synced` | Wait for NTP; uploads buffer until clock is valid |
| `weatherHmacSelfTest failed` | SHA256/HMAC mismatch — re-flash; check `sha256.c` is compiled with the sketch |
| `Upload skipped: HMAC signing failed` | SCE TRNG or secret parse failure; check `SECRET_HMAC_SECRET_HEX` |
| `Upload deferred (backoff)` | Recent 401/403/429; waits per Retry-After / exponential backoff |
| `Upload skipped: validation failed` | Sensor wiring / bogus readings |
| HTTP 401 / 403 | Wrong device id, key id, or secret; re-provision or rotate key |
| HTTP 429 | Rate limited; firmware backs off, buffer retained |
| HTTP 400 | Field out of range — check Serial debug values |

### HTTPS / certificates

The R4 WiFi firmware embeds a CA bundle that works with most public HTTPS sites (including Let's Encrypt). If SSL fails only for your server, update the WiFi firmware or add a custom root CA via `sslClient.setCACert()` (see [Arduino SSL docs](https://docs.arduino.cc/tutorials/uno-r4-wifi/wifi-examples)).

## Migration from classic setup

1. Move the sensor shield/cabling from the old sender Uno/Nano to the R4 WiFi (same pins).
2. Remove both nRF24 modules.
3. Configure `secrets.h` and flash `weather_station_r4_wifi.ino`.
4. Run in parallel with the old receiver for a few days if desired.
5. Decommission receiver Arduino + `receive.py` when stable.

## HMAC debug / self-test

**Bench only** (`SERIAL_DEBUG 1`):

1. On boot, confirm `weatherHmacSelfTest OK`.
2. Confirm `NTP time synced` before uploads.
3. Log HTTP status lines only — never print `SECRET_HMAC_SECRET_HEX`, nonces, or signatures in production.

**Host-side vector check** (Linux, OpenSSL dev headers):

```bash
cd weather_station/code/weather_station_r4_wifi/tools
g++ -std=c++17 -Wall -Wextra -o hmac_selftest hmac_selftest.cpp -lcrypto
./hmac_selftest
```

Expected vectors are in `tools/hmac_test_vectors.txt`. Firmware `weatherHmacSelfTest()` uses the same constants.

## File layout

```
weather_station_r4_wifi/
├── weather_station_r4_wifi.ino   # Main sketch
├── weather_hmac.h / .cpp         # WEATHER-HMAC-V1 helpers
├── sha256.h / .c                 # Vendored SHA-256 + HMAC (no mbedtls)
├── secrets.h.example             # Template credentials
├── secrets.h                     # Your credentials (gitignored, create locally)
├── tools/
│   ├── hmac_test_vectors.txt     # Known WEATHER-HMAC-V1 digests
│   ├── hmac_selftest.cpp         # Host OpenSSL vector check
│   └── tsl2591_selftest.cpp      # Host check of lux formula + auto-ranging
├── TODO.md                       # Server-side wind + bulk API plans
├── DEV_Config.* / TSL2591.*      # Light sensor driver (I2C helpers, polling)
├── tsl2591_logic.h               # Lux formula + auto-ranging (host-testable)
└── README.md                     # This file
```
