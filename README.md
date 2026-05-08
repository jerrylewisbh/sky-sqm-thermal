# ESPHome Sky Thermal Weather Station

An advanced all-in-one meteorological station for ESP32, featuring a 32×24 MLX90640 thermal infrared camera, environmental sensing (BME280, TSL2591/TSL2561), and mechanical weather instruments (anemometer + rain sensor).

Designed to complement all-sky camera systems for astrophotography rigs: it provides both thermal imaging of the sky and a categorical "Sky Condition" classifier so you can see at a glance — or wire to automations — whether the sky is clear enough to image.

## Features

- **Live thermal imaging** — 32×24 heatmap rendered as a categorical color BMP keyed off Δ-from-ambient (deep blue = very clear → red = heavy cloud), so the image is interpretable across seasons without auto-stretching.
- **Sky Condition classifier** — Boltwood-style 6-bucket label (`very_clear` → `heavy_cloud`) computed from the thermal-vs-ambient temperature delta. See [docs/sky-condition.md](docs/sky-condition.md) for the full method.
- **Environmental sensing** — temperature, humidity, barometric pressure, illuminance.
  - TSL2591 with auto-gain across 4 levels for full dynamic range from starlight (~0.001 lx) to direct sun (~88,000 lx).
  - Sub-lux precision (3 decimals) so twilight / moonlight values aren't truncated to zero.
  - TSL2561 supported as a fallback if TSL2591 isn't present.
- **Wind & rain** — pulse-counted anemometer with sliding-window smoothing and isolated relay-sensed rain detector.
- **Self-diagnosing I²C bus** — at boot the device scans the bus and logs every ACKing address, plus a runtime retry every 30 s for any sensor that wasn't found at startup.
- **HTTP API** for external scripts and dashboards:
  - `http://<IP>/thermal` — live web dashboard (auto-refresh, cache-busted)
  - `http://<IP>/json` — full sensor + raw thermal frame JSON
  - `http://<IP>/thermal.bmp` — instant 32×24 categorical-color BMP
- **Home Assistant native** — exposes all sensors plus the `Sky Condition` text sensor via the ESPHome API and MQTT.

## Hardware Required

- **Microcontroller:** ESP32 (DevKit V1 or similar)
- **Thermal camera:** MLX90640 — **110° wide-angle variant recommended** (covers ~40 % of the sky hemisphere from zenith); 55° version also supported (~9 %)
- **Environmental sensor:** BME280 (I²C, 0x76 or 0x77)
- **Light sensor:** TSL2591 (I²C, 0x29) preferred — auto-gain logic is tuned for this part. TSL2561 (0x39 or 0x49) works as a fallback.
- **Anemometer:** passive 3-cup reed-switch type
- **Rain sensor:** 12 V contact sensor through a relay for ESP32-side isolation

## Wiring

| Component | Pin | ESP32 GPIO |
|---|---|---|
| **I²C bus (all sensors)** | SDA | GPIO 21 |
| **I²C bus (all sensors)** | SCL | GPIO 22 |
| **Anemometer** | Signal | GPIO 25 (internal pullup) |
| **Rain sensor relay** | NO/COM | GPIO 26 (internal pullup) |

I²C sensors run on 3.3 V. **Use 4.7 kΩ pull-ups on SDA and SCL to 3.3 V** — relying on internal pull-ups becomes unreliable as you add devices. The 12 V rain sensor must be isolated through a relay.

## Installation

1. Install [ESPHome](https://esphome.io/).
2. Clone this repo.
3. Create `secrets.yaml`:
   ```yaml
   wifi_ssid: "Your_SSID"
   wifi_password: "Your_Password"
   mqtt_broker: "10.0.0.100"
   mqtt_username: "user"
   mqtt_password: "pass"
   ```
4. Flash:
   ```bash
   esphome run sensortest.yaml
   ```

## API

### `GET /json`

Returns current sensor readings plus the full 768-element thermal frame:

```json
{
  "sensors": {
    "temp": 8.7,
    "hum": 57.8,
    "pres": 892.4,
    "lux": 0.0533,
    "wind": null,
    "rain": false,
    "thermal_min": -14.1,
    "thermal_max": -0.1,
    "thermal_avg": -11.2,
    "thermal_center": -11.6,
    "sky_delta_median": -19.9,
    "sky_condition": "clear"
  },
  "frame": [-12.2, -12.3, -12.4, ...]
}
```

| Field | Meaning |
|---|---|
| `temp` / `hum` / `pres` | BME280 ambient air |
| `lux` | TSL2591/TSL2561 illuminance (3-decimal precision; `88000` if saturated; `null` if sensor missing) |
| `wind` | Anemometer reading in km/h, `null` until the wind has been observed at least once |
| `rain` | Boolean from rain sensor, `null` if not configured |
| `thermal_min/max/avg/center` | Statistics over the 32×24 thermal frame in °C |
| `sky_delta_median` | `mean(frame) − ambient` in °C — the cloud signal (field name kept for HA continuity; computed as mean for performance) |
| `sky_condition` | One of `very_clear`, `clear`, `hazy`, `light_cloud`, `cloudy`, `heavy_cloud`, `unknown` |
| `frame` | 768 floats, row-major (32 cols × 24 rows), °C |

### `GET /thermal.bmp`

Raw 32×24 BMP with categorical Δ-from-ambient coloring (no auto-stretch). Useful for image-processing pipelines and external dashboards. Cache-busted via `?t=<millis>` query param when embedded in `/thermal`.

| Color | Δ range (°C) | Sky state |
|---|---|---|
| Deep blue | < −25 | very_clear |
| Cyan | −25 to −15 | clear |
| Green | −15 to −8 | hazy |
| Yellow | −8 to −3 | light_cloud |
| Orange | −3 to +3 | cloudy |
| Red | > +3 | heavy_cloud |
| Gray | n/a | ambient unavailable (BME280 not responding) |

### `GET /thermal`

HTML dashboard auto-refreshing every 2 s. Embeds `/thermal.bmp` and the env line.

## Home Assistant Integration

After flashing, the following entities appear automatically (via API or MQTT):

- `sensor.sky_average_temp`, `sensor.sky_min_temp`, `sensor.sky_max_temp`, `sensor.sky_center_temp`
- `sensor.ambient_temperature`, `sensor.ambient_humidity`, `sensor.barometric_pressure`
- `sensor.illuminance`
- `sensor.anemometer_wind_speed`, `binary_sensor.rain_sensor`, `sensor.rain_numeric`
- `text_sensor.sky_thermal_raw_data` — comma-separated thermal frame
- **`text_sensor.sky_condition`** — categorical sky state, useful for automations:
  ```yaml
  automation:
    - alias: Close roof on clouds
      trigger:
        platform: state
        entity_id: sensor.sky_condition
        to: heavy_cloud
        for: "00:02:00"
      action: ...
  ```

## Documentation

- [Sky Condition classifier — method, thresholds, calibration, references](docs/sky-condition.md)

## License

MIT
