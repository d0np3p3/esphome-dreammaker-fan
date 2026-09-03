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
stay on original firmware for now.

**That makes the bind capture more important, not less.** An earlier draft of
this document postponed it for want of a key to compare against — that was the
wrong reading of what the capture is for. The goal is not to *extract* a key
value from the trace. It is to learn **the message sequence of the bind**: which
handles, which direction, and what the confirming key press on the fan puts on
the wire.

That sequence is the one thing blocking an ESPHome-side bind. Everything else
already works: ESPHome connects to the remote as GATT client, discovers
services, registers notify, and writes to FF02 — all accepted (PROTOCOL.md,
2026-08-10). It is the *same role* the fan's original module plays. What is
missing is step 3 of the manual's flow, "press any key on the fan", for which
there is no known wire equivalent (TODO.md, "Ein ESPHome-seitiger Bind ist nicht
möglich"). A sniffer trace of a real bind shows exactly that step.

**And if ESPHome can complete the bind, the key question may dissolve.** The key
is generated during pairing. If the *fan* side generates it and hands it over,
then an ESPHome fan performing the same bind generates its own — no dump, ever.
If the *remote* supplies it, ESPHome reads it during the bind — also no dump.
Only a two-sided derivation from exchanged material would leave real work. The
capture decides which, and in two of three cases the NVS prerequisite is gone.

Evidence that something is written *into* the remote during the bind: its FF01
value differs before and after (PROTOCOL.md, `29 B0 …` → `0D 0A …`).

### Check this before touching any hardware

Remote #1's `ble_key` is known — it is the working setup — and its **post-bind**
FF01 is already written down. The two were never compared, because the 2026-08-10
refutation used remote #2, whose post-bind value was never read:

```
remote #1 post-bind FF01[0:8]   0D 0A 40 15 DC 7C 45 43
   (nrf-sniffer-bind-capture.md transcribes this as  0D 0A 40 15 5D C7 C4 54 —
    the two documents disagree after byte 4; test against both)
compare against                 dm_ble_key from your secrets.yaml
```

A match means the key is simply readable from the remote after a bind, and the
whole NVS prerequisite disappears without any capture at all. Costs ten seconds.
Do it first.

### A ciphertext corpus is the fallback, not the plan

If the bind turns out to hand the key over, none of this is needed — decryption
works and the table is redundant again. It earns its place only in the third
case above, where the key is derived two-sidedly and stays out of reach.

The learn table was retired on 2026-08-03 because DES decryption made it
unnecessary (TODO.md, *Eingestellt*). Without key access that reasoning no
longer holds, and the table comes back as a path — with one advantage it did not
have before: the payload's behaviour is now fully understood.

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

Worth the minute it takes, because it tells you what Capture B is starting from
and whether Capture A is possible at all. The last trace suggests the answer is
no. Capture 60 s, press *Power* once in the middle, and look at offset 9:

- **any frame with `status=0x02`** → still bound. Capture A is available, and
  it is worth taking *before* B, since B's *Power + M* ends this bind.
- **only `status=0x01`** → unbound. Skip A and go straight to Capture B; there
  is nothing left to spend.

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

## Capture B — the bind exchange

**This is the priority capture.** Procedure as written in
[`nrf-sniffer-bind-capture.md`](nrf-sniffer-bind-capture.md); what changes is
what you are looking for.

Spending the bind costs nothing right now. Its only value was the `ble_key` in
fan #3's NVS, which is unreachable — an unspendable asset. And the last trace
suggests the remote may already be unbound, in which case there is nothing to
spend.

Sniffer running **before** the first button press — the `CONNECT_IND` must be in
the trace or the sniffer cannot follow the connection and everything after it is
lost.

1. **Fan:** *Head-shaking + Timer* → 4 LEDs flash
2. **Remote:** *Power + M* → 8 LEDs flash
3. **Press any key on the fan** → confirmation tone

### What to extract, in order

1. **Step 3 on the wire.** The confirming press is the missing piece. Which
   side writes, to which handle, with what value, between the tone and the
   preceding traffic? This is what an ESPHome bind has to reproduce.
2. **Direction of the handout.** Does the fan write 8 bytes to the remote, or
   does the remote hand them up? That decides whether ESPHome can pick its own
   key or has to read one.
3. **FF01 before and after.** Confirm the value changes across this bind too,
   and record the post-bind value — the one quantity remote #2 has never had
   measured.
4. **Any 8-byte value anywhere.** The key is 8 bytes; if it crosses the link at
   all, it looks like this.
5. **SMP packets.** If pairing is present, Wireshark derives the LTK from a
   Legacy Pairing exchange and decrypts the rest — the DA14580 is BLE 4.0/4.1
   and cannot do LE Secure Connections.

The bind handshake is a plain challenge-echo with no crypto of its own
(PROTOCOL.md, SOLVED 2026-06-10), so if a key crosses the link it is not hidden
by application-layer encryption.

### Afterwards

Re-bind the remote to the fan the normal way and re-read FF01 with
`bind_capture.yaml` — a post-bind value for remote #2 is worth having on record
regardless of how the trace reads, and needs no sniffer.

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
