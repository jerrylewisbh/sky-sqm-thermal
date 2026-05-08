#pragma once
#include "esphome.h"
#include <Adafruit_MLX90640.h>
#include <Adafruit_BME280.h>
#include <Adafruit_TSL2561_U.h>
#include <Adafruit_TSL2591.h>
#include <string>
#include <sstream>
#include <iomanip>
#include "esphome/components/web_server_base/web_server_base.h"

namespace esphome {
namespace sky_thermal {

static const char *const TAG = "sky_thermal";

class SkyThermal;

class ThermalHandler : public AsyncWebHandler {
 public:
  SkyThermal *parent;
  ThermalHandler(SkyThermal *parent) : parent(parent) {}
  bool canHandle(AsyncWebServerRequest *request) const override {
    char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
    std::string url = request->url_to(buffer).str();
    return (url == "/thermal" || url == "/json" || url == "/thermal.bmp");
  }
  void handleRequest(AsyncWebServerRequest *request) override;
};

class SkyThermal : public PollingComponent {
 public:
  Adafruit_MLX90640 mlx;
  Adafruit_BME280 bme;
  Adafruit_TSL2561_Unified tsl2561 = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);
  Adafruit_TSL2591 tsl2591 = Adafruit_TSL2591(2591);

  float frame[768];
  sensor::Sensor *mean_sensor_{nullptr};
  sensor::Sensor *min_sensor_{nullptr};
  sensor::Sensor *max_sensor_{nullptr};
  sensor::Sensor *center_sensor_{nullptr};
  sensor::Sensor *bme_temp_sensor_{nullptr};
  sensor::Sensor *bme_humidity_sensor_{nullptr};
  sensor::Sensor *bme_pressure_sensor_{nullptr};
  sensor::Sensor *tsl_illuminance_sensor_{nullptr};
  text_sensor::TextSensor *image_sensor_{nullptr};
  sensor::Sensor *wind_sensor_{nullptr};
  binary_sensor::BinarySensor *rain_sensor_{nullptr};

  ThermalHandler *handler_{nullptr};
  bool registered_{false};
  bool mlx_found_{false}, bme_found_{false}, tsl2561_found_{false}, tsl2591_found_{false}, wind_active_{false};
  uint32_t last_retry_ms_{0};

  void set_mean_sensor(sensor::Sensor *s) { mean_sensor_ = s; }
  void set_min_sensor(sensor::Sensor *s) { min_sensor_ = s; }
  void set_max_sensor(sensor::Sensor *s) { max_sensor_ = s; }
  void set_center_sensor(sensor::Sensor *s) { center_sensor_ = s; }
  void set_bme_temp_sensor(sensor::Sensor *s) { bme_temp_sensor_ = s; }
  void set_bme_humidity_sensor(sensor::Sensor *s) { bme_humidity_sensor_ = s; }
  void set_bme_pressure_sensor(sensor::Sensor *s) { bme_pressure_sensor_ = s; }
  void set_tsl_illuminance_sensor(sensor::Sensor *s) { tsl_illuminance_sensor_ = s; }
  void set_image_sensor(text_sensor::TextSensor *s) { image_sensor_ = s; }
  void set_wind_sensor(sensor::Sensor *s) { wind_sensor_ = s; }
  void set_rain_sensor(binary_sensor::BinarySensor *s) { rain_sensor_ = s; }

  void scan_i2c_bus_() {
    ESP_LOGI(TAG, "--- I2C scan on SDA=21 SCL=22 ---");
    int found = 0;
    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
      Wire.beginTransmission(addr);
      uint8_t err = Wire.endTransmission();
      if (err == 0) {
        const char *name = "?";
        if (addr == 0x33) name = "MLX90640";
        else if (addr == 0x76 || addr == 0x77) name = "BME280";
        else if (addr == 0x29) name = "TSL2591";
        else if (addr == 0x39 || addr == 0x49) name = "TSL2561";
        ESP_LOGI(TAG, "  ACK 0x%02X (%s)", addr, name);
        found++;
      } else if (err == 4) {
        ESP_LOGW(TAG, "  bus error at 0x%02X (err=4) — possible lockup", addr);
      }
    }
    ESP_LOGI(TAG, "--- scan done, %d device(s) ---", found);
    if (found == 0) {
      ESP_LOGE(TAG, "NO I2C devices ACKed. Check: pull-ups on SDA/SCL (4.7k to 3.3V), wiring, power (brownout?), bus lockup (power-cycle).");
    }
  }

