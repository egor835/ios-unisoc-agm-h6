#!/bin/bash
# Write an iOS root image (tools/mkiosroot.sh output) to the iosroot GPT partition,
# from Android on slot A with root:  tools/flash-iosroot.sh ios/ios16-root.img
# The image goes through /data/local/tmp (adb push), then dd; both ends are hashed
# over exactly the image length (the partition is larger than the image).
set -euo pipefail
img=${1:?usage: tools/flash-iosroot.sh <root image>}
dev() { adb shell "su -c '$1'" | tr -d '\r'; }

[ "$(adb shell getprop ro.boot.slot_suffix | tr -d '\r')" = "_a" ] || { echo "phone must run Android from slot A" >&2; exit 1; }
dev id | grep -q 'uid=0' || { echo "no root (su) on the phone" >&2; exit 1; }
size=$(stat -c %s "$img")
psize=$(dev 'blockdev --getsize64 /dev/block/by-name/iosroot')
[ "$size" -le "$psize" ] || { echo "image ($size) larger than iosroot ($psize)" >&2; exit 1; }
[ $((size % 1048576)) = 0 ] || { echo "image size is not a multiple of 1 MiB" >&2; exit 1; }
mib=$((size / 1048576))

echo "== local sha256 ($mib MiB)"
want=$(sha256sum "$img" | cut -c1-64)
echo "$want"
echo "== push"
adb push "$img" /data/local/tmp/iosroot.img
echo "== dd to iosroot"
dev "dd if=/data/local/tmp/iosroot.img of=/dev/block/by-name/iosroot bs=1048576 count=$mib conv=fsync 2>&1 | tail -1"
dev 'rm /data/local/tmp/iosroot.img'
echo "== verify"
got=$(dev "dd if=/dev/block/by-name/iosroot bs=1048576 count=$mib 2>/dev/null | sha256sum" | cut -c1-64)
echo "$got"
[ "$got" = "$want" ] && echo "iosroot OK" || { echo "iosroot MISMATCH" >&2; exit 1; }
