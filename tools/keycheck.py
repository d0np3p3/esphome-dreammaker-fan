#!/usr/bin/env python3
"""Test a known ble_key against everything the remote exposes over GATT.

The key is a per-device secret and must never be committed. Pass it on the
command line or via DM_BLE_KEY; this script only reads it.

    python3 tools/keycheck.py "00 11 22 33 44 55 66 77"   # your key, not this one
    DM_BLE_KEY="..." python3 tools/keycheck.py

Two questions are asked of it:

  1. Is the key *contained* in any recorded FF01 message, in any plausible
     transcription? (windows, reversed, nibble-shifted, byte-swapped, hashed)
  2. Is the key *derivable* from the bind's two tokens — the FF01 value before
     the bind and after it? (byte ops, hashes, DES both directions)

Run against remote #1 on 2026-09-03 both came back negative, which is what
closed the "is the post-bind FF01 the key" question. Re-run it whenever a new
(capture, key) pair appears — a bind captured with the resulting NVS dump is
the case worth feeding in.

Needs pycryptodome for the DES pass; skipped automatically if absent.
"""

import hashlib
import itertools
import os
import sys

try:
    from Crypto.Cipher import DES
except ImportError:
    DES = None


def h(s):
    return bytes.fromhex(s.replace(" ", "").replace(":", ""))


# Recorded FF01 values. See PROTOCOL.md, "GATT handshake" and "Structure
# CONFIRMED on a second remote".
MESSAGES = {
    "r1 pre-bind": h("29 B0 C3 6E 91 97 B4 71 00 01 03 01 00 03 29 B0 C3 6E 91 97"),
    # The two documents disagree after byte 4; both are internally consistent.
    "r1 post-bind (PROTOCOL.md)": h("0D 0A 40 15 DC 7C 45 43 00 01 03 01 00 03 0D 0A 40 15 DC 7C"),
    "r1 post-bind (bind-capture.md)": h("0D 0A 40 15 5D C7 C4 54 00 01 03 01 00 03 0D 0A 40 15 5D C7"),
    "r2 unpaired": h("FC 55 40 41 68 7C 7F 5F 00 01 03 01 00 03 FC 55 40 41 68 7C"),
}
MAC_R1 = h("4B F2 7E 47 E5 6E")
MID = h("00 01 03 01 00 03")  # constant across both remotes
HASHES = ("md5", "sha1", "sha256")


def nibble_shift(data, n):
    bits = "".join(f"{b:02x}" for b in data)[n:]
    return bytes.fromhex(bits[: len(bits) - len(bits) % 2])


def containment(key):
    """Is the key sitting somewhere in a recorded message?"""
    hits = []
    for name, msg in MESSAGES.items():
        views = {
            "": msg,
            " reversed": msg[::-1],
            " byteswapped": bytes(
                b for i in range(0, len(msg) - 1, 2) for b in (msg[i + 1], msg[i])
            ),
            **{f" nibbleshift{n}": nibble_shift(msg, n) for n in (1, 2, 3)},
        }
        for label, view in views.items():
            for i in range(max(0, len(view) - 7)):
                if view[i : i + 8] == key:
                    hits.append(f"{name}{label} window@{i}")

        for const in range(256):
            if bytes(b ^ const for b in msg[:8]) == key:
                hits.append(f"{name} head8 XOR {const:02X}")

        for label, part in (("full", msg), ("head8", msg[:8]), ("mid", msg[8:14])):
            for algo in HASHES:
                digest = hashlib.new(algo, part).digest()
                if key in (digest[:8], digest[-8:]):
                    hits.append(f"{algo}({name}/{label})")

        # Weaker signal, but a shared run would be worth knowing about.
        for n in (4, 5, 6):
            for i in range(len(key) - n + 1):
                if key[i : i + n] in msg:
                    hits.append(f"{name} shares {n}-byte run {key[i:i + n].hex(' ').upper()}")
    return hits


def derivation(key):
    """Is the key computed from the bind's before/after tokens?"""
    tokens = {name: msg[:8] for name, msg in MESSAGES.items() if name.startswith("r1")}
    tokens.update({f"{n} reversed": t[::-1] for n, t in list(tokens.items())})
    hits = []

    for (na, a), (nb, b) in itertools.permutations(tokens.items(), 2):
        for label, fn in (
            ("XOR", lambda x, y: x ^ y),
            ("ADD", lambda x, y: (x + y) & 0xFF),
            ("SUB", lambda x, y: (x - y) & 0xFF),
        ):
            if bytes(fn(x, y) for x, y in zip(a, b)) == key:
                hits.append(f"{label} {na} / {nb}")

    parts = dict(tokens, MID=MID, MAC=MAC_R1)
    for size in (1, 2, 3):
        for combo in itertools.permutations(parts.items(), size):
            data = b"".join(v for _, v in combo)
            label = " | ".join(k for k, _ in combo)
            for algo in HASHES:
                digest = hashlib.new(algo, data).digest()
                for window, offset in ((digest[:8], "[:8]"), (digest[-8:], "[-8:]"), (digest[8:16], "[8:16]")):
                    if window == key:
                        hits.append(f"{algo}({label}){offset}")

    if DES is not None:
        for nk, k in tokens.items():
            for nd, d in list(tokens.items()) + [("MID padded", MID + b"\x00\x00")]:
                if nk == nd:
                    continue
                cipher = DES.new(k, DES.MODE_ECB)
                if cipher.encrypt(d) == key:
                    hits.append(f"DES-ENC {nd} under {nk}")
                if cipher.decrypt(d) == key:
                    hits.append(f"DES-DEC {nd} under {nk}")
    return hits


def main():
    raw = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("DM_BLE_KEY", "")
    if not raw:
        sys.exit(__doc__)
    key = h(raw)
    if len(key) != 8:
        sys.exit(f"ble_key must be 8 bytes, got {len(key)}")

    hits = containment(key) + derivation(key)
    if hits:
        print("MATCH FOUND:")
        for hit in hits:
            print(f"  {hit}")
    else:
        print("No match. The key is not present in, nor derivable from, any")
        print("recorded FF01 value — consistent with it being generated during")
        print("the bind and stored only in the fan's NVS.")
    if DES is None:
        print("\n(pycryptodome missing — DES pass skipped)")


if __name__ == "__main__":
    main()
