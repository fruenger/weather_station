# TODO — Weather Station R4 WiFi / Server

## Wind: rolling mean and gusts on the server (recommended)

**Context (firmware v1.4+):** The R4 WiFi uploads **raw anemometer revolutions per 500 ms snapshot** (`wind_speed` field). There is no 10 s rolling average or gust detection on the device anymore.

**Problem:** The website currently assumes `wind_speed` is revolutions per **1 s** and multiplies by `WIND_ROTATIONS_TO_MPS = 0.14` (`datasets/views.py`, `datasets/plots.py`). The dashboard header averages over ~2 minutes (`JD_TWO_MINUTES`), not 10 s. With 500 ms samples this understates wind by ~2× until conversion is fixed.

**Proposal:**

1. **Ingest / storage:** Keep storing raw revolutions per sample in `Dataset.wind_speed` (no change to API schema).
2. **Conversion to m/s:** Derive sample interval from `jd` spacing (or assume 0.5 s for R4 WiFi after migration):
   `m/s = wind_speed × (1000 / interval_ms) × 0.14`
3. **Dashboard header:** Replace the 2 min average with a **10 s rolling mean** in m/s (last ~20 samples at 2 Hz).
4. **Gusts:** Compute on the server from the same window, e.g. `gust = max(m/s) − mean(m/s)` or peak 500 ms sample minus sustained mean; optional new field `wind_gust` or derived only in plots/API.
5. **Plots / CSV download:** Apply the same interval-aware conversion; document that classic 1 Hz senders still use 1 s intervals.

**Priority:** Medium — do before or together with R4 WiFi field deployment so displayed wind speed stays correct.

---

## Light sensor: switch to the Adafruit lux formula

**Context (firmware v1.6):** `tsl2591_logic.h` still uses the Waveshare formula

```
lux = (ch0 − 2·ch1) / (t_ms · gain / 762)
```

It was kept on purpose so that new values stay comparable with the archive. Since v1.6 a negative result is clamped to 0; older firmware wrapped it to large values, e.g. the 53 000 lx night artefact.

**Problem:**

- The formula goes negative as soon as the infrared channel exceeds half of the full-spectrum channel (`ch1/ch0 > 0.5`). That is typical for street lighting and for the light-polluted night sky, so the night sky brightness reads 0.
- With the old driver, 38 % of the night samples were the 53 000 lx artefact. Part of that may have been noise from the gain stuck at 1x, but IR-rich night light is likely.
- A night brightness signal would be useful: clouds reflect city light, so an overcast sky is brighter. It could become a second night-time feature for the cloud detection (`datasets/cloud_detection.py` on the website), independent of the IR sensor.
- The constant 762 is undocumented (comment "GA * 53").

**Proposal:** use the formula of the Adafruit TSL2591 library:

```
cpl = t_ms · gain / 408
lux = (ch0 − ch1) · (1 − ch1/ch0) / cpl      (0 if ch0 == 0)
```

It stays positive for any physical light, since `ch0` (full spectrum) always contains the infrared part `ch1`.

**Effect on the values:** the scale changes. The ratio new/old depends on the infrared fraction `r = ch1/ch0`:

| r | 0.15 | 0.25 | 0.35 | 0.45 | ≥ 0.5 |
|---|------|------|------|------|-------|
| new / old | 0.55 | 0.60 | 0.75 | 1.6 | old = 0, new > 0 |

Daylight (r ≈ 0.15–0.3) therefore reads roughly 40 % lower. Neither formula is calibrated for the window in front of the sensor. An absolute scale can be fitted afterwards against the DWD global irradiance (`cloud_eval/`).

**Before deciding:** run v1.6 for a few nights and check how often the night values are 0 at high gain. If they are rarely 0, the IR fraction was mostly noise and the switch is optional.

**Steps:**

1. Change `computeLux()` and `LUX_DF` in `tsl2591_logic.h`; adapt `tools/tsl2591_selftest.cpp` (expected values, IR-rich case > 0).
2. Bump the firmware version and note the scale change in `README.md`.
3. On the website, add a **Calibration epoch** in the admin at the time of flashing; the cloud detection then recalibrates the lux feature. Ideally flash together with the planned hardware changes (open IR view, quartz window), so that one epoch covers all of them.
4. Note the break in the illuminance series for plots / CSV users (website `README.md`, upload field table).
5. After some weeks, compare lux with the DWD global irradiance in `cloud_eval/`, and evaluate night brightness as an additional cloud feature with `validate_detector.py`.

**Priority:** Medium — cheap to do, but only worth it together with the next calibration epoch.

---

## Bulk upload API (optional server optimization)

**Problem:** In burst mode the Arduino uploads up to ~60 samples per minute via separate HTTP POST requests on one keep-alive TLS connection. Each POST still carries full HTTP headers and Django/DRF processing overhead.

**Proposal:** Add a bulk ingestion endpoint on the Django website, e.g.:

```
POST /weather_station/weather_api/datasets/bulk/
Content-Type: application/json
Authorization: Basic …

{
  "samples": [
    {
      "jd": 2460123.45678,
      "temperature": 23.45,
      "pressure": 1013.2,
      …
    },
    …
  ]
}
```

**Benefits:**

- One HTTP request per burst instead of ~60
- Shorter WiFi active time → lower energy use on solar power
- Less server load (one transaction, optional `bulk_create`)

**Implementation sketch (website):**

1. Add `BulkDatasetSerializer` in `datasets/api/serializers.py` (list of `DatasetSerializer` fields, max e.g. 120 items).
2. Add `BulkCreateDatasetView` in `datasets/api/views.py` with same auth as `CreateDatasetView`.
3. Register URL in `datasets/api/urls.py`.
4. Add tests in `datasets/tests.py` (valid bulk, partial validation errors, empty list).
5. Update `weather_station_r4_wifi.ino` to send JSON bulk in `flushRingBuffer()` when endpoint is available.

**Arduino side (after server is ready):**

- Use `ArduinoJson` to build a compact JSON array from `ringBuffer[]`.
- Single `POST` per burst with `Content-Type: application/json`.
- Fallback: keep existing per-sample keep-alive POSTs if bulk returns 404.

**Priority:** Low — current keep-alive burst is acceptable; implement when field deployment shows upload time or server load is an issue.
