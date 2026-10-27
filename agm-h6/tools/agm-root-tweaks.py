#!/usr/bin/env python3
"""AGM H6 additions on top of ios-topaz tools/sweet-root-tweaks.py.

  sudo agm-root-tweaks.py MOUNTED_ROOT [SHELL_DIR]

Run as root on the HFS+ root image mounted read-write. Idempotent.

  migration   DataMigration's "is migration needed" check reads LastSystemVersion
              from the ByHost preference file, named after IOPlatformUUID. XNU
              derives that UUID from /chosen/unique-chip-id + chip-id (ADT from
              tools/mkadt.py --agm: 0x41474d4836000001 / 0x8015) ->
              601DEC1A-A4FC-5FEE-B42B-74463E68D152. Without the file the
              progress-bar migration starts again on every boot and never ends
              within the bootloader watchdog's ~5 minutes on one core.
              Also com.apple.DataMigration.plist (topaz image v10).
  launchd     fairplayd.H2 (FairPlay, needs the SEP) and bluetoothd (no
              Bluetooth here) crash and respawn every ~10 s: their jobs go.
  shell       with SHELL_DIR (shell/out from shell/build-shell.sh): /usr/bin/toybox and
              /usr/bin/agm-shelld, and a launchd job for agm-shelld (root shell on TCP
              port 2323, over USB Ethernet: tools/agm-sh.sh).
"""
import os
import plistlib
import sys

MOBILE = 501
PLATFORM_UUID = "601DEC1A-A4FC-5FEE-B42B-74463E68D152"
DROP_PROGRAMS = {"fairplayd.H2", "bluetoothd"}


def save(path, data, uid, gid, mode):
    with open(path, "wb") as f:
        plistlib.dump(data, f, fmt=plistlib.FMT_BINARY)
    os.chown(path, uid, gid)
    os.chmod(path, mode)


def build_version(root):
    with open(os.path.join(root, "System/Library/CoreServices/SystemVersion.plist"), "rb") as f:
        return plistlib.load(f)["ProductBuildVersion"]


def migration(root, build):
    prefs = os.path.join(root, "private/var/mobile/Library/Preferences")
    byhost = os.path.join(prefs, "ByHost")
    os.makedirs(byhost, exist_ok=True)
    os.chown(byhost, MOBILE, MOBILE)
    os.chmod(byhost, 0o755)
    values = {"LastSystemVersion": build,
              "DMUserDataDisposition": {"SyntheticDidUpgrade": False, "buildVersion": build}}
    for u in (PLATFORM_UUID, PLATFORM_UUID.lower()):
        path = os.path.join(byhost, "com.apple.migration.%s.plist" % u)
        data = {}
        if os.path.exists(path):
            with open(path, "rb") as f:
                data = plistlib.load(f)
        data.update(values)
        save(path, data, MOBILE, MOBILE, 0o600)
        print("ByHost", os.path.basename(path), sorted(values))
    path = os.path.join(prefs, "com.apple.DataMigration.plist")
    save(path, {"LastSystemVersion": build}, MOBILE, MOBILE, 0o600)
    print("pref com.apple.DataMigration LastSystemVersion", build)


def launchd(root):
    path = os.path.join(root, "System/Library/xpc/launchd.plist")
    st = os.stat(path)
    with open(path, "rb") as f:
        plist = plistlib.load(f)
    jobs = plist["LaunchDaemons"]
    drop = []
    for key, job in jobs.items():
        prog = job.get("Program") or (job.get("ProgramArguments") or [""])[0]
        if os.path.basename(prog) in DROP_PROGRAMS:
            drop.append((key, job.get("Label")))
    for key, _ in drop:
        del jobs[key]
    save(path, plist, st.st_uid, st.st_gid, st.st_mode & 0o7777)
    print("launchd.plist: removed", [label for _, label in drop] or "nothing (already gone)")


def shell(root, shell_dir):
    for name in ("toybox", "agm-shelld"):
        dst = os.path.join(root, "usr/bin", name)
        with open(os.path.join(shell_dir, name), "rb") as f, open(dst, "wb") as o:
            o.write(f.read())
        os.chown(dst, 0, 0)
        os.chmod(dst, 0o755)
        print("shell: /usr/bin/" + name)
    path = os.path.join(root, "System/Library/xpc/launchd.plist")
    st = os.stat(path)
    with open(path, "rb") as f:
        plist = plistlib.load(f)
    plist["LaunchDaemons"]["/System/Library/LaunchDaemons/com.agm.shelld.plist"] = {
        "Label": "com.agm.shelld", "Program": "/usr/bin/agm-shelld",
        "ProgramArguments": ["/usr/bin/agm-shelld"], "RunAtLoad": True, "KeepAlive": True,
        "UserName": "root", "POSIXSpawnType": "Interactive",
        "StandardOutPath": "/var/agm-shelld.log", "StandardErrorPath": "/var/agm-shelld.log"}
    save(path, plist, st.st_uid, st.st_gid, st.st_mode & 0o7777)
    print("launchd.plist: com.agm.shelld")


def main():
    if len(sys.argv) not in (2, 3) or not os.path.isdir(os.path.join(sys.argv[1], "System/Library/xpc")):
        sys.exit(__doc__)
    if os.geteuid() != 0:
        sys.exit("run as root (files must be owned by mobile/root)")
    root = sys.argv[1]
    build = build_version(root)
    print("build", build)
    migration(root, build)
    launchd(root)
    if len(sys.argv) == 3:
        shell(root, sys.argv[2])


if __name__ == "__main__":
    main()
