# ESPHome DreamMaker Fan Component

**v2.2** — Native UART integration for Zeico / DreamMaker Smart Fan (DM-FAN01 / DM-FAN02-W)  
Fully local, no cloud, no Tuya — works 100% offline via Home Assistant.

> **Status:** ✅ Live tested on real hardware (2026-05-23), all features confirmed working.

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
| WiFi keepalive (prevents MCU reboot) | ✅ |
| Boot state sync from MCU | ✅ |
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

The fan board uses an **ESP32-WROOM-32D**. The pads `BOOT`, `RXD`, `TXD`, `GND` and a `RESET` button are exposed on the PCB for flashing — connect a USB-to-UART adapter here. These pads are **only used during flashing** and have nothing to do with the ongoing MCU communication.

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
dm_fan.yaml                    ← ESPHome configuration (adapt this)
components/
  dm_fan/
    __init__.py                ← Namespace declaration
    fan.py                     ← Python codegen (fan platform)
    dm_fan.h                   ← C++ component (all logic here)
```

---

## Protocol reference

### Frame format (FA CE magic)

```
FA CE | len_hi len_lo | CMD | payload... | checksum
```
Checksum = sum of all bytes mod 256.

### RX: MCU → ESP — State push (CMD=0x84, resource=0x2347)

State is sent as JSON via the cloud log layer. Key fields:

| Field | Values |
|-------|--------|
| `power` | 0=OFF, 1=ON |
| `speed` | 1–100 (%) |
| `mode` | 0=direct, 1=natural, 2=smart |
| `roll_enable` | 0=OFF, 1=ON (oscillation) |
| `roll_angle` | 30 / 60 / 90 / 120 / 140 (degrees) |
| `power_delay` | 0–480 (timer in minutes) |
| `sound` | 0=OFF, 1=ON |
| `light` | 0=OFF, 1=ON |
| `child_lock` | 0=OFF, 1=ON |
| `temperature` | float °C |
| `humidity` | float % |
| `deviceException` | 4194304=normal, 12582912=motor/error |

### TX: ESP → MCU — Single-property commands (CMD=0x04)

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
| 0x04 | Oscillation Angle | uint8 0x1E/3C/5A/78/8C |
| 0x05 | Rotate Left/Right | uint8 1=left 2=right (UNCONFIRMED) |
| 0x06 | Timer | uint16 BE minutes |
| 0x07 | Sound | bool |
| 0x08 | LED | bool |
| 0x09 | Child Lock | bool |

### Boot sequence (confirmed from original firmware log)

```
1. ESP → MCU: action:2,  resource:0x232A  (request full state)
2. MCU → ESP: action:82, resource:0x232A, data:128 bytes (full state)
3. ESP → MCU: action:82, resource:0x78,   data:59 bytes  (WiFi status)
```

### WiFi keepalive

MCU sends query every ~60s:
```
FA CE 00 09 02 00 78 00 00 00 00 00 00 [chk]    action:2 resource:0x78
```
ESP responds (confirmed from original firmware log):
```
FA CE 00 3B 82 00 78 [56 bytes state] [chk]     action:82 resource:0x78 data:59 bytes
```
Without response → MCU pulls EN pin LOW → POWERON_RESET after ~4 minutes.

### MCU control commands (ESP must ACK, not obey)

| Resource | Meaning | ESPHome response |
|----------|---------|-----------------|
| 0x238D | Reset ESP (SW_CPU_RESET) | ACK `action:81` + ignore |
| 0x1F44 | Start WiFi provisioning | ACK `action:81` + ignore |

---

## Known Limitations

**Rotate buttons unconfirmed:** `RES_ROTATE (0x05)` was derived from the firmware binary, not from live UART captures.

---

## Changelog

### v2.2 (2026-05-29)
- **Speed slider fixed:** `t.set_speed(true)` was missing from `get_traits()` — HA fan card now shows speed percentage slider
- **Mode now a fan preset:** mode moved from template select to `FanTraits::set_supported_preset_modes` — syncs from MCU physical buttons, visible in HA fan card
- **`parse_buf_` enlarged to 160 bytes** — handles large MCU boot-state responses
- Baudrate confirmed: 19200
- WiFi-Response corrected: `action:82, resource:0x78, 59 bytes`
- Boot-Init added: ESP sends `action:2, resource:0x232A` on startup
- MCU command ACK for 0x238D / 0x1F44
- All features live-tested on real hardware

### v2.1
- TX protocol rewritten based on 31 confirmed UART captures
- Single-property CMD=0x04 commands
- WiFi keepalive (prevented periodic reboots)
- Temperature/Humidity sensors working
- ESPHome 2026.x compatibility

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
