#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/fan/fan.h"
#include "esphome/components/sensor/sensor.h"
#include <algorithm>
#include <cstring>
#include <set>
#include <string>

namespace esphome {
namespace dm_fan {

static const char *const TAG = "dm_fan.v3.0.0";

// ── Protocol constants ────────────────────────────────────────────────────────
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
constexpr uint8_t RES_ROTATE    = 0x05;  // 0x01=left 0x02=right (UNCONFIRMED)
constexpr uint8_t RES_TIMER     = 0x06;  // uint16 BE minutes 0-480
constexpr uint8_t RES_SOUND     = 0x07;
constexpr uint8_t RES_LED       = 0x08;
constexpr uint8_t RES_CHILDLOCK = 0x09;

// ── WiFi handshake — 3-stage sequence (confirmed from hardware UART capture) ──
//
// On the FIRST WiFi query the original firmware sends three sequential 73-byte
// frames 100 ms apart, encoding the connection-state progression:
//   Stage 1: not connected / booting   (flags: ...00 01 00 00 02 00 02)
//   Stage 2: WiFi connected, no cloud  (flags: ...00 01 00 01 00 03 00 03)
//   Stage 3: cloud/local ready         (flags: ...00 01 00 01 00 00 01 00 04)
// On ALL subsequent queries only Stage 3 is sent.
//
// The 73-byte frame layout (action=0x82, resource=0x0078):
//   FA CE 00 44      magic + length (0x44 = 68 = payload bytes)
//   [13 bytes] header (incl. 82 00 78 ... 3B)
//   [16 bytes] "dmiot_v1.1.0\0\0\0\0"
//   [16 bytes] "zeico_3.0.0\0\0\0\0\0"
//   [16 bytes] "dc4f22b19a03\0\0\0\0"  (MAC, fixed — MCU ignores it)
//   [11 bytes] stage flags
//   [1 byte]   checksum
// Total = 4 + 68 (payload) + 1 (checksum) = 73 bytes. Array size is deduced
// with [] so it can never drift from the byte content.
//
// Sending only Stage 3 (our previous v2.x behaviour) works but the MCU may
// reset the ESP within the first boot window if it expects the full sequence.
// Sending all three stages costs <1 ms of UART time and is handled
// non-blocking via a pending-stage counter polled in loop().

static const uint8_t WIFI_STAGE1[] = {
  0xFA,0xCE,0x00,0x44,0x82,0x00,0x78,0x00,0x00,0x00,0x00,0x00,0x3B,
  // "dmiot_v1.1.0\0\0\0\0"
  0x64,0x6D,0x69,0x6F,0x74,0x5F,0x76,0x31,0x2E,0x31,0x2E,0x30,0x00,0x00,0x00,0x00,
  // "zeico_3.0.0\0\0\0\0\0"
  0x7A,0x65,0x69,0x63,0x6F,0x5F,0x33,0x2E,0x30,0x2E,0x30,0x00,0x00,0x00,0x00,0x00,
  // MAC (fixed placeholder — MCU ignores value)
  0x44,0x43,0x34,0x46,0x32,0x32,0x42,0x31,0x39,0x41,0x30,0x33,0x00,0x00,0x00,0x00,
  // Stage 1 flags + checksum
  0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x02,0x00,0x02,0x43
};
static const uint8_t WIFI_STAGE2[] = {
  0xFA,0xCE,0x00,0x44,0x82,0x00,0x78,0x00,0x00,0x00,0x00,0x00,0x3B,
  0x64,0x6D,0x69,0x6F,0x74,0x5F,0x76,0x31,0x2E,0x31,0x2E,0x30,0x00,0x00,0x00,0x00,
  0x7A,0x65,0x69,0x63,0x6F,0x5F,0x33,0x2E,0x30,0x2E,0x30,0x00,0x00,0x00,0x00,0x00,
  0x44,0x43,0x34,0x46,0x32,0x32,0x42,0x31,0x39,0x41,0x30,0x33,0x00,0x00,0x00,0x00,
  // Stage 2 flags + checksum
  0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x00,0x03,0x00,0x03,0x46
};
static const uint8_t WIFI_STAGE3[] = {
  0xFA,0xCE,0x00,0x44,0x82,0x00,0x78,0x00,0x00,0x00,0x00,0x00,0x3B,
  0x64,0x6D,0x69,0x6F,0x74,0x5F,0x76,0x31,0x2E,0x31,0x2E,0x30,0x00,0x00,0x00,0x00,
  0x7A,0x65,0x69,0x63,0x6F,0x5F,0x33,0x2E,0x30,0x2E,0x30,0x00,0x00,0x00,0x00,0x00,
  0x44,0x43,0x34,0x46,0x32,0x32,0x42,0x31,0x39,0x41,0x30,0x33,0x00,0x00,0x00,0x00,
  // Stage 3 flags + checksum
  0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x00,0x01,0x00,0x04,0x45
};

// ── RX payload offsets ────────────────────────────────────────────────────────
// parse_buf_[0] = CMD byte (frame byte 4, after 4-byte header FA CE 00 24)
// frame byte N → parse_buf_[N - 4]
namespace rx {
  constexpr uint8_t POWER      = 18;  // frame[22]
  constexpr uint8_t SPEED      = 19;  // frame[23]
  constexpr uint8_t MODE       = 20;  // frame[24]
  constexpr uint8_t OSC        = 21;  // frame[25]
  constexpr uint8_t ANGLE      = 22;  // frame[26]
  constexpr uint8_t TIMER_H    = 23;  // frame[27]
  constexpr uint8_t TIMER_L    = 24;  // frame[28]
  constexpr uint8_t SOUND      = 25;  // frame[29]
  constexpr uint8_t LED        = 26;  // frame[30]
  constexpr uint8_t CHILD_LOCK = 27;  // frame[31]
  constexpr uint8_t TEMP_B0    = 28;  // frame[32]  IEEE754 float LE
  constexpr uint8_t TEMP_B1    = 29;  // frame[33]
  constexpr uint8_t TEMP_B2    = 30;  // frame[34]
  constexpr uint8_t TEMP_B3    = 31;  // frame[35]
  constexpr uint8_t HUM_B0     = 32;  // frame[36]  IEEE754 float LE
  constexpr uint8_t HUM_B1     = 33;  // frame[37]
  constexpr uint8_t HUM_B2     = 34;  // frame[38]
  constexpr uint8_t HUM_B3     = 35;  // frame[39]
}

// ── Angle helpers ─────────────────────────────────────────────────────────────
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
  uint8_t  mode        = 0;
  bool     oscillation = false;
  uint8_t  roll_angle  = 0x5A;  // 90°
  uint16_t timer_min   = 0;
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