  void try_init_sensors_() {
    if (!mlx_found_) {
      if (mlx.begin(0x33, &Wire)) {
        ESP_LOGI(TAG, "✓ MLX90640 Found at 0x33");
        mlx.setMode(MLX90640_CHESS); mlx.setRefreshRate(MLX90640_2_HZ);
        mlx_found_ = true;
      } else { ESP_LOGW(TAG, "✗ MLX90640 init failed at 0x33 (no ACK or bad ID)"); }
    }
    if (!bme_found_) {
      if (bme.begin(0x76, &Wire)) { ESP_LOGI(TAG, "✓ BME280 Found at 0x76"); bme_found_ = true; }
      else if (bme.begin(0x77, &Wire)) { ESP_LOGI(TAG, "✓ BME280 Found at 0x77"); bme_found_ = true; }
      else { ESP_LOGW(TAG, "✗ BME280 init failed at 0x76/0x77"); }
    }
    if (!tsl2591_found_ && !tsl2561_found_) {
      if (tsl2591.begin(&Wire)) {
        ESP_LOGI(TAG, "✓ TSL2591 Found at 0x29");
        tsl2591.setGain(TSL2591_GAIN_LOW); tsl2591.setTiming(TSL2591_INTEGRATIONTIME_100MS);
        tsl2591_found_ = true;
      } else if (tsl2561.begin(&Wire)) {
        ESP_LOGI(TAG, "✓ TSL2561 Found");
        tsl2561.enableAutoRange(true); tsl2561_found_ = true;
      } else { ESP_LOGW(TAG, "✗ Light sensor init failed (TSL2591 0x29 / TSL2561 0x39|0x49)"); }
    }
  }

  void setup() override {
    ESP_LOGI(TAG, "I2C Setup starting on pins 21/22 @ 100kHz...");
    Wire.begin(21, 22, 100000);
    delay(200);
    scan_i2c_bus_();
    try_init_sensors_();
  }

  void loop() override {
    if (!this->registered_ && web_server_base::global_web_server_base != nullptr && 
        web_server_base::global_web_server_base->get_server() != nullptr) {
      this->handler_ = new ThermalHandler(this);
      web_server_base::global_web_server_base->get_server()->addHandler(this->handler_);
      this->registered_ = true;
    }
  }

