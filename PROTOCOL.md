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

The remote has only **4 physical buttons** → button-capture set is 5 actions:

| Button | Icon | Short press | Long press |
|--------|------|-------------|------------|
| Power | ⏻ | On/Off | — |
| Air Volume / Mode | M | Speed gear cycle 1→2→3→4 | Mode: Direct → Natural → Smart |
| Head-shaking | ∿ | Oscillation On/Off | — |
| Timed Shutdown | 🕐 | Timer cycle 0h→1h→2h→3h→4h | — |

Remote indicators: 4 mode LEDs (Direct/Natural/Smart) + Bluetooth LED + a 1/2/3/4
air-volume/timer indicator. All **8 LEDs flash during pairing**.

### Pairing / bind procedure (user manual — CRITICAL for capture) ⚠️

**Original pairing (remote ↔ fan, mediated by the fan's BLE module):**
1. On the **fan**: hold *Head-shaking + Timer* together → top 4 fan LEDs flash =
   fan Bluetooth reset, fan enters pairing-wait state.
2. On the **remote**: press *Power + M* together → all 8 remote LEDs flash =
   remote enters pairing state.
3. Press any key on the fan → bind; a confirmation tone means the bind succeeded.
4. No fan action within 15 s after bind → pairing exits, remote stops flashing.

**Bluetooth reset / unbind:**
- On the **remote**: press *Power + M* together → 8 LEDs flash → previous bind is
  **cleared**, remote returns to fresh pairing mode.
- On the **fan**: hold *Head-shaking + Timer* → top 4 LEDs flash → fan unbinds.

> **Implication for our capture:** the remote likely only *streams button events
> to its currently-bound peer*. Our echo stops the blinking (bind accepted at the
> app layer) but the remote may still consider the **original Tuya module** its
> bound peer, so no notifications reach the ESP32. **Before capturing, reset the
> remote with Power + M** so it enters fresh pairing mode and binds to the ESP32.
> This is the leading hypothesis for why the echo succeeds but no button
> notifications follow — test it before assuming SMP is the blocker.

### Advertisement manufacturer data

After the 2-byte company ID (which `esp32_ble_tracker` strips into the
ServiceData UUID), the manufacturer data is:

| Offset | Bytes | Field | Notes |
|--------|-------|-------|-------|
| 0–1 | `02 01` | Protocol version | observed 2.1 |
| 2–7 | 6 | Device MAC | BLE byte order |
| 8 | 1 | **Sequence counter** | increments ~every 20 s / on activity |
| 9 | 1 | Status | `0x01` = idle |
| 10–17 | 8 | Payload | all-zero when idle; button data when active |

Captured 2026-06-01 (idle): `4B:F2:7E:47:E5:6E`, company `DM`,
`02 01 4B F2 7E 47 E5 6E 0B 01 00 00 00 00 00 00 00 00`.

The `dm_fan` component decodes this when `ble_remote: true` and logs every
beacon. Changed counter/status/payload → `INFO` (button event), repeated idle
heartbeat → `DEBUG`.

**Confirmed 2026-06-10 on hardware:** the payload `[10..17]` stays all-zero even
during button presses / pairing-mode rapid advertising. The advertisement is a
pure *heartbeat* — **button commands do NOT travel over advertisements, they go
over a GATT connection** (the remote is `connectable: true`). Phase 3 (GATT) is
required for button reception; see below.

### UART forward to MCU — resource `0x1F41` (EXPERIMENTAL, unconfirmed)

In the original firmware the ESP forwards the beacon to the MCU:

```
ESP→MCU (action:2): FA CE 00 0C 02 1F 41 [counter] [8-byte payload] [chk]
MCU→ESP (action:82): FA CE 00 0A 82 1F 41 ... 01 [chk]   (ACK, value 0x01)
```

> ⚠️ Frame length/format reverse-engineered, **not yet confirmed on hardware**.
> `BLE->mcu report timeout!` after 2 failures triggers `SW_CPU_RESET` (0x238D).
> Gated behind `ble_report_to_mcu: true`, **off by default**.

### Full resource-ID map (from flash-firmware analysis)

| Dec | Hex | Action | Meaning |
|-----|-----|--------|---------|
| 113 | 0x0071 | 81/84 | Heartbeat / version report to cloud |
| 121 | 0x0079 | 84 | Status report |
| 127 | 0x007F | 1 | Device-info (comm/rf/mcu version + signal) |
| 2000 | 0x07D0 | 1 | Boot device-announce (MAC, SSID, model) |
| 2004 | 0x07D4 | 2/4 | Get/Set property |
| 8001 | 0x1F41 | 2/82 | **BLE remote beacon / pairing** |
| 8004 | 0x1F44 | 1/81 | Provisioning start (WiFi reset) |
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

### GATT table — confirmed (2026-06-10, `ble_discovery.yaml`)

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

**Phase 3 implementation** in `dm_fan.h`:
- On connect: read FF01 → store 20-byte challenge.
- On FF01 NOTIFY (first, 20-byte): write the same bytes back to FF02 (WRITE).
- On subsequent FF01/FF02 NOTIFY: decode payload → fan action.

**Per-button capture table** (fill in during the current ble_capture session).
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

**Revised path forward (replaces the optimistic "no crypto" note above):**
- *Cheap test first:* `ble_capture.yaml` now has HS1–HS4 handshake buttons.
  Press them while connected and watch the remote LED — if any write stops the
  blinking and unlocks notifications, the handshake is trivial and we win.
- *If HS1–HS4 fail:* the bind requires the Tuya key-exchange. The most reliable
  route is the **SWD dump of the remote (DA14580)** — it contains the GATT
  pairing logic and the bind-key derivation in cleartext. Dumping the **original
  ESP module firmware** is the alternative (it held the Tuya BLE SDK + any stored
  bind key; note NVS showed `ble_model=0` / `ble_key` zeroed = the remote was
  never bound to *this* fan, so the key must be derived during a fresh local
  pairing, not imported).
- Beacon transport is ruled out: the advertisement payload `[10..17]` stays
  all-zero during button presses (confirmed), and the DA14580 (BLE 4.x) does not
  use extended advertising — so buttons do not travel over the beacon.

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
