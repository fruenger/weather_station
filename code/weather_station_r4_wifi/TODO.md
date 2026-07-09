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
