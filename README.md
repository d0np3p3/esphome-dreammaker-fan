# ESPHome DM Fan Component

**v2.1** — Native UART integration for Zeico / Dream Maker Smart Fan (DM-FAN01 / DM-FAN02)

## Quick Start

1. Copy `dm_fan.yaml` to your ESPHome config directory.
2. Provide the required entries in your `secrets.yaml` (`wifi_ssid`, `wifi_password`, `wifi_ap_password`, `api_encryption_key`, `ota_password`).
3. Flash via ESPHome: `esphome run dm_fan.yaml`
4. The fan appears in Home Assistant as a fan entity with speed 1–100%, oscillation,
   mode select, angle select, timer, child lock, LED, sound, and temperature/humidity sensors.

## Flashing the PCB

The fan board uses an **ESP32-WROOM-32D**. The pads `BOOT`, `RXD`, `TXD`, `GND` and a `RESET` button are exposed on the PCB — no soldering of new pads required.

### Connections (USB-to-UART adapter → PCB)

| Adapter | PCB pad |
|---------|---------|
| TX | RXD |
| RX | TXD |
| GND | GND |
| 3.3 V | 3.3 V (if not powered separately) |

### Procedure

1. Connect the adapter as above.
2. Bridge **MCU RST to GND** — there is a 5-pin header next to the ROHS label on the board; the Reset and GND pins are among those five. This holds the fan MCU in reset so it stays silent on the shared UART lines during flashing. Without this step the MCU periodically resets the ESP32.
3. Bridge the **BOOT** pad to **GND** with a jumper wire (pulls GPIO0 low → flash mode). The BOOT pad is located next to the UART pads.
4. Press **RESET** briefly — the ESP32 boots into flash mode ("waiting for download" in serial).
5. Remove the BOOT–GND bridge.
6. Flash: `esphome run dm_fan.yaml`
7. Remove the MCU RST–GND bridge to let the fan MCU run again.
8. After the first OTA-capable flash, subsequent updates can be done wirelessly.

> **Baud rate note:** Community testing (hbgcreaghaht, BobeOlsen) found **9600 baud** as the default and 19200 as an alternative that may work better on some units. If the MCU appears silent after flashing, change `baud_rate` in `dm_fan.yaml` from `9600` to `19200`.

> **Note:** Power the board from 3.3 V on the adapter — do **not** connect mains/motor power while flashing.

## Normal UART wiring (ESP32 ↔ fan MCU, post-flash)

| ESP32 pin | Fan UART |
|-----------|---------|
| GPIO17 (TX) | RX |
| GPIO16 (RX) | TX |
| GND | GND |
| 3.3 V | VCC (logic only — motor power is separate) |

## Changelog

### v2.1
- Mode exposed as separate `select` entity (no preset_modes on the Fan entity)
- Rotate-left / rotate-right buttons added (`RES_ROTATE 0x05`, UNCONFIRMED)
- Millis-rollover fix: 300 ms anti-flap guard now uses unsigned subtraction — no 49-day freeze

### v2.0
- TX protocol completely rewritten based on 31 confirmed UART captures (ESP→MCU)
- Single-property CMD=0x04 commands instead of full 42-byte state dump
- Timer: uint16 BE minutes, max 8h (was 4h)
- Temperature/Humidity: IEEE754 float LE (was raw byte guess)
- Sound/LED/ChildLock: correct byte offsets (rx::SOUND=25, rx::LED=26, rx::CHILD_LOCK=27)
- `rx::` namespace for all RX payload offsets — self-documenting
- msg_counter_ for TX frame sequencing (matches original firmware)
- ESPHome 2026.x: `external_components` + `esp-idf` framework

### v1.0
- Initial implementation (full state frame, byte positions partially unconfirmed)

## File structure

```
dm_fan.yaml                        ← ESPHome configuration
components/
  dm_fan/
    __init__.py                    ← Namespace declaration
    fan.py                         ← Python codegen (fan platform)
    dm_fan.h                       ← C++ component
```

## Protocol summary

### RX: MCU → ESP (CMD=0x84, 41 bytes)
| Byte | Field | Values |
|------|-------|--------|
| 22 | Power | 0=OFF 1=ON |
| 23 | Speed | 1-100% direct |
| 24 | Mode | 0=direct 1=natural 2=smart |
| 25 | Oscillation | 0=OFF 1=ON |
| 26 | Angle | 0x1E=30° 0x3C=60° 0x5A=90° 0x78=120° 0x8C=140° |
| 27-28 | Timer | uint16 BE minutes (0=off, 60=1h…480=8h) |
| 29 | Sound | 0=OFF 1=ON |
| 30 | LED | 0=OFF 1=ON |
| 31 | Child Lock | 0=OFF 1=ON |
| 32-35 | Temperature | IEEE754 float LE (e.g. 00 00 C4 41 = 24.5°C) |
| 36-39 | Humidity | IEEE754 float LE (e.g. 00 00 1C 42 = 39.0%) |

### TX: ESP → MCU (CMD=0x04, 17/18 bytes)
```
FA CE | 00 0C | 04 | 23 47 | [counter 4B BE] | 00 | 03 | 00 | [resource] | [value] | [chk]
```
| Resource | Property | Type |
|----------|----------|------|
| 0x00 | Power | bool |
| 0x01 | Speed | uint8 1-100 |
| 0x02 | Mode | uint8 0/1/2 |
| 0x03 | Oscillation ON/OFF | bool |
| 0x04 | Oscillation Angle | uint8 |
| 0x05 | Rotate | uint8 0x01=left 0x02=right (UNCONFIRMED) |
| 0x06 | Timer | uint16 BE minutes |
| 0x07 | Sound | bool |
| 0x08 | LED | bool |
| 0x09 | Child Lock | bool |

### WiFi keepalive
MCU sends periodic query `FA CE 00 09 02 00 78 ... 4B`
ESP responds `FA CE 00 09 81 00 70 02 ... C4`
Without this response the MCU reboots the ESP every 5-10 min.
