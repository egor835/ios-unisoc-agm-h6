#!/bin/bash
# Cut the iosroot partition (Apple HFS+ type, 128 GiB by default) off the end of userdata on the AGM H6,
# from Android on slot A with Magisk root:
#   tools/agm-repartition.sh [GIB]
# DESTROYS ANDROID'S USERDATA: the f2fs on userdata is larger than the shrunk partition afterwards, so
# Android must be wiped (recovery: "Wipe data/factory reset") right after this script. Needs
# tools/agm-backup.sh first (backup/gpt-sda-*.bin are the untouched tables to go back to).
# Only the two GPT copies are written (first 6 and last 5 blocks of the LUN); the new tables are
# made by tools/gptsplit.py from the backup and read back after writing.
set -euo pipefail
cd "$(dirname "$0")/.."
gib=${1:-128}
dev() { adb shell "su -c '$1'" | tr -d '\r'; }
[ "$(adb shell getprop ro.boot.slot_suffix | tr -d '\r')" = "_a" ] || { echo "phone must run Android from slot A" >&2; exit 1; }
dev id | grep -q 'uid=0' || { echo "no root (su) on the phone" >&2; exit 1; }
[ -s backup/gpt-sda-head.bin ] && [ -s backup/gpt-sda-tail.bin ] && [ -s backup/gpt-sda-blocks.txt ] ||
	{ echo "run tools/agm-backup.sh first" >&2; exit 1; }
if dev 'ls /dev/block/by-name/' | grep -qx iosroot; then
	echo "iosroot exists already, nothing to do"; exit 0
fi
disk=$(dev 'readlink -f /dev/block/by-name/userdata' | sed 's/[0-9]*$//')
blocks=$(( $(dev "blockdev --getsize64 $disk") / 4096 ))
[ "$blocks" = "$(cat backup/gpt-sda-blocks.txt)" ] || { echo "LUN size differs from the backup" >&2; exit 1; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
# the tables on the phone must still be the backed-up ones
dev "dd if=$disk of=/data/local/tmp/h.bin bs=4096 count=6 2>/dev/null; dd if=$disk of=/data/local/tmp/t.bin bs=4096 skip=$((blocks - 5)) count=5 2>/dev/null; chmod 644 /data/local/tmp/h.bin /data/local/tmp/t.bin"
adb pull /data/local/tmp/h.bin "$tmp/h.bin" >/dev/null; adb pull /data/local/tmp/t.bin "$tmp/t.bin" >/dev/null
cmp -s "$tmp/h.bin" backup/gpt-sda-head.bin && cmp -s "$tmp/t.bin" backup/gpt-sda-tail.bin ||
	{ echo "GPT on the phone differs from backup/: re-run tools/agm-backup.sh" >&2; exit 1; }
python3 -I tools/gptsplit.py backup/gpt-sda-head.bin backup/gpt-sda-tail.bin "$blocks" iosroot \
	$((gib * 262144)) "$tmp/head-new.bin" "$tmp/tail-new.bin"
python3 -I tools/gptshow.py "$tmp/head-new.bin" "$tmp/tail-new.bin" "$blocks"
echo
echo "This rewrites the GPT of $disk; Android's userdata must be wiped afterwards (all Android data lost)."
read -r -p "Type YES to write the new GPT: " ok
[ "$ok" = "YES" ] || { echo "nothing written"; exit 1; }
adb push "$tmp/head-new.bin" /data/local/tmp/h.bin >/dev/null; adb push "$tmp/tail-new.bin" /data/local/tmp/t.bin >/dev/null
dev "dd if=/data/local/tmp/h.bin of=$disk bs=4096 count=6 conv=fsync,notrunc 2>/dev/null; dd if=/data/local/tmp/t.bin of=$disk bs=4096 seek=$((blocks - 5)) count=5 conv=fsync,notrunc 2>/dev/null"
dev "dd if=$disk of=/data/local/tmp/h.bin bs=4096 count=6 2>/dev/null; dd if=$disk of=/data/local/tmp/t.bin bs=4096 skip=$((blocks - 5)) count=5 2>/dev/null; chmod 644 /data/local/tmp/h.bin /data/local/tmp/t.bin"
adb pull /data/local/tmp/h.bin "$tmp/h2.bin" >/dev/null; adb pull /data/local/tmp/t.bin "$tmp/t2.bin" >/dev/null
dev 'rm /data/local/tmp/h.bin /data/local/tmp/t.bin'
cmp -s "$tmp/h2.bin" "$tmp/head-new.bin" && cmp -s "$tmp/t2.bin" "$tmp/tail-new.bin" || { echo "READBACK MISMATCH" >&2; exit 1; }
echo "GPT written and verified. Now: adb reboot recovery -> Wipe data/factory reset -> reboot; then"
echo "set Android up again (adb, Magisk) and run tools/agm-install.sh."
