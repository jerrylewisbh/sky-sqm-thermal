#pragma once
#include "esphome.h"
#include <Adafruit_MLX90640.h>
#include <Adafruit_BME280.h>
#include <Adafruit_TSL2561_U.h>
#include <string>
#include <sstream>
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
    std::string url = request->url_to(buffer);
    return (url == "/thermal" || url == "/json" || url == "/thermal.bmp");
  }

  void handleRequest(AsyncWebServerRequest *request) override;
};

class SkyThermal : public PollingComponent {
 public:
  Adafruit_MLX90640 mlx;
  Adafruit_BME280 bme;
  Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);

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

  void setup() override {
    Wire.begin(21, 22, 400000); 
    mlx.begin(0x33, &Wire); mlx.setMode(MLX90640_CHESS); mlx.setRefreshRate(MLX90640_4_HZ);
    bme.begin(0x76, &Wire);
    tsl.begin(&Wire); tsl.enableAutoRange(true);
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
    float min_t = 100, max_t = -100, sum = 0, center_t = 0;
    if (mlx.getFrame(frame) == 0) {
      std::string image_data = "";
      image_data.reserve(768 * 5);
      for (int i = 0; i < 768; i++) {
        sum += frame[i]; 
        if (frame[i] < min_t) min_t = frame[i];
        if (frame[i] > max_t) max_t = frame[i];
        char buf[10]; sprintf(buf, "%.1f", frame[i]);
        image_data += buf; if (i < 767) image_data += ",";
      }
      center_t = frame[400]; // Approx center pixel
      this->last_frame_json = "[" + image_data + "]";
      if (mean_sensor_) mean_sensor_->publish_state(sum / 768.0);
      if (min_sensor_) min_sensor_->publish_state(min_t);
      if (max_sensor_) max_sensor_->publish_state(max_t);
      if (center_sensor_) center_sensor_->publish_state(center_t);
      if (image_sensor_) image_sensor_->publish_state(image_data);
    }
    
    float b_temp = bme.readTemperature();
    float b_hum = bme.readHumidity();
    float b_pres = bme.readPressure() / 100.0F;
    if (bme_temp_sensor_) bme_temp_sensor_->publish_state(b_temp);
    if (bme_humidity_sensor_) bme_humidity_sensor_->publish_state(b_hum);
    if (bme_pressure_sensor_) bme_pressure_sensor_->publish_state(b_pres);
    
    sensors_event_t event; tsl.getEvent(&event);
    if (tsl_illuminance_sensor_) tsl_illuminance_sensor_->publish_state(event.light);
    
    float wind = wind_sensor_ ? wind_sensor_->state : 0;
    bool rain = rain_sensor_ ? rain_sensor_->state : false;
    
    char env_buf[256];
    sprintf(env_buf, "Temp: %.1f&deg;C | Hum: %.1f%% | Pres: %.1f hPa | Lux: %.0f | Wind: %.1f km/h | Rain: %s", 
            b_temp, b_hum, b_pres, event.light, wind, rain ? "WET" : "Dry");
    this->last_env_str = env_buf;
    
    char json_buf[1024];
    sprintf(json_buf, "{\"temp\":%.1f,\"hum\":%.1f,\"pres\":%.1f,\"lux\":%.0f,\"wind\":%.1f,\"rain\":%s,\"thermal_min\":%.1f,\"thermal_avg\":%.1f,\"thermal_max\":%.1f,\"thermal_center\":%.1f}",
            b_temp, b_hum, b_pres, event.light, wind, rain ? "true" : "false", min_t, sum/768.0, max_t, center_t);
    this->last_data_json = json_buf;
  }

  std::string last_frame_json = "[]";
  std::string last_env_str = "Waiting...";
  std::string last_data_json = "{}";

  void dump_config() override { LOG_UPDATE_INTERVAL(this); }
};

inline void ThermalHandler::handleRequest(AsyncWebServerRequest *request) {
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  std::string url = request->url_to(buffer);
  
  if (url == "/json") {
    std::string json = "{\"sensors\":" + this->parent->last_data_json + ",\"pixels\":" + this->parent->last_frame_json + "}";
    request->send(200, "application/json", json.c_str());
    return;
  }

  if (url == "/thermal.bmp") {
    uint32_t file_size = 54 + (32 * 24 * 3);
    uint8_t *bmp = (uint8_t *)malloc(file_size);
    if (!bmp) { request->send(500, "text/plain", "Out of memory"); return; }
    memset(bmp, 0, file_size);
    bmp[0]='B'; bmp[1]='M'; *(uint32_t*)&bmp[2]=file_size; *(uint32_t*)&bmp[10]=54;
    *(uint32_t*)&bmp[14]=40; *(int32_t*)&bmp[18]=32; *(int32_t*)&bmp[22]=24;
    bmp[26]=1; bmp[28]=24; *(uint32_t*)&bmp[34]=32*24*3;

    float minT = 100, maxT = -100;
    for(int i=0; i<768; i++) { if(this->parent->frame[i]<minT) minT=this->parent->frame[i]; if(this->parent->frame[i]>maxT) maxT=this->parent->frame[i]; }
    if(maxT - minT < 1.0) maxT = minT + 1.0;

    for(int y=0; y<24; y++) {
      for(int x=0; x<32; x++) {
        float v = this->parent->frame[(23-y)*32 + x];
        float nv = (v - minT) / (maxT - minT);
        int r = (int)(nv * 255); int b = 255 - r;
        int pos = 54 + (y * 32 + x) * 3;
        bmp[pos] = b; bmp[pos+1] = 0; bmp[pos+2] = r; 
      }
    }
    AsyncWebServerResponse *response = request->beginResponse(200, "image/bmp", bmp, file_size);
    response->addHeader("Content-Disposition", "inline; filename=thermal.bmp");
    request->send(response);
    free(bmp);
    return;
  }

  std::string html = "<html><body style='background:#222;color:#fff;text-align:center;font-family:sans-serif;'>"
      "<h1>Weather Station</h1><div style='font-size:1.5em;margin:20px;'>" + this->parent->last_env_str + "</div>"
      "<img src='/thermal.bmp' style='width:640px;height:480px;image-rendering:pixelated;border:2px solid #555;'><br>"
      "<script>setTimeout(()=>location.reload(), 2000);</script></body></html>";
  request->send(200, "text/html", html.c_str());
}

} //namespace sky_thermal
} //namespace esphome
