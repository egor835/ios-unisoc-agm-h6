#!/bin/bash
# Back up the AGM H6's partitions before anything else (from Android on slot A, Magisk root):
#   tools/agm-backup.sh            -> backup/<name>.img, backup/gpt-sda-{head,tail}.bin, backup/SHA256SUMS
# Each partition goes dd -> /data/local/tmp -> adb pull, hashed on both ends (adb exec-out with Magisk's
# su turns LF into CRLF in binary output, so no streaming). backup/boot_b.img is also what
# shim-agm/build-agm.sh overlays the shim onto. Keep this directory: prodnv/l_fixnv hold the phone's
# calibration, IMEI and MACs; they are never written by these tools.
set -euo pipefail
cd "$(dirname "$0")/.."
PARTS="boot_a boot_b dtb_a dtbo_a fbootlogo hypervsior_a init_boot_a l_fixnv1_a l_fixnv1_b l_fixnv2_a l_fixnv2_b
l_runtimenv1 l_runtimenv2 logo miscdata persist prodnv sml_a spl_a teecfg_a trustos_a uboot_a vbmeta_a vendor_boot_a"
dev() { adb shell "su -c '$1'" | tr -d '\r'; }
dev id | grep -q 'uid=0' || { echo "no root (su) on the phone" >&2; exit 1; }
mkdir -p backup
pull() {        # pull <device file> <local file>
	local want got
	want=$(dev "sha256sum $1" | cut -c1-64)
	dev "chmod 644 $1"
	adb pull "$1" "$2" >/dev/null
	dev "rm $1"
	got=$(sha256sum "$2" | cut -c1-64)
	[ "$got" = "$want" ] || { echo "$2: hash mismatch" >&2; exit 1; }
	echo "$got  $2" >> backup/SHA256SUMS.new
}
: > backup/SHA256SUMS.new
for p in $PARTS; do
	echo "== $p"
	dev "dd if=/dev/block/by-name/$p of=/data/local/tmp/$p.img bs=1048576 2>/dev/null"
	pull /data/local/tmp/$p.img backup/$p.img
done
# GPT of the UFS LUN with userdata (sda): the first 6 and the last 5 blocks of 4096 bytes
disk=$(dev 'readlink -f /dev/block/by-name/userdata' | sed 's/[0-9]*$//')
blocks=$(( $(dev "blockdev --getsize64 $disk") / 4096 ))
echo "== GPT of $disk ($blocks blocks)"
dev "dd if=$disk of=/data/local/tmp/gpt-head.bin bs=4096 count=6 2>/dev/null"
dev "dd if=$disk of=/data/local/tmp/gpt-tail.bin bs=4096 skip=$((blocks - 5)) count=5 2>/dev/null"
pull /data/local/tmp/gpt-head.bin backup/gpt-sda-head.bin
pull /data/local/tmp/gpt-tail.bin backup/gpt-sda-tail.bin
echo "$blocks" > backup/gpt-sda-blocks.txt
mv backup/SHA256SUMS.new backup/SHA256SUMS
python3 -I tools/gptshow.py backup/gpt-sda-head.bin backup/gpt-sda-tail.bin "$blocks"
echo "backup done: backup/ ($(wc -l < backup/SHA256SUMS) files)"
