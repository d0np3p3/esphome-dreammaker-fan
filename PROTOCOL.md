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

## BLE remote (Phase 1 — receive + decode, v4.0.0-beta)

The DreamMaker remote uses a **DA14580** BLE chip and advertises a
manufacturer-specific beacon with company ID **`0x4D44` ("DM")** — fully
proprietary, not Tuya/Xiaomi/Zigbee.

### Remote model & buttons (user manual, DM-FCB01)

| Field | Value |
|-------|-------|
| Model | **DM-FCB01** ("Dream Maker Infrared Remote Controller") |
| Dimensions | 130 × 28.5 × 15.5 mm |
| Weight | 37 g |
| Battery | LR-AAA, 1.5 V |
| Applies to | Dream Maker Feel Fan — Freedom / Flagship version |

The remote has **4 physical buttons** → 5 actions (M distinguishes short/long):

| Button | Icon | Short press | Long press |
|--------|------|-------------|------------|
| Power | ⏻ | On/Off | — |
| Air Volume / Mode | M | Speed gear cycle 1→2→3→4 | Mode: Direct → Natural → Smart |
| Head-shaking | ∿ | Oscillation On/Off | — |
| Timed Shutdown | 🕐 | Timer cycle 0h→1h→2h→3h→4h | — |

Remote indicators: 4 mode LEDs (Direct/Natural/Smart) + Bluetooth LED + a 1/2/3/4
air-volume/timer indicator. All **8 LEDs flash during pairing**.

### Pairing / bind procedure (user manual — CRITICAL for capture) ⚠️

**Reset and pairing are the SAME action** — the manual lists them under two
headings ("Bluetooth Pairing" and "Bluetooth Reset/Pair Unbinding") but the
physical step is identical. Each side has exactly **one** reset-and-pair combo:

| Side | Combo | Result (LEDs + state) |
|------|-------|-----------------------|
| **Remote** | *Power + M* together | 8 LEDs flash → old bind **cleared** AND remote enters pairing-wait |
| **Fan** | *Head-shaking + Timer* together | top 4 LEDs flash → old bind **cleared** AND fan enters pairing-wait |

There is no separate "reset mode" vs "pairing mode": pressing the combo unbinds
the previous peer and puts the device into the waiting state in one step.