  void update() override {
    if ((!mlx_found_ || !bme_found_ || (!tsl2591_found_ && !tsl2561_found_)) &&
        millis() - last_retry_ms_ > 30000) {
      last_retry_ms_ = millis();
      ESP_LOGW(TAG, "Retrying missing I2C sensors...");
      scan_i2c_bus_();
      try_init_sensors_();
    }

    float min_t = NAN, max_t = NAN, sum = NAN, center_t = NAN;
    int mlx_status = mlx_found_ ? mlx.getFrame(frame) : -99;
    if (mlx_found_ && mlx_status != 0) {
      ESP_LOGW(TAG, "MLX90640 getFrame() returned %d (bus glitch?)", mlx_status);
    }
    if (mlx_found_ && mlx_status == 0) {
      std::string image_data = "";
      image_data.reserve(768 * 5);
      sum = 0; min_t = 100; max_t = -100;
      for (int i = 0; i < 768; i++) {
        sum += frame[i]; 
        if (frame[i] < min_t) min_t = frame[i];
        if (frame[i] > max_t) max_t = frame[i];
        char buf[10]; sprintf(buf, "%.1f", frame[i]);
        image_data += buf; if (i < 767) image_data += ",";
      }
      center_t = frame[400];
      this->last_frame_json = "[" + image_data + "]"; 
      if (mean_sensor_) mean_sensor_->publish_state(sum / 768.0);
      if (min_sensor_) min_sensor_->publish_state(min_t);
      if (max_sensor_) max_sensor_->publish_state(max_t);
      if (center_sensor_) center_sensor_->publish_state(center_t);
    }
    
    float b_temp = NAN, b_hum = NAN, b_pres = NAN;
    if (bme_found_) {
      b_temp = bme.readTemperature(); b_hum = bme.readHumidity(); b_pres = bme.readPressure() / 100.0F;
      if (bme_temp_sensor_) bme_temp_sensor_->publish_state(b_temp);
      if (bme_humidity_sensor_) bme_humidity_sensor_->publish_state(b_hum);
      if (bme_pressure_sensor_) bme_pressure_sensor_->publish_state(b_pres);
    }
    
    float lux = NAN;
    if (tsl2591_found_) {
      uint32_t lum = tsl2591.getFullLuminosity();
      uint16_t full = lum & 0xFFFF;
      uint16_t ir = lum >> 16;
      tsl2591Gain_t cur_gain = tsl2591.getGain();

      // Treat near-saturation (>50000) as saturation: above ~50k the formula
      // (ch0-ch1)*(1-ch1/ch0)/cpl collapses toward 0 in IR-rich light.
      bool saturated = (full >= 50000 || ir >= 50000);

      if (saturated) {
        if (cur_gain == TSL2591_GAIN_MAX)       tsl2591.setGain(TSL2591_GAIN_HIGH);
        else if (cur_gain == TSL2591_GAIN_HIGH) tsl2591.setGain(TSL2591_GAIN_MED);
        else if (cur_gain == TSL2591_GAIN_MED)  tsl2591.setGain(TSL2591_GAIN_LOW);
        ESP_LOGD(TAG, "TSL2591 near-saturation (full=%u ir=%u), gain stepped down", full, ir);
        // Report a high value so HA knows it's bright, not dark.
        // 88000 lx ~ practical max at LOW gain / 100ms integration.
        lux = 88000.0f;
      } else if (full < 128 && cur_gain != TSL2591_GAIN_MAX) {
        if (cur_gain == TSL2591_GAIN_LOW)       tsl2591.setGain(TSL2591_GAIN_MED);
        else if (cur_gain == TSL2591_GAIN_MED)  tsl2591.setGain(TSL2591_GAIN_HIGH);
        else if (cur_gain == TSL2591_GAIN_HIGH) tsl2591.setGain(TSL2591_GAIN_MAX);
        ESP_LOGD(TAG, "TSL2591 dim (full=%u), gain stepped up", full);
        lux = tsl2591.calculateLux(full, ir);
        if (lux < 0 || std::isnan(lux)) lux = NAN;
      } else {
        lux = tsl2591.calculateLux(full, ir);
        if (lux < 0) { ESP_LOGW(TAG, "TSL2591 calculateLux returned %.1f (saturated)", lux); lux = 88000.0f; }
        else if (std::isnan(lux)) { ESP_LOGW(TAG, "TSL2591 calculateLux returned NaN"); lux = NAN; }
        // Formula-collapse guard: if ch0 was high and result came out tiny, it's IR-rich saturation.
        else if (lux < 1.0f && full > 20000) {
          ESP_LOGW(TAG, "TSL2591 formula-collapse (full=%u ir=%u lux=%.3f) -> reporting bright", full, ir, lux);
          lux = 88000.0f;
        }
      }
    } else if (tsl2561_found_) {
      sensors_event_t event; tsl2561.getEvent(&event); lux = event.light;
    }
    if (!std::isnan(lux) && tsl_illuminance_sensor_) tsl_illuminance_sensor_->publish_state(lux);
    
    float wind = wind_sensor_ && wind_sensor_->has_state() ? wind_sensor_->state : NAN;
    if (!std::isnan(wind) && wind > 0) wind_active_ = true;
    
    std::string rs = rain_sensor_ && rain_sensor_->has_state() ? (rain_sensor_->state ? "WET" : "Dry") : "N/A";

    std::stringstream env;
    env << std::fixed << std::setprecision(1);
    if (!std::isnan(b_temp)) env << "Temp: " << b_temp << "&deg;C | ";
    if (!std::isnan(b_hum)) env << "Hum: " << b_hum << "% | ";
    if (!std::isnan(b_pres)) env << "Pres: " << b_pres << " hPa | ";
    if (!std::isnan(lux)) {
      if (lux >= 10) env << "Lux: " << (int)lux << " | ";
      else env << "Lux: " << std::setprecision(3) << lux << std::setprecision(1) << " | ";
    }
    if (wind_active_ && !std::isnan(wind)) env << "Wind: " << wind << " km/h | ";
    if (rs != "N/A") env << "Rain: " << rs;
    this->last_env_str = env.str();
    
    std::stringstream json;
    json << std::fixed << std::setprecision(1);
    json << "{\"temp\":"; if (std::isnan(b_temp)) json << "null"; else json << b_temp;
    json << ",\"hum\":"; if (std::isnan(b_hum)) json << "null"; else json << b_hum;
    json << ",\"pres\":"; if (std::isnan(b_pres)) json << "null"; else json << b_pres;
    json << ",\"lux\":"; if (std::isnan(lux)) json << "null"; else json << std::setprecision(4) << lux << std::setprecision(1);
    json << ",\"wind\":"; if (!wind_active_ || std::isnan(wind)) json << "null"; else json << wind;
    json << ",\"rain\":"; if (rs == "N/A") json << "null"; else json << (rain_sensor_->state ? "true" : "false");
    json << ",\"thermal_min\":"; if (std::isnan(min_t)) json << "null"; else json << min_t;
    json << ",\"thermal_max\":"; if (std::isnan(max_t)) json << "null"; else json << max_t;
    json << ",\"thermal_avg\":"; if (std::isnan(sum)) json << "null"; else json << sum/768.0;
    json << ",\"thermal_center\":"; if (std::isnan(center_t)) json << "null"; else json << center_t;
    json << "}";
    
    this->last_data_json = json.str();
  }

