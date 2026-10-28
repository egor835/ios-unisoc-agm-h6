#!/usr/bin/env python3
"""Make the timesync file a .logarchive needs, from the headers of its tracev3 files.

  mktimesync.py ARCHIVE.logarchive

logd on the AGM H6 never writes /var/db/diagnostics/timesync, and without it `log show` drops every
entry. Each tracev3 file starts with a header chunk (tag 0x1000) holding the mach timebase, a
continuous time and the wall clock (seconds) at that moment, and the boot UUID (subchunk 0x6102)
(layout as in mandiant/macos-UnifiedLogs, src/header.rs). Per boot: one boot record (0xbbb0) and one
timesync record per tracev3 file ("Ts"), written to ARCHIVE/timesync/0000000000000000.timesync.
"""
import glob
import os
import struct
import sys
import uuid


def header(path):
    with open(path, "rb") as f:
        d = f.read(0x1000)
    if len(d) < 0x100:
        return None
    tag, sub, size = struct.unpack_from("<IIQ", d, 0)
    if tag != 0x1000:
        return None
    numer, denom, cont, wall_s, _unk, bias_min, dst, _flags = struct.unpack_from("<IIQQIiII", d, 16)
    # subchunks follow: walk them for 0x6102 (boot uuid)
    off = 16 + 40
    boot = None
    while off + 8 <= 16 + size:
        st, ssz = struct.unpack_from("<II", d, off)
        if st == 0x6102:
            boot = uuid.UUID(bytes=d[off + 8:off + 24])
            break
        if st not in (0x6100, 0x6101, 0x6103) or ssz == 0:
            break
        off += 8 + ssz
    return boot, numer, denom, cont, wall_s, bias_min, dst


def main():
    arch = sys.argv[1]
    boots = {}
    for p in sorted(glob.glob(os.path.join(arch, "*", "*.tracev3")) + glob.glob(os.path.join(arch, "*.tracev3"))):
        h = header(p)
        if h is None or h[0] is None:
            print("skip", p)
            continue
        boot, numer, denom, cont, wall_s, bias, dst = h
        boots.setdefault(boot, []).append((cont, wall_s, numer, denom, bias, dst))
    out = bytearray()
    for boot, recs in boots.items():
        recs.sort()
        cont, wall_s, numer, denom, bias, dst = recs[0]
        ns = cont * numer // denom
        boot_time = wall_s * 1000000000 - ns
        out += struct.pack("<HHI16sIIqiI", 0xbbb0, 48, 0, boot.bytes, numer, denom, boot_time, bias, dst)
        for cont, wall_s, numer, denom, bias, dst in recs:
            out += struct.pack("<IIQqiI", 0x207354, 0, cont, wall_s * 1000000000, bias, dst)
        print("boot %s: %d tracev3, timebase %d/%d, boot at %d" % (boot, len(recs), numer, denom, boot_time // 10**9))
    os.makedirs(os.path.join(arch, "timesync"), exist_ok=True)
    with open(os.path.join(arch, "timesync", "0000000000000000.timesync"), "wb") as f:
        f.write(out)


if __name__ == "__main__":
    main()
