# TODO — Weather Station R4 WiFi / Server

## Energy: optional optimizations (solar 30 W + lead-acid 33 Ah)

**Status (v1.9.1):** hourly self-reset off (saves the 5 min live mode with one upload per second every hour), first PM measurement right after boot. Everything below is optional and mainly matters in **November–January**.

**Estimated budget** (not measured — measure first, see step 1):

| Consumer (5 V level) | Estimate |
|---|---|
| UNO R4 WiFi, WiFi permanently connected | ~100 mA, mostly the ESP32-S3 WiFi module |
| PMSA003I (fan ~35 s every 5 min) | ~10 mA average |
| Small sensors + multiplexer | a few mA |
| LEDs on rain drop / reed module, board power LED | a few mA each |
| **Total** | **~110–150 mA ≈ 0.6–0.8 W** |

From the 12 V battery, including the board's step-down converter and the charge controller's own consumption, roughly **0.8–1.1 W ≈ 19–26 Wh/day**.

**Solar yield**, from PVGIS (JRC, monthly means 2011–2020 for Golm). Assumes 30 Wp and ~60 % system efficiency (PWM controller, lead-acid charging, temperature, soiling):

| Month | kWh/m²/day, tilt 40° south | → Wh/day | kWh/m²/day, horizontal | → Wh/day |
|---|---|---|---|---|
| Nov | 1.63 | ~29 | 0.90 | ~16 |
| Dec | 1.16 | ~21 | 0.56 | ~10 |
| Jan | 1.30 | ~23 | 0.70 | ~13 |
| Jun | 5.43 | ~98 | 5.58 | ~100 |

**Reading:**
- In summer there is several times more energy than needed.
- With the panel tilted ~40° south, December/January are **roughly balanced**.
- A flat or poorly oriented panel gives a **deficit of ~10 Wh/day** over about three months, which the battery cannot bridge. Usable capacity is ~200 Wh: 33 Ah × 12 V at 50 % depth of discharge, less in the cold.
- Monthly means hide dark spells of one to two weeks. The battery covers about 8–10 days without any yield.

**Options, in order of benefit per effort:**

1. **Measure:**
   - Current at the battery (station + controller) with a clamp meter or an inline meter, once in idle and once during an upload.
   - Check the panel orientation. All further decisions depend on these numbers.
2. **Battery voltage on the website:**
   - Voltage divider from the battery to a free analog pin (A0/A2/A3), e.g. 100 kΩ / 22 kΩ for ≤ 24 V.
   - New upload field `battery_voltage`: model, serializer and bulk CSV column on the website, plus the firmware.
   - Shows early whether winter becomes critical.
3. **Panel:**
   - Tilt it steeper for winter (~60°). That raises December yield and sheds snow.
   - Or use a larger panel (50–100 W), the simplest robust fix if step 1 shows a deficit.
4. **PMSA003I less often:** `PM_MEASUREMENT_INTERVAL` 15 min instead of 5 min saves ~5–7 mA (~0.7 Wh/day).
5. **Remove LEDs** on the rain drop module (power + output LED), the reed module and the board power LED: a few mA each, hardware only.
6. **Upload less often**, e.g. every 5 min instead of every minute:
   - Fewer TLS sessions and transmit bursts; data on the website up to 5 min late.
   - Needs `RING_BUFFER_SIZE` ≥ 300 + margin. At ~56 bytes per `BufferedSample` that does not fit in RAM, so the buffer would have to be compacted first: drop `cycle`/`packet_number` and store values as scaled integers, ~24–32 bytes per sample.
7. **Small or uncertain gains:**
   - Disconnecting WiFi between bursts: the ESP32-S3 stays powered; reconnecting costs energy and was a source of hangs before v1.8.
   - Sleeping the RA4M1 instead of `delay()`: small compared to the WiFi module.
   - The installed WiFiS3 library offers no modem power-save mode.

---

## Wind: rolling mean and gusts on the server (recommended)

**Context (firmware v1.4+):** The R4 WiFi uploads **raw anemometer revolutions per 500 ms snapshot** (`wind_speed` field). There is no 10 s rolling average or gust detection on the device anymore.

**Update v1.9:** snapshots are 1 Hz again, so `wind_speed` is revolutions per **1 s** and the website's conversion (`× 0.14`) is correct; the factor-2 problem of the 500 ms samples (v1.4–v1.8) is gone. Points 3–4 below (10 s mean, gusts) remain open.

**Problem (v1.4–v1.8):** The website assumes `wind_speed` is revolutions per **1 s** and multiplies by `WIND_ROTATIONS_TO_MPS = 0.14` (`datasets/views.py`, `datasets/plots.py`). The dashboard header averages over ~2 minutes (`JD_TWO_MINUTES`), not 10 s. With 500 ms samples this understated wind by ~2×.

**Proposal:**

1. **Ingest / storage:** Keep storing raw revolutions per sample in `Dataset.wind_speed` (no change to API schema).
2. **Conversion to m/s:** Derive sample interval from `jd` spacing (or assume 0.5 s for R4 WiFi after migration):
   `m/s = wind_speed × (1000 / interval_ms) × 0.14`
3. **Dashboard header:** Replace the 2 min average with a **10 s rolling mean** in m/s (last ~20 samples at 2 Hz).
4. **Gusts:** Compute on the server from the same window, e.g. `gust = max(m/s) − mean(m/s)` or peak 500 ms sample minus sustained mean; optional new field `wind_gust` or derived only in plots/API.
5. **Plots / CSV download:** Apply the same interval-aware conversion; document that classic 1 Hz senders still use 1 s intervals.

**Priority:** Medium — do before or together with R4 WiFi field deployment so displayed wind speed stays correct.

---

## Light sensor: follow-ups to the Adafruit lux formula (done in v1.7)

The switch from the Waveshare formula `(ch0 − 2·ch1)/(t·gain/762)` to the Adafruit formula `(ch0 − ch1)(1 − ch1/ch0)/(t·gain/408)` is implemented (`tsl2591_logic.h`, host test updated). Reason: the old formula went negative for infrared-rich light (`ch1/ch0 > 0.5`), so the light-polluted night sky read 0. The night sky brightness is a candidate second night-time cloud feature, since clouds reflect city light. Daylight now reads about 40 % lower than with v1.6.

Still open:

1. When flashing v1.7: add a **Calibration epoch** in the website admin (ideally together with the open IR view and the quartz window, so one epoch covers all changes).
2. After some weeks: compare lux with the DWD global irradiance in `cloud_eval/` (absolute scale behind the window), and evaluate night brightness as an additional cloud feature with `validate_detector.py`.

**Priority:** Medium — step 1 at deployment, step 2 once data from the new hardware exists.

---

## Bulk upload API (done in v1.9)

Implemented: `POST /weather_api/datasets/bulk/` on the website (CSV body, WEATHER-HMAC-V1 signed for the bulk path and `text/csv`) and in the firmware (`flushRingBuffer()` → `postBulk()`, body hashed and sent line by line). One request per minute instead of one per sample. See README → *Bulk upload and I2C recovery (v1.9)* and the website `docs/upload-hmac-v1.md`.
