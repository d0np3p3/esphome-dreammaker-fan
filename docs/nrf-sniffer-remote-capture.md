# Capturing the remote with the nRF52840 sniffer

Companion to [`nrf-sniffer-bind-capture.md`](nrf-sniffer-bind-capture.md). That
document covers the *bind* exchange; this one covers recording the remote itself
— what the first trace actually contained, why it contained nothing usable, and
what the next run has to do differently.

## Re-reading `remote.pcap` (2026-09-03, 27.5 s, 6830 frames)

The trace is good. The first analysis of it was not, on two counts.

**The captured device is remote #2, not a fan.** `84:0A:10:78:19:33` is the
remote we have been working with since 2026-08-10 (PROTOCOL.md, "Structure
CONFIRMED on a second remote"; `bind_capture.yaml` points its `ble_client` at
exactly this address). Company ID `0x4D44` identifies *DreamMaker*, not the fan
— and the fan's own ESP module does not advertise like this. So there is no
"fan broadcasts its state without a connection" hypothesis to test: what was
recorded is the remote doing what PROTOCOL.md already describes.

**Offset 8 is the sequence counter, not a status byte.** The 18-byte layout is
documented (PROTOCOL.md, "Advertisement manufacturer data"):

```
Offset:  0  1  2  3  4  5  6  7   8    9   10 .. 17
        02 01 84 0A 10 78 19 33  28   01   00 00 00 00 00 00 00 00
        └ver┘ └──── own MAC ────┘ │    │   └──────── payload ────────┘
                                  │    └── STATUS: 01 = idle, 02 = command
                                  └── sequence counter (per event)
```

So the observed `0x28 → 0x29 → 0x2A` is the counter running 40 → 41 → 42. The
byte the first analysis read as constant — offset 9 — is the status, and it
stayed `0x01` for all 6830 frames.

### What follows from that

**The trace contains no button press at all.** Not one frame has `status=0x02`,
and bytes 10–17 are zero for exactly that reason: PROTOCOL.md's table says the
payload is all-zero whenever `status=0x01`. Nothing was missed by the sniffer;
there was nothing to miss.

**The remote was unbound while it was recorded.** This is the 2026-06-01
behaviour, on record in PROTOCOL.md's own correction note: an *unbound* remote
keeps emitting idle heartbeats and produces a zero payload even when buttons are
pressed. A press on an unbound remote bumps the counter and nothing else —
which is precisely the event at t=12.13 s. The correlation with a button press
was real; the conclusion drawn from it was not.

**The absence of GATT traffic is expected, not a failure.** Button commands
travel over advertisements, never over GATT (PROTOCOL.md, correction of
2026-07-31). The only GATT conversation this remote ever has is the bind, and
the bind was not completed inside the capture window.

### ⚠️ Was *Power + M* pressed during this session?

It matters, and the answer is not in the pcap. Per the manual (PROTOCOL.md,
"Pairing / bind procedure"), reset and pairing are the same action: *Power + M*
clears the remote's existing bind in one step, whether or not a fan completes
the pairing afterwards. An unbound remote #2 is consistent with that having
happened.

If it did, remote #2 is no longer bound to fan #3, and the `ble_key` in fan #3's
NVS no longer describes any future traffic from that remote.

**Dump fan #3's NVS anyway, now.** The key remains the ground truth for the
19-payload corpus captured on 2026-08-10 while the two were still bound, it is
the only known-good (capture, key) pair this project has, and the dump costs
nothing:

```bash
esptool.py --port COMx read_flash 0x9000 0x4000 nvs_backup.bin
```

### Cross-check against the v4 branch

The handoff asked whether `ble_model = 513` fits this 18-byte structure. It
does: `0x0201` is the NVS `ble_model` sanity value (README, "Sanity check"), and
`02 01` is exactly what sits at offset 0–1 of every advertisement. PROTOCOL.md
labels those two bytes "protocol version 2.1"; they carry the same value the fan
stores as `ble_model`. Worth noting as a correspondence — it has not been
verified that a device with a different `ble_model` advertises different bytes.

---

## Order of operations

One step here is irreversible, so the sequence is not free:

1. **Dump fan #3's NVS.** Before anything else, and regardless of what happened
   above. Flashing that fan destroys the key permanently.
2. **Capture A** — command beacons, only if remote #2 is still bound to fan #3.
3. **Capture B** — the bind exchange, using fan #4 as partner.

Capture B necessarily presses *Power + M*, which ends any bind Capture A relies
on. B after A, never the other way round.

---

## Capture A — a labelled button corpus

Only worth running while the remote is bound to a fan. If it emits nothing but
`status=0x01`, it is unbound and this capture cannot produce commands — go to
Capture B.

