#!/usr/bin/env python3
"""Show (read-only) a GPT from dumps of the first and last blocks of a disk.

  gptshow.py HEAD.bin TAIL.bin DISK_BLOCKS [BLOCK_SIZE]

HEAD.bin: first blocks (protective MBR, primary header at LBA 1, entries);
TAIL.bin: last blocks (backup entries, backup header at the last LBA).
Checks both headers' CRCs and the entry array CRC, lists the partitions.
"""
import struct
import sys
import uuid
import zlib


def header(blob, off):
    h = blob[off:off + 92]
    (sig, rev, hsize, hcrc, _r, my_lba, alt_lba, first_use, last_use, disk_guid,
     ent_lba, n_ent, ent_size, ent_crc) = struct.unpack("<8sIIIIQQQQ16sQIII", h)
    if sig != b"EFI PART":
        sys.exit("no GPT signature at offset %#x" % off)
    hb = bytearray(blob[off:off + hsize])
    hb[16:20] = b"\0\0\0\0"
    ok = zlib.crc32(bytes(hb)) & 0xffffffff == hcrc
    return dict(my_lba=my_lba, alt_lba=alt_lba, first_use=first_use, last_use=last_use,
                guid=uuid.UUID(bytes_le=disk_guid), ent_lba=ent_lba, n_ent=n_ent,
                ent_size=ent_size, ent_crc=ent_crc, crc_ok=ok)


def entries(blob, off, h):
    raw = blob[off:off + h["n_ent"] * h["ent_size"]]
    ok = zlib.crc32(raw) & 0xffffffff == h["ent_crc"]
    out = []
    for i in range(h["n_ent"]):
        e = raw[i * h["ent_size"]:(i + 1) * h["ent_size"]]
        if e[:16] == b"\0" * 16:
            continue
        first, last, attrs = struct.unpack_from("<QQQ", e, 32)
        name = e[56:128].decode("utf-16-le").rstrip("\0")
        out.append((i, name, first, last, attrs, uuid.UUID(bytes_le=e[:16])))
    return out, ok


if __name__ == "__main__":
    head, tail = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
    nblocks = int(sys.argv[3])
    bs = int(sys.argv[4]) if len(sys.argv) > 4 else 4096
    p = header(head, bs)
    print("primary: crc_ok=%s my=%d alt=%d usable=%d..%d entries@%d n=%d size=%d" %
          (p["crc_ok"], p["my_lba"], p["alt_lba"], p["first_use"], p["last_use"],
           p["ent_lba"], p["n_ent"], p["ent_size"]))
    ents, eok = entries(head, p["ent_lba"] * bs, p)
    print("primary entries crc_ok=%s" % eok)
    tail_first = nblocks - len(tail) // bs
    b = header(tail, (p["alt_lba"] - tail_first) * bs)
    print("backup:  crc_ok=%s my=%d alt=%d entries@%d" % (b["crc_ok"], b["my_lba"], b["alt_lba"], b["ent_lba"]))
    bents, bok = entries(tail, (b["ent_lba"] - tail_first) * bs, b)
    print("backup entries crc_ok=%s identical=%s" % (bok, bents == ents))
    for i, name, first, last, attrs, tguid in sorted(ents, key=lambda x: x[2]):
        print("%3d %-20s %10d..%-10d %9.1f MiB attrs=%#x" % (i, name, first, last, (last - first + 1) * bs / 2**20, attrs))
    last_end = max(e[3] for e in ents)
    print("free after last partition: %d blocks (%.1f MiB)" % (p["last_use"] - last_end, (p["last_use"] - last_end) * bs / 2**20))
