#!/bin/bash
# Build the AGM H6 iOS root image from scratch, from the iPhone X (iPhone10,3/10,6) 16.7.16 IPSW:
#   sudo -v; [AGM_APPS=copy|migrate] tools/agm-mkroot.sh IPSW OUT.img [SIZE]     (SIZE default 16G)
# AGM_APPS: how the stock apps from /private/var/staged_system_apps get in (tools/agm-root-tweaks.py):
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

echo "== tweaks"
sudo fsck.hfsplus -fy "$out" >/dev/null || true
mkdir -p "$mnt"
sudo mount -t hfsplus -o loop,force "$out" "$mnt"
sudo python3 -I "$topaz/tools/sweet-root-tweaks.py" "$mnt"
if [ -x "$here/shell/out/agm-shelld" ]; then
	sudo python3 -I "$here/tools/agm-root-tweaks.py" "$mnt" "$here/shell/out" --apps="${AGM_APPS:-migrate}"
else
	echo "(no shell/out: run shell/build-shell.sh first for the root shell)"
	sudo python3 -I "$here/tools/agm-root-tweaks.py" "$mnt" --apps="${AGM_APPS:-migrate}"
fi
sudo umount "$mnt"
sudo fsck.hfsplus -fy "$out" >/dev/null || true
echo "done: $out  (tools/flash-iosroot.sh $out)"
