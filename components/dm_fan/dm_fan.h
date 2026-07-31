#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/fan/fan.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#ifdef USE_ESP32_BLE_DEVICE
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#endif
#include <algorithm>
#include <cstring>
#include <string>

namespace esphome {
namespace dm_fan {

static const char *const TAG = "dm_fan.v4.0.0-beta";

// ── Protocol constants ────────────────────────────────────────────────────────
constexpr uint8_t MAGIC_0   = 0xFA;
constexpr uint8_t MAGIC_1   = 0xCE;
constexpr uint8_t CMD_STATE = 0x84;  // MCU→ESP full state push (RX)
constexpr uint8_t CMD_SET   = 0x04;  // ESP→MCU single-property command (TX)
constexpr uint8_t CMD_QUERY = 0x02;  // MCU→ESP WiFi status query

// BLE remote beacon — manufacturer-specific advertisement, company ID 0x4D44 ("DM")
constexpr uint16_t BLE_COMPANY_DM = 0x4D44;  // little-endian "DM" = DreamMaker

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
class DmFan : public fan::Fan, public Component, public uart::UARTDevice
#ifdef USE_ESP32_BLE_DEVICE
            , public esp32_ble_tracker::ESPBTDeviceListener
#endif
{
 public:
  void set_temperature_sensor(sensor::Sensor *s)          { temperature_ = s; }
  void set_humidity_sensor(sensor::Sensor *s)              { humidity_ = s; }
  void set_mcu_version_sensor(text_sensor::TextSensor *s)  { mcu_version_ = s; }
  void set_log_raw_frames(bool v)                          { log_raw_frames_ = v; }
  void set_ble_remote(bool v)                              { ble_remote_ = v; }
  void set_ble_report_to_mcu(bool v)                       { ble_report_to_mcu_ = v; }

  // ── Lifecycle ─────────────────────────────────────────────────────────────
  void setup() override {
    ESP_LOGI(TAG, "DM Fan v4.0.0-beta — TX=GPIO17 RX=GPIO16 19200 baud");
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
    // Wire the entity's preset modes (registered in setup() via
    // set_supported_preset_modes) into the fresh traits. This is the
    // non-deprecated replacement for FanTraits::set_supported_preset_modes()
    // and keeps the presets visible in the Home Assistant fan card.
    this->wire_preset_modes_(t);
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

    // Optimistically reflect the command in HA right away. The MCU echo
    // arrives ~40 ms later and is swallowed by the 300 ms anti-flap lock,
    // so without this push HA would never see power/speed/mode/oscillation
    // changes triggered from the fan card.
    this->state       = desired_.power;
    this->speed       = desired_.speed;
    this->oscillating = desired_.oscillation;
    this->set_preset_mode_(preset_name_(desired_.mode));
    this->publish_state();
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

  // ── BLE remote beacon receiver (Phase 1) ───────────────────────────────────
  // The DreamMaker remote (and the fan itself in original firmware) advertises a
  // manufacturer-specific BLE beacon with company ID 0x4D44 ("DM"). Layout of the
  // manufacturer data AFTER the 2-byte company ID (which esp32_ble_tracker strips
  // into the ServiceData uuid):
  //   [0..1]  protocol version (observed 0x02 0x01)
  //   [2..7]  device MAC (BLE byte order)
  //   [8]     sequence counter (increments ~every 20 s / on activity)
  //   [9]     status (0x01 = idle)
  //   [10..]  payload (8 bytes, all-zero when idle — button data when active)
  //
  // This handler only DECODES and LOGS by default. Forwarding to the MCU over
  // UART (resource 0x1F41) is gated behind set_ble_report_to_mcu() because the
  // exact frame format is still being reverse-engineered and a malformed report
  // can make the MCU reset the ESP.
#ifdef USE_ESP32_BLE_DEVICE
  bool parse_device(const esp32_ble_tracker::ESPBTDevice &device) override {
    if (!ble_remote_) return false;
    const auto dm_uuid = esp32_ble::ESPBTUUID::from_uint16(BLE_COMPANY_DM);
    for (auto &md : device.get_manufacturer_datas()) {
      if (!(md.uuid == dm_uuid)) continue;
      on_dm_beacon_(device, md.data);
      return true;
    }
    return false;
  }
#endif

 protected:
  FanState desired_;
  FanState hw_state_;

  // Anti-flap: short guard against a stale spontaneous frame arriving right
  // after a HA command. Only applies to counter==0 frames now (our own command
  // echoes are identified by their non-zero echo counter). Rollover-safe.
  static constexpr uint32_t STALE_GUARD_MS = 250;
  uint32_t last_control_time_ = 0;

  // WiFi 3-stage handshake state
  // wifi_stage_pending_: 0=idle, 2=waiting to send stage2, 3=waiting to send stage3
  bool     wifi_handshake_done_ = false;
  int      wifi_stage_pending_  = 0;
  uint32_t wifi_stage_time_     = 0;

  sensor::Sensor *temperature_{nullptr};
  sensor::Sensor *humidity_{nullptr};
  text_sensor::TextSensor *mcu_version_{nullptr};
  bool log_raw_frames_{false};

  // BLE remote beacon state. Payload length varies by sender: the remote
  // advertises 8 bytes, the fan's own beacon (original firmware) 10.
  static constexpr size_t BLE_PAYLOAD_MAX = 16;
  bool    ble_remote_{false};
  bool    ble_report_to_mcu_{false};
  bool    ble_have_last_{false};
  uint8_t ble_last_counter_{0};
  uint8_t ble_last_status_{0};
  uint8_t ble_last_payload_[BLE_PAYLOAD_MAX]{};
  size_t  ble_last_payload_len_{0};

  static const char *mode_name_(uint8_t m) {
    if (m == 1) return "natural";
    if (m == 2) return "smart";
    return "direct";
  }

  // Preset-mode label as registered via set_supported_preset_modes().
  static const char *preset_name_(uint8_t m) {
    if (m == 1) return "Natural Breeze";
    if (m == 2) return "Smart Breeze";
    return "Direct Breeze";
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
        if (log_raw_frames_) {
          // Format: "FA CE len_hi len_lo payload... checksum"
          char hex[512];
          int pos = snprintf(hex, sizeof(hex), "FA CE %02X %02X",
                             (uint8_t)(parse_len_ >> 8), (uint8_t)(parse_len_ & 0xFF));
          for (uint16_t i = 0; i < parse_len_ && pos < (int)sizeof(hex) - 4; i++)
            pos += snprintf(hex + pos, sizeof(hex) - pos, " %02X", parse_buf_[i]);
          snprintf(hex + pos, sizeof(hex) - pos, " %02X", b);
          ESP_LOGD(TAG, "RX raw [len=%u]: %s", parse_len_, hex);
        }
        // Checksum = sum of ALL bytes (magic + length + payload) mod 256
        uint8_t chk = MAGIC_0 + MAGIC_1
                    + (uint8_t)(parse_len_ >> 8)
                    + (uint8_t)(parse_len_ & 0xFF);
        for (uint16_t i = 0; i < parse_len_; i++) chk += parse_buf_[i];
        if (chk == b) {
          uint8_t cmd = parse_buf_[0];
          if      (cmd == CMD_QUERY && parse_len_ >= 9)  on_wifi_query_();
          else if (cmd == CMD_STATE && parse_len_ >= 36) on_state_frame_();
          else if (cmd == 0x01      && parse_len_ >= 3)  on_action1_(parse_buf_[1], parse_buf_[2]);
          else if (cmd == 0x82      && parse_len_ >= 7)  on_boot_response_();
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
  // 0x238D = reset command    → ACK + ignore (ESPHome does not reboot on demand)
  // 0x1F44 = remote pairing   → ACK with data=[0x01] ("agree to pair")
  // Others                    → ACK + log
  //
  // Frame format confirmed against original firmware on a fake-MCU testbench:
  //   MCU→ESP: FA CE 00 0A 01 1F 44 [msg_id 4B] 00 01 [data] [chk]
  //   ESP→MCU: FA CE 00 0A 81 1F 44 [msg_id 4B] 00 01 01   [chk]
  //                                                    └─ data=0x01 = "agree"
  // The original firmware always answers data=[0x01] regardless of the request
  // data byte, so the ANSWER byte carries the agree(1)/unagree(0) decision.
  // Our previous ACK sent data_len=0 (no data byte) — the MCU logs
  // "BLE->mcu unagree to pair!" / times out in that case.
  void on_action1_(uint8_t res_hi, uint8_t res_lo) {
    uint16_t res = ((uint16_t)res_hi << 8) | res_lo;
    // Echo the request msg_id when the frame is long enough to carry one.
    uint8_t m0 = 0x01, m1 = 0x00, m2 = 0x00, m3 = 0x00;
    if (parse_len_ >= 7) {
      m0 = parse_buf_[3]; m1 = parse_buf_[4];
      m2 = parse_buf_[5]; m3 = parse_buf_[6];
    }
    // Only 0x1F44 is confirmed to need the trailing agree byte. Other action:1
    // resources (e.g. 0x238D reset) keep the original data_len=0 ACK, since
    // their real response format was never captured.
    const bool agree = (res == 0x1F44);
    uint8_t f[15] = {MAGIC_0, MAGIC_1, 0x00, (uint8_t)(agree ? 0x0A : 0x09), 0x81,
                     res_hi, res_lo, m0, m1, m2, m3,
                     0x00, (uint8_t)(agree ? 0x01 : 0x00), 0x01, 0x00};
    const uint8_t len = agree ? 15 : 14;
    uint8_t chk = 0;
    for (int i = 0; i < len - 1; i++) chk += f[i];
    f[len - 1] = chk;
    write_array(f, len);
    if      (res == 0x238D) ESP_LOGD(TAG, "MCU reset cmd (0x238D) → ACK, ignoring");
    else if (agree)         ESP_LOGI(TAG, "MCU remote-pairing trigger (0x1F44) → ACK agree=1");
    else                    ESP_LOGD(TAG, "MCU action:1 res=0x%04X → ACK", res);
  }

  // ── Boot-state response (action:0x82, resource:0x232A) ───────────────────
  // MCU responds to our boot-init request with ~80 bytes of device state
  // including version strings. We dump the full payload once for analysis
  // and scan for the "fan_" ASCII marker to extract mcu_version (e.g. "fan_0001").
  void on_boot_response_() {
    uint16_t res = ((uint16_t)parse_buf_[1] << 8) | parse_buf_[2];
    if (res == 0x1F41) {
      // ACK for our experimental BLE→MCU beacon report. Seeing this confirms
      // the reverse-engineered 0x1F41 frame format is accepted by the MCU.
      ESP_LOGI(TAG, "MCU ACKed BLE report (action:82 res:0x1F41, len=%u)", parse_len_);
      return;
    }
    if (res != 0x232A) {
      ESP_LOGD(TAG, "Boot response resource=0x%04X len=%u — ignored", res, parse_len_);
      return;
    }
    ESP_LOGI(TAG, "Boot state response received (len=%u)", parse_len_);

    // Full hex dump (one-time, at boot) so the complete 80-byte payload is
    // visible for protocol analysis — including any version strings beyond fan_.
    {
      char hex[3 * 160 + 1] = {};
      int pos = 0;
      for (uint16_t i = 0; i < parse_len_ && pos < (int)sizeof(hex) - 3; i++)
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", parse_buf_[i]);
      ESP_LOGI(TAG, "Boot response full payload: %s", hex);

      // ASCII view — printable chars only, '.' for the rest. Makes embedded
      // version strings (dmiot_v1.1.0, v3.1.6, fan_0001) immediately readable.
      char ascii[161] = {};
      int apos = 0;
      for (uint16_t i = 0; i < parse_len_ && apos < (int)sizeof(ascii) - 1; i++) {
        uint8_t c = parse_buf_[i];
        ascii[apos++] = (c >= 0x20 && c < 0x7F) ? (char) c : '.';
      }
      ESP_LOGI(TAG, "Boot response ASCII:   %s", ascii);
    }

    // Scan payload for "fan_" ASCII prefix (0x66 0x61 0x6E 0x5F)
    for (uint16_t i = 0; i + 4 <= parse_len_; i++) {
      if (parse_buf_[i]   == 0x66 && parse_buf_[i+1] == 0x61 &&
          parse_buf_[i+2] == 0x6E && parse_buf_[i+3] == 0x5F) {
        char ver[17] = {};
        for (int j = 0; j < 16 && (i + j) < parse_len_ && parse_buf_[i + j] != 0x00; j++)
          ver[j] = (char) parse_buf_[i + j];
        ESP_LOGI(TAG, "MCU version: %s (offset %u)", ver, i);
        if (mcu_version_) mcu_version_->publish_state(ver);
        return;
      }
    }
    ESP_LOGW(TAG, "Boot response: 'fan_' marker not found — see ASCII dump above");
    if (mcu_version_) mcu_version_->publish_state("unknown");
  }

  // ── MCU state report → HA ─────────────────────────────────────────────────
  void on_state_frame_() {
    // Echo counter (frame bytes [3-6] / payload offset 3-6, uint32 BE).
    // Non-zero = the MCU is echoing a command WE sent (only the ESP issues
    // commands, so any non-zero value is necessarily our own echo).
    // Zero = a spontaneous change (physical button or periodic report).
    uint32_t echo = ((uint32_t)parse_buf_[3] << 24) |
                    ((uint32_t)parse_buf_[4] << 16) |
                    ((uint32_t)parse_buf_[5] <<  8) |
                     (uint32_t)parse_buf_[6];

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
      "snd=%d led=%d lock=%d temp=%.1f°C hum=%.1f%% echo=%u",
      n.power, n.speed, mode_name_(n.mode), n.oscillation,
      byte_to_angle(n.roll_angle), n.timer_min,
      n.sound, n.led, n.child_lock, temp, hum, (unsigned) echo
    );

    // Sensors always publish — temp/hum are independent of fan-state flap handling.
    if (temperature_ && temp > -10.0f && temp < 60.0f)
      temperature_->publish_state(temp);
    if (humidity_ && hum >= 0.0f && hum <= 100.0f)
      humidity_->publish_state(hum);

    // ── Flap suppression (counter-aware, replaces the old 300 ms blanket lock) ──
    // 1. Our own command echo (counter != 0): the final desired state was already
    //    pushed to HA optimistically in control(). A multi-command batch echoes
    //    each step with intermediate states, so publishing them would flap HA.
    if (echo != 0) {
      // Keep the change-detection baseline current so the next spontaneous
      // frame is not flagged as a (redundant) state change.
      hw_state_ = n;
      ESP_LOGD(TAG, "Echo of our cmd (ctr=%u) — HA already updated optimistically",
               (unsigned) echo);
      return;
    }
    // 2. Spontaneous frame (counter 0) arriving right after our command may be a
    //    stale pre-command report. A short guard window prevents a flash of the
    //    old value before our optimistic state settles. Physical button presses
    //    outside this window are reflected immediately (no blanket 300 ms block).
    if (millis() - last_control_time_ < STALE_GUARD_MS) {
      ESP_LOGD(TAG, "Spontaneous frame within %u ms guard — skipping (stale?)",
               (unsigned) STALE_GUARD_MS);
      return;
    }

    if (n != hw_state_) {
      hw_state_ = n;
      desired_  = hw_state_;
      this->set_preset_mode_(preset_name_(n.mode));
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
    ESP_LOGD(TAG, "TX: res=0x%02X val=0x%02X ctr=%u", resource, value,
             (unsigned) (msg_counter_ - 1));
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
    ESP_LOGD(TAG, "TX: res=0x%02X val=%u min ctr=%u", resource, value,
             (unsigned) (msg_counter_ - 1));
  }

  // ── BLE beacon → decode + log (+ optional MCU report) ──────────────────────
#ifdef USE_ESP32_BLE_DEVICE
  void on_dm_beacon_(const esp32_ble_tracker::ESPBTDevice &device,
                     const std::vector<uint8_t> &d) {
    // d = manufacturer data after the 2-byte company ID. Need at least
    // proto(2) + mac(6) + counter(1) + status(1) = 10 bytes.
    if (d.size() < 10) {
      ESP_LOGD(TAG, "DM beacon from %s too short (%u bytes)",
               device.address_str().c_str(), (unsigned) d.size());
      return;
    }
    uint16_t proto   = ((uint16_t) d[0] << 8) | d[1];
    uint8_t  counter = d[8];
    uint8_t  status  = d[9];

    uint8_t payload[BLE_PAYLOAD_MAX] = {};
    size_t  pn = std::min(BLE_PAYLOAD_MAX, d.size() - 10);
    for (size_t i = 0; i < pn; i++) payload[i] = d[10 + i];

    char phex[3 * BLE_PAYLOAD_MAX + 1] = {};
    int  pos = 0;
    for (size_t i = 0; i < pn; i++)
      pos += snprintf(phex + pos, sizeof(phex) - pos, "%02X ", payload[i]);

    // Highlight changes — a changed counter/status/payload is the interesting
    // event (button press), a repeated idle beacon is just the ~20 s heartbeat.
    bool changed = !ble_have_last_ || counter != ble_last_counter_ ||
                   status != ble_last_status_ || pn != ble_last_payload_len_ ||
                   memcmp(payload, ble_last_payload_, pn) != 0;

    if (changed) {
      ESP_LOGI(TAG,
        "DM remote beacon %s proto=0x%04X ctr=%u status=0x%02X payload[%u]=[ %s]",
        device.address_str().c_str(), proto, counter, status, (unsigned) pn, phex);
    } else {
      ESP_LOGD(TAG, "DM remote beacon %s (idle heartbeat, ctr=%u)",
               device.address_str().c_str(), counter);
    }

    ble_have_last_        = true;
    ble_last_counter_     = counter;
    ble_last_status_      = status;
    ble_last_payload_len_ = pn;
    memcpy(ble_last_payload_, payload, BLE_PAYLOAD_MAX);

    // Optional, experimental: forward the beacon to the MCU (resource 0x1F41).
    if (ble_report_to_mcu_ && changed)
      report_beacon_to_mcu_(counter, payload);
  }
#endif

  // EXPERIMENTAL — frame format reverse-engineered, not yet confirmed on hardware.
  // FA CE 00 0C | 02 1F 41 | [counter] | [8-byte payload] | [chk]
  void report_beacon_to_mcu_(uint8_t counter, const uint8_t *payload8) {
    uint8_t f[17];
    f[0] = MAGIC_0; f[1] = MAGIC_1; f[2] = 0x00; f[3] = 0x0C;
    f[4] = 0x02;    f[5] = 0x1F;    f[6] = 0x41;
    f[7] = counter;
    for (int i = 0; i < 8; i++) f[8 + i] = payload8[i];
    f[16] = checksum_(f, 16);
    write_array(f, 17);
    ESP_LOGD(TAG, "BLE→MCU report (res=0x1F41 ctr=%u) — EXPERIMENTAL", counter);
  }
};

}  // namespace dm_fan
}  // namespace esphome
