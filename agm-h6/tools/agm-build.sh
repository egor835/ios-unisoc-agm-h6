#!/bin/bash
# Build an AGM H6 boot_b image: kernel on the Mac build host (topaz tools/build-local.sh with
# SWEET_AGM=1, patches from the topaz checkout there), payload (kernel + ADT + boot-args), shim.
#   tools/agm-build.sh NAME [--no-kernel]     -> xnu-out/NAME/{kernel,agm.adt,payload.bin,shim-boot_b.img}
# Env: MAC=user@host of the Mac build host (required), MAC_TOPAZ (default ~/ios-topaz on the Mac).
# boot-args.txt holds the kernel command line; @WIFIMAC@ is replaced by wifimac.local (the phone's
# Wi-Fi MAC from prodnv wifimac.txt, 12 hex digits, not in git).
set -euo pipefail
cd "$(dirname "$0")/.."
name=${1:?usage: tools/agm-build.sh NAME [--no-kernel]}
MAC=${MAC:?set MAC=user@host of the Mac build host}
MAC_TOPAZ=${MAC_TOPAZ:-'~/ios-topaz'}
out=xnu-out/$name
mkdir -p "$out"
if [ "${2:-}" != "--no-kernel" ]; then
	echo "== kernel (SWEET_AGM=1)"
	ssh "$MAC" "cd $MAC_TOPAZ && SWEET_AGM=1 tools/build-local.sh > /tmp/agm-build.log 2>&1" ||
		{ ssh "$MAC" "grep -E 'error:' /tmp/agm-build.log | sort -u | tail -20"; exit 1; }
fi
args=$(tr -d '\n' < boot-args.txt)
if [[ $args == *@WIFIMAC@* ]]; then
	[ -s wifimac.local ] || { echo "wifimac.local missing (12 hex digits)" >&2; exit 1; }
	args=${args//@WIFIMAC@/$(tr -d ' \n' < wifimac.local)}
fi
[ -s "$out/agm.adt" ] || cp adt/agm.adt "$out/agm.adt"
scp -q "$out/agm.adt" "$MAC:/tmp/agm.adt"
ssh "$MAC" "cd ~/topaz-xnu-build && python3 $MAC_TOPAZ/tools/mkpayload.py out/kernel.development.sweet /tmp/agm.adt /tmp/agm-payload.bin --cmdline '$args' > /dev/null"
scp -q "$MAC:/tmp/agm-payload.bin" "$out/payload.bin"
scp -q "$MAC:topaz-xnu-build/out/kernel.development.sweet" "$out/kernel"
shim-agm/build-agm.sh "$PWD/$out/payload.bin" | tail -1
cp shim-agm/out/shim-boot_b.img "$out/"
echo "$out/shim-boot_b.img  (tools/run-b-once.sh $out/shim-boot_b.img)"
