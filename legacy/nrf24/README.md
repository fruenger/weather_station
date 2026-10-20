# Archive: two-board setup with nRF24L01 (until 2026)

Before the Arduino UNO R4 WiFi, the station used two boards: a sender Uno/Nano read the sensors and transmitted them by nRF24L01 radio to a receiver Arduino, and `receive.py` on a PC uploaded the data to the website. This setup is no longer in use; the current firmware is in `../../code/weather_station_r4_wifi/`.

The files are kept for reference only and are not maintained.

| Path | Content |
|---|---|
| `weather_station/` | Sender sketch v2.2.1 (sensors + nRF24, 16 × int16 per 32-byte packet) |
| `receive/` | Receiver sketch, `receive.py` uploader (WEATHER-HMAC-V1) and old test measurements |
| `transmit/` | Minimal nRF24 transmit test |
| `wiring/` | Pin table and diagrams of the Uno + nRF24 setup |

Packet layout of the radio link:

```
[timestamp, temperature*100, pressure*10, humidity*100, illuminance, rain_tips, wind_revolutions,
 packet_number, sky_temp*100, box_temp*100, rain_detection, rain_analog, pm1_0, pm2_5, pm10, uv_index]
```

The sender still contains the old TSL2591 driver with the Waveshare lux formula, which reads 0 for infrared-rich light; the R4 firmware uses the Adafruit formula since v1.7.
