#!/usr/bin/env python3
"""Decrypt DM-FCB01 command beacons with a candidate ble_key.

    python3 tools/beacon_decrypt.py "<ble_key>" "<payload>" ["<payload>" ...]

Payloads are the 8 bytes at offset 10-17 of the manufacturer data, taken from
beacons with status=0x02 (offset 9). The key is a per-device secret: pass it on
the command line, never commit it.

The checksum test is the one every result in PROTOCOL.md rests on: with the
right key all payloads satisfy byte[7] == sum(byte[0..6]) & 0xFF (18/18 on the
reference capture); with a wrong key about 1 in 256 does.

Needs pycryptodome.
"""

import sys

from Crypto.Cipher import DES

BUTTONS = {0xF1: "Timer", 0xF2: "Oscillation", 0xF3: "Speed", 0xF4: "Power", 0xF5: "Mode"}
MODES = {0: "direct", 1: "natural", 2: "smart"}


def h(s):
    return bytes.fromhex(s.replace(" ", "").replace(":", ""))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    key = h(sys.argv[1])
    if len(key) != 8:
        sys.exit(f"ble_key must be 8 bytes, got {len(key)}")

    cipher = DES.new(key, DES.MODE_ECB)
    ok = 0
    payloads = sys.argv[2:]
    for raw in payloads:
        ct = h(raw)
        if len(ct) != 8:
            print(f"{raw}: skipped, not 8 bytes")
            continue
        pt = cipher.decrypt(ct)
        valid = pt[7] == sum(pt[:7]) & 0xFF
        ok += valid
        fields = (
            f"{BUTTONS.get(pt[0], f'?{pt[0]:02X}'):<11} power={pt[1]} speed={pt[2]:>3} "
            f"mode={MODES.get(pt[3], pt[3])} osc={pt[4]} angle={pt[5]} timer={pt[6]}min"
        )
        print(f"{ct.hex(' ').upper()} -> {pt.hex(' ').upper()}  {'OK ' if valid else 'BAD'}  {fields}")

    print(f"\nchecksum {ok}/{len(payloads)}", end="  ")
    if ok == len(payloads):
        print("- this is the key these beacons were encrypted with")
    elif ok <= max(1, len(payloads) // 50):
        print("- random level, wrong key")
    else:
        print("- mixed: check for corrupted frames or payloads from another remote")


if __name__ == "__main__":
    main()