**Full original pairing flow (remote ↔ fan via the fan's BLE module):**
1. **Fan**: hold *Head-shaking + Timer* → top 4 LEDs flash → fan in pairing-wait.
2. **Remote**: press *Power + M* → 8 LEDs flash → remote in pairing-wait.
3. Press any key on the **fan** → completes the bind; a confirmation tone sounds.
4. No fan action within 15 s → pairing exits, remote stops flashing.

> **Implication for our capture:** the remote likely only *streams button events
> to its currently-bound peer*. Our echo stops the blinking (app-layer bind
> accepted) but the remote may still consider the **original Tuya module** its
> bound peer, so no notifications reach the ESP32. Because reset = pairing, simply
> pressing **Power + M** on the remote clears the old bind and opens a fresh
> pairing window — connect the ESP32 via GATT during that window so the
> remote binds to *us*. This is the leading hypothesis for why the echo succeeds
> but no button notifications follow — test it before assuming SMP is the blocker.

### Advertisement manufacturer data

After the 2-byte company ID (which `esp32_ble_tracker` strips into the
ServiceData UUID), the manufacturer data is:

| Offset | Bytes | Field | Notes |
|--------|-------|-------|-------|
| 0–1 | `02 01` | Protocol version | observed 2.1 |
| 2–7 | 6 | Device MAC | BLE byte order |
| 8 | 1 | **Sequence counter** | increments per event; **resets to `0x01` on re-pair** |
| 9 | 1 | **Status** | `0x01` = idle heartbeat · `0x02` = **command / button press** |
| 10–17 | 8 | Payload | all-zero when `status=0x01`; **8-byte encrypted command when `status=0x02`** |

Captured 2026-06-01 (idle): `4B:F2:7E:47:E5:6E`, company `DM`,
`02 01 4B F2 7E 47 E5 6E 0B 01 00 00 00 00 00 00 00 00`.

Note the leading `02 01` is the same value the fan stores in NVS as `ble_model`
(`0x0201` = 513, the README's sanity check). Whether a device with a different
`ble_model` advertises different bytes here has not been tested.

> **Reading a sniffer trace against this table:** offset 8 moves constantly and
> offset 9 rarely does, which invites reading them the wrong way round. Offset 8
> is the counter; a trace where offset 9 never leaves `0x01` contains no button
> presses at all, no matter how much offset 8 varies. See
> [`docs/nrf-sniffer-remote-capture.md`](docs/nrf-sniffer-remote-capture.md).

The `dm_fan` component decodes this when `ble_remote: true` and logs every
beacon. Changed counter/status/payload → `INFO` (button event), repeated idle
heartbeat → `DEBUG`.

### ⭐ CORRECTION 2026-07-31 — buttons DO travel over advertisements

> An earlier note here claimed the payload `[10..17]` "stays all-zero even during
> button presses" and that button commands therefore only travel over GATT.
> **That was wrong** — it was measured while the remote was **unbound**. A
> verified capture from a *paired* setup shows the opposite.

**Verified capture** (ESPHome fan as passive BLE scanner next to remote
`4B:F2:7E:47:E5:6E` — the *same* remote as ours — while it was paired with an
original fan): **27 button presses produced 23 distinct 8-byte payloads**, all
with `status=0x02`.

```
Time          Ctr   St    Payload (8 bytes)
02:16:26.376  02    02    05 88 22 7D D3 30 9E A3   [A]
02:16:32.312  03    02    EF 8E 65 38 46 18 6C 8A   [B]
02:16:39.467  04    02    05 88 22 7D D3 30 9E A3   [A]  ← repeat of ctr=02
02:16:41.106  05    02    EF 8E 65 38 46 18 6C 8A   [B]  ← repeat of ctr=03
02:16:41.719  06    02    61 93 BE 5B 57 53 8E D5   [C]
02:16:43.054  07    02    F4 F9 1D D5 2E 70 E6 F0   [D]
02:16:44.383  08    02    7E D8 93 D7 D3 9E 9A 62   [E]
02:16:47.147  09    02    CF C2 F6 D7 26 AD 3E A0   [F]
02:16:50.219  0A    02    E0 25 AE 86 F2 FC 42 56   [G]
02:16:52.788  0B    02    E6 BC A0 D5 97 6C 77 77   [H]
02:16:56.878  0C    02    E0 25 AE 86 F2 FC 42 56   [G]  ← repeat of ctr=0A
02:16:57.080  0D    02    CC AD 56 30 EF AF 6C 37   [I]
02:16:58.207  0E    02    29 A3 26 28 22 64 2A 62   [J]
02:16:59.445  0F    02    BC 24 D9 73 2E C5 AE 30   [K]
02:17:01.402  10    02    6B FC 90 AB 09 EE 10 DA   [L]
02:17:02.632  11    02    FF 81 17 0C 2A 7B 8A 49   [M]
02:17:04.351  12    02    C4 56 E7 AA 95 40 E3 60   [N]
02:17:05.694  13    02    5F 0F EC 59 BF 6E CC AC   [O]
02:17:06.602  14    02    89 EE EA 0A 1D A1 18 EB   [P]
02:17:09.176  15    02    1C E8 F1 50 76 C0 22 77   [Q]
02:17:12.650  16    02    CC C3 C4 81 AD D6 CC C3   [R]
02:17:15.430  17    02    C5 8C E2 70 F9 15 15 8F   [S]
02:17:16.126  18    02    EF DE 0C 5D 29 7D BC E8   [T]
02:17:19.614  19    02    D0 F8 61 D6 CA F3 D2 BD   [U]
02:17:21.042  1A    02    EF DE 0C 5D 29 7D BC E8   [T]  ← repeat of ctr=18
02:17:46.846  1B    01    -- idle --
02:18:06.003  1C    01    -- idle --
02:18:30.574  1D    01    -- idle --
02:18:34.891  01    02    4C 37 3B 18 8E 9D 61 A9   [V]  ← after re-pair, ctr reset!
02:18:36.101  02    02    33 BB 4A B9 BE BE 04 20   [W]
```

**Conclusions — these drive the whole Phase 3 design:**

1. **Payload is independent of the counter.** Four repeats ([A]@02+04, [B]@03+05,
   [G]@0A+0C, [T]@18+1A) prove **no rolling code**: the same target state always
   produces the same ciphertext. Deterministic, ECB-like.
2. **23 distinct payloads for 27 presses** — far more than the 4 physical buttons.
   The payload most likely encodes the **complete target state** (mode + speed +
   oscillation + …), not a single button ID. Consistent with the remote's LEDs
   mirroring fan state.
3. **Encrypted, but statistically a real block cipher** (bit balance ≈51%, full
   byte variance at all 8 positions) — not simple XOR/obfuscation.
4. **Idle heartbeats continue while unbound** (`status=0x01`) — which is exactly
   what our earlier unbound measurement saw, hence the wrong conclusion.
5. **Counter resets to `0x01` after re-pairing** — a useful "freshly paired" marker.

### 🔓 SOLVED 2026-08-03 — the payload is DES-ECB, and it fully decodes

The 8-byte payload is **single DES in ECB mode**, keyed with the 8-byte
`ble_key` stored in the fan's NVS. Decrypting the 18 captured payloads yields a
clean, self-consistent 8-byte state struct — no learn table required.

```
byte  field        values
────  ───────────  ────────────────────────────────────────────────
 [0]  button       0xF1 Timer · 0xF2 Oscillation · 0xF3 Speed
                   0xF4 Power · 0xF5 Mode
 [1]  power        0 / 1
 [2]  speed        0x01=1 · 0x23=35 · 0x46=70 · 0x64=100  (the 4 gears)
 [3]  mode         0 direct · 1 natural · 2 smart
 [4]  oscillation  0 / 1
 [5]  roll_angle   always 0x00 — slot exists, this remote never fills it
 [6]  timer        0x00=0 · 0x3C=60 · 0x78=120 · 0xB4=180 · 0xF0=240 min
 [7]  checksum     sum(byte[0..6]) & 0xFF
```

**Evidence.** Against a random-decryption baseline the fit is unambiguous:

| metric | random expectation | DES with `ble_key` |
|--------|-------------------|--------------------|
| bytes < 16 | 6.25 % | **66.7 %** |
| zero bytes | 0.39 % | **34.7 %** |
| checksum `byte[7] == sum(byte[0..6])` | ~0.4 % of rows | **18 / 18 rows** |

Every field lands exactly on its documented set: the four speed gears, the five
timer steps, three modes. The capture sequence also decodes as a coherent
session — two Power presses, a four-step Speed cycle, a three-step Mode cycle,
an Oscillation toggle, a five-step Timer cycle — matching how the buttons were
actually pressed.

**Cross-check:** the same key produces pure noise on the older 23-payload
capture (byte-value distribution indistinguishable from random, no checksum
hits). That capture is from a different bond era — which independently confirms
the key is bond-specific and that this key belongs to the *current* bond.

**Byte [0] is the pressed button, bytes [1..6] are the complete resulting target
state.** The remote sends the full state, not just a key ID — consistent with
its LEDs mirroring fan state.

### Byte [5] is the `roll_angle` slot, not padding

The payload mirrors the UART state layout field for field:

```
UART state:  data9   data10  data11  data12       data13      data14-15
             power   speed   mode    roll_enable  roll_angle  power_delay

Beacon:      [1]     [2]     [3]     [4]          [5]         [6]
             power   speed   mode    oscillation  ——          timer
```

Five of six fields line up exactly, and the gap falls precisely on
`roll_angle` — so byte [5] is the angle field by position, not spare padding.
The same order appears in the cloud JSON (`power, mode, speed, roll_enable,
roll_angle, power_delay`).

It stays `0x00` because **this remote cannot control or display the angle**: the
DM-FCB01 has four buttons, Head-shaking is a plain on/off toggle with no
long-press function, and there is no angle indicator. The remote never learns
the value, so it leaves the field empty — a fan-side or app-side sender would
presumably populate it.

Practical consequence: nothing to implement. Sending `0x00` as an angle would
be wrong, and the field carries no information from this remote. The angle
remains settable from HA, which already works.

> 🔒 The `ble_key` value is a **per-device secret** and is deliberately not
> committed here. It is read from the fan's NVS (`nvs` partition at `0x9000`,
> key `ble_key`, 8-byte blob). Each user must supply their own; see the NVS
> section below for how to extract it.

**Implementation note:** ESP-IDF ships mbedTLS, which provides DES
(`mbedtls/des.h`: `mbedtls_des_setkey_dec` + `mbedtls_des_crypt_ecb`). Verify
`MBEDTLS_DES_C` is enabled in the build — DES is deprecated and some configs
disable it by default.

This supersedes the learn-table plan: decryption is exact, works for any state
without teach-in, and needs no per-user capture session.

### UART forward to MCU — resource `0x1F41` (EXPERIMENTAL, unconfirmed)

In the original firmware the ESP forwards the beacon to the MCU. **If the MCU is
the side that decrypts the payload** — plausible, since the original ESP module
was only the radio bridge — this makes the fan react to the remote again without
breaking the cipher or building a learn table.

**Corrected format (2026-08-01)** — now follows the same envelope as every other
ESP→MCU frame:

```
ESP→MCU (action:2):
FA CE | 00 11 | 02 | 1F 41 | [msg_counter 4B BE] | 00 | 08 | [8B payload] | chk
└magic┘ └len ┘  cmd  └res─┘                        pad  len

  len   = 0x11 = 17 payload bytes (cmd 1 + res 2 + counter 4 + pad 1 +
                                   data_len 1 + data 8)
  f[12] = 0x08  — bytes following, matches "data_length:8" in the original log

Example: FA CE 00 11 02 1F 41 00 00 00 05 00 08 92 43 A0 3F A6 59 2F AA D4

MCU→ESP (action:82): FA CE 00 0A 82 1F 41 ... 01 [chk]   (ACK, value 0x01)
```

> **Previous format was wrong.** It used a single-byte beacon counter directly
> after the resource, where the envelope expects a 4-byte message counter plus
> pad and data_len — the MCU would have parsed garbage. Verified against the
> confirmed rule that `f[12]` counts the bytes after it (holds for the
> `send_cmd_byte_` frame: `data_len=3`, and for the `0x1F44` pair: `data_len=1`).

### ✅ Frame format CONFIRMED on hardware (2026-08-01)

```
[I] DM remote beacon 4B:F2:7E:47:E5:6E proto=0x0201 ctr=137 status=0x02
    payload[8]=[ 8B E0 00 F1 62 32 D5 66 ]
[I] BLE→MCU report (0x1F41, beacon ctr=137, msg ctr=13) — EXPERIMENTAL
[I] MCU ACKed BLE report (action:82 res:0x1F41, len=10)
```

The MCU parses the corrected frame and answers `action:82 res:0x1F41 len=10` —
exactly the length seen in the original firmware. No reset, no timeout. The
envelope (4-byte msg counter + pad + `data_len=8`) is therefore **correct**, and
the beacon's own counter is **not** required in the frame.

### ❗ But the fan does not react — the ESP is the decrypting side

Despite the valid ACK the fan performs no action. The decisive clue is *where
the key lives*: `ble_key` / `ble_mac` / `ble_model` sit in the **ESP module's
NVS**, not the MCU's. So in the original architecture the **ESP decrypts** the
beacon and forwards a *decoded* command; the MCU never sees ciphertext.

That matches the observation exactly: our frame is structurally valid (→ ACK),
but the 8 forwarded bytes are meaningless to the MCU (→ no action).

**Consequences:**
- Forwarding the raw payload cannot work, however correct the envelope is.
- Either the payload is **decrypted** before forwarding, or the learn-table
  route is used and we set the state ourselves via the normal `0x2347` commands
  (which already work reliably).
- The learn table stays viable and needs no crypto at all.
- Decryption is testable offline: the 8-byte `ble_key` from the paired NVS dump
  against captured 8-byte payloads. An 8-byte key with an 8-byte block points at
  a 64-bit block cipher (DES / TEA family) or a vendor scheme. Plausible
  plaintext is easy to recognise — a decoded state should carry power 0/1,
  speed 1–100, mode 0–2, angle ∈ {30,60,90,120,140}.

> ⚠️ `BLE->mcu report timeout!` after 2 failures triggers `SW_CPU_RESET`
> (0x238D). Still gated behind `ble_report_to_mcu: true`, **off by default** —
> the ACK proves the format, not that forwarding is useful.

### Full resource-ID map (from flash-firmware analysis)

| Dec | Hex | Action | Meaning |
|-----|-----|--------|---------|
| 113 | 0x0071 | 81/84 | Heartbeat / version report to cloud |
| 121 | 0x0079 | 84 | Status report |
| 127 | 0x007F | 1 | Device-info (comm/rf/mcu version + signal) |
| 2000 | 0x07D0 | 1 | Boot device-announce (MAC, SSID, model) |
| 2004 | 0x07D4 | 2/4 | Get/Set property |
| 8001 | 0x1F41 | 2/82 | **BLE remote beacon / pairing** |
| 8004 | 0x1F44 | 1/81 | **Remote-pairing trigger** (fan: Head-shaking + Timer) |
| 9002 | 0x232A | 2/82 | Boot state request |
| 9013 | 0x2335 | 4 | Set command (alternative path) |
| 9031 | 0x2347 | 84/82 | Fan state push / WiFi response |
| 9101 | 0x238D | 1/81 | Reset command |

### State-frame field order — JSON vs binary ⚠️

The cloud JSON template declares fields as:
`deviceException, useException, power, mode, speed, roll_enable, roll_angle,
power_delay, sound, light, child_lock, temperature, humidity, ext1..ext6`.

This is the **JSON serialization order, NOT the binary push-frame (0x2347) byte
order.** The push-frame binary layout is empirically confirmed as
`power, speed, mode, …` (see the RX table above — POWER@18, SPEED@19, MODE@20).
Do **not** reorder the RX offsets to match the JSON order; speed and mode would
swap. `roll_enable` / `roll_control` / `roll_angle` are three separate properties
and map to our `OSC_ONOFF` (0x03) / `ROTATE` (0x05) / `OSC_ANGLE` (0x04).
`ext1..ext6` = fragmented static product_id, irrelevant.

### Cloud endpoints (for firewall blocking, original firmware only)

```
cloud1.dm-maker.com   TCP cloud link
api2.dm-maker.com     OTA firmware server
```

Irrelevant for ESPHome — the ESP no longer talks to the cloud.

### Phase 3 — buttons over GATT (open)

Flash-dump analysis (2026-05-23) showed the remote uses **standard BLE bonding,
not custom crypto** — the "beaconkey" is just the BLE LTK, handled natively by
the ESP-IDF stack. So no manual decryption is needed. Two paths:

- **Path A (recommended): re-bond** the remote with the ESPHome ESP32 (Just
  Works). No NVS manipulation. Requires the ESP32 to act in the correct GATT role.
- **Path B: import the original LTK** — impossible: all NVS dumps were taken
  **unpaired** (`ble_model=0`, `ble_key` zeroed), so no LTK exists to import.

### GATT table — confirmed (2026-06-10)

Full GATT enumeration of remote `4B:F2:7E:47:E5:6E`:

| Service UUID | Start | End | Notes |
|---|---|---|---|
| `0x1800` | `0x01` | `0x09` | GAP (Generic Access) |
| `0x1801` | `0x0C` | `0x0F` | GATT (Generic Attribute) |
| `0x00FF` | `0x10` | `0x18` | **DM proprietary — button service** |

**Characteristics of service `0x00FF`:**

| Char UUID | Handle | Properties | Decoded |
|---|---|---|---|
| `0xFF01` | `0x12` | `0x16` | READ · WRITE_NR · **NOTIFY** |
| `0xFF02` | `0x16` | `0x1A` | READ · WRITE · **NOTIFY** |

(Property bits: 0x02=READ, 0x04=WRITE_NR, 0x08=WRITE, 0x10=NOTIFY.)
GAP service `0x1800` has the standard `0x2A00`(name)/`0x2A01`/`0x2A02`/`0x2A04`.
MTU negotiated: 23 (default).

Both `0xFF01` and `0xFF02` support NOTIFY → the remote **pushes button events as
GATT notifications** on this service. The ESP32 is the **GATT client** (central),
the remote is the **GATT server** (peripheral). This sets the Phase 3 role:
**ESP32 connects, subscribes to notify on 0xFF01/0xFF02, decodes the payload.**

**First characteristic value captured** (20 bytes, during the discovery read —
exact trigger not yet isolated):
```
29 b0 c3 6e 91 97 b4 71 00 01 03 01 00 03 29 b0 c3 6e 91 97
└──── 6 bytes ────┘                       └──── repeat ─────┘
```
The 6-byte block `29 b0 c3 6e 91 97` appears at offset 0 and again at offset 14;
the middle is `b4 71 00 01 03 01 00 03`. Not the remote's own MAC
(`4B F2 7E 47 E5 6E`) — likely a session/pairing token + a small command tuple.
**Needs button-by-button capture** (clean notify, see below) to map bytes→actions.

> ⚠️ The `ble_client` **text_sensor** platform crashes the ESPHome↔HA API when a
> characteristic value is non-UTF-8 binary (`TextSensorStateResponse: String
> field had bad UTF-8`), looping disconnect/reconnect. Do **not** use text_sensor
> to read these. Phase 3 native code must register for notify and log/handle the
> raw bytes directly (never publish them as a string).

Open items before Path A can be coded:

1. ✅ **GATT role + UUIDs** — done: ESP32=client, notify on 0xFF01/0xFF02.
2. **Per-button capture** — subscribe to notify, press each button, log hex,
   map payload → fan action. (Native notify handler, not text_sensor.)
3. **Pairing/bonding** — whether notify works unbonded (as in this capture) or
   the remote later requires bonding; which button combo triggers pairing.

### GATT handshake — SOLVED (2026-06-10)

The bind handshake is a **plain challenge-echo**, no AES crypto:

1. Remote → ESP **FF01 NOTIFY/READ** (20 bytes):
   `TokenA(6) | Mid(8) | TokenA(6)`
   Example: `29 b0 c3 6e 91 97 b4 71 00 01 03 01 00 03 29 b0 c3 6e 91 97`

2. ESP → Remote **FF02 WRITE** (same 20 bytes verbatim — echo the challenge).

3. Remote stops blinking. Remote → ESP **FF01 NOTIFY** (bind-confirmed state, 20 bytes):
   `TokenB(6) | Mid2(8) | TokenB(6)`
   Example: `0D 0A 40 15 DC 7C 45 43 00 01 03 01 00 03 0D 0A 40 15 DC 7C`

After step 3, the remote is bound and streams button events as further
notifications on FF01 (and possibly FF02).

### ⭐ Re-reading 2026-08-03 — the handshake is `8 | 6 | 6`, not `6 | 8 | 6`

The grouping above was wrong. Regrouped, the constant part becomes contiguous
and the tail is exactly a repeat of the head:

```
                  [0..7]  8 bytes          [8..13] 6 bytes    [14..19] 6 bytes
step 1  29 B0 C3 6E 91 97 B4 71   |   00 01 03 01 00 03   |   29 B0 C3 6E 91 97
step 3  0D 0A 40 15 DC 7C 45 43   |   00 01 03 01 00 03   |   0D 0A 40 15 DC 7C
                variable                   CONSTANT              = bytes [0..5]
```

- middle 6 bytes are **identical** in both messages → fixed metadata
- trailing 6 bytes are **exactly** `[0..5]` in both → integrity repeat
- leading **8 bytes** are variable — and 8 bytes is exactly the `ble_key` size

**Hypothesis: the first 8 bytes of the bind message ARE the DES key.** That
would make the bind a plain key handout, matching the fact that the key is not
derivable (below) and never touches the cloud.

**Decisive experiment:** pair a fan running original firmware while capturing
FF01, then dump NVS and compare `ble_key` against the leading 8 bytes. If they
match, ESPHome can learn the key during its own bind and no NVS dump is ever
needed — which removes the main obstacle for other users.

### Structure CONFIRMED on a second remote (2026-08-10)

A second, previously unpaired remote (`84:0A:10:78:19:33`) was connected with
ESPHome acting as GATT client. Its FF01 value:

```
                  [0..7]  8 bytes          [8..13] 6 bytes    [14..19] 6 bytes
remote #2  FC 55 40 41 68 7C 7F 5F   |   00 01 03 01 00 03   |   FC 55 40 41 68 7C
```

The middle six bytes are **byte-identical** to both messages captured from
remote #1, and the tail again repeats bytes `[0..5]`. So the `8|6|6` grouping is
confirmed across two independent devices, and those six constant bytes are
protocol metadata rather than anything device-specific.

Also confirmed by this run: **ESPHome can connect to the remote as a GATT
client** (service discovery completes, notify registration on FF01 succeeds, and
writes to FF02 are accepted). The earlier "Cannot poll, not connected" was only
a wrong MAC in the config — every remote has its own address.

Note the FF01 value repeated unchanged 60 s later, i.e. it is a stable
characteristic value rather than a per-connection random challenge. That fits
the older capture, where the value differed *before* and *after* a successful
bind.

### FF01 never notifies - it is read-only in practice (2026-08-10)

Registering for notifications on FF01 succeeds (`Register for notify on 0xFF01
complete`), but **nothing is ever pushed** - not on connect, not on Power+M, not
at any point during a multi-minute connection. With polling disabled
(`update_interval: never`) the characteristic produced no data at all.

Every value we have ever seen from FF01 therefore came from a **read**, not a
notification. The characteristic advertises the NOTIFY property but does not use
it, at least not in the states we could reach.

Consequence: an ESPHome-side bind cannot be driven by "wait for the challenge,
answer immediately". There is no challenge push to react to.

### ESPHome cannot complete the bind by echoing (2026-08-10)

Echoing the FF01 value back to FF02 does **not** bind the remote. After the
echo it keeps sending idle heartbeats only - no `status=0x02` command beacons
ever appear. The bind stays incomplete, and the FF01 value stays unchanged
across polls, whereas the older capture showed it differing before and after a
successful bind.

The likely reason is the manual's step 3: the original flow is completed by
**pressing a key on the fan**, and there is no equivalent when ESPHome is the
peer. The plain echo is evidently a handshake step, not the bind itself.

### ❌ SETTLED: FF01 is NOT the key (2026-08-10)

Remote `84:0A:10:78:19:33` was paired with an original-firmware fan and its
command beacons captured — 19 payloads, 18 distinct. Every candidate derivable
from the GATT data was tested against them:

- all thirteen 8-byte windows of the 20-byte FF01 message
- FF01 reversed, and truncated/zero-padded variants
- the remote's MAC, reversed and zero-padded both ways
- MD5 / SHA1 / SHA256 of FF01, of FF01[0:8], and of the MAC
- the known `ble_key` of the *other* remote, as a control

**Best result: 1 of 19 checksums — exactly random expectation** (1/256 per
payload over 19 payloads ≈ 0.07 expected hits; one hit is unremarkable). By
comparison, the correct key scored 18/18 on the earlier capture.

**Conclusion: the DES key is generated during pairing and exists only in the
fan's NVS.** It is not derivable from anything the remote exposes over GATT, not
from its address, and not from the cloud. Reading it requires a fan still
running original firmware.

This closes the question that gated moving the feature to `main`. The NVS-dump
prerequisite is real and unavoidable with everything known so far.

### FF01 is readable without pairing - but it is not the key

The decisive observation from this run: ESPHome connected to a completely
unpaired remote and read FF01 with **no authentication and no bond**. If those
leading 8 bytes are the DES key, no bind is needed at all - a device could
simply connect once, read FF01, and have the key. That would remove the NVS-dump
prerequisite entirely.

**Decisive experiment**, now well defined because the pre-bind value is on
record:

1. Pair remote `84:0A:10:78:19:33` with a fan running original firmware
2. Dump that fan's NVS
3. Compare `ble_key` against `FC 55 40 41 68 7C 7F 5F`

A match proves the shortcut. No match proves the key is generated during
pairing, which settles the question the other way and leaves the NVS route as
the only path - either outcome is worth having.

### Where the key does NOT come from

Two negative results, both useful:

**Not from the cloud.** A `dmiot2mqtt` capture taken *during* an active remote
pairing shows only `resource_id:127` heartbeats and one `9031` state push — no
pairing message, no key exchange. Pairing is **purely local** between remote and
ESP module.

**Not derivable from known identifiers.** Several hundred candidate derivations
were tested offline against the known (`ble_mac`, `ble_key`) pair — direct byte
slices, MD5/SHA1/SHA256/SHA512 (head and tail truncation), XOR combinations, and
DES of each value keyed with the others, over remote MAC, fan MAC, `product_id`,
`device_id`, `device_key` and `ble_model`. **No match.** The key is therefore
generated at pairing time, not computed from device identity.

**Phase 3 implementation** in `dm_fan.h`:
- On connect: read FF01 → store 20-byte challenge.
- On FF01 NOTIFY (first, 20-byte): write the same bytes back to FF02 (WRITE).
- On subsequent FF01/FF02 NOTIFY: decode payload → fan action.

**Per-button capture table** — superseded by the DES decryption above; the
button is byte [0] of the decrypted payload, no capture table needed.
The remote (DM-FCB01) has only 4 buttons; M has a short/long press → 5 actions:

| Action | Button + press | FF01 payload (hex) | FF02 payload (hex) |
|--------|----------------|-------------------|-------------------|
| Power On/Off | ⏻ short | | |
| Speed cycle | M short | | |
| Mode cycle (Direct/Natural/Smart) | M long | | |
| Oscillation On/Off | ∿ short | | |
| Timer cycle (0/1/2/3/4h) | 🕐 short | | |

Note: the remote has no dedicated Speed+/Speed−, no angle, no Sound/LED/Child-lock
buttons — those fan properties are only reachable over UART from HA, not the
remote. The remote cycles speed and timer; it cannot set an absolute value.

Context: DreamMaker is a Tuya OEM (Tuya BLE remote protocol). The original
architecture was `remote --BLE--> Tuya ESP module --FACE 0x1F41--> fan MCU`.
We replaced the Tuya module with ESPHome. The handshake turned out to be a
**plain echo** (no AES), not the encrypted Tuya bind we feared. Once the
per-button table is complete, dm_fan.h Phase 3 can be implemented.

**Revised path forward (2026-07-31 — supersedes the GATT-only plan above):**

- ✅ **Beacon transport is the primary route, NOT GATT.** The earlier "beacon
  ruled out" note was based on an *unbound* remote and is retracted — see the
  23-payload capture above. Buttons are broadcast as `status=0x02`
  advertisements with an 8-byte encrypted payload.
- ✅ **No crypto break required** — the state→payload mapping is deterministic,
  so a learn-table matches payloads without decryption.
- 🔄 **Bonding still matters**, but only to make the remote *emit* `status=0x02`
  beacons at all: an unbound remote sends idle heartbeats only. The fan-side
  pairing trigger is `0x1F44` (Head-shaking + Timer); the remote-side combo is
  Power + M.
- ❓ **GATT challenge-echo** (FF01→FF02, documented above) still works and stops
  the blinking, but produced **no** button notifications. It is most likely part
  of the *bind* protocol, not the command path. Keep it for binding; stop
  expecting button data from it.
- ⏸️ **SWD dump of the remote (DA14580)** is now lower priority. It was also
  attempted and failed: ST-Link reports `chipid: 0x000` even with
  `--connect-under-reset` → debug port appears locked (a wiring fault was not
  fully excluded).

### Bond state in NVS — where the fan stores its remote binding

A **genuinely paired** original fan (`mcu_version: fan_0002`) was dumped and its
NVS decoded. Relevant keys:

| NVS key | Type | Content |
|---------|------|---------|
| `ble_model` | u16 LE | `0x0201` = **513** — non-zero means *paired* (`0` = unpaired) |
| `ble_mac` | blob 6 B | the bound remote's MAC — matched `4B:F2:7E:47:E5:6E` exactly |
| `ble_key` | blob 8 B | the bond / beacon key (**value kept out of this repo**, see note) |

> **NVS blob gotcha:** for `type=0x41` (blob) with `span=2`, the actual blob
> bytes are **not** in the entry's 8-byte data field (that holds
> `[size_lo, size_hi, chunk_idx, reserved, crc32(4B)]`) but at the **start of the
> next 32-byte block**. NVS is wear-levelled, so several copies of each key exist —
> use the one with a real CRC, not `ffffffff`.