  std::string last_env_str = "Initializing...";
  std::string last_data_json = "{}";
  std::string last_frame_json = "[]";
  void dump_config() override { LOG_UPDATE_INTERVAL(this); }
};

inline void ThermalHandler::handleRequest(AsyncWebServerRequest *request) {
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  std::string url = request->url_to(buffer).str();
  if (url == "/json") {
    request->send(200, "application/json",
      ("{\"sensors\":" + this->parent->last_data_json +
       ",\"frame\":" + this->parent->last_frame_json + "}").c_str());
    return;
  }
  if (url == "/thermal.bmp") {
    if (!this->parent->mlx_found_) { request->send(404, "text/plain", "No Cam"); return; }
    uint32_t fs = 54 + (32 * 24 * 3);
    uint8_t *bmp = (uint8_t *)malloc(fs);
    memset(bmp, 0, fs);
    bmp[0]='B'; bmp[1]='M'; *(uint32_t*)&bmp[2]=fs; *(uint32_t*)&bmp[10]=54;
    *(uint32_t*)&bmp[14]=40; *(int32_t*)&bmp[18]=32; *(int32_t*)&bmp[22]=24;
    bmp[26]=1; bmp[28]=24; *(uint32_t*)&bmp[34]=32*24*3;
    float mi=100, ma=-100;
    for(int i=0; i<768; i++) { if(this->parent->frame[i]<mi) mi=this->parent->frame[i]; if(this->parent->frame[i]>ma) ma=this->parent->frame[i]; }
    if(ma-mi<1.0) ma=mi+1.0;
    for(int y=0; y<24; y++) {
      for(int x=0; x<32; x++) {
        float v = this->parent->frame[(23-y)*32 + x];
        float nv = (v-mi)/(ma-mi); int r = (int)(nv*255); int pos = 54+(y*32+x)*3;
        bmp[pos] = 255-r; bmp[pos+1] = 0; bmp[pos+2] = r; 
      }
    }
    AsyncWebServerResponse *res = request->beginResponse(200, "image/bmp", bmp, fs);
    request->send(res); free(bmp); return;
  }
  std::string html = "<html><body style='background:#222;color:#fff;text-align:center;font-family:sans-serif;'>"
      "<h1>Weather Station</h1><div style='font-size:1.5em;margin:20px;'>" + this->parent->last_env_str + "</div>";
  if (this->parent->mlx_found_) html += "<img src='/thermal.bmp' style='width:640px;height:480px;image-rendering:pixelated;border:2px solid #555;'><br>";
  else html += "<p style='color:#f44'>Thermal Camera NOT FOUND</p>";
  html += "<script>setTimeout(()=>location.reload(), 2000);</script></body></html>";
  request->send(200, "text/html", html.c_str());
}

} //namespace sky_thermal
} //namespace esphome
