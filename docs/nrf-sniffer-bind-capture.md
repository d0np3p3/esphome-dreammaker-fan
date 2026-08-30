# Capturing the pairing exchange with an nRF52840 sniffer

The one open question in this project: **where does the DES `ble_key` come from?**
Everything reachable without a sniffer has been ruled out (see PROTOCOL.md).
What has never been observed is the actual bind conversation between the remote
and the fan's original ESP module — and that is where the key must be
established.

## Why this should work

| | |
|---|---|
| Remote chip | **DA14580** — Bluetooth 4.0/4.1 |
| Consequence | LE Secure Connections arrived in 4.2, so this can only do **Legacy Pairing** |
| Meaning | Wireshark can derive the keys and decrypt the link, provided the capture starts **before** the connection and catches the full pairing exchange |

With LE Secure Connections a sniffer would be helpless. Legacy Pairing is
sniffable. That is the whole reason this is worth doing.

There is also a good chance decryption is not even needed: our own ESP32 read
FF01 in cleartext from an unpaired remote without any bond, so at least part of
this service is unencrypted.

## The fleet, as of 2026-08-30

| Fan | Firmware | Remote | Role in this test |
|---|---|---|---|
| #1 | ESPHome | remote #1, `ble_key` known | working setup - leave alone |
| #2 | ESPHome v3.1.0 | none | overwintering |
| #3 | **original** | remote #2 (`84:0A:10:78:19:33`) | **NVS holds remote #2's key - dump it** |
| #4 | **original** | none | **pairing partner for the capture** |

Two things follow from this:

**Dump fan #3's NVS while you still can.** It holds the `ble_key` for remote #2,
generated when they were paired. Flash that fan and the key is gone for good -
and with it any chance of using remote #2 with ESPHome.

```bash
esptool.py --port COMx read_flash 0x9000 0x4000 nvs_backup.bin
```

**That dump also makes this test decisive rather than exploratory.** With a known
(capture, `ble_key`) pair you are no longer guessing what to look for: you scan
the capture for those exact 8 bytes. Either they appear - and you know where the
key comes from - or they do not, and the key is derived locally from something
that was exchanged.

Use fan #4 as the pairing partner so fan #3's key stays intact until it is
safely dumped.

## What you need

- **nRF52840 dongle** with *nRF Sniffer for Bluetooth LE* firmware
- **Wireshark** with the nRF Sniffer plugin
- The **remote** (`84:0A:10:78:19:33` or whichever you use)
- A fan running **original firmware** — the ESPHome fan cannot pair, so it
  cannot be the partner here
- Optional but valuable: the ability to dump that fan's NVS afterwards

## Procedure

1. **Start the sniffer first.** In Wireshark, select the remote's MAC in the
   nRF Sniffer toolbar and begin capturing **before touching any button**. The
   pairing window is about 15 s and the `CONNECT_IND` must be in the capture or
   the sniffer cannot follow the connection.

2. **Fan:** hold *Head-shaking + Timer* → 4 LEDs flash

3. **Remote:** press *Power + M* → 8 LEDs flash

4. **Press any key on the fan** → confirmation tone, bind complete

5. Stop the capture.

6. **If possible:** dump the fan's NVS and extract `ble_key`.
   ```
   esptool.py --port COMx read_flash 0x9000 0x4000 nvs_backup.bin
   ```
   This gives the ground truth to compare against.

## What to look for

**First check the capture contains `CONNECT_IND`.** Without it the sniffer
never followed the connection and the rest is missing.

Then, in order of interest:

1. **GATT writes to `0xFF02`** — what does the *fan* write there? Our echo of
   the FF01 value was wrong; the real value is what matters.
2. **GATT reads/notifies on `0xFF01`** — does the value differ before and after
   the bind, as the old capture suggested?
3. **Any 8-byte value** anywhere in the exchange. The key is 8 bytes; if it is
   transmitted at all, it will look like this.
4. **SMP packets** (pairing request/response, confirm, random). If present,
   Wireshark can derive the LTK and decrypt everything that follows.

## The decisive comparison

If the NVS dump succeeds, compare `ble_key` against every 8-byte value in the
capture. A match identifies where the key comes from, and whether a device can
obtain it without an NVS dump.

If no value matches, the key is derived locally on both sides from something
exchanged — then the derivation is the remaining puzzle, and the captured
inputs are what feed it.

## Known values for cross-reference

```
remote #2  84:0A:10:78:19:33
  FF01 while unpaired:  FC 55 40 41 68 7C 7F 5F 00 01 03 01 00 03 FC 55 40 41 68 7C
                        [--------- 8 --------] [------ 6 -----] [------ 6 ------]
                         variable               constant across   = bytes [0..5]
                                                both remotes

remote #1  4B:F2:7E:47:E5:6E
  FF01 before bind:     29 B0 C3 6E 91 97 B4 71 | 00 01 03 01 00 03 | 29 B0 C3 6E 91 97
  FF01 after bind:      0D 0A 40 15 5D C7 C4 54 | 00 01 03 01 00 03 | 0D 0A 40 15 5D C7
```

Note the value **changes across a successful bind** — so whatever the bind
writes, it lands in this characteristic.

## Already ruled out — do not spend time re-testing

- `ble_key` is **not** any 8-byte window of the FF01 message (all 13 tested)
- **not** the remote's MAC in any padding/ordering
- **not** MD5/SHA1/SHA256 of FF01, FF01[0:8], or the MAC
- **not** derivable from `product_id`, `device_id`, `device_key` or `ble_model`
  (several hundred derivations tested)
- **not** issued by the cloud — a capture during an active pairing shows only
  heartbeats, no key exchange

Method used for all of the above: decrypt captured command beacons with the
candidate and check `byte[7] == sum(byte[0..6]) & 0xFF`. The correct key scores
18/18; everything else scored at random level. Reuse that test — it is decisive
and takes seconds.
