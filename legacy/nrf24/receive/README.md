# Legacy serial → HTTPS uploader (`receive.py`)

Runs on **Windows** (and Linux). Uploads use **WEATHER-HMAC-V1** (no Basic Auth).

## Config

1. Copy `weather_station_config_example.json` to `weather_station_config.json`.
2. Set `device_id`, `key_id`, `hmac_secret_hex` from `provision_upload_device` / `rotate_upload_key`.
3. Keep `canonical_path` as `/weather_station/weather_api/datasets/` even if testing against a local URL.
4. On Windows, config is written atomically and ACLs are restricted to the current user (`icacls`). Strip CR from secrets if an editor introduced CRLF.

## Time

`X-Weather-Timestamp` uses UTC `time.time()`. Keep Windows time sync enabled; skew beyond ±300 s fails auth.

## Operation

- Interactive: `python receive.py`
- Or Task Scheduler / Windows service — do **not** put secrets in task arguments or Event Log messages.
- Failed uploads append to `failed_uploads.csv` locally.

Never log HMAC secrets, signatures, or nonces.
