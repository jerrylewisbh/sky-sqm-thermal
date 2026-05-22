#pragma once
#include "esphome.h"
#include <Adafruit_MLX90640.h>
#include <Adafruit_BME280.h>
#include <Adafruit_TSL2561_U.h>
#include <Adafruit_TSL2591.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <algorithm>
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

  static constexpr int CROP_X = 4;
  static constexpr int CROP_Y = 4;
  static constexpr int OUT_WIDTH = 32 - (2 * CROP_X);
  static constexpr int OUT_HEIGHT = 24 - (2 * CROP_Y);
  static constexpr int OUT_PIXELS = OUT_WIDTH * OUT_HEIGHT;

  float frame[OUT_PIXELS];
  sensor::Sensor *mean_sensor_{nullptr};
  sensor::Sensor *min_sensor_{nullptr};
  sensor::Sensor *max_sensor_{nullptr};
  sensor::Sensor *center_sensor_{nullptr};
  sensor::Sensor *bme_temp_sensor_{nullptr};
  sensor::Sensor *bme_humidity_sensor_{nullptr};
  sensor::Sensor *bme_pressure_sensor_{nullptr};
  sensor::Sensor *tsl_illuminance_sensor_{nullptr};
  sensor::Sensor *sky_brightness_mpsas_sensor_{nullptr};
  text_sensor::TextSensor *image_sensor_{nullptr};
  text_sensor::TextSensor *sky_condition_sensor_{nullptr};
  sensor::Sensor *wind_sensor_{nullptr};

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
  void set_sky_brightness_mpsas_sensor(sensor::Sensor *s) { sky_brightness_mpsas_sensor_ = s; }
  void set_sqm_calibration_k(float k) { sqm_calibration_k_ = k; }
  void set_bme_pressure_offset(float offset) { bme_pressure_offset_ = offset; }
  void set_bme_humidity_offset(float offset) { bme_humidity_offset_ = offset; }
  void set_image_sensor(text_sensor::TextSensor *s) { image_sensor_ = s; }
  void set_sky_condition_sensor(text_sensor::TextSensor *s) { sky_condition_sensor_ = s; }
  void set_wind_sensor(sensor::Sensor *s) { wind_sensor_ = s; }

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
    float raw_frame[768];
    int mlx_status = mlx_found_ ? mlx.getFrame(raw_frame) : -99;
    if (mlx_found_ && mlx_status != 0) {
      ESP_LOGW(TAG, "MLX90640 getFrame() returned %d (bus glitch?)", mlx_status);
    }
    if (mlx_found_ && mlx_status == 0) {
      for (int y = 0; y < OUT_HEIGHT; y++) {
        for (int x = 0; x < OUT_WIDTH; x++) {
          frame[y * OUT_WIDTH + x] = raw_frame[(y + CROP_Y) * 32 + (x + CROP_X)];
        }
      }
      std::string image_data = "";
      image_data.reserve(OUT_PIXELS * 5);
      sum = 0; min_t = 100; max_t = -100;
      for (int i = 0; i < OUT_PIXELS; i++) {
        sum += frame[i]; 
        if (frame[i] < min_t) min_t = frame[i];
        if (frame[i] > max_t) max_t = frame[i];
        char buf[10]; sprintf(buf, "%.1f", frame[i]);
        image_data += buf; if (i < OUT_PIXELS - 1) image_data += ",";
      }
      center_t = frame[(OUT_HEIGHT / 2) * OUT_WIDTH + (OUT_WIDTH / 2)];
      this->last_frame_json = "[" + image_data + "]"; 
      if (mean_sensor_) mean_sensor_->publish_state(sum / (float)OUT_PIXELS);
      if (min_sensor_) min_sensor_->publish_state(min_t);
      if (max_sensor_) max_sensor_->publish_state(max_t);
      if (center_sensor_) center_sensor_->publish_state(center_t);
    }
    
    float b_temp = NAN, b_hum = NAN, b_pres = NAN;
    if (bme_found_) {
      b_temp = bme.readTemperature(); 
      b_hum = bme.readHumidity(); 
      b_pres = bme.readPressure() / 100.0F;
      
      if (!std::isnan(b_pres)) b_pres += bme_pressure_offset_;
      
      if (!std::isnan(b_hum)) {
          b_hum += bme_humidity_offset_;
          if (b_hum > 100.0f) b_hum = 100.0f;
          if (b_hum < 0.0f) b_hum = 0.0f;
      }

      if (bme_temp_sensor_) bme_temp_sensor_->publish_state(b_temp);
      if (bme_humidity_sensor_) bme_humidity_sensor_->publish_state(b_hum);
      if (bme_pressure_sensor_) bme_pressure_sensor_->publish_state(b_pres);
    }
    last_ambient_ = b_temp;

    last_sky_delta_median_ = NAN;
    last_cloud_fraction_ = NAN;
    last_abs_cloud_fraction_ = NAN;
    if (mlx_found_ && mlx_status == 0 && !std::isnan(sum)) {
      // Absolute-temperature cloud fraction (ambient-independent).
      int abs_cloud_pixels = 0;
      for (int i = 0; i < OUT_PIXELS; i++) {
        if (frame[i] > CLOUD_PIXEL_ABS_CUTOFF) abs_cloud_pixels++;
      }
      last_abs_cloud_fraction_ = abs_cloud_pixels / (float)OUT_PIXELS;

      if (!std::isnan(b_temp)) {
        last_sky_delta_median_ = (sum / (float)OUT_PIXELS) - b_temp;
        // Delta-based cloud fraction (ambient-relative).
        int cloud_pixels = 0;
        for (int i = 0; i < OUT_PIXELS; i++) {
          if ((frame[i] - b_temp) > CLOUD_PIXEL_DELTA_CUTOFF) cloud_pixels++;
        }
        last_cloud_fraction_ = cloud_pixels / (float)OUT_PIXELS;
      }
    }
    {
      const char *delta_lbl    = classify_delta(last_sky_delta_median_);
      const char *frac_lbl     = classify_fraction(last_cloud_fraction_);
      const char *abs_frac_lbl = classify_fraction(last_abs_cloud_fraction_);
      // Pessimistic of all three: the most cloudy verdict wins.
      const char *combined = pessimistic_label(delta_lbl, frac_lbl);
      combined = pessimistic_label(combined, abs_frac_lbl);
      last_sky_condition_ = combined;
      ESP_LOGD(TAG, "sky: Δ=%.1f→%s frac=%.2f→%s abs_frac=%.2f→%s -> %s",
               last_sky_delta_median_, delta_lbl,
               last_cloud_fraction_, frac_lbl,
               last_abs_cloud_fraction_, abs_frac_lbl,
               last_sky_condition_.c_str());
    }
    if (sky_condition_sensor_) sky_condition_sensor_->publish_state(last_sky_condition_);
    
    float lux = NAN;
    if (tsl2591_found_) {
      uint32_t lum = tsl2591.getFullLuminosity();
      uint16_t full = lum & 0xFFFF;
      uint16_t ir = lum >> 16;
      tsl2591Gain_t cur_gain = tsl2591.getGain();
      ESP_LOGD(TAG, "TSL2591 raw: full=%u ir=%u ratio=%.2f gain=%d",
               full, ir, full > 0 ? (float)ir / (float)full : 0.0f, (int)cur_gain);

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
        // Formula-collapse: when full ≈ ir, the visible-only signal (full-ir) is ~0 and
        // the formula returns ~0 even in bright light. Two regimes:
        //   - true near-saturation (full > 50000): cap at 88000 lx
        //   - mid-range with anomalous ir/full ratio: estimate from ch0 alone, log a warning
        else if (lux < 1.0f && full > 50000) {
          ESP_LOGW(TAG, "TSL2591 saturation collapse (full=%u ir=%u) -> reporting bright", full, ir);
          lux = 88000.0f;
        }
        else if (lux < 1.0f && full > 40000) {
          // Linear ch0 estimate at current gain. Underestimates if light is mostly visible
          // (since we'd normally expect ch1 < ch0); but for IR-heavy collapse cases this
          // is much closer to truth than pinning to 88000.
          float scale = 88000.0f / 65535.0f;  // LOW gain, 100 ms
          if (cur_gain == TSL2591_GAIN_MED)  scale /= 25.0f;
          else if (cur_gain == TSL2591_GAIN_HIGH) scale /= 428.0f;
          else if (cur_gain == TSL2591_GAIN_MAX)  scale /= 9876.0f;
          float est = full * scale;
          ESP_LOGW(TAG, "TSL2591 formula-collapse (full=%u ir=%u, ir/full=%.2f) -> ch0-only estimate %.0f lx",
                   full, ir, (float)ir / (float)full, est);
          lux = est;
        }
      }
    } else if (tsl2561_found_) {
      sensors_event_t event; tsl2561.getEvent(&event); lux = event.light;
    }
    if (!std::isnan(lux) && tsl_illuminance_sensor_) tsl_illuminance_sensor_->publish_state(lux);

    last_sky_mpsas_ = lux_to_mpsas(lux, sqm_calibration_k_);
    if (sky_brightness_mpsas_sensor_) sky_brightness_mpsas_sensor_->publish_state(last_sky_mpsas_);
    
    float wind = wind_sensor_ && wind_sensor_->has_state() ? wind_sensor_->state : NAN;
    if (!std::isnan(wind) && wind > 0) wind_active_ = true;

    std::stringstream env;
    env << std::fixed << std::setprecision(1);
    if (!std::isnan(b_temp)) env << "Temp: " << b_temp << "&deg;C | ";
    if (!std::isnan(b_hum)) env << "Hum: " << b_hum << "% | ";
    if (!std::isnan(b_pres)) env << "Pres: " << b_pres << " hPa | ";
    if (!std::isnan(lux)) {
      if (lux >= 10) env << "Lux: " << (int)lux << " | ";
      else if (lux > 0.0001f) env << "Lux: " << std::setprecision(3) << lux << std::setprecision(1) << " | ";
      else env << "Lux: 0.000 | ";
    }
    if (wind_active_ && !std::isnan(wind)) env << "Wind: " << wind << " km/h | ";
    env << "Sky: " << last_sky_condition_;
    {
      // Show the higher of the two fractions — that's the one that drove the verdict.
      float shown_frac = NAN;
      if (!std::isnan(last_cloud_fraction_) && !std::isnan(last_abs_cloud_fraction_))
        shown_frac = std::max(last_cloud_fraction_, last_abs_cloud_fraction_);
      else if (!std::isnan(last_abs_cloud_fraction_)) shown_frac = last_abs_cloud_fraction_;
      else if (!std::isnan(last_cloud_fraction_))     shown_frac = last_cloud_fraction_;

      bool any = !std::isnan(last_sky_delta_median_) || !std::isnan(shown_frac);
      if (any) env << " (";
      if (!std::isnan(last_sky_delta_median_)) {
        env << "&Delta;" << last_sky_delta_median_ << "&deg;C";
        if (!std::isnan(shown_frac)) env << ", ";
      }
      if (!std::isnan(shown_frac)) env << (int)(shown_frac * 100) << "% cloud";
      if (any) env << ")";
    }
    this->last_env_str = env.str();
    
    std::stringstream json;
    json << std::fixed << std::setprecision(1);
    json << "{\"temp\":"; if (std::isnan(b_temp)) json << "null"; else json << b_temp;
    json << ",\"hum\":"; if (std::isnan(b_hum)) json << "null"; else json << b_hum;
    json << ",\"pres\":"; if (std::isnan(b_pres)) json << "null"; else json << b_pres;
    json << ",\"lux\":"; if (std::isnan(lux)) json << "null"; else json << std::setprecision(4) << lux << std::setprecision(1);
    json << ",\"wind\":"; if (!wind_active_ || std::isnan(wind)) json << "null"; else json << wind;
    json << ",\"thermal_min\":"; if (std::isnan(min_t)) json << "null"; else json << min_t;
    json << ",\"thermal_max\":"; if (std::isnan(max_t)) json << "null"; else json << max_t;
    json << ",\"thermal_avg\":"; if (std::isnan(sum)) json << "null"; else json << sum/(float)OUT_PIXELS;
    json << ",\"thermal_center\":"; if (std::isnan(center_t)) json << "null"; else json << center_t;
    json << ",\"sky_delta_median\":"; if (std::isnan(last_sky_delta_median_)) json << "null"; else json << last_sky_delta_median_;
    json << ",\"sky_cloud_fraction\":"; if (std::isnan(last_cloud_fraction_)) json << "null"; else json << std::setprecision(3) << last_cloud_fraction_ << std::setprecision(1);
    json << ",\"sky_abs_cloud_fraction\":"; if (std::isnan(last_abs_cloud_fraction_)) json << "null"; else json << std::setprecision(3) << last_abs_cloud_fraction_ << std::setprecision(1);
    json << ",\"sky_condition\":\"" << last_sky_condition_ << "\"";
    json << ",\"sky_brightness_mpsas\":"; if (std::isnan(last_sky_mpsas_)) json << "null"; else json << std::setprecision(2) << last_sky_mpsas_ << std::setprecision(1);
    json << "}";
    
    this->last_data_json = json.str();
  }

  std::string last_env_str = "Initializing...";
  std::string last_data_json = "{}";
  std::string last_frame_json = "[]";
  float last_ambient_ = NAN;
  float last_sky_delta_median_ = NAN;
  float last_cloud_fraction_ = NAN;
  float last_abs_cloud_fraction_ = NAN;
  std::string last_sky_condition_ = "unknown";
  float last_sky_mpsas_ = NAN;
  float sqm_calibration_k_ = 19.5f;   // YAML-tunable; calibrate against reference SQM or B-filter telescope photometry
  float bme_pressure_offset_ = 0.0f;
  float bme_humidity_offset_ = 0.0f;

  // Convert lux to magnitudes per square arcsecond (SQM scale).
  // mpsas = K - 2.5 * log10(lux). Only meaningful at night (lux below ~10).
  static float lux_to_mpsas(float lux, float k) {
    if (std::isnan(lux) || lux < 0) return NAN;
    if (lux > 10.0f) return NAN;
    
    // Floor at 1e-6 lx to avoid log10(0). 
    // mpsas will be capped at a practical dark limit.
    float effective_lux = (lux < 1.0e-6f) ? 1.0e-6f : lux;
    float mpsas = k - 2.5f * log10f(effective_lux);
    
    if (mpsas > 23.0f) mpsas = 23.0f; // Practical limit for a perfectly dark sky
    return mpsas;
  }

  // Per-pixel "is this cloud?" cutoff, in degrees C below ambient.
  // Pixels with (sky_temp - ambient) > -10 are counted as cloud.
  static constexpr float CLOUD_PIXEL_DELTA_CUTOFF = -20.0f;

  // Absolute-temperature cutoff (independent of ambient): water cloud in the
  // troposphere is essentially never colder than -15°C; clear sky at zenith
  // is essentially never warmer than -15°C. Pixels above this are cloud
  // regardless of what the BME280 reports for ambient.
  static constexpr float CLOUD_PIXEL_ABS_CUTOFF = -18.0f;

  // Cloudiness rank (0 = clearest, 5 = most cloudy). Used to pick the
  // more pessimistic of the delta- and fraction-based classifications.
  static int cloudiness_rank(const char *label) {
    if (!strcmp(label, "very_clear"))    return 0;
    if (!strcmp(label, "clear"))         return 1;
    if (!strcmp(label, "mostly_clear"))  return 2;
    if (!strcmp(label, "partly_cloudy")) return 3;
    if (!strcmp(label, "mostly_cloudy")) return 4;
    if (!strcmp(label, "overcast"))      return 5;
    return -1;  // unknown
  }

  // Mean Δ classifier (Boltwood-style). Good for uniform sky.
  static const char *classify_delta(float delta) {
    if (std::isnan(delta)) return "unknown";
    if (delta < -35) return "very_clear";
    if (delta < -25) return "clear";
    if (delta < -18) return "mostly_clear";
    if (delta < -12) return "partly_cloudy";
    if (delta <  -6) return "mostly_cloudy";
    return "overcast";
  }

  // Cloud-fraction classifier (METAR-style). Catches cellular/broken cloud
  // patterns that wash out under a mean.
  static const char *classify_fraction(float frac) {
    if (std::isnan(frac)) return "unknown";
    if (frac < 0.05f) return "very_clear";
    if (frac < 0.25f) return "clear";
    if (frac < 0.50f) return "mostly_clear";
    if (frac < 0.75f) return "partly_cloudy";
    if (frac < 0.95f) return "mostly_cloudy";
    return "overcast";
  }

  // Take the more pessimistic (more cloudy) of two labels.
  static const char *pessimistic_label(const char *a, const char *b) {
    int ra = cloudiness_rank(a), rb = cloudiness_rank(b);
    if (ra < 0) return b;
    if (rb < 0) return a;
    return (ra >= rb) ? a : b;
  }

  // Per-pixel BMP color. Uses the same cloud test as the classifier:
  // pixel is cloud if it's above EITHER the absolute cutoff OR (ambient + delta cutoff).
  // The "above cloud cutoff" boundary is where cool colors flip to warm, so the
  // visual matches the verdict in both daytime and cold-night regimes.
  static void abs_temp_color_bgr(float t, float ambient, uint8_t &b, uint8_t &g, uint8_t &r) {
    if (std::isnan(t)) { b=128; g=128; r=128; return; }   // gray

    // Effective cloud-pixel cutoff = min(abs cutoff, ambient + delta cutoff):
    // a pixel is "cloud" if it's above EITHER cutoff (less restrictive wins).
    float cutoff = CLOUD_PIXEL_ABS_CUTOFF;
    if (!std::isnan(ambient)) {
      float delta_cutoff = ambient + CLOUD_PIXEL_DELTA_CUTOFF;
      if (delta_cutoff < cutoff) cutoff = delta_cutoff;
    }
    float c = t - cutoff;   // c > 0 means the pixel is "cloud", c < 0 means "clear/haze"

    // ----- pixel is "clear" (below cutoff): pure blues/cyans -----
    if (c < -25) { b=140; g= 40; r=  0; return; }   // dark navy  : extremely clear
    if (c < -15) { b=200; g= 80; r= 20; return; }   // deep blue  : very clear
    if (c <  -7) { b=240; g=140; r= 40; return; }   // mid blue   : clear
    if (c <  -2) { b=255; g=200; r= 80; return; }   // light blue : light haze
    if (c <   0) { b=255; g=240; r=140; return; }   // cyan       : heavy haze (just below cutoff)
    // ----- pixel is "cloud" (above cutoff): yellows, oranges, reds -----
    if (c <   4) { b= 80; g=220; r=240; return; }   // yellow     : light/thin cloud
    if (c <  10) { b= 40; g=140; r=240; return; }   // orange     : solid cloud
    if (c <  20) { b= 30; g= 50; r=220; return; }   // red        : thick cloud
                   b=255; g=180; r=255;             // pink/white : extremely thick/warm (e.g. summer storms)
  }

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
    int out_w = SkyThermal::OUT_WIDTH;
    int out_h = SkyThermal::OUT_HEIGHT;
    uint32_t fs = 54 + (out_w * out_h * 3);
    uint8_t *bmp = (uint8_t *)malloc(fs);
    memset(bmp, 0, fs);
    bmp[0]='B'; bmp[1]='M'; *(uint32_t*)&bmp[2]=fs; *(uint32_t*)&bmp[10]=54;
    *(uint32_t*)&bmp[14]=40; *(int32_t*)&bmp[18]=out_w; *(int32_t*)&bmp[22]=out_h;
    bmp[26]=1; bmp[28]=24; *(uint32_t*)&bmp[34]=out_w*out_h*3;
    float amb = this->parent->last_ambient_;
    for(int y=0; y<out_h; y++) {
      for(int x=0; x<out_w; x++) {
        float v = this->parent->frame[(out_h-1-y)*out_w + x];
        uint8_t bb, gg, rr;
        SkyThermal::abs_temp_color_bgr(v, amb, bb, gg, rr);
        int pos = 54+(y*out_w+x)*3;
        bmp[pos] = bb; bmp[pos+1] = gg; bmp[pos+2] = rr;
      }
    }
    AsyncWebServerResponse *res = request->beginResponse(200, "image/bmp", bmp, fs);
    res->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    request->send(res); free(bmp); return;
  }
  std::string html = "<html><body style='background:#222;color:#fff;text-align:center;font-family:sans-serif;'>"
      "<h1>Weather Station</h1><div style='font-size:1.5em;margin:20px;'>" + this->parent->last_env_str + "</div>";
  if (this->parent->mlx_found_) {
    html += "<img src='/thermal.bmp?t=" + std::to_string(millis()) + "' style='width:640px;height:480px;image-rendering:pixelated;border:2px solid #555;'><br>";
  }
  else html += "<p style='color:#f44'>Thermal Camera NOT FOUND</p>";
  html += "<script>setTimeout(()=>location.reload(), 2000);</script></body></html>";
  request->send(200, "text/html", html.c_str());
}

} //namespace sky_thermal
} //namespace esphome
