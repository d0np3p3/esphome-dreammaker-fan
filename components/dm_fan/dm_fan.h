#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/fan/fan.h"
#include "esphome/components/sensor/sensor.h"
#include <algorithm>

namespace esphome {
namespace dm_fan {

static const char *const TAG = "dm_fan";

static const uint8_t MAGIC_0    = 0xFA;
static const uint8_t MAGIC_1    = 0xCE;
static const uint8_t CMD_STATE  = 0x84;
static const uint8_t CMD_QUERY  = 0x02;

static const uint8_t WIFI_RESPONSE[14] = {
  0xFA, 0xCE, 0x00, 0x09, 0x81, 0x00, 0x70,
  0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC4
};

struct FanState {
  uint8_t power       = 0;
  uint8_t speed       = 35;
  uint8_t mode        = 0;
  uint8_t oscillation = 0;
  uint8_t timer       = 0;
  uint8_t child_lock  = 0;
  uint8_t lights      = 1;
  uint8_t sounds      = 1;
  float   temp        = 0;
  float   hum         = 0;
};

class DmFan : public fan::Fan, public Component, public uart::UARTDevice {
 public:
  void setup() override {
    ESP_LOGI(TAG, "DM Fan (Ultra Stable Final) ready!");
  }

  fan::FanTraits get_traits() override {
    auto traits = fan::FanTraits();
    traits.set_oscillation(true);
    traits.set_supported_speed_count(100);
    traits.set_supported_preset_modes({"direct", "natural", "smart"});
    return traits;
  }

  void loop() override {
    while (available()) {
      uint8_t b;
      read_byte(&b);
      rx_.push_back(b);
      if (rx_.size() > 256) {
        ESP_LOGW(TAG, "RX buffer overflow, clearing!");
        rx_.clear();
      }
    }
    parse_();
  }

  void control(const fan::FanCall &call) override {
    bool changed = false;

    if (call.get_state().has_value()) {
      state_.power = *call.get_state() ? 1 : 0;
      changed = true;
    }
    if (call.get_speed().has_value()) {
      state_.speed = (uint8_t) std::max(1, std::min(100, *call.get_speed()));
      changed = true;
    }
    if (call.get_oscillating().has_value()) {
      state_.oscillation = *call.get_oscillating() ? 1 : 0;
      changed = true;
    }
    if (call.get_preset_mode().has_value()) {
      auto mode = *call.get_preset_mode();
      if (mode == "natural") state_.mode = 1;
      else if (mode == "smart") state_.mode = 2;
      else state_.mode = 0;
      changed = true;
    }

    if (changed) send_state_();
  }

  // --- EXTERNE FEATURES (Von YAML aufgerufen) ---
  void set_child_lock(bool v) { state_.child_lock = v ? 1 : 0; send_state_(); }
  void set_lights(bool v)     { state_.lights = v ? 1 : 0; send_state_(); }
  void set_sounds(bool v)     { state_.sounds = v ? 1 : 0; send_state_(); }
  void set_timer(float v) {
    static const uint8_t map[] = {0x00, 0x3C, 0x78, 0xB4, 0xF0};
    int h = (int) v;
    if (h >= 0 && h <= 4) {
      state_.timer = map[h];
      send_state_();
    }
  }

  // --- GETTER FÜR DAS YAML-FEEDBACK (Hardware-Tasten Sync) ---
  bool get_child_lock() { return state_.child_lock == 1; }
  bool get_lights()     { return state_.lights == 1; }
  bool get_sounds()     { return state_.sounds == 1; }

  void set_temperature_sensor(sensor::Sensor *s) { temp_ = s; }
  void set_humidity_sensor(sensor::Sensor *s)    { hum_ = s; }

 protected:
  FanState state_;
  std::vector<uint8_t> rx_;

  sensor::Sensor *temp_{nullptr};
  sensor::Sensor *hum_{nullptr};

  static const char *mode_name_(uint8_t m) {
    if (m == 1) return "natural";
    if (m == 2) return "smart";
    return "direct";
  }

  void parse_() {
    while (rx_.size() >= 2) {
      if (rx_[0] != MAGIC_0 || rx_[1] != MAGIC_1) {
        rx_.erase(rx_.begin());
        continue;
      }
      if (rx_.size() < 4) break;

      uint16_t plen = (rx_[2] << 8) | rx_[3];
      uint16_t total = 4 + plen + 1;
      if (rx_.size() < total) break;

      uint8_t chk = 0;
      for (uint16_t i = 0; i < total - 1; i++) chk += rx_[i];
      if (chk != rx_[total - 1]) {
        ESP_LOGW(TAG, "CRC Error!");
        rx_.erase(rx_.begin());
        continue;
      }

      uint8_t cmd = rx_[4];
      if (cmd == CMD_QUERY) {
        write_array(WIFI_RESPONSE, sizeof(WIFI_RESPONSE));
      } else if (cmd == CMD_STATE && total >= 42) {
        on_state_frame_();
      }

      rx_.erase(rx_.begin(), rx_.begin() + total);
    }
  }

  void on_state_frame_() {
    bool fan_changed = (state_.power != rx_[22]) || (state_.speed != rx_[23]) || 
                       (state_.mode != rx_[24]) || (state_.oscillation != rx_[25]);
    
    state_.power       = rx_[22];
    state_.speed       = rx_[23];
    state_.mode        = rx_[24];
    state_.oscillation = rx_[25];
    state_.timer       = rx_[28];
    state_.child_lock  = rx_[31];
    state_.lights      = rx_[32];
    state_.sounds      = rx_[33];

    if (fan_changed) {
      this->state = (state_.power == 1);
      this->speed = state_.speed;
      this->oscillating = (state_.oscillation == 1);
      this->preset_mode = mode_name_(state_.mode);
      this->publish_state();
    }

    float temp_val = (float) rx_[12];
    float hum_val  = (float) rx_[15];
    if (temp_ && temp_val >= 10.0f && temp_val <= 50.0f && temp_val != state_.temp) {
      state_.temp = temp_val;
      temp_->publish_state(temp_val);
    }
    if (hum_ && hum_val >= 10.0f && hum_val <= 100.0f && hum_val != state_.hum) {
      state_.hum = hum_val;
      hum_->publish_state(hum_val);
    }
  }

  uint8_t checksum_(const uint8_t *buf, size_t len) {
    uint8_t s = 0;
    for (size_t i = 0; i < len; i++) s += buf[i];
    return s;
  }

  void send_state_() {
    uint8_t f[42] = {
      0xFA, 0xCE, 0x00, 0x25, 0x84, 0x23, 0x47, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x1B, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, state_.power, state_.speed, state_.mode, state_.oscillation,
      0x5A, 0x00, state_.timer, 0x01, 0x01, state_.child_lock, state_.lights,
      state_.sounds, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    if (state_.mode == 1) {
      f[15] = 0x00;
      f[23] = 0x01;
    }

    f[41] = checksum_(f, 41);
    write_array(f, 42);
  }
};

}  // namespace dm_fan
}  // namespace esphome
