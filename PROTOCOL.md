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
2. MCU → ESP: action:82, resource:0x232A, data_length:0x80 = **128 bytes**
              (total frame length:89 — note the log prints length in decimal
               but data_length in hex; earlier docs mis-read 0x80 as "80 bytes")
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
| 0x1F44 | **Remote-pairing trigger** | ACK `action:81` with `data_len=1, data=[0x01]` = "agree" |

> ⚠️ **Corrected 2026-07-31:** `0x1F44` was previously documented as "Start WiFi
> provisioning". It is actually the **remote-pairing trigger**, confirmed live:
> holding *Head-shaking + Timer* on the fan emits repeated `0x1F44` frames.
>
> ```
> MCU→ESP: FA CE 00 0A 01 1F 44 [msg_id 4B] 00 01 [data] [chk]
> ESP→MCU: FA CE 00 0A 81 1F 44 [msg_id 4B] 00 01  01    [chk]
>                                                   └─ 0x01 = "agree to pair"
> ```
>
> The original firmware always answers `data=[0x01]` regardless of the request
> byte → the **answer** byte carries the agree(1)/unagree(0) decision. Matching
> MCU log strings in the firmware binary: `BLE->mcu agree to pair!`,
> `BLE->mcu unagree to pair!`, `BLE->mcu report timeout!`.
> Our ACK previously sent `data_len=0` — fixed in `on_action1_()`.

---

---

## Reference material (from firmware-binary + testbench analysis)

Collected from a fake-MCU testbench (original firmware driven against a
simulated MCU over UART) and from strings/offsets in `ota_0_0x110000.bin`.

### `0x0078` WiFi-query response — full 56-byte structure

The MCU's periodic WiFi query is answered with a 0x44-length frame whose payload
decodes as three null-padded ASCII fields plus status bytes:

```
FA CE 00 44 82 00 78 [msg_id 4B] 00 3B
64 6D 69 6F 74 5F 76 31 2E 31 2E 30 00 00 00 00   "dmiot_v1.1.0"  (16 B)
7A 65 69 63 6F 5F 33 2E 30 2E 30 00 00 00 00 00   "zeico_3.0.0"   (16 B)
35 63 30 31 33 62 62 66 36 30 64 63 00 00 00 00   "5c013bbf60dc"  (16 B)
00 00 00 00 01 00 01 02 00 02                     status bytes    (8 B)
[checksum]
```

| Offset | Len | Content |
|--------|-----|---------|
| 0 | 16 | `comm_version`, ASCII, null-padded |
| 16 | 16 | `firmware_version`, ASCII, null-padded |
| 32 | 16 | 12-hex-digit device/chip ID, ASCII (device-specific) |
| 48 | 8 | status bytes, meaning unclear |

Our `on_wifi_query_()` currently sends zero bytes in these fields. That works
(it prevents the MCU reset) but reports no real versions — this layout would
allow publishing genuine values as diagnostic sensors.

### State payload byte layout — 9-byte prefix (verified)

Verified 1:1 against a real frame (25.7 °C / 63.0 % / 70 % speed / 90°). Note the
prefix is **9** bytes, not 8 as an earlier JSON-derived guess assumed:

```
data[0:9]   prefix (deviceException / useException / reserved, usually 0)
data[9]     power
data[10]    speed
data[11]    mode
data[12]    roll_enable (oscillation)
data[13]    roll_angle
data[14:16] power_delay (timer, uint16 BE)
data[16]    sound
data[17]    light (LED)
data[18]    child_lock
data[19:23] temperature (float LE)
data[23:27] humidity (float LE)
```

### `deviceException` values

| Value | Bits | Meaning |
|-------|------|---------|
| `0x400000` | 22 | normal |
| `0xC00000` | 22+23 | error combination |
| `0x800000` | 23 only | **third value, meaning unknown** (seen with `"source":"manual"`) |

### `ext1`–`ext6` — fragmented `product_id`

All six `ext` fields are constant across state frames and together encode a
substring of the device's own `product_id` as ASCII (6 × 4-byte chunks):

```
ext1=97  → 'a'      ext2=102 → 'f'      ext3/ext4 → 2 chars each
ext5=943206968 → 4 chars                ext6=1630823777 → 4 chars
combined: "af43818828a4ea" = product_id[3:17]
```

Device-specific but always the same encoding scheme. Static — irrelevant for
control, documented so nobody re-investigates it.

### Flash partition table (from boot log)

| Label | Type/ST | Offset | Size |
|-------|---------|--------|------|
| nvs | 01/02 | `0x009000` | 16 KB |
| otadata | 01/00 | `0x00D000` | 8 KB |
| phy_init | 01/01 | `0x00F000` | 4 KB |
| factory | 00/00 | `0x010000` | 1 MB |
| ota_0 | 00/10 | `0x110000` | 1 MB |
| ota_1 | 00/11 | `0x210000` | 1 MB |

### Firmware string offsets (`ota_0_0x110000.bin`)

```
0x0000AC  "cloud1.dm-maker.com"
0x0000C4  "api2.dm-maker.com"
0x0046B2  "BLE:paired model:%d"
0x004C28  "dmiot_ble_init"
0x00539A  "received_ap->ssid:%s,key:%s,mark:%s"
0x0053D6  "BLE->mcu agree to pair!"
0x005406  "BLE->mcu unagree to pair!"
0x00543A  "BLE->mcu report timeout!"
0x00AC10  "btc_ble_storage"
0x00EC0A  "btm_ble_set_encryption"
0x00ECAE  "btm_ble_ltk_request_reply"
```
