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
| MCU version readout | ✅ |
| **BLE remote (DM-FCB01)** | ✅ **v4.0.0-beta branch only** — see below |

---

## Which config do I need?

| | Config | Branch |
|---|---|---|
| **Most users** — control from Home Assistant | [`dm_fan.yaml`](dm_fan.yaml) | `main` |
| You still use the original remote (DM-FCB01) | [`remote_control.yaml`](remote_control.yaml) | `v4.0.0-beta` |

The remote support lives on the **`v4.0.0-beta`** branch. It needs a per-device
key that can only be extracted **before** flashing ESPHome, so it is not part of
the stable `main` line — everything else works identically on both.

---

## BLE remote control (`v4.0.0-beta` branch)

**Confirmed working on hardware (2026-08-03)** — all five button actions decode
and drive the fan: power, the four speed gears, all three modes, oscillation and
the full timer cycle.

> Requires `ref: v4.0.0-beta` in `external_components` — the component on `main`
> does not contain the BLE code.

The original DM-FCB01 remote keeps working after flashing ESPHome. It broadcasts
each button press as an encrypted BLE advertisement, which `dm_fan` decrypts and
turns into fan commands — fully local, no cloud, no re-pairing.

**Requires the fan's `ble_key`** — an 8-byte per-device secret stored in the
fan's NVS. Without it the presses are logged but cannot be executed.

```yaml
fan:
  - platform: dm_fan
    id: my_fan
    uart_id: uart_bus
    ble_remote: true
    ble_key: !secret dm_ble_key    # 8 bytes hex, e.g. "00 11 22 33 44 55 66 77"

esp32_ble_tracker:                 # required by ble_remote
  scan_parameters:
    active: false
    interval: 200ms
    window: 100ms
```

See [`remote_control.yaml`](remote_control.yaml) for a complete config.

### Getting the `ble_key` — do this BEFORE flashing

The key only exists in the NVS of a fan still running the **original firmware**.
Once ESPHome is flashed it may be gone, so dump it first:

```bash
esptool.py --port COMx read_flash 0x9000 0x4000 nvs_backup.bin
```

Then locate the `ble_key` entry. Two pitfalls:

- For `type=0x41` (blob) with `span=2` the 8 payload bytes are **not** in the
  metadata entry — they sit at the start of the **following** 32-byte block.
- NVS is wear-levelled, so several copies exist. Use the one with a real CRC,
  not `ffffffff`.

Sanity check: `ble_model` must be `0x0201` (513) and `ble_mac` must match your
remote's MAC. If `ble_model` is `0`, that fan was never paired.

> ⚠️ **Do not re-pair the remote** (Power + M). A new bond generates a new key
> and your saved `ble_key` becomes useless.

### How it works

The remote broadcasts a manufacturer-specific advertisement (company ID
`0x4D44` = "DM"). A button press carries an 8-byte payload encrypted with
**single DES in ECB mode**, keyed with `ble_key`. Decrypted it holds the pressed
button plus the complete target state and a checksum. Frames failing the
checksum are rejected, so a wrong key or a neighbour's remote can never drive
your fan.

Full protocol details, including the decrypted byte layout and the evidence
behind it, are in [PROTOCOL.md](PROTOCOL.md).

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
dm_fan.yaml                    ← ESPHome configuration (UART only)
remote_control.yaml            ← configuration WITH BLE remote (v4.0.0-beta)
PROTOCOL.md                    ← ESP32 ↔ MCU + BLE remote protocol reference
TODO.md                        ← open work, ordered by priority
components/
  dm_fan/
    __init__.py                ← Namespace declaration
    fan.py                     ← Python codegen (fan platform)
    dm_fan.h                   ← C++ component (all logic)
    des.h                      ← single-DES for the remote payload

Research / debugging configs (not needed for normal use):
  ble_remote_test.yaml         ← log raw remote payloads
  ble_mcu_forward_test.yaml    ← 0x1F41 forward experiment (dead end, kept for reference)
  ble_capture.yaml             ← GATT connect + bind handshake capture
  ble_discovery.yaml           ← GATT service/characteristic discovery
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