> 🔒 The concrete `ble_key` / `device_key` / WiFi credentials from that dump are
> **deliberately not committed** to this public repo. They are per-device secrets.
> Keep them in local notes only.

Boot log confirms the state in cleartext: `DM-LOG: BLE:paired model:513`
(format string `BLE:paired model:%d` at `0x0046B2` in the firmware image). The
marker appears **only on cold boot**, never during normal operation.

Four earlier reference dumps (all MAC `98:F4:AB:24:FF:F8`) had `ble_model=0` and
empty `ble_key`/`ble_mac` — **that fan was never successfully paired**, across
March 2025 → May 2026. This retroactively explains why the fake-MCU testbench
built on that image never triggered any BLE reaction.

**Crypto assessment:** the firmware uses **standard ESP-IDF Bluedroid** bonding —
strings `btc_ble_storage`, `btm_ble_set_encryption`, `btm_ble_ltk_request_reply`
are stock stack functions, no proprietary AES/XXTEA. Combined with the
deterministic beacon mapping, this means the remaining unknown is the beacon
cipher, which the learn-table approach makes unnecessary to solve.

---

## Fan MCU debug interface (SWD)

The fan PCB (label **ZMZFS01 20200525**) has a 5-pad SWD header near the ESP32:

| Pad | Function |
|-----|----------|
| GND | Ground |
| RESET | MCU NRST |
| SWCLK | SWD clock |
| SWDIO | SWD data |
| VDDS | 3.3 V supply — **do not connect** if board is already powered |

