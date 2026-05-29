# ESPHome DreamMaker Fan Component

**v3.0.0-beta** — Native UART integration for Zeico / DreamMaker Smart Fan (DM-FAN01 / DM-FAN02-W)  
Fully local, no cloud, no Tuya — works 100% offline via Home Assistant.

> **Status:** 🧪 Beta — based on v2.2 (live-tested) + 3-stage WiFi handshake from hardware capture

---

## What's new in v3.0.0-beta

### 3-Stage WiFi Handshake (non-blocking)

The original firmware responds to the first MCU WiFi query with **three** sequential
68-byte frames (Stage 1 → 100 ms → Stage 2 → 100 ms → Stage 3), not a single frame.
This was confirmed directly from hardware UART captures of the factory firmware by
independent hardware analysis (cross-referenced with our own captures).

Previous versions sent only one response frame, which works in most cases but can
cause the MCU to reset the ESP in the first boot window before `wifi_handshake_done_`
is set.

v3.0 implements the full sequence **non-blocking**: Stage 1 is sent immediately on
the first query; Stages 2 and 3 are dispatched from `loop()` after 100 ms each,
without blocking the ESPHome scheduler.

Subsequent queries (every ~60 s) still receive only Stage 3, as before.

### `build_cmd_header_()` documentation fix

`f[12]` was always set by the caller after `build_cmd_header_()` returned, but the
comment in v2.2 was misleading (`f[13] = 0x00` appeared to skip `f[12]`). The code
was functionally correct; the comment is now explicit.

---

## Features

| Feature | Status |
|---------|--------|
| Power ON/OFF | ✅ |
| Speed 1–100% | ✅ |
| Mode: Direct / Natural / Smart (fan preset, syncs from MCU) | ✅ |
| Oscillation ON/OFF | ✅ |
| Oscillation Angle (30°/60°/90°/120°/140°) | ✅ |
| Timer 0–8h (1h steps) | ✅ |
| Sound (button tones) | ✅ |
| LED display | ✅ |
| Child lock | ✅ |
| Temperature sensor | ✅ |
| Humidity sensor | ✅ |
| WiFi keepalive — 3-stage (prevents MCU reboot) | ✅ v3.0 |
| Boot state sync from MCU | ✅ |
| Anti-flap lock (300 ms) | ✅ |
| BLE remote | 🔜 planned |

---

## Hardware

- **Fan:** DM-FAN01 / DM-FAN02-W (identical hardware, battery difference only)
- **MCU chip:** ESP32-WROOM-32D
- **UART2:** TX=GPIO17 (ESP→MCU), RX=GPIO16 (MCU→ESP)
- **Baudrate:** 19200 (confirmed from hardware test)
- **Framework:** ESP-IDF (required for UART2)

---

## Installation

1. Copy `components/dm_fan/` into your ESPHome config folder.
2. Copy `dm_fan.yaml` and adapt WiFi credentials + API key.
3. Flash via USB first time, then OTA.

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [dm_fan]
```

---

## Flashing the ESP32 (one-time)

The fan board uses an **ESP32-WROOM-32D**. The pads `BOOT`, `RXD`, `TXD`, `GND` and
a `RESET` button are exposed on the PCB for flashing.

### Connections (USB-to-UART adapter → PCB flash pads)

| Adapter | PCB pad |
|---------|---------|
| TX | RXD |
| RX | TXD |
| GND | GND |

> **Note:** Power the board from original fan power — do **not** connect motor power while flashing (two connectors)

### Procedure

1. Connect the adapter as above.
2. Bridge **MCU RST to GND** — there is a 5-pin header next to the ROHS label on the board; Reset and GND are among those five pins. This holds the fan MCU in reset so it cannot send data while the flash UART is busy. Without this the MCU resets the ESP32 every 5–10 min.
3. Bridge the **BOOT** pad to **GND** (pulls GPIO0 low → ESP32 enters flash mode). BOOT is located next to the UART pads.
4. Bridge **RESET** briefly — the ESP32 boots into flash mode ("waiting for download" in serial).
5. Remove the BOOT–GND bridge.
6. Flash via esptool, ESPWeb Tool, or your preferred tool. **Make a backup first.**
7. Remove the MCU RST–GND bridge — the fan MCU resumes normal operation.
8. After the first flash, subsequent updates can be done wirelessly via OTA.

## ESP32 ↔ Fan MCU communication (internal, no wiring needed)

After flashing, the ESP32 talks to the fan MCU over an **internal UART already wired on the PCB** — GPIO17 (TX) and GPIO16 (RX). No external cables are required for this.

---

## File structure

```
dm_fan.yaml                    ← ESPHome configuration
components/
  dm_fan/
    __init__.py                ← Namespace declaration
    fan.py                     ← Python codegen (fan platform)
    dm_fan.h                   ← C++ component (all logic)
