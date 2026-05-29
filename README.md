# ESPHome DreamMaker Fan Component

Native UART integration for Zeico / DreamMaker Smart Fan (DM-FAN01 / DM-FAN02-W).  
Fully local, no cloud, no Tuya — works 100% offline via Home Assistant.

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
| WiFi keepalive — 3-stage (prevents MCU reboot) | ✅ |
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

Reference the component directly from GitHub — no need to copy files:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/d0np3p3/esphome-dreammaker-fan
      ref: main
    components: [dm_fan]
```

1. Copy `dm_fan.yaml` and adapt WiFi credentials + API key.
2. Flash via USB the first time, then OTA.

Alternatively, copy `components/dm_fan/` into your ESPHome config folder and use a local source:

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

After flashing, the ESP32 talks to the fan MCU over an **internal UART already wired on the PCB** — no external cables needed. See [PROTOCOL.md](PROTOCOL.md) for the full communication reference.

---

## File structure

```
dm_fan.yaml                    ← ESPHome configuration
PROTOCOL.md                    ← ESP32 ↔ MCU communication reference
components/
  dm_fan/
    __init__.py                ← Namespace declaration
    fan.py                     ← Python codegen (fan platform)
    dm_fan.h                   ← C++ component (all logic)
```

---

## Changelog

See [releases](https://github.com/d0np3p3/esphome-dreammaker-fan/releases) or [commit history](https://github.com/d0np3p3/esphome-dreammaker-fan/commits/main) for the full changelog.

---

## Credits

Reverse engineered by **d0np3p3** with AI assistance (Claude + Gemini).  
Protocol analysis from original firmware UART logs, BLE snoop captures, and NVS dumps.

### Alternative: ArduinoIDE / HomeKit

If you prefer HomeKit over Home Assistant, see the **[ArduinoIDE HomeSpan sketch](https://github.com/dhewg/esphome-miot/issues/50#issuecomment-4547236665)** in the esphome-miot thread.

### Special thanks

- **[@BobeOlsen](https://github.com/BobeOlsen)** — early research and hardware exploration
- **[@hbgcreag](https://github.com/hbgcreaghaht)** — contributions to protocol understanding
- **[@dhewg](https://github.com/dhewg)** and the **[esphome-miot](https://github.com/dhewg/esphome-miot/issues/50)** community — foundational work on Zeico/DreamMaker UART protocol, without which this project would not have been possible