  // ── Lifecycle ─────────────────────────────────────────────────────────────
  void setup() override {
    ESP_LOGI(TAG, "DM Fan v3.0.0 — TX=GPIO17 RX=GPIO16 19200 baud");
    this->set_supported_preset_modes({"Direct Breeze", "Natural Breeze", "Smart Breeze"});
    auto restore = this->restore_state_();
    if (restore.has_value()) restore->apply(*this);

    // Pre-date the anti-flap lock so the first MCU state frame after boot
    // is not swallowed by the 300 ms window (rollover-safe).
    last_control_time_ = millis() - 1000;

    // Boot-Init: request full state from MCU (action:2, resource:0x232A)
    // Confirmed from original firmware log — ESP always sends this on startup
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
    t.set_speed(true);
    t.set_supported_speed_count(100);
    t.set_supported_preset_modes({"Direct Breeze", "Natural Breeze", "Smart Breeze"});
    return t;
  }

  void loop() override {
    // Non-blocking WiFi handshake stage dispatch
    // Stages 2 and 3 are sent 100 ms after each previous stage,
    // without blocking the ESPHome main loop.
    if (wifi_stage_pending_ > 0) {
      uint32_t now = millis();
      // Rollover-safe: uint32 underflow wraps correctly
      if (now - wifi_stage_time_ >= 100) {
        if (wifi_stage_pending_ == 2) {
          write_array(WIFI_STAGE2, sizeof(WIFI_STAGE2));
          ESP_LOGD(TAG, "WiFi handshake → Stage 2 sent");
          wifi_stage_pending_ = 3;
          wifi_stage_time_    = now;
        } else if (wifi_stage_pending_ == 3) {
          write_array(WIFI_STAGE3, sizeof(WIFI_STAGE3));
          ESP_LOGD(TAG, "WiFi handshake → Stage 3 sent");
          wifi_stage_pending_  = 0;
          wifi_handshake_done_ = true;
        }
      }
    }

    while (available()) {
      uint8_t b;
      read_byte(&b);
      parse_byte_(b);
    }
  }

