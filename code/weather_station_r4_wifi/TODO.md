# TODO — Weather Station R4 WiFi / Server

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
