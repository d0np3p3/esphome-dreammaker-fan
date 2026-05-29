#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/fan/fan.h"
#include "esphome/components/sensor/sensor.h"
#include <algorithm>
#include <string>

namespace esphome {
namespace dm_fan {

static const char *const TAG = "dm_fan.v2.2";

// ── Protocol constants ────────────────────────────────────────────────────────
// FA CE magic also in BLE provisioning finish: FA CE AA 00
constexpr uint8_t MAGIC_0   = 0xFA;
constexpr uint8_t MAGIC_1   = 0xCE;
constexpr uint8_t CMD_STATE = 0x84;  // MCU→ESP full state push (RX)
constexpr uint8_t CMD_SET   = 0x04;  // ESP→MCU single-property command (TX)
constexpr uint8_t CMD_QUERY = 0x02;  // MCU→ESP WiFi status query

// Resource IDs for CMD_SET — confirmed from 31 TX captures
constexpr uint8_t RES_POWER     = 0x00;
constexpr uint8_t RES_SPEED     = 0x01;
constexpr uint8_t RES_MODE      = 0x02;
constexpr uint8_t RES_OSC_ONOFF = 0x03;
constexpr uint8_t RES_OSC_ANGLE = 0x04;
constexpr uint8_t RES_ROTATE    = 0x05;  // 0x01=left 0x02=right (osc OFF, UNCONFIRMED)
constexpr uint8_t RES_TIMER     = 0x06;  // uint16 BE minutes 0-480 (8h)
constexpr uint8_t RES_SOUND     = 0x07;
constexpr uint8_t RES_LED       = 0x08;
constexpr uint8_t RES_CHILDLOCK = 0x09;

// WiFi response — CONFIRMED from original firmware log:
// MCU sends: action:2,  resource:0x78, length:9
// ESP sends:  action:82, resource:0x78, length:0x3B (59 bytes = 3 header + 56 data)
// Data = full fan state (56 bytes). Until state is known, send zeros.
// Note: our previous assumption (action:0x81, resource:0x70) was WRONG.

// ── RX payload offsets ────────────────────────────────────────────────────────
// parse_buf_[0] = CMD byte (frame byte 4, after 4-byte header FA CE 00 24)
// frame byte N → parse_buf_[N - 4]
// All confirmed from UART captures + image table
namespace rx {
  constexpr uint8_t POWER      = 18;  // frame[22]  0=OFF 1=ON
  constexpr uint8_t SPEED      = 19;  // frame[23]  1-100%
  constexpr uint8_t MODE       = 20;  // frame[24]  0=direct 1=natural 2=smart
  constexpr uint8_t OSC        = 21;  // frame[25]  0=OFF 1=ON
  constexpr uint8_t ANGLE      = 22;  // frame[26]  0x1E/3C/5A/78/8C
  constexpr uint8_t TIMER_H    = 23;  // frame[27]  uint16 BE high byte
  constexpr uint8_t TIMER_L    = 24;  // frame[28]  uint16 BE low byte
  constexpr uint8_t SOUND      = 25;  // frame[29]  confirmed
  constexpr uint8_t LED        = 26;  // frame[30]  confirmed
  constexpr uint8_t CHILD_LOCK = 27;  // frame[31]  confirmed
  constexpr uint8_t TEMP_B0    = 28;  // frame[32]  IEEE754 float LE e.g. 00 00 C4 41 = 24.5°C
  constexpr uint8_t TEMP_B1    = 29;  // frame[33]
  constexpr uint8_t TEMP_B2    = 30;  // frame[34]
  constexpr uint8_t TEMP_B3    = 31;  // frame[35]
  constexpr uint8_t HUM_B0     = 32;  // frame[36]  IEEE754 float LE e.g. 00 00 1C 42 = 39.0%
  constexpr uint8_t HUM_B1     = 33;  // frame[37]
  constexpr uint8_t HUM_B2     = 34;  // frame[38]
  constexpr uint8_t HUM_B3     = 35;  // frame[39]
}

// ── Angle helpers — confirmed ─────────────────────────────────────────────────
static uint8_t angle_to_byte(int deg) {
  if (deg <= 30)  return 0x1E;
  if (deg <= 60)  return 0x3C;
  if (deg <= 90)  return 0x5A;
  if (deg <= 120) return 0x78;
  return 0x8C;
}
static int byte_to_angle(uint8_t b) {
  switch (b) {
    case 0x1E: return 30;
    case 0x3C: return 60;
    case 0x5A: return 90;
    case 0x78: return 120;
    case 0x8C: return 140;
    default:   return 90;
  }
}

// ── Internal state ────────────────────────────────────────────────────────────
struct FanState {
  bool     power       = false;
  uint8_t  speed       = 35;
  uint8_t  mode        = 0;     // 0=direct 1=natural 2=smart
  bool     oscillation = false;
  uint8_t  roll_angle  = 0x5A;  // default 90°
  uint16_t timer_min   = 0;     // 0-480 min
  bool     sound       = true;
  bool     led         = true;
  bool     child_lock  = false;

