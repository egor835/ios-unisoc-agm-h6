#!/bin/bash
# Build the AGM H6 iOS root image from scratch, from the iPhone X (iPhone10,3/10,6) 16.7.16 IPSW:
#   sudo -v; [AGM_APPS=copy|migrate] tools/agm-mkroot.sh IPSW OUT.img [SIZE]     (SIZE default 16G)
# AGM_APPS: how the stock apps from /private/var/staged_system_apps get in (tools/agm-root-tweaks.py):
# AGM_BOOTSTRAP=1: also the rootless Procursus bootstrap and Sileo (downloaded to bootstrap/dl, checked
# against the hashes below; set up at the first boot, see agm-root-tweaks.py; root/mobile password alpine).
# migrate (default) = DataMigration at the first boot installs them, as on an iPhone (works on AGM
# since the 8 cores and the kernel-fed watchdog); copy = into /Applications with migration off.
# Linux, needs: apfs-fuse, hfsprogs (mkfs.hfsplus, fsck.hfsplus), rsync, python3, unzip, sudo.
# Steps: the OS / SystemOS / AppOS images named by the IPSW's BuildManifest -> ../tools/mkiosroot.sh
# (topaz: HFS+ image with the cryptexes inside) -> ../tools/sweet-root-tweaks.py (topaz bring-up
# changes) -> tools/agm-root-tweaks.py (AGM changes; with shell/out from shell/build-shell.sh also the
# root shell). Then tools/flash-iosroot.sh OUT.img writes it to the phone.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
topaz=$(cd "$here/.." && pwd)
ipsw=$(realpath "${1:?usage: tools/agm-mkroot.sh IPSW OUT.img [SIZE]}")
out=$(realpath -m "${2:?usage: tools/agm-mkroot.sh IPSW OUT.img [SIZE]}")
size=${3:-16G}
[ ! -e "$out" ] || { echo "refusing to overwrite $out" >&2; exit 1; }
work=$(mktemp -d "${TMPDIR:-/var/tmp}/agm-mkroot.XXXXXX")
mnt="$work/root"
cleanup() { mountpoint -q "$mnt" && sudo umount "$mnt"; rm -rf "$work"; }
trap cleanup EXIT

echo "== images from the BuildManifest"
unzip -q -o "$ipsw" BuildManifest.plist -d "$work"
read -r os sysos appos < <(python3 -I - "$work/BuildManifest.plist" <<'PY'
import plistlib, sys
m = plistlib.load(open(sys.argv[1], "rb"))["BuildIdentities"][0]["Manifest"]
print(m["OS"]["Info"]["Path"], m["Cryptex1,SystemOS"]["Info"]["Path"], m["Cryptex1,AppOS"]["Info"]["Path"])
PY
)
echo "OS $os, SystemOS $sysos, AppOS $appos"
unzip -q -o "$ipsw" "$os" "$sysos" "$appos" -d "$work"

echo "== mkiosroot.sh"
"$topaz/tools/mkiosroot.sh" "$work/$os" "$work/$sysos" "$work/$appos" "$out" "$size"
rm -f "$work"/*.dmg

tweak_opts=(--apps="${AGM_APPS:-migrate}")
if [ "${AGM_BOOTSTRAP:-0}" = 1 ]; then
	echo "== bootstrap downloads"
	mkdir -p "$here/bootstrap/dl"
	fetch() {       # fetch URL FILE SHA256
		[ -s "$2" ] || curl -fsSL -o "$2" "$1"
		echo "$3  $2" | sha256sum -c --quiet || { echo "$2: hash mismatch" >&2; exit 1; }
	}
	bs="$here/bootstrap/dl/bootstrap-ssh-iphoneos-arm64-1900.tar.zst"
	sileo="$here/bootstrap/dl/org.coolstar.sileo_2.5.1_iphoneos-arm64.deb"
	fetch https://apt.procurs.us/bootstraps/1900/bootstrap-ssh-iphoneos-arm64.tar.zst "$bs" \
		2c639b83423e4365a3a849e2bc7c56671cdcc8da534d424b6b1779a27c0c7c08
	fetch https://github.com/Sileo/Sileo/releases/download/2.5.1/org.coolstar.sileo_2.5.1_iphoneos-arm64.deb "$sileo" \
		8e3c90e5a7d32f4ca207a0ac30d3cfa8a13dca86a2b4e11cb3f9e5c68d7bc97a
	tweak_opts+=(--bootstrap="$bs" --sileo="$sileo")
fi

echo "== tweaks"
sudo fsck.hfsplus -fy "$out" >/dev/null || true
mkdir -p "$mnt"
sudo mount -t hfsplus -o loop,force "$out" "$mnt"
sudo python3 -I "$topaz/tools/sweet-root-tweaks.py" "$mnt"
if [ -x "$here/shell/out/agm-shelld" ]; then
	sudo python3 -I "$here/tools/agm-root-tweaks.py" "$mnt" "$here/shell/out" "${tweak_opts[@]}"
else
	echo "(no shell/out: run shell/build-shell.sh first for the root shell)"
	sudo python3 -I "$here/tools/agm-root-tweaks.py" "$mnt" "${tweak_opts[@]}"
fi
sudo umount "$mnt"
sudo fsck.hfsplus -fy "$out" >/dev/null || true
echo "done: $out  (tools/flash-iosroot.sh $out)"
