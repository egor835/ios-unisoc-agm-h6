#!/bin/bash
# Install the iOS root on the AGM H6 (from Android on slot A, Magisk root; after tools/agm-repartition.sh):
#   tools/agm-install.sh ROOT.img
# Writes ROOT.img (tools/agm-mkroot.sh) to the iosroot partition and verifies it, queues the WCN firmware
# and the GPU libraries for the drop box, and saves the phone's Wi-Fi MAC to wifimac.local for the boot-args (the WCN firmware does not know it).
set -euo pipefail
cd "$(dirname "$0")/.."
img=${1:?usage: tools/agm-install.sh ROOT.img}
dev() { adb shell "su -c '$1'" | tr -d '\r'; }
[ "$(adb shell getprop ro.boot.slot_suffix | tr -d '\r')" = "_a" ] || { echo "phone must run Android from slot A" >&2; exit 1; }
dev id | grep -q 'uid=0' || { echo "no root (su) on the phone" >&2; exit 1; }
dev 'ls /dev/block/by-name/' | grep -qx iosroot || { echo "no iosroot partition: tools/agm-repartition.sh first" >&2; exit 1; }
if [ ! -s wifimac.local ]; then
	f=$(dev 'for p in /mnt/vendor/wifimac.txt /productinfo/wifimac.txt /mnt/vendor/productinfo/wifimac.txt; do [ -f $p ] && echo $p && break; done')
	if [ -n "$f" ]; then
		dev "cat $f" | tr -d ':\n ' | tr 'A-F' 'a-f' > wifimac.local
		echo "Wi-Fi MAC from $f: $(cat wifimac.local)"
	else
		echo "wifimac.txt not found; put the Wi-Fi MAC (12 hex digits) into wifimac.local" >&2
	fi
fi
tools/flash-iosroot.sh "$img"
# Files from outside the IPSW, into the new root at the first iOS boot (drop box): the WCN firmware
# from Android's /odm (boot-arg agm_wcn=1) and the GPU userland from gpu/ (agm_gpu=1, topaz_inject).
drops=()
mkdir -p wcn
if [ ! -s wcn/wcnmodem.bin ]; then
	dev 'cp /odm/firmware/wcnmodem.bin /data/local/tmp/wcnmodem.bin && chmod 644 /data/local/tmp/wcnmodem.bin' &&
		adb pull /data/local/tmp/wcnmodem.bin wcn/wcnmodem.bin >/dev/null && dev 'rm /data/local/tmp/wcnmodem.bin' || true
fi
[ -s wcn/wcnmodem.bin ] && drops+=(wcn/wcnmodem.bin=/usr/lib/agm-wcnmodem.bin:644) ||
	echo "no wcn/wcnmodem.bin: Wi-Fi will not start (boot-arg agm_wcn=1)" >&2
for f in libagm-OSMesa.dylib libagm-gl.dylib libagm-inject.dylib; do
	if [ -s "gpu/out/$f" ]; then drops+=("gpu/out/$f=/usr/lib/$f:755"); else echo "no gpu/out/$f (gpu/: build-gl.sh)" >&2; fi
done
if [ ${#drops[@]} -gt 0 ]; then
	python3 -I tools/agm-drop.py "${drops[@]}"
else
	python3 -I tools/agm-drop.py --clear
fi
echo "installed. Next: tools/agm-build.sh NAME, then tools/agm-run.sh xnu-out/NAME/shim-boot_b.img"
