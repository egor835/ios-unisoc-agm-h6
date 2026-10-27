#!/usr/bin/env python3
"""Extract the payload of an unencrypted, uncompressed IM4P (e.g. DeviceTree).

  im4p-extract.py IN.im4p OUT

IM4P ::= SEQUENCE { IA5String "IM4P", IA5String type, IA5String description,
                    OCTET STRING payload, ... (KBAG / compression info) }
Refuses anything with a KBAG (encrypted) or a compressed payload.
"""
import sys


def der(buf, off):
    tag = buf[off]
    off += 1
    n = buf[off]
    off += 1
    if n & 0x80:
        k = n & 0x7f
        n = int.from_bytes(buf[off:off + k], "big")
        off += k
    return tag, buf[off:off + n], off + n


def main():
    blob = open(sys.argv[1], "rb").read()
    tag, seq, _ = der(blob, 0)
    if tag != 0x30:
        sys.exit("not a DER SEQUENCE")
    items, off = [], 0
    while off < len(seq):
        t, v, off = der(seq, off)
        items.append((t, v))
    if items[0] != (0x16, b"IM4P"):
        sys.exit("not an IM4P")
    kind, desc, payload = items[1][1].decode(), items[2][1].decode(), items[3][1]
    if len(items) > 4:
        sys.exit("extra fields after the payload (KBAG/compression): refusing")
    if payload[:4] in (b"bvx2", b"bvx1", b"bvxn") or payload[:8] == b"complzss":
        sys.exit("compressed payload: refusing")
    open(sys.argv[2], "wb").write(payload)
    print("%s (%s): %d bytes -> %s" % (kind, desc, len(payload), sys.argv[2]))


if __name__ == "__main__":
    main()
