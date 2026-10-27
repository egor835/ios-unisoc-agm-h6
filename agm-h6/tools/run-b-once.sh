#!/bin/bash
# Boot an image on slot B exactly once, starting from Android on slot A (root via Magisk):
#   tools/run-b-once.sh shim-agm/out/shim-boot_b.img
# 1. writes the image to boot_b (same thing `fastboot flash boot_b` does) and checks it,
# 2. sets the AOSP A/B metadata in misc: slot B first with tries=2 (this bootloader rolls back
#    at 1, so that is exactly one boot of B), slot A as fallback
#    (tools/abctl.py refuses unless misc holds a valid BCAB block),
# 3. reboots. The bootloader spends B's only try; after any reset it boots slot A again,
#    no fastboot and no six manual reboots needed.
# The misc block is saved to backup/misc-<time>.bin before it is changed.
set -euo pipefail
cd "$(dirname "$0")/.."
img=${1:?usage: tools/run-b-once.sh <boot_b image>}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

dev() { adb shell "su -c '$1'" | tr -d '\r'; }

[ "$(adb shell getprop ro.boot.slot_suffix | tr -d '\r')" = "_a" ] || { echo "phone must run Android from slot A" >&2; exit 1; }
dev id | grep -q 'uid=0' || { echo "no root (su) on the phone" >&2; exit 1; }
psize=$(dev 'blockdev --getsize64 /dev/block/by-name/boot_b')
[ "$(stat -c %s "$img")" = "$psize" ] || { echo "image size != boot_b size ($psize)" >&2; exit 1; }

echo "== boot_b"
want=$(sha256sum "$img" | cut -c1-64)
adb push "$img" /data/local/tmp/boot_b.img >/dev/null
dev 'dd if=/data/local/tmp/boot_b.img of=/dev/block/by-name/boot_b bs=1M conv=fsync 2>/dev/null'
got=$(dev 'sha256sum /dev/block/by-name/boot_b' | cut -c1-64)
dev 'rm /data/local/tmp/boot_b.img'
[ "$got" = "$want" ] || { echo "boot_b readback mismatch" >&2; exit 1; }
echo "boot_b ok ($want)"

echo "== misc"
dev 'dd if=/dev/block/by-name/misc of=/data/local/tmp/misc.bin bs=4096 count=1 2>/dev/null; chmod 644 /data/local/tmp/misc.bin'
adb pull /data/local/tmp/misc.bin "$tmp/misc.bin" >/dev/null
cp "$tmp/misc.bin" "backup/misc-$(date +%Y%m%d-%H%M%S).bin"
python3 -I tools/abctl.py once-b "$tmp/misc.bin" "$tmp/misc-new.bin"
adb push "$tmp/misc-new.bin" /data/local/tmp/misc-new.bin >/dev/null
dev 'dd if=/data/local/tmp/misc-new.bin of=/dev/block/by-name/misc bs=4096 count=1 conv=fsync,notrunc 2>/dev/null'
want=$(sha256sum "$tmp/misc-new.bin" | cut -c1-64)
got=$(dev 'dd if=/dev/block/by-name/misc bs=4096 count=1 2>/dev/null | sha256sum' | cut -c1-64)
dev 'rm /data/local/tmp/misc.bin /data/local/tmp/misc-new.bin'
[ "$got" = "$want" ] || { echo "misc readback mismatch" >&2; exit 1; }
echo "misc ok"

echo "== reboot into slot B (one try)"
adb reboot