**Wiring to ST-Link V2:** SWCLK→SWCLK, SWDIO→SWDIO, GND→GND, NRST→RESET (optional).

The fan MCU is an ARM Cortex-M device (exact part TBD). Connecting an ST-Link V2
would allow:
- Full firmware dump of the fan MCU (if readout-protection is not set)
- Verification/correction of the FA CE binary protocol from the MCU side
- Debugging MCU–ESP interactions in real time

This is lower priority than Phase 3 (BLE buttons) but useful for confirming
the UART protocol details (0x1F41 frame format, boot sequence).

---

## Remote debug interface (SWD + UART) — DA1458x

The remote PCB (label **Remote Control V1.0 / 2020-02-26**) exposes a full debug
header. The presence of a **1.5 V supply pad** confirms a **Dialog DA14580/DA14585**
in single-cell boost mode (U2 = BLE SoC, Y2 = 16 MHz crystal, U1 = external SPI flash).

| Pad | Function | Pad | Function |
|-----|----------|-----|----------|
| TX  | UART TX  | SWC | SWD clock |
| RX  | UART RX  | RST | Reset |
| GND | Ground   | SWD | SWD data |
| 1.5V| Supply (do not connect) | SDIO | (SD/aux) |
|     |          | SCLK | (SD/aux) |