```

---

## Protocol reference

### Frame format

```
FA CE | len_hi len_lo | CMD | payload... | checksum
```
Checksum = sum of ALL bytes (magic + length + payload) mod 256.

### WiFi keepalive — 3-stage handshake

MCU sends periodic query (~60 s):
```
FA CE 00 09 02 00 78 00 00 00 00 00 00 4B    action:2 resource:0x78
```

ESP response — **first query only**: three 68-byte frames, 100 ms apart:
```
Stage 1: FA CE 00 44 82 00 78 ... flags: 00 00 00 00 00 01 00 00 02 00 02  CK=0x43
Stage 2: FA CE 00 44 82 00 78 ... flags: 00 00 00 01 00 01 00 00 03 00 03  CK=0x46
Stage 3: FA CE 00 44 82 00 78 ... flags: 00 00 00 01 00 01 00 00 01 00 04  CK=0x45
```
**All subsequent queries**: Stage 3 only.

Without response → MCU pulls EN pin LOW → POWERON_RESET after ~4 minutes.

### Boot sequence

```
1. ESP → MCU: action:2,  resource:0x232A  (request full state)
2. MCU → ESP: action:82, resource:0x232A, data:128 bytes
3. MCU → ESP: periodic state reports (action:84, resource:0x2347)
```

### RX: MCU → ESP state frame (CMD=0x84, 41 bytes)

| Byte | Field | Values |
|------|-------|--------|
| 22 | Power | 0=OFF, 1=ON |
| 23 | Speed | 1–100% |
| 24 | Mode | 0=direct, 1=natural, 2=smart |
| 25 | Oscillation | 0=OFF, 1=ON |
| 26 | Angle | 0x1E=30° 0x3C=60° 0x5A=90° 0x78=120° 0x8C=140° |
| 27–28 | Timer | uint16 BE minutes (0–480) |
| 29 | Sound | 0=OFF, 1=ON |
| 30 | LED | 0=OFF, 1=ON |
| 31 | Child Lock | 0=OFF, 1=ON |
| 32–35 | Temperature | IEEE754 float LE (e.g. 00 00 C4 41 = 24.5°C) |
| 36–39 | Humidity | IEEE754 float LE (e.g. 00 00 1C 42 = 39.0%) |

### TX: ESP → MCU single-property commands (CMD=0x04)

```
FA CE | 00 0C | 04 | 23 47 | [counter 4B BE] | 00 | 03 | 00 | [resource] | [value] | [chk]
Timer: len=0x0D, sub_len=04, uint16 BE minutes
```

| Resource | Property | Type |
|----------|----------|------|
| 0x00 | Power | bool |
| 0x01 | Speed | uint8 1–100 |
| 0x02 | Mode | uint8 0/1/2 |
| 0x03 | Oscillation | bool |
| 0x04 | Oscillation Angle | uint8 |
| 0x05 | Rotate Left/Right | uint8 1=left 2=right (**UNCONFIRMED**) |
| 0x06 | Timer | uint16 BE minutes |
| 0x07 | Sound | bool |
| 0x08 | LED | bool |
| 0x09 | Child Lock | bool |

### MCU control commands (ESP must ACK, not obey)

| Resource | Meaning | ESPHome response |
|----------|---------|-----------------|
| 0x238D | Reset ESP | ACK `action:81` + ignore |
| 0x1F44 | Start WiFi provisioning | ACK `action:81` + ignore |

---

## Known Limitations

**Rotate buttons unconfirmed:** `RES_ROTATE (0x05)` was derived from the firmware binary, not from live UART captures.

---

## Changelog

### v3.0.0-beta
- **3-stage WiFi handshake** (non-blocking): full Stage 1+2+3 sequence on first
  query, Stage 3 only on subsequent queries — matches original firmware behaviour
  confirmed from hardware UART capture
- `build_cmd_header_()` comment clarified (f[12] caller responsibility documented)
- TAG updated to `dm_fan.v3.0.0-beta`

### v2.2 (2026-05-29)
- **Speed slider fixed:** `t.set_speed(true)` was missing — HA fan card now shows speed percentage slider
- **Mode now a fan preset:** syncs from MCU physical buttons, visible in HA fan card
- **`parse_buf_` enlarged to 160 bytes** — handles large MCU boot-state responses
- Baudrate confirmed: 19200
- WiFi-Response corrected: action:82, resource:0x78, 59 bytes
- Boot-Init added: ESP sends action:2, resource:0x232A on startup
- MCU command ACK for 0x238D / 0x1F44
- All features live-tested on real hardware

### v2.1
- TX protocol rewritten from 31 confirmed captures
- Single-property CMD=0x04 commands
- Temperature/Humidity sensors
- Millis-rollover fix

### v2.0
- Initial public release

---

## Credits

Reverse engineered by **d0np3p3** with AI assistance (Claude + Gemini).  
Protocol analysis from original firmware UART logs, BLE snoop captures, and NVS dumps.

### Special thanks

- **[@BobeOlsen](https://github.com/BobeOlsen)** — early research and hardware exploration
- **[@hbgcreag](https://github.com/hbgcreaghaht)** — contributions to protocol understanding  
- **[@dhewg](https://github.com/dhewg)** and the **[esphome-miot](https://github.com/dhewg/esphome-miot/issues/50)** community — foundational work on Zeico/DreamMaker UART protocol, without which this project would not have been possible
