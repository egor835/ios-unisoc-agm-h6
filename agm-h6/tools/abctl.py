#!/usr/bin/env python3
"""Read / edit the AOSP A/B metadata (struct bootloader_control) in a misc dump.

The AGM H6 bootloader picks the slot with AOSP's ab_select_slot logic from
misc + 2048 (strings "ab_select_slot", "Attempting slot %c, tries_remaining %d").
Layout (system/core boot_control_definition.h), 32 bytes, little endian:
  0  char slot_suffix[4]
  4  u32  magic = 0x42414342 ("BCAB")
  8  u8   version
  9  nb_slot:3, recovery_tries_remaining:3, merge_status:3 (bits 72..80)
  11 u8   reserved0
  12 slot_metadata[4], 2 bytes each: priority:4, tries_remaining:3,
     successful_boot:1 | verity_corrupted:1, reserved:7
  20 u8   reserved1[8]
  28 u32  crc32 of bytes 0..27

Usage:
  abctl.py show <misc.bin>
  abctl.py once-b <misc.bin> <out.bin> [tries]
        slot B highest priority, slot A kept bootable as the fallback.
        tries defaults to 2: unlike plain AOSP, this bootloader rolls back to the
        other slot once a non-successful slot is down to 1 try (`fastboot
        set_active b` gives 7 -> six boots of B; tries=1 -> B never boots).
"""
import struct
import sys
import zlib

OFF = 2048
MAGIC = 0x42414342


def parse(blob):
    c = blob[OFF:OFF + 32]
    if len(c) < 32:
        sys.exit("misc dump too short")
    magic, = struct.unpack_from("<I", c, 4)
    crc, = struct.unpack_from("<I", c, 28)
    if magic != MAGIC:
        sys.exit("no BCAB magic at misc+2048 (got %#x): not the AOSP layout, refusing" % magic)
    if zlib.crc32(c[:28]) & 0xffffffff != crc:
        sys.exit("bad crc32 (%#x vs %#x): refusing" % (zlib.crc32(c[:28]) & 0xffffffff, crc))
    bits = c[9] | c[10] << 8
    slots = []
    for i in range(4):
        b0, b1 = c[12 + 2 * i], c[13 + 2 * i]
        slots.append({"priority": b0 & 0xf, "tries": (b0 >> 4) & 7,
                      "successful": b0 >> 7, "verity_corrupted": b1 & 1})
    return {"suffix": c[:4].split(b"\0")[0].decode(errors="replace"), "version": c[8],
            "nb_slot": bits & 7, "recovery_tries": (bits >> 3) & 7, "merge": (bits >> 6) & 7,
            "slots": slots, "raw": bytearray(c)}


def show(st):
    print("suffix=%r version=%d nb_slot=%d recovery_tries=%d merge=%d" %
          (st["suffix"], st["version"], st["nb_slot"], st["recovery_tries"], st["merge"]))
    for i, s in enumerate(st["slots"][:max(st["nb_slot"], 2)]):
        print("  slot %s: priority=%d tries=%d successful=%d verity_corrupted=%d" %
              ("ab"[i] if i < 2 else i, s["priority"], s["tries"], s["successful"], s["verity_corrupted"]))


def set_slot(raw, i, priority, tries, successful):
    raw[12 + 2 * i] = (priority & 0xf) | (tries & 7) << 4 | (successful & 1) << 7
    raw[13 + 2 * i] &= ~1 & 0xff  # verity_corrupted = 0


if __name__ == "__main__":
    cmd, path = sys.argv[1], sys.argv[2]
    blob = bytearray(open(path, "rb").read())
    st = parse(blob)
    show(st)
    if cmd == "once-b":
        a = st["slots"][0]
        if not a["successful"] and a["tries"] == 0:
            sys.exit("slot A is not marked bootable: refusing to make B the only try")
        raw = st["raw"]
        tries = int(sys.argv[4]) if len(sys.argv) > 4 else 2
        set_slot(raw, 1, 15, tries, 0)                   # B: try first, once with tries=2
        set_slot(raw, 0, 14, a["tries"], a["successful"])  # A: fallback, unchanged state
        struct.pack_into("<I", raw, 28, zlib.crc32(bytes(raw[:28])) & 0xffffffff)
        blob[OFF:OFF + 32] = raw
        open(sys.argv[3], "wb").write(blob)
        print("->")
        show(parse(blob))
    elif cmd != "show":
        sys.exit(__doc__)
