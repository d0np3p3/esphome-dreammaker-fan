# ESP32 ↔ Fan MCU Communication

After flashing, the ESP32 communicates with the fan MCU over an **internal UART already wired on the PCB** — GPIO17 (TX) and GPIO16 (RX). No external cables are required for normal operation.

- **Baudrate:** 19200
- **Framework:** ESP-IDF (required for UART2)

---

## Frame format

```
FA CE | len_hi len_lo | CMD | payload... | checksum
```

Checksum = sum of ALL bytes (magic + length + payload) mod 256.

---

## WiFi keepalive — 3-stage handshake

MCU sends a periodic WiFi status query (~60 s):

```
FA CE 00 09 02 00 78 00 00 00 00 00 00 4B    action:2 resource:0x78
```

ESP response — **first query only**: three 68-byte frames, 100 ms apart:

```
Stage 1: FA CE 00 44 82 00 78 ... flags: 00 00 00 00 00 01 00 00 02 00 02  CK=0x43
Stage 2: FA CE 00 44 82 00 78 ... flags: 00 00 00 01 00 01 00 00 03 00 03  CK=0x46
Stage 3: FA CE 00 44 82 00 78 ... flags: 00 00 00 01 00 01 00 00 01 00 04  CK=0x45
```

**All subsequent queries:** Stage 3 only.

Without a response the MCU pulls EN pin LOW → POWERON_RESET after ~4 minutes.

---

## Boot sequence

```
1. ESP → MCU: action:2,  resource:0x232A  (request full state)
2. MCU → ESP: action:82, resource:0x232A, data:80 bytes  (total frame ~89 bytes)
             → contains version strings incl. mcu_version "fan_0001"
3. MCU → ESP: periodic state reports (action:84, resource:0x2347)
```

### Known version strings (from original firmware, resource_id:127 cloud heartbeat)

| Field | Value |
|-------|-------|
| `comm_version` | `dmiot_v1.1.0` (ESP WiFi stack) |
| `rf_version` | `v3.1.6` (BLE stack / ESP-IDF version) |
| `mcu_version` | `fan_0001` (Fan MCU firmware) |

---

## RX: MCU → ESP state frame (CMD=0x84, 36 bytes payload)

The full frame is `FA CE 00 24 84 ...` (0x24 = 36 bytes payload + 1 checksum = 41 bytes total).

| Frame byte | Payload offset | Field | Values |
|------------|---------------|-------|--------|
| 4 | 0 | CMD | 0x84 |
| 5–6 | 1–2 | Resource | 0x23 0x47 |
| 7–10 | 3–6 | **Echo counter** | uint32 BE — echoes the TX msg counter of the last ESP→MCU command that triggered this state report. **0x00000000 = spontaneous** (physical button press or periodic update) |
| 11 | 7 | unknown | 0x00 |
| 12 | 8 | Data length | 0x1B = 27 |
| 13–21 | 9–17 | unknown/padding | 0x00 … |
| 22 | 18 | Power | 0=OFF, 1=ON |
| 23 | 19 | Speed | 1–100% |
| 24 | 20 | Mode | 0=direct, 1=natural, 2=smart |
| 25 | 21 | Oscillation | 0=OFF, 1=ON |
| 26 | 22 | Angle | 0x1E=30° 0x3C=60° 0x5A=90° 0x78=120° 0x8C=140° |
| 27–28 | 23–24 | Timer | uint16 BE minutes (0–480) |
| 29 | 25 | Sound | 0=OFF, 1=ON |
| 30 | 26 | LED | 0=OFF, 1=ON |
| 31 | 27 | Child Lock | 0=OFF, 1=ON |
| 32–35 | 28–31 | Temperature | IEEE754 float LE (e.g. 00 00 C4 41 = 24.5°C) |
| 36–39 | 32–35 | Humidity | IEEE754 float LE (e.g. 00 00 1C 42 = 39.0%) |

**Echo counter confirmed from hardware log (2026-06-01):**
```
TX ctr=3 (Timer 360min) → RX frame bytes [3-6] = 00 00 00 03
TX ctr=5 (Sound ON)     → RX frame bytes [3-6] = 00 00 00 05
Physical button press   → RX frame bytes [3-6] = 00 00 00 00
```

---

## TX: ESP → MCU single-property commands (CMD=0x04)

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
| 0x05 | Rotate Left/Right | uint8 1=left 2=right (unconfirmed) |
| 0x06 | Timer | uint16 BE minutes |
| 0x07 | Sound | bool |
| 0x08 | LED | bool |
| 0x09 | Child Lock | bool |

---

## MCU control commands (ESP must ACK, not obey)

| Resource | Meaning | ESPHome response |
|----------|---------|-----------------|
| 0x238D | Reset ESP | ACK `action:81` + ignore |
| 0x1F44 | Start WiFi provisioning | ACK `action:81` + ignore |