**ST-Link V2 wiring:** SWC→SWCLK, SWD→SWDIO, GND→GND, RST→NRST (optional).
Leave 1.5V open (battery powers the board).

**Dumping the remote firmware is the most direct route to Phase 3** — the dump
reveals the GATT characteristic UUIDs, the button command bytes, and the pairing
logic in cleartext, far more reliable than live GATT discovery.

- **Tool:** Dialog/Renesas **SmartSnippets Toolbox** (free) — native DA1458x
  support, reads OTP and external SPI flash over SWD.
- ⚠️ DA1458x OTP may have a read-protection bit. **Read only first**, never write.
- The DA14580 boots from OTP or external SPI flash (U1) — that flash holds the
  application image with the BLE GATT table and button-to-command mapping.

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

### External validation

Independent app-based TX captures by a community member (BobeOlsen, in
`dhewg/esphome-miot#50`) confirm this resource mapping 1:1, including the exact
angle bytes (`0x1E/3C/78/8C`) and timer values (`0x3C/78/B4/F0`).

> ⚠️ An older comment of ours in that thread speculated the WiFi-query response
> was `action:81 / resource:0x70`. That was never verified and is **wrong** — the
> confirmed answer is `action:82 / resource:0x78` (echoes the *query* resource).
> A correction note in that thread is still outstanding.
