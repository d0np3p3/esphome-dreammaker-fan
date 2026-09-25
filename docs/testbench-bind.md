# Binding the remote against a bench ESP32 running the original firmware

The key question — where does `ble_key` come from? — has always been blocked by
one prerequisite: the only ground truth sits in the NVS of a fan's ESP module,
and reading it means opening a fan that still runs original firmware.

This setup removes that prerequisite. A spare ESP32 dev board takes the place of
the fan's original module. It runs the original firmware, a simulated MCU
triggers pairing over UART, and the remote binds to **the bench board** — whose
flash you can read as often as you like. Every bind yields a matched
(capture, key) pair, repeatably, without touching any fan.

## Why this was never tried

PROTOCOL.md already describes a fake-MCU testbench: original firmware driven
against a simulated MCU. It "never triggered any BLE reaction" — which was
blamed on the image's NVS being unpaired (`ble_model=0`).

The real reason is simpler. At the time, `0x1F44` was documented as "start WiFi
provisioning". It was only identified as the **remote-pairing trigger** on
2026-07-31. The testbench was never sent the one frame that starts a bind.

## Why the answer must be in the air

The remote encrypts its beacons with `ble_key`, so the remote knows the key. The
fan's module stores it in NVS. And the key is fresh per bind (the older
23-payload capture decodes to noise under the current key). So during a bind,
the key is either **transmitted** or **derived on both sides from transmitted
material**. There is no third option — a sniffer trace of the bind contains the
answer, at worst behind SMP encryption, which Wireshark can remove for Legacy
Pairing when the pairing exchange is in the capture.

The firmware carries stock Bluedroid bonding strings (`btm_ble_ltk_request_reply`,
`btc_ble_storage`), so SMP bonding during the bind is likely. Check for it first.

---

## Hardware

| Part | Notes |
|---|---|
| ESP32 dev board | ESP32-WROOM-32(D) class, **4 MB flash** — the partition table ends past 3 MB (`ota_1` at `0x210000`) |
| Simulated MCU | a USB-UART adapter on the board's UART2 (GPIO16 RX / GPIO17 TX), 19200 baud — or the existing testbench |
| Remote | **remote #2** (`84:0A:10:78:19:33`) |
| Sniffer | nRF52840 dongle, setup as in [`nrf-sniffer-remote-capture.md`](nrf-sniffer-remote-capture.md) |
| Flash image | the first-half backup (`0x000000`–`0x200000`): bootloader, partition table, `otadata` and `ota_0` |

**Use remote #2, not remote #1.** Remote #1 drives the working ESPHome fan, and
re-pairing it invalidates the `ble_key` in your `secrets.yaml`.

## Step 1 — check which app actually boots

The first-half backup covers `ota_0` (`0x110000`–`0x210000`) but not `ota_1`.
Flash it, open the serial console, and look for the bootloader line naming the
loaded partition. `0x110000` is `ota_0` — good. If it loads from `0x210000`, the
running image is in the missing second half, and that backup
(TODO.md, *Flash-Backup Hälfte 2*) becomes a prerequisite.

## Step 2 — start with an empty NVS

Erase the NVS partition (`0x9000`, 16 KB) on the bench board before the first
boot. The backup's NVS belongs to a real fan and holds its WiFi credentials and
cloud identity — the bench should not carry them, and it should not reach the
cloud either (keep it off your WiFi, or block `cloud1.dm-maker.com` and
`api2.dm-maker.com`).

Watch the boot log for `dmiot_ble_init`. If the firmware refuses to start the BLE
side without its identity keys, that is a finding in itself; only then consider
restoring an NVS, and strip the credentials from it first.

Never commit any dump from this setup. Everything in it is per-device secret.

## Step 3 — get the exact `0x1F44` frame

The request's data byte is not on record (PROTOCOL.md shows `[data]`). Take it
from the real thing: on fan #1 (ESPHome) set `log_raw_frames: true`, hold
*Head-shaking + Timer*, and copy the logged `FA CE 00 0A 01 1F 44 …` frame.

That is harmless for fan #1: ESPHome keeps `ble_key` in its config, not in
anything the MCU's pairing mode can clear. Do **not** press *Power + M* on
remote #1.

Checksum is the sum of all bytes mod 256, as for every frame.

## Step 4 — the bind, under the sniffer

1. Start the sniffer, unfiltered. It must be recording before step 3, or the
   `CONNECT_IND` is lost and the sniffer cannot follow the connection.
2. Simulated MCU → bench: send the `0x1F44` frame.
   Expect the ACK `FA CE 00 0A 81 1F 44 … 01` and the log line
   `BLE->mcu agree to pair!`.
3. Remote #2: *Power + M* → 8 LEDs flash.
4. The real flow ends with a key press on the fan, which the MCU reports to the
   module. What that report looks like on UART is unknown — **log everything the
   bench sends to the simulated MCU from here on**, and if the bind does not
   complete, re-send `0x1F44` as the most likely candidate.
5. Stop the capture once the remote stops flashing.

**Success looks like:** a cold boot of the bench prints `BLE:paired model:513`,
and remote #2 now emits `status=0x02` beacons.

## Step 5 — read the key and prove it

```bash
esptool.py --port COMx read_flash 0x9000 0x4000 bench_nvs_1.bin
```

Extract `ble_key` as described in README (blob gotcha: the bytes sit at the start
of the following 32-byte block). Then prove it is the live key before trusting
it: press a few buttons on remote #2, take the `status=0x02` payloads from the
sniffer, and decrypt them:

```bash
python3 tools/beacon_decrypt.py "<ble_key>" "<8-byte payload>" ["<payload>" ...]
```

Every payload must decode with a matching checksum and sensible fields. That
checksum test is the one used for every earlier result — 18/18 with the right
key, random level with anything else.

## Step 6 — bind again

Repeat steps 4–5 with remote #2, same bench, fresh capture, `bench_nvs_2.bin`.

| Result | Meaning |
|---|---|
| same key both times | derived from something fixed — the pair's identities, or constants in firmware |
| different key | fresh per bind, and therefore in the air: the two captures show where |

**Compare with the parity bits masked** (`byte & 0xFE`). DES ignores the lowest
bit of every key byte, so two values differing only there are the *same* key —
a firmware that sets parity on one side and not the other would otherwise look
like two different keys. `tools/keycheck.py` and `tools/beacon_decrypt.py`
already treat keys this way.

Either outcome is decisive, and with two matched pairs the capture analysis is a
lookup, not a hunt.

## What to look for in each capture

In order:

1. **SMP packets** (Pairing Request/Response, Confirm, Random). If present,
   decrypt the link in Wireshark before anything else.
2. **Writes by the bench to the remote**, FF02 first — the fan side's real
   writes have never been observed; every FF02 write on record is our own echo.
3. **The key's 8 bytes**, known now from the NVS, anywhere in the trace —
   parity bits masked, as above.
4. **FF01 before and after** the bind, for remote #2's missing post-bind value.
5. **Handles `0x14` and `0x18`.** Service `0x00FF` spans `0x10`–`0x18`, but only
   FF01 (`0x12`) and FF02 (`0x16`) are documented. Probably descriptors — check.

## Afterwards

Remote #2 ends up bound to the bench, not to fan #3. That is acceptable: fan #3's
key is unreachable anyway. To use remote #2 with an ESPHome fan later, the bench
has just handed you its key.

If this works, the same bench can bind any DM-FCB01 — which would turn "dump your
fan's NVS before flashing" into "bind your remote to a bench board once", a far
lower bar for other users. Worth writing up if it holds.