  // ── HA → MCU: power / speed / oscillation ────────────────────────────────
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
    if (call.has_preset_mode()) {
      const char *pm = call.get_preset_mode();
      uint8_t mode = 0;
      if (strcmp(pm, "Natural Breeze") == 0)    mode = 1;
      else if (strcmp(pm, "Smart Breeze") == 0) mode = 2;
      desired_.mode = mode;
      send_cmd_byte_(RES_MODE, desired_.mode);
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

  // Anti-flap: rollover-safe 300 ms lock after any HA→MCU command
  uint32_t last_control_time_ = 0;

  // WiFi 3-stage handshake state
  // wifi_stage_pending_: 0=idle, 2=waiting to send stage2, 3=waiting to send stage3
  bool     wifi_handshake_done_ = false;
  int      wifi_stage_pending_  = 0;
  uint32_t wifi_stage_time_     = 0;

  sensor::Sensor *temperature_{nullptr};
  sensor::Sensor *humidity_{nullptr};

  static const char *mode_name_(uint8_t m) {
    if (m == 1) return "natural";
    if (m == 2) return "smart";
    return "direct";
  }

  // ── State machine parser ──────────────────────────────────────────────────
  // 2-byte length field (big-endian) per frame header FA CE [len_hi] [len_lo]
  enum class ParseState { MAGIC0, MAGIC1, LEN_H, LEN_L, PAYLOAD, CHECKSUM };
  ParseState parse_st_ = ParseState::MAGIC0;
  // 160 bytes: large enough for the ~128-byte boot state response (0x232A)
  // so it is not rejected as "invalid length"; normal frames are <= 41 bytes.
  uint8_t    parse_buf_[160]{};
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
          ESP_LOGW(TAG, "Invalid frame length %u — discarding", parse_len_);
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
        // Checksum = sum of ALL bytes (magic + length + payload) mod 256
        uint8_t chk = MAGIC_0 + MAGIC_1
                    + (uint8_t)(parse_len_ >> 8)
                    + (uint8_t)(parse_len_ & 0xFF);
        for (uint16_t i = 0; i < parse_len_; i++) chk += parse_buf_[i];
        if (chk == b) {
          uint8_t cmd = parse_buf_[0];
          if      (cmd == CMD_QUERY && parse_len_ >= 9)  on_wifi_query_();
          else if (cmd == CMD_STATE && parse_len_ >= 36) on_state_frame_();
          else if (cmd == 0x01)                          on_action1_(parse_buf_[1], parse_buf_[2]);
          else ESP_LOGD(TAG, "Unknown CMD=0x%02X len=%u", cmd, parse_len_);
        } else {
          ESP_LOGW(TAG, "Checksum error: got 0x%02X expected 0x%02X", b, chk);
        }
        parse_st_ = ParseState::MAGIC0;
        break;
      }
    }
  }

  // ── WiFi keepalive handler ─────────────────────────────────────────────────
  // First query: send Stage 1 immediately, schedule Stage 2+3 non-blocking.
  // Subsequent queries: send Stage 3 only.
  void on_wifi_query_() {
    if (!wifi_handshake_done_ && wifi_stage_pending_ == 0) {
      // First query ever: send Stage 1 now, queue Stage 2 + 3
      write_array(WIFI_STAGE1, sizeof(WIFI_STAGE1));
      ESP_LOGD(TAG, "WiFi query (first) → Stage 1 sent, queuing Stage 2+3");
      wifi_stage_pending_ = 2;
      wifi_stage_time_    = millis();
    } else if (wifi_handshake_done_) {
      // All subsequent queries: Stage 3 only
      write_array(WIFI_STAGE3, sizeof(WIFI_STAGE3));
      ESP_LOGD(TAG, "WiFi query → Stage 3 sent");
    }
    // If wifi_stage_pending_ != 0 we are still in the middle of the first
    // handshake — the MCU sent a second query before we finished. Ignore it;
    // the pending-stage dispatch in loop() will complete the sequence.
  }

  // ── Generic ACK for MCU action:1 commands ─────────────────────────────────
  // 0x238D = reset command  → ACK + ignore (ESPHome does not reboot on demand)
  // 0x1F44 = provisioning   → ACK + ignore
  // Others                  → ACK + log
  void on_action1_(uint8_t res_hi, uint8_t res_lo) {
    uint16_t res = ((uint16_t)res_hi << 8) | res_lo;
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

  // ── MCU state report → HA ─────────────────────────────────────────────────
  void on_state_frame_() {
    // Anti-flap: ignore MCU echo during 300 ms after a HA command.
    // uint32 subtraction is rollover-safe — no 49-day freeze bug.
    if (millis() - last_control_time_ < 300) {
      ESP_LOGD(TAG, "Anti-flap lock active — skipping HA update");
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
      uint8_t hb[4] = {parse_buf_[rx::HUM_B0], parse_buf_[rx::HUM_B1],
                       parse_buf_[rx::HUM_B2], parse_buf_[rx::HUM_B3]};
      memcpy(&hum, hb, 4);
    }

    ESP_LOGI(TAG,
      "MCU: pwr=%d spd=%d%% mode=%s osc=%d angle=%d° tmr=%dmin "
      "snd=%d led=%d lock=%d temp=%.1f°C hum=%.1f%%",
      n.power, n.speed, mode_name_(n.mode), n.oscillation,
      byte_to_angle(n.roll_angle), n.timer_min,
      n.sound, n.led, n.child_lock, temp, hum
    );

    // Plausibility-checked sensor publish
    if (temperature_ && temp > -10.0f && temp < 60.0f)
      temperature_->publish_state(temp);
    if (humidity_ && hum >= 0.0f && hum <= 100.0f)
      humidity_->publish_state(hum);

    if (n != hw_state_) {
      hw_state_ = n;
      desired_  = hw_state_;
      static const char *const MODE_NAMES[] = {
        "Direct Breeze", "Natural Breeze", "Smart Breeze"};
      this->set_preset_mode_(n.mode < 3 ? MODE_NAMES[n.mode] : MODE_NAMES[0]);
      this->state       = hw_state_.power;
      this->speed       = hw_state_.speed;
      this->oscillating = hw_state_.oscillation;
      this->publish_state();
    }
  }

  // ── TX frame builders ─────────────────────────────────────────────────────
  uint32_t msg_counter_ = 0;

  // Fills bytes [0..12] of a standard 17-byte CMD_SET frame.
  // f[12] is intentionally left to the caller (0x03 for byte/bool, 0x04 for uint16).
  // f[13] = 0x00 is fixed padding, set here.
  void build_cmd_header_(uint8_t *f, uint8_t payload_len) {
    f[0]  = MAGIC_0; f[1]  = MAGIC_1;
    f[2]  = 0x00;    f[3]  = payload_len;
    f[4]  = CMD_SET;
    f[5]  = 0x23;    f[6]  = 0x47;
    // 4-byte message counter, big-endian — mirrors original firmware behaviour
    f[7]  = (msg_counter_ >> 24) & 0xFF;
    f[8]  = (msg_counter_ >> 16) & 0xFF;
    f[9]  = (msg_counter_ >>  8) & 0xFF;
    f[10] = (msg_counter_      ) & 0xFF;
    msg_counter_++;
    f[11] = 0x00;
    // f[12] set by caller (0x03 or 0x04)
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
    f[12] = 0x03;
    f[14] = resource;
    f[15] = value;
    f[16] = checksum_(f, 16);
    write_array(f, 17);
    ESP_LOGD(TAG, "TX: res=0x%02X val=0x%02X ctr=%u", resource, value, msg_counter_ - 1);
  }

  void send_cmd_bool_(uint8_t resource, bool value) {
    send_cmd_byte_(resource, value ? 0x01 : 0x00);
  }

  void send_cmd_uint16_(uint8_t resource, uint16_t value) {
    uint8_t f[18];
    build_cmd_header_(f, 0x0D);
    f[12] = 0x04;
    f[14] = resource;
    f[15] = (value >> 8) & 0xFF;
    f[16] = (value     ) & 0xFF;
    f[17] = checksum_(f, 17);
    write_array(f, 18);
    ESP_LOGD(TAG, "TX: res=0x%02X val=%u min ctr=%u", resource, value, msg_counter_ - 1);
  }
};

}  // namespace dm_fan
}  // namespace esphome
