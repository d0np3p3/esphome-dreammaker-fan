# Correction note for `dhewg/esphome-miot#50`

Ready to paste. Corrects our own earlier comment (19 May) which guessed
`action:81 / resource:0x70` for the WiFi-query response. That was never
verified and is wrong.

Post as a **new comment** — don't edit the old one away. Leaving the wrong
guess visible with a correction below it is more useful to readers than a
silent edit.

---

## Text to post

Correction to my earlier comment in this thread — the `0x70` response I posted
was **speculation read out of the firmware binary**, never verified against a
real capture. It is wrong, and I'd rather flag it than leave people building on
it.

**Wrong (my earlier guess):**
```
FA CE 00 09 81 00 70 02 00 00 00 00 00 C4
             ^^ action 81      ^^ resource 0x70
```

**Correct, confirmed from live UART captures** (original firmware running
against a simulated MCU on a test bench):

```
action: 0x82
resource: 0x0078      <- echoes the QUERY resource, there is no 0x70
```

The MCU polls with `action:2, resource:0x0078` roughly every 60 s, and the ESP
answers `action:0x82` on the **same** resource. The response payload is 56 bytes:

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
| 32 | 16 | 12-hex-digit device ID, ASCII (device-specific) |
| 48 | 8 | status bytes, meaning still unclear |

Without a response the MCU pulls the ESP's EN pin low and reboots it after
about four minutes, so this one matters if you're writing your own firmware.

This is also consistent with @BobeOlsen's independent app captures in this
thread — those matched our resource mapping 1:1, including the exact angle
bytes (`0x1E/3C/78/8C` = 30/60/120/140°) and timer values
(`0x3C/78/B4/F0` = 1–4 h).

Sorry for the noise. Full protocol notes here:
https://github.com/d0np3p3/esphome-dreammaker-fan/blob/main/PROTOCOL.md

---

## Optional addition — only if you want to mention the remote

The BLE remote work is on a beta branch and needs a per-device key, so it may
be more noise than help for that thread. Include only if it seems welcome:

> Unrelated but possibly interesting for this thread: the original DM-FCB01
> remote turned out to broadcast each button press as a BLE advertisement
> (company ID `0x4D44`), with an 8-byte payload encrypted using **single DES in
> ECB mode**. The key is stored per device in the fan's NVS as `ble_key`.
> Decrypted, the payload holds the pressed button, the complete target state
> and a checksum — so the remote can keep working after flashing ESPHome,
> without re-pairing. Details:
> https://github.com/d0np3p3/esphome-dreammaker-fan/blob/v4.0.0-beta/PROTOCOL.md
