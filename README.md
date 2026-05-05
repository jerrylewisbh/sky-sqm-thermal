# ESPHome Sky Thermal Weather Station

An advanced all-in-one meteorological station for ESP32, featuring a 32x24 MLX90640 Thermal Infrared Camera, environmental sensing (BME280/TSL2561), and mechanical weather instruments (Anemometer/Rain Sensor).

This project was specifically designed to complement All-Sky camera systems, providing both visual thermal data of the sky and precise local weather metrics.

##  Features

- **Live Thermal Imaging:** Real-time 32x24 heatmap with auto-scaling and "Ironbow" color palette.
- **Environmental Sensing:** High-precision Temperature, Humidity, Barometric Pressure, and Light Intensity (Lux).
- **Wind & Rain Tracking:** Optimized pulse counting for anemometers and isolated relay sensing for rain detectors.
- **High-Performance API:** Direct endpoints for integration with external scripts:
  - `http://<IP>/thermal`: Web dashboard.
  - `http://<IP>/json`: Raw sensor data and thermal pixel array in JSON format.
  - `http://<IP>/thermal.bmp`: Instant uncompressed bitmap image of the thermal feed.
- **Home Assistant Native:** Full "Set and Forget" integration via the ESPHome Native API.

## Hardware Required

- **Microcontroller:** ESP32 (DevKit V1 or similar).
- **Thermal Camera:** MLX90640 (110° or 55° version).
- **Environmental Sensor:** BME280 (I2C).
- **Light Sensor:** TSL2561 (I2C).
- **Anemometer:** Passive 3-cup reed switch type.
- **Rain Sensor:** 12V contact sensor (requires a 12V relay for isolation).

## Wiring Diagram

| Component | Pin | ESP32 GPIO |
| :--- | :--- | :--- |
| **I2C Bus (All sensors)** | SDA | GPIO 21 |
| **I2C Bus (All sensors)** | SCL | GPIO 22 |
| **Anemometer** | Signal | GPIO 25 (Internal Pullup) |
| **Rain Sensor Relay** | NO/COM | GPIO 26 (Internal Pullup) |

*Note: All I2C sensors should be powered by the 3.3V rail. The Relay should be used to isolate the 12V rain sensor signal from the ESP32 pins.*

##  Installation

1. Install [ESPHome](https://esphome.io/).
2. Clone this repository.
3. (Optional) Create a `secrets.yaml` for your WiFi credentials:
   ```yaml
   wifi_ssid: "Your_SSID"
   wifi_password: "Your_Password"
   ```
4. Flash the device:
   ```bash
   esphome run sensortest.yaml
   ```

## API Usage

### JSON Endpoint
Get all current readings immediately:
`GET /json`
```json
{
  "sensors": {
    "temp": 22.3,
    "lux": 6.0,
    "wind": 4.5,
    "rain": false,
    "thermal_center": 24.1
  },
  "pixels": [24.0, 24.5, ...]
}
```

### Direct BMP Feed
Retrieve a raw 32x24 bitmap for image processing:
`GET /thermal.bmp`

## ⚖️ License
MIT