What is new here versus the 2026-08-03 corpus: that one was decoded, but the
button labels were inferred from the decrypted state struct. A timed press log
gives ground truth for every field at once.

1. Start the sniffer, capture unfiltered (see *Bench setup* below).
2. Press, ~10 s apart, noting the wall-clock time of each:
   Power on · M ×4 (speed 1→2→3→4) · M long ×3 (Direct → Natural → Smart) ·
   Head-shaking on · Head-shaking off · Timer ×4 (1h→2h→3h→4h) · Power off.
3. **Do not press Power + M.** That is the bind reset.
4. Stop, then decrypt the `status=0x02` payloads with the dumped `ble_key` and
   lay the plaintexts against the press log.

Expected per PROTOCOL.md: a full 8-byte target-state struct per press, checksum
`byte[7] == sum(byte[0..6]) & 0xFF`, and identical ciphertext whenever the same
target state recurs (DES-ECB, no rolling code).

## Capture B — the bind exchange

The procedure is in [`nrf-sniffer-bind-capture.md`](nrf-sniffer-bind-capture.md)
and is unchanged. Two additions from this trace:

- **Pair against fan #4**, the spare on original firmware. Fan #3 stays untouched
  until its NVS is safely dumped.
- **Afterwards, dump fan #4's NVS too.** That yields the key generated by the
  very bind you just recorded — the decisive comparison, with both sides of it
  captured in one session rather than reconstructed from two.

---

## Bench setup

**Take the ESPHome nodes off the remote first.** A held GATT connection stops
the remote advertising entirely (TODO.md, "Verbunden = kein Advertising"), so a
running `bind_capture.yaml` node will silence exactly what you are trying to
record. The trace shows `98:F4:AB:3C:9D:D6` (Espressif) scanning the remote —
that is almost certainly ours. Power it down for the capture.

**Move the dongle to the remote.** ~25 corrupted frames per 882 clean ones is a
high error rate, and the APsystems EZ1 inverter (`80:64:6F:55:D5:5E`) in the
same room is the likely cause. The remote came in at −75 to −86 dBm; a few
centimetres of separation buys more than any filtering does.

**Do not use `--follow`.** Broken in ble-sniffer 0.20.0 — the target device is
not captured. Record everything, filter afterwards.

**Never run a terminal sniff and Wireshark on COM5 at the same time.**

```powershell
nrfutil ble-sniffer sniff --port COM5 --output-pcap-file remote-buttons.pcap
```

Toolchain setup (nrfutil install, firmware flashing, the Wireshark extcap shim)
is in the handoff notes; the working configuration is nrfutil on COM5 reporting
as *nRF Sniffer for Bluetooth LE*.

---

## Validating frames

The first analysis proposed discarding any manufacturer-data value seen only
once. That rule would have thrown away the counter's every legitimate step —
each new counter value is unique by definition. Filter structurally instead:

- offset 2–7 must equal the frame's own advertising address
- offset 9 must be `0x01` or `0x02`
- offset 10–17 must be all-zero whenever offset 9 is `0x01`
- the AD structure must be length `0x15` (18 data bytes + 2 company ID + 1 type)
- the counter must not run backwards

That last test, not the frequency, is what settles the single `0x2A` frame at
t=18.03 s: `0x29` frames continue after it, and the counter never decrements, so
that frame is corrupt. Right call, wrong reason — and the reason matters,
because on the next trace the frequency rule would silently delete real presses.

## tshark filters for the real structure

```powershell
# every frame from the remote
tshark -r trace.pcap -Y "btle.advertising_address == 84:0a:10:78:19:33"

# manufacturer data as a time series (counter/status/payload are inside `data`)
tshark -r trace.pcap -T fields -e frame.time_relative -e btcommon.eir_ad.entry.data `
  -Y "btle.advertising_address == 84:0a:10:78:19:33 && btcommon.eir_ad.entry.company_id == 0x4d44"

# button presses only — status byte at offset 9
tshark -r trace.pcap -T fields -e frame.time_relative -e btcommon.eir_ad.entry.data `
  -Y "btcommon.eir_ad.entry.company_id == 0x4d44 && btcommon.eir_ad.entry.data[9] == 02"

# the bind: connection requests, then GATT writes
tshark -r trace.pcap -Y "btle.advertising_header.pdu_type == 5" `
  -T fields -e frame.time_relative -e btle.initiator_address -e btle.advertising_address
tshark -r trace.pcap -Y "btatt.opcode == 0x52 || btatt.opcode == 0x12" `
  -T fields -e frame.time_relative -e btatt.handle -e btatt.value
```

The offset-9 slice assumes `btcommon.eir_ad.entry.data` starts *after* the
company ID, which is how the first trace came out (18 bytes, beginning `02 01`).
Confirm it on the first frame of a new capture; if your build includes the
company ID in that field, shift the index by 2.
