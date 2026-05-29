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
2. MCU → ESP: action:82, resource:0x232A, data:128 bytes
3. MCU → ESP: periodic state reports (action:84, resource:0x2347)
```

---

## RX: MCU → ESP state frame (CMD=0x84, 41 bytes)

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