  bool operator==(const FanState &o) const {
    return power==o.power && speed==o.speed && mode==o.mode
        && oscillation==o.oscillation && roll_angle==o.roll_angle
        && timer_min==o.timer_min && sound==o.sound
        && led==o.led && child_lock==o.child_lock;
  }
  bool operator!=(const FanState &o) const { return !(*this == o); }
};

// ── Main component ────────────────────────────────────────────────────────────
class DmFan : public fan::Fan, public Component, public uart::UARTDevice {
 public:
  void set_temperature_sensor(sensor::Sensor *s) { temperature_ = s; }
  void set_humidity_sensor(sensor::Sensor *s)    { humidity_ = s; }

  // ── Lifecycle ────────────────────────────────────────────────────────────
  void setup() override {
    ESP_LOGI(TAG, "DM Fan v2.2 ready — TX=GPIO17 RX=GPIO16 19200 baud");
    // Mode handled as separate Select entity — no preset_modes on Fan
    auto restore = this->restore_state_();
    if (restore.has_value()) restore->apply(*this);
    // Boot-Init: request full state from MCU (action:2, resource:0x232A)
    // Confirmed from original firmware log: ESP always sends this first
    uint8_t init[14] = {MAGIC_0, MAGIC_1, 0x00, 0x09, 0x02, 0x23, 0x2A,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t ichk = 0;
    for (int i = 0; i < 13; i++) ichk += init[i];
    init[13] = ichk;
    write_array(init, 14);
    ESP_LOGD(TAG, "Boot-Init: requesting MCU state (action:2 resource:0x232A)");
  }

  fan::FanTraits get_traits() override {
    fan::FanTraits t;
    t.set_oscillation(true);
    t.set_supported_speed_count(100);
    return t;
  }

  void loop() override {
    while (available()) {
      uint8_t b;
      read_byte(&b);
      parse_byte_(b);
    }
  }

  // ── HA → MCU: fan::Fan control (power / speed / oscillation) ─────────────
  void control(const fan::FanCall &call) override {
    last_control_time_ = millis();
    if (call.get_state().has_value()) {
      desired_.power = *call.get_state();
      send_cmd_bool_(RES_POWER, desired_.power);
    }
    if (call.get_speed().has_value()) {
      desired_.speed = (uint8_t) std::max(1, std::min(100, (int)*call.get_speed()));
      send_cmd_byte_(RES_SPEED, desired_.speed);
    }
    if (call.get_oscillating().has_value()) {
      desired_.oscillation = *call.get_oscillating();
      send_cmd_bool_(RES_OSC_ONOFF, desired_.oscillation);
    }
  }

  // ── Public API — callable from YAML lambdas ───────────────────────────────
  void set_mode(uint8_t mode) {
    if (mode > 2) return;
    desired_.mode = mode;
    send_cmd_byte_(RES_MODE, desired_.mode);
  }
  void set_roll_angle(int deg) {
    desired_.roll_angle = angle_to_byte(deg);
    send_cmd_byte_(RES_OSC_ANGLE, desired_.roll_angle);
  }
  void set_sound(bool v)      { desired_.sound      = v; send_cmd_bool_(RES_SOUND,     v); }
  void set_led(bool v)        { desired_.led        = v; send_cmd_bool_(RES_LED,       v); }
  void set_child_lock(bool v) { desired_.child_lock = v; send_cmd_bool_(RES_CHILDLOCK, v); }
  void set_timer_hours(float h) {
    desired_.timer_min = (uint16_t)(std::max(0.0f, std::min(8.0f, h)) * 60.0f);
    send_cmd_uint16_(RES_TIMER, desired_.timer_min);
  }
  void rotate_left()  { send_cmd_byte_(RES_ROTATE, 0x01); }  // UNCONFIRMED
  void rotate_right() { send_cmd_byte_(RES_ROTATE, 0x02); }  // UNCONFIRMED

  uint8_t  get_mode()        const { return desired_.mode; }
  int      get_roll_angle()  const { return byte_to_angle(desired_.roll_angle); }
  bool     get_sound()       const { return desired_.sound; }
  bool     get_led()         const { return desired_.led; }
  bool     get_child_lock()  const { return desired_.child_lock; }
  float    get_timer_hours() const { return desired_.timer_min / 60.0f; }

 protected:
  FanState desired_;
  FanState hw_state_;
  // Rollover-safe: millis() - last_control_time_ < 300
  // uint32 underflow wraps correctly — no 49-day freeze bug
  uint32_t last_control_time_ = 0;

  sensor::Sensor *temperature_{nullptr};
  sensor::Sensor *humidity_{nullptr};

  static const char *mode_name_(uint8_t m) {
    if (m == 1) return "natural";
    if (m == 2) return "smart";
    return "direct";
  }

  // ── State machine parser (byte-by-byte, no buffer overflow) ──────────────
  enum class ParseState { MAGIC0, MAGIC1, LEN_H, LEN_L, PAYLOAD, CHECKSUM };
  ParseState parse_st_ = ParseState::MAGIC0;
  uint8_t    parse_buf_[64]{};
  uint16_t   parse_len_ = 0;
  uint16_t   parse_idx_ = 0;

  void parse_byte_(uint8_t b) {
    switch (parse_st_) {
      case ParseState::MAGIC0:
        if (b == MAGIC_0) parse_st_ = ParseState::MAGIC1;
        break;
      case ParseState::MAGIC1:
        if      (b == MAGIC_1) parse_st_ = ParseState::LEN_H;
        else if (b == MAGIC_0) parse_st_ = ParseState::MAGIC1;  // FA FA CE resync
        else                   parse_st_ = ParseState::MAGIC0;
        break;
      case ParseState::LEN_H:
        parse_len_ = (uint16_t)(b << 8);
        parse_st_  = ParseState::LEN_L;
        break;
      case ParseState::LEN_L:
        parse_len_ |= b;
        parse_idx_  = 0;
        if (parse_len_ == 0 || parse_len_ > sizeof(parse_buf_)) {
          ESP_LOGW(TAG, "Invalid frame length %u", parse_len_);
          parse_st_ = ParseState::MAGIC0;
        } else {
          parse_st_ = ParseState::PAYLOAD;
        }
        break;
      case ParseState::PAYLOAD:
        parse_buf_[parse_idx_++] = b;
        if (parse_idx_ >= parse_len_) parse_st_ = ParseState::CHECKSUM;
        break;
      case ParseState::CHECKSUM: {
        uint8_t chk = MAGIC_0 + MAGIC_1
                    + (uint8_t)(parse_len_ >> 8)
                    + (uint8_t)(parse_len_ & 0xFF);
        for (uint16_t i = 0; i < parse_len_; i++) chk += parse_buf_[i];
        if (chk == b) {
          uint8_t cmd = parse_buf_[0];
          if      (cmd == CMD_QUERY && parse_len_ >= 9)  on_wifi_query_();
          else if (cmd == CMD_STATE && parse_len_ >= 36) on_state_frame_();
          else if (cmd == 0x01) on_action1_(parse_buf_[1], parse_buf_[2]);
          else ESP_LOGD(TAG, "CMD 0x%02X len=%u", cmd, parse_len_);
        } else {
          ESP_LOGW(TAG, "Checksum error: got 0x%02X expected 0x%02X", b, chk);
        }
        parse_st_ = ParseState::MAGIC0;
        break;
      }
    }
  }

  void on_wifi_query_() {
    // CONFIRMED from original firmware log:
    // action:82, resource:0x78, data_length:0x3B (59 bytes total payload)
    // Full frame = FA CE 00 3B 82 00 78 [56 bytes state] [chk]
    uint8_t f[64] = {
      0xFA, 0xCE,   // magic
      0x00, 0x3B,   // length = 59
      0x82,         // action = 0x82 (confirmed, NOT 0x81)
      0x00, 0x78,   // resource = 0x78 (echo, NOT 0x70)
    };
    // Bytes 7-62: fan state (56 bytes)
    // Fill with current desired state at known positions
    // Remaining unknown bytes stay 0x00
    f[7 + 15] = desired_.power ? 1 : 0;      // ~frame[22]
    f[7 + 16] = desired_.speed;               // ~frame[23]
    f[7 + 17] = desired_.mode;                // ~frame[24]
    f[7 + 18] = desired_.oscillation ? 1 : 0; // ~frame[25]
    f[7 + 19] = desired_.roll_angle;          // ~frame[26]
    f[7 + 21] = desired_.timer_min >> 8;      // ~frame[27]
    f[7 + 22] = desired_.timer_min & 0xFF;    // ~frame[28]
    // checksum = sum of all bytes except last
    uint8_t chk = 0;
    for (int i = 0; i < 63; i++) chk += f[i];
    f[63] = chk;
    write_array(f, 64);
    ESP_LOGD(TAG, "WiFi query → responding (action:82 resource:78 len:59)");
  }

  void on_action1_(uint8_t res_hi, uint8_t res_lo) {
    // Generic ACK for MCU commands (action:1).
    // Confirmed resources:
    //   0x238D = reset command  → ACK + ignore (no reboot in ESPHome)
    //   0x1F44 = provisioning   → ACK + ignore
    //   Others                  → ACK + log
    uint16_t res = ((uint16_t)res_hi << 8) | res_lo;
    // Build ACK: FA CE 00 09 81 res_hi res_lo 01 00 00 00 00 00 [chk]
    uint8_t f[14] = {MAGIC_0, MAGIC_1, 0x00, 0x09, 0x81,
                     res_hi, res_lo, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t chk = 0;
    for (int i = 0; i < 13; i++) chk += f[i];
    f[13] = chk;
    write_array(f, 14);
    if      (res == 0x238D) ESP_LOGD(TAG, "MCU reset cmd (0x238D) → ACK, ignoring");
    else if (res == 0x1F44) ESP_LOGD(TAG, "MCU provisioning cmd (0x1F44) → ACK, ignoring");
    else                    ESP_LOGD(TAG, "MCU action:1 res=0x%04X → ACK", res);
  }

  void on_state_frame_() {
    // Anti-flap: rollover-safe 300ms lock
    if (millis() - last_control_time_ < 300) {
      ESP_LOGD(TAG, "Lock active, skipping HA update");
      return;
    }

    FanState n;
    n.power       = parse_buf_[rx::POWER] != 0;
    n.speed       = parse_buf_[rx::SPEED];
    n.mode        = parse_buf_[rx::MODE];
    n.oscillation = parse_buf_[rx::OSC] != 0;
    n.roll_angle  = parse_buf_[rx::ANGLE];
    n.timer_min   = ((uint16_t)parse_buf_[rx::TIMER_H] << 8) | parse_buf_[rx::TIMER_L];
    n.sound       = parse_buf_[rx::SOUND] != 0;
    n.led         = parse_buf_[rx::LED] != 0;
    n.child_lock  = parse_buf_[rx::CHILD_LOCK] != 0;

    // IEEE754 float LE — confirmed: 00 00 C4 41 = 24.5°C, 00 00 1C 42 = 39.0%
    float temp = 0.0f, hum = 0.0f;
    if (parse_len_ > rx::HUM_B3) {
      uint8_t tb[4] = {parse_buf_[rx::TEMP_B0], parse_buf_[rx::TEMP_B1],
                       parse_buf_[rx::TEMP_B2], parse_buf_[rx::TEMP_B3]};
      memcpy(&temp, tb, 4);
      uint8_t hb[4] = {parse_buf_[rx::HUM_B0],  parse_buf_[rx::HUM_B1],
                       parse_buf_[rx::HUM_B2],  parse_buf_[rx::HUM_B3]};
      memcpy(&hum, hb, 4);
    }

    ESP_LOGI(TAG,
      "MCU: pwr=%d spd=%d%% mode=%s osc=%d angle=%d° tmr=%dmin "
      "snd=%d led=%d lock=%d temp=%.1f°C hum=%.1f%%",
      n.power, n.speed, mode_name_(n.mode), n.oscillation,
      byte_to_angle(n.roll_angle), n.timer_min,
      n.sound, n.led, n.child_lock, temp, hum
    );

    if (temperature_ && temp > -10.0f && temp < 60.0f)
      temperature_->publish_state(temp);
    if (humidity_ && hum >= 0.0f && hum <= 100.0f)
      humidity_->publish_state(hum);

    if (n != hw_state_) {
      hw_state_ = n;
      desired_  = hw_state_;
      this->state       = hw_state_.power;
      this->speed       = hw_state_.speed;
      this->oscillating = hw_state_.oscillation;
      this->publish_state();
    }
  }

  // ── TX frame builders — confirmed from 31 captures ───────────────────────
  uint32_t msg_counter_ = 0;

  void build_cmd_header_(uint8_t *f, uint8_t payload_len) {
    f[0]  = MAGIC_0; f[1]  = MAGIC_1;
    f[2]  = 0x00;    f[3]  = payload_len;
    f[4]  = CMD_SET;
    f[5]  = 0x23;    f[6]  = 0x47;
    f[7]  = (msg_counter_ >> 24) & 0xFF;
    f[8]  = (msg_counter_ >> 16) & 0xFF;
    f[9]  = (msg_counter_ >>  8) & 0xFF;
    f[10] = (msg_counter_      ) & 0xFF;
    msg_counter_++;
    f[11] = 0x00;
    f[13] = 0x00;
  }

  uint8_t checksum_(const uint8_t *buf, size_t len) {
    uint8_t s = 0;
    for (size_t i = 0; i < len; i++) s += buf[i];
    return s;
  }

  void send_cmd_byte_(uint8_t resource, uint8_t value) {
    uint8_t f[17];
    build_cmd_header_(f, 0x0C);
    f[12] = 0x03; f[14] = resource; f[15] = value;
    f[16] = checksum_(f, 16);
    write_array(f, 17);
    ESP_LOGD(TAG, "TX: res=0x%02X val=0x%02X", resource, value);
  }

  void send_cmd_bool_(uint8_t resource, bool value) {
    send_cmd_byte_(resource, value ? 0x01 : 0x00);
  }

  void send_cmd_uint16_(uint8_t resource, uint16_t value) {
    uint8_t f[18];
    build_cmd_header_(f, 0x0D);
    f[12] = 0x04; f[14] = resource;
    f[15] = (value >> 8) & 0xFF;
    f[16] = (value     ) & 0xFF;
    f[17] = checksum_(f, 17);
    write_array(f, 18);
    ESP_LOGD(TAG, "TX: res=0x%02X val=%u min", resource, value);
  }
};

}  // namespace dm_fan
}  // namespace esphome
