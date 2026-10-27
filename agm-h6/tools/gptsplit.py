#!/usr/bin/env python3
"""Build new GPT images that cut a partition off the end of the last one.

  gptsplit.py HEAD.bin TAIL.bin DISK_BLOCKS NEW_NAME NEW_BLOCKS OUT_HEAD.bin OUT_TAIL.bin

Only the last partition (by first LBA) shrinks; NEW_NAME takes the freed end,
1 MiB aligned, in the first empty entry slot, with the Apple HFS+ type GUID.
The disk and partition GUIDs of existing entries stay. Both headers and both
entry arrays are rewritten with fresh CRCs. Nothing is written to any device:
the outputs replace the same blocks as the inputs (dd them back).
"""
import struct
import sys
import uuid
import zlib

BS = 4096
HFS_TYPE = uuid.UUID("48465300-0000-11aa-aa11-00306543ecac")


def main():
    head = bytearray(open(sys.argv[1], "rb").read())
    tail = bytearray(open(sys.argv[2], "rb").read())
    nblocks, name, new_blocks = int(sys.argv[3]), sys.argv[4], int(sys.argv[5])
    hdr = head[BS:BS + 92]
    hsize, = struct.unpack_from("<I", hdr, 12)
    my_lba, alt_lba, first_use, last_use = struct.unpack_from("<QQQQ", hdr, 24)
    ent_lba, n_ent, ent_size = struct.unpack_from("<QII", hdr, 72)
    assert (my_lba, alt_lba) == (1, nblocks - 1) and ent_size == 128
    ents = bytearray(head[ent_lba * BS:ent_lba * BS + n_ent * ent_size])

    used = []
    free_slot = None
    for i in range(n_ent):
        e = ents[i * 128:(i + 1) * 128]
        if e[:16] == b"\0" * 16:
            free_slot = i if free_slot is None else free_slot
            continue
        first, last = struct.unpack_from("<QQ", e, 32)
        used.append((first, last, i, e[56:128].decode("utf-16-le").rstrip("\0")))
    if any(u[3] == name for u in used):
        sys.exit("a partition named %s already exists" % name)
    lastp = max(used)
    if lastp[1] != last_use:
        sys.exit("last partition does not end at the last usable LBA")
    new_first = (last_use + 1 - new_blocks) // 256 * 256
    if new_first <= lastp[0] + 256 * 1024:
        sys.exit("would leave the last partition under 1 GiB")
    # shrink the last partition
    struct.pack_into("<Q", ents, lastp[2] * 128 + 40, new_first - 1)
    # new entry
    e = bytearray(128)
    e[0:16] = HFS_TYPE.bytes_le
    e[16:32] = uuid.uuid4().bytes_le
    struct.pack_into("<QQQ", e, 32, new_first, last_use, 0)
    e[56:56 + 2 * len(name)] = name.encode("utf-16-le")
    ents[free_slot * 128:(free_slot + 1) * 128] = e
    ent_crc = zlib.crc32(bytes(ents)) & 0xffffffff

    def make_header(my, alt, elba):
        h = bytearray(head[BS:BS + hsize])
        struct.pack_into("<QQ", h, 24, my, alt)
        struct.pack_into("<Q", h, 72, elba)
        struct.pack_into("<I", h, 88, ent_crc)
        struct.pack_into("<I", h, 16, 0)
        struct.pack_into("<I", h, 16, zlib.crc32(bytes(h)) & 0xffffffff)
        return h

    # primary: header LBA 1, entries from ent_lba
    head[BS:BS + hsize] = make_header(1, nblocks - 1, ent_lba)
    head[ent_lba * BS:ent_lba * BS + len(ents)] = ents
    # backup: entries right below the backup header, header at the last LBA
    tail_first = nblocks - len(tail) // BS
    b_ent_lba = nblocks - 1 - (len(ents) + BS - 1) // BS
    tail[(b_ent_lba - tail_first) * BS:(b_ent_lba - tail_first) * BS + len(ents)] = ents
    tail[(nblocks - 1 - tail_first) * BS:(nblocks - 1 - tail_first) * BS + hsize] = make_header(nblocks - 1, 1, b_ent_lba)
    open(sys.argv[6], "wb").write(head)
    open(sys.argv[7], "wb").write(tail)
    print("%s: %d..%d shrinks to %d..%d; %s = %d..%d (%.1f GiB) in slot %d" %
          (lastp[3], lastp[0], lastp[1], lastp[0], new_first - 1, name, new_first, last_use,
           (last_use - new_first + 1) * BS / 2**30, free_slot))


if __name__ == "__main__":
    main()
