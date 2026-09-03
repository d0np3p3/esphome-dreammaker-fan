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

This is now the **first thing the next capture answers**, and it costs one
button press — see *Step 0* below.

### Cross-check against the v4 branch

The handoff asked whether `ble_model = 513` fits this 18-byte structure. It
does: `0x0201` is the NVS `ble_model` sanity value (README, "Sanity check"), and
`02 01` is exactly what sits at offset 0–1 of every advertisement. PROTOCOL.md
labels those two bytes "protocol version 2.1"; they carry the same value the fan
stores as `ble_model`. Worth noting as a correspondence — it has not been
verified that a device with a different `ble_model` advertises different bytes.

---

## Without an NVS dump (situation as of 2026-09-03)

The fan's ESP module is not reachable for a flash read, and fan and remote both
stay on original firmware for now. That removes the `ble_key` from the picture
entirely, and it changes what each capture is worth:

| | With NVS dump | Without |
|---|---|---|
| Capture A — button beacons | validates the DES chain end to end | **the only route to usable data**: a labelled ciphertext corpus |
| Capture B — bind exchange | decisive: compare the capture against the real key | **postpone** — no key to compare against, and it costs the bind |

So the order inverts. Capture A is the priority, Capture B is off the table for
now, and the reason is worth being explicit about: *Power + M* clears the bind
between remote #2 and fan #3. Without an NVS dump afterwards, spending that bind
buys a capture nobody can check an answer against. Don't spend it yet.

### Why a ciphertext corpus is still worth having

The learn table was retired on 2026-08-03 because DES decryption made it
unnecessary (TODO.md, *Eingestellt*). Without key access that reasoning no
longer holds, and the table comes back as the only path — with one advantage it
did not have before: the payload's behaviour is now fully understood.

PROTOCOL.md establishes that the payload is deterministic ECB with no rolling
code — the same target state always produces the same eight bytes, confirmed by
four independent repeats. That is exactly the property a match table needs. A
receiver can compare the raw 8-byte payload against stored values and act,
without ever holding the key.

The catch, and it is a real one: the payload encodes the **complete target
state**, not a button ID. So entries are needed per reachable state, not per
button — 23 distinct payloads came out of 27 presses. The table is therefore
worth building against the states actually used, not exhaustively.

This is also the one dataset that is only capturable **right now**, while the
two devices are still paired and on original firmware. The analysis can wait;
the recording cannot.

---

## Step 0 — is the remote still bound? (one button press)

Everything below depends on this, and the last trace suggests the answer may be
no. Capture 60 s, press *Power* once in the middle, and look at offset 9:

- **any frame with `status=0x02`** → still bound. Go to Capture A.
- **only `status=0x01`** → unbound. Capture A cannot produce commands; nothing
  further is recordable until a re-bind, which is Capture B and postponed.

Use the `data[9] == 02` filter below. Do not press anything else.

## Capture A — a labelled button corpus

What is new versus the 2026-08-03 corpus: that one was decoded, but the button
labels were inferred from the decrypted state struct. Here the log *is* the
ground truth, and it needs no key.

Record three things per press, not two — the **resulting fan state** is what
turns a ciphertext list into a table:

| Time | Button | Fan state after (LEDs: speed · mode · osc · timer) |
|---|---|---|

1. Start the sniffer, capture unfiltered (see *Bench setup*).
2. Press ~10 s apart, reading the fan's LEDs after each:
   Power on · M ×4 (speed 1→2→3→4) · M long ×3 (Direct → Natural → Smart) ·
   Head-shaking on · Head-shaking off · Timer ×4 (1h→2h→3h→4h) · Power off.
3. **Do not press Power + M.** That is the bind reset — it ends the pairing this
   capture depends on.
4. Extract the `status=0x02` payloads and pair each with its logged state.

Two checks that work without the key, both from PROTOCOL.md's ECB result:

- **Repeats must be byte-identical.** Drive the fan back to a state you already
  recorded and press again; the payload must match the earlier one exactly. If
  it does not, the no-rolling-code conclusion does not hold for this remote and
  the whole table approach is dead — worth knowing in five minutes rather than
  after a full corpus.
- **Distinct states must give distinct payloads.** A collision would mean the
  payload does not carry the full state after all.

Recovering the key itself from such a corpus is not realistic: single DES is
brute-forceable in principle, but the plaintexts are not known here — only their
structure — and it is not a hobby-scale computation. Treat the table as the
deliverable.

## Capture B — the bind exchange (postponed)

The procedure stays as written in
[`nrf-sniffer-bind-capture.md`](nrf-sniffer-bind-capture.md). Do not run it
under the current constraints: its value is the comparison against a dumped
`ble_key`, and pressing *Power + M* without that dump spends the remaining bind
for an uncheckable result.

When NVS access returns, run it against **fan #4** (the spare on original
firmware) and dump fan #4's NVS immediately afterwards — that yields the key
generated by the very bind just recorded, both sides from one session.

---

## Bench setup

**Take the ESPHome nodes off the remote first.** A held GATT connection stops
the remote advertising entirely (TODO.md, "Verbunden = kein Advertising"), so a
running `bind_capture.yaml` node will silence exactly what you are trying to
record. The trace shows `98:F4:AB:3C:9D:D6` (Espressif) scanning the remote —
that is almost certainly ours. With the BT proxy off (2026-09-03) this should be
settled; if that address still appears in a new trace, something else is up.

**PC Bluetooth and a Logitech Bolt receiver are noise, not a problem.** Neither
will connect to the remote, and both live at their own addresses, so the
advertising-address filter removes them from the analysis. They do share the
2.4 GHz band and add to the error rate — if corrupted frames stay high after
moving the dongle, turn off the PC's Bluetooth for the capture and leave Bolt
running only if the keyboard or mouse is actually needed to drive the capture.

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
