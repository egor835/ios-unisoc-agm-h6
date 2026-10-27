#!/usr/bin/env python3
"""Drop files into the iOS root of the AGM H6 from Android (adb, Magisk root).

usage: agm-drop.py LOCAL=/ios/path[:MODE[:UID[:GID]]] ...   (MODE octal, default 755; owner default
       root:wheel, needs a kernel with patch 0989; GID defaults to UID)
       agm-drop.py --clear                         (zero the drop header: nothing is dropped)

The drop is written into the partition iosroot at 32 GiB (block 8388608 of 4096 bytes), after
the 16 GiB iOS volume and far before the console log at the partition's end; the kernel's
bsd/dev/agm_dropbox.c (patch 0970) writes the files into place at the next iOS boot, once per
generation (the time of this run). Checks before writing: iosroot is /dev/block/by-name/iosroot,
128 GiB exactly; the drop fits in 1 GiB. Afterwards the blocks are read back and compared.
"""
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import time

PART = "/dev/block/by-name/iosroot"
PART_BYTES = 128 << 30
OFF = 32 << 30
BLOCK = 4096
MAX = 1 << 30
ENTRIES = 31


def fnv(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
    return h


def adb(*args, check=True):
    r = subprocess.run(["adb", "shell", *args], capture_output=True, text=True, timeout=600)
    if check and r.returncode != 0:
        sys.exit("adb shell %s: %s%s" % (" ".join(args), r.stdout, r.stderr))
    return r.stdout.strip()


def su(cmd):
    return adb("su", "-c", cmd)


def build(specs):
    entries, blobs = [], []
    at = BLOCK
    for spec in specs:
        local, _, dest = spec.partition("=")
        dest, _, rest = dest.partition(":")
        mode, _, owner = rest.partition(":")
        mode = int(mode or "755", 8)
        flags = 0
        if owner:
            uid, _, gid = owner.partition(":")
            uid = int(uid)
            gid = int(gid) if gid else uid
            flags = (1 << 31) | (gid & 0x7fff) << 16 | (uid & 0xffff)
        data = open(local, "rb").read()
        if not dest.startswith("/") or len(dest.encode()) >= 96:
            sys.exit("bad path: %s" % dest)
        entries.append(struct.pack("<96sIIQQQ", dest.encode(), mode, flags, at, len(data), fnv(data)))
        pad = (-len(data)) % BLOCK
        blobs.append(data + b"\0" * pad)
        print("  %-40s %9d bytes  %o" % (dest, len(data), mode))
        at += len(data) + pad
    if len(entries) > ENTRIES or at > MAX:
        sys.exit("too much: %d files, %d bytes" % (len(entries), at))
    gen = int(time.time())
    header = b"AGMDROP1" + struct.pack("<IIQ", len(entries), 1, gen) + b"\0" * 40 + b"".join(entries)
    header += b"\0" * (BLOCK - len(header))
    return header + b"".join(blobs), gen


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if sys.argv[1] == "--clear":
        image, gen = b"\0" * BLOCK, 0
    else:
        image, gen = build(sys.argv[1:])
    size = su("blockdev --getsize64 %s" % PART)
    if int(size) != PART_BYTES:
        sys.exit("iosroot is %s bytes, not %d: not writing" % (size, PART_BYTES))
    digest = hashlib.sha256(image).hexdigest()
    with tempfile.NamedTemporaryFile(suffix=".drop") as f:
        f.write(image)
        f.flush()
        subprocess.run(["adb", "push", f.name, "/data/local/tmp/agm.drop"], check=True, capture_output=True)
    if su("sha256sum /data/local/tmp/agm.drop").split()[0] != digest:
        sys.exit("pushed copy differs")
    blocks = len(image) // BLOCK
    su("dd if=/data/local/tmp/agm.drop of=%s bs=%d seek=%d count=%d conv=notrunc,fsync 2>&1" %
       (PART, BLOCK, OFF // BLOCK, blocks))
    back = su("dd if=%s bs=%d skip=%d count=%d 2>/dev/null | sha256sum" % (PART, BLOCK, OFF // BLOCK, blocks))
    if back.split()[0] != digest:
        sys.exit("read back differs!")
    print("drop generation %d: %d bytes at iosroot+32 GiB, verified" % (gen, len(image)))


if __name__ == "__main__":
    main()
