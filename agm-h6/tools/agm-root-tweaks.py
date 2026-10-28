#!/usr/bin/env python3
"""AGM H6 additions on top of ios-topaz tools/sweet-root-tweaks.py.

  sudo agm-root-tweaks.py MOUNTED_ROOT [SHELL_DIR] [--apps=copy|migrate]
                         [--bootstrap=BOOTSTRAP.tar.zst --sileo=SILEO.deb]

Run as root on the HFS+ root image mounted read-write. Idempotent.

  migration   DataMigration's "is migration needed" check reads LastSystemVersion
              from the ByHost preference file, named after IOPlatformUUID. XNU
              derives that UUID from /chosen/unique-chip-id + chip-id (ADT from
              tools/mkadt.py --agm: 0x41474d4836000001 / 0x8015) ->
              601DEC1A-A4FC-5FEE-B42B-74463E68D152. Without the file the
              progress-bar migration starts again on every boot and never ends
              within the bootloader watchdog's ~5 minutes on one core.
              Also com.apple.DataMigration.plist (topaz image v10).
  apps        the 32 removable stock apps (Notes, Calculator, Mail, ...) come in
              /private/var/staged_system_apps; on an iPhone DataMigration's
              SystemAppMigrator installs them at the first boot.
              --apps=copy (default): migration stays off (as above) and the apps are
              copied into /Applications, where LaunchServices finds them.
              --apps=migrate: no migration state at all (also not the
              com.apple.migration LastSystemVersion of sweet-root-tweaks.py), so
              DataMigration runs at the first boot and installs them (and runs the
              keyboard and other migrators); it may take long.
  launchd     fairplayd.H2 (FairPlay, needs the SEP) and bluetoothd (no
              Bluetooth here) crash and respawn every ~10 s: their jobs go.
              vmd (visual voicemail, no telephony), syncdefaultsd (iCloud KVS,
              crash loop) and wifip2pd (AWDL) spin and heat the phone: disabled.
  plug-ins    BackgroundTaskAgent (UserEventAgent) and BTAudioHALPlugin (mediaserverd)
              retry the missing CommCenter / bluetoothd without pause: moved to
              *.disabled next to their directories.
  status bar  UIStatusBarVisualProviderClassName = the iPhone X's status bar (the screen is 375x812
              points @2x, kernel 0992 + mkadt.py canvas 750x1624; no iPhone has that scale).
  mobile tmp  /private/var/mobile/tmp (mobile's temporary directory: apps without a container,
              e.g. URLSession downloads, fail with ENOENT without it).
  bootstrap   with --bootstrap/--sileo: the rootless Procursus bootstrap (iphoneos-arm64, CF 1900)
              in /private/preboot/jb-agm and /var/jb -> it (as on real rootless jailbreaks: XNU
              reports paths under /private/var as /var/..., which breaks Sileo's giveMeRoot check),
              the markers Sileo detects it by (.procursus_strapped, .installed_dopamine),
              /Applications/Sileo.app/Sileo -> the real one (giveMeRoot's build-time path), and
              /var/jb/.agm-firstboot.sh, run once by agm-shelld at the first boot: prep_bootstrap.sh,
              passwords "alpine" for root and mobile, dpkg -i Sileo, uicache.
  shell       with SHELL_DIR (shell/out from shell/build-shell.sh): /usr/bin/toybox,
              /usr/bin/agm-shelld and /usr/bin/hfsgrow (agm-shelld grows the 16 GiB root over its
              partition at the first boot), and a launchd job for agm-shelld (root shell on TCP
              port 2323, over USB Ethernet: tools/agm-sh.sh).
"""
import os
import plistlib
import shutil
import sys

MOBILE = 501
PLATFORM_UUID = "601DEC1A-A4FC-5FEE-B42B-74463E68D152"
DROP_PROGRAMS = {"fairplayd.H2", "bluetoothd"}
# spin without telephony / AWDL / iCloud and keep CPUs busy (heat): disabled
DISABLE_PROGRAMS = {"vmd", "syncdefaultsd", "wifip2pd"}


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
    for key, job in jobs.items():
        prog = job.get("Program") or (job.get("ProgramArguments") or [""])[0]
        if os.path.basename(prog) in DISABLE_PROGRAMS:
            job["Disabled"] = True
            print("launchd.plist: disabled", job.get("Label"))
    save(path, plist, st.st_uid, st.st_gid, st.st_mode & 0o7777)
    print("launchd.plist: removed", [label for _, label in drop] or "nothing (already gone)")


def apps_copy(root):
    staged = os.path.join(root, "private/var/staged_system_apps")
    n = 0
    for name in sorted(os.listdir(staged)):
        dst = os.path.join(root, "Applications", name)
        if name.endswith(".app") and not os.path.exists(dst):
            shutil.copytree(os.path.join(staged, name), dst, symlinks=True, copy_function=shutil.copy2)
            n += 1
            for d, dirs, files in os.walk(dst):           # copytree keeps modes, not owners
                src_d = os.path.join(staged, os.path.relpath(d, os.path.join(root, "Applications")))
                for e in dirs + files:
                    st = os.lstat(os.path.join(src_d, e))
                    os.lchown(os.path.join(d, e), st.st_uid, st.st_gid)
            st = os.stat(os.path.join(staged, name))
            os.chown(dst, st.st_uid, st.st_gid)
    print("apps: %d staged apps copied into /Applications" % n)


def migration_on(root):
    prefs = os.path.join(root, "private/var/mobile/Library/Preferences")
    gone = []
    for rel in ["com.apple.migration.plist", "com.apple.DataMigration.plist",
                "ByHost/com.apple.migration.%s.plist" % PLATFORM_UUID,
                "ByHost/com.apple.migration.%s.plist" % PLATFORM_UUID.lower()]:
        path = os.path.join(prefs, rel)
        if os.path.exists(path):
            os.unlink(path)
            gone.append(rel)
    print("migration: on at the first boot; removed", gone or "nothing")


# plug-ins that retry a missing daemon in a tight loop (tens of thousands of times a second):
# BackgroundTaskAgent (UserEventAgent) looks up CommCenter's com.apple.commcenter.xpc -> launchd ~75% CPU;
# BTAudioHALPlugin (mediaserverd) reconnects to bluetoothd -> mediaserverd ~50% CPU
PLUGINS_OFF = ["System/Library/UserEventPlugins/com.apple.BackgroundTaskAgentPlugin.plugin",
               "System/Library/Audio/Plug-Ins/HAL/BTAudioHALPlugin.driver"]


def plugins_off(root):
    for rel in PLUGINS_OFF:
        src = os.path.join(root, rel)
        dst_dir = os.path.dirname(src) + ".disabled"
        if os.path.exists(src):
            os.makedirs(dst_dir, exist_ok=True)
            os.rename(src, os.path.join(dst_dir, os.path.basename(src)))
            print("plug-in off:", rel)


def status_bar(root):
    """UIKit has no status bar layout for a 375x812 point screen at scale 2 (it draws the red
    rdar://45025538 bar); it takes the class named by the UIStatusBarVisualProviderClassName default
    instead: the iPhone X one (1125 pixels wide @3x = the same 375 points)."""
    path = os.path.join(root, "private/var/mobile/Library/Preferences/.GlobalPreferences.plist")
    data = {}
    if os.path.exists(path):
        with open(path, "rb") as f:
            data = plistlib.load(f)
    data["UIStatusBarVisualProviderClassName"] = "_UIStatusBarVisualProvider_Split1125"
    save(path, data, MOBILE, MOBILE, 0o600)
    print("status bar: _UIStatusBarVisualProvider_Split1125")


def mobile_tmp(root):
    path = os.path.join(root, "private/var/mobile/tmp")
    os.makedirs(path, exist_ok=True)
    os.chown(path, MOBILE, MOBILE)
    os.chmod(path, 0o755)
    print("mobile tmp: /private/var/mobile/tmp")


FIRSTBOOT = """#!/var/jb/bin/bash
# agm-root-tweaks.py: the bootstrap's setup that needs iOS running; agm-shelld runs this once.
set -x
export PATH=/var/jb/usr/local/bin:/var/jb/usr/bin:/var/jb/bin:/var/jb/usr/sbin:/var/jb/sbin:/usr/bin:/bin:/usr/sbin:/sbin
if [ -f /var/jb/prep_bootstrap.sh ]; then
    NO_PASSWORD_PROMPT=1 /var/jb/bin/sh /var/jb/prep_bootstrap.sh
fi
printf 'alpine\\n' | pw usermod mobile -h 0
printf 'alpine\\n' | pw usermod root -h 0
dpkg -i /var/jb/var/cache/agm/sileo.deb && rm -f /var/jb/var/cache/agm/sileo.deb
uicache -a
rm -f /var/jb/.agm-firstboot.sh
"""


def bootstrap(root, tar_zst, sileo_deb):
    import io
    import subprocess
    import tarfile
    jb = os.path.join(root, "private/preboot/jb-agm")
    os.makedirs(jb, exist_ok=True)
    os.chmod(jb, 0o755)
    raw = subprocess.run(["zstd", "-dc", tar_zst], check=True, capture_output=True).stdout
    n = 0
    with tarfile.open(fileobj=io.BytesIO(raw)) as t:
        members = []
        for m in t.getmembers():
            if not m.name.startswith("./var/jb/"):
                continue
            m.name = m.name[len("./var/jb/"):]
            if m.islnk() and m.linkname.startswith("./var/jb/"):
                m.linkname = m.linkname[len("./var/jb/"):]
            members.append(m)
        t.extractall(jb, members=members, numeric_owner=True, filter="fully_trusted")
        n = len(members)
    link = os.path.join(root, "private/var/jb")
    if os.path.lexists(link):
        os.unlink(link)
    os.symlink("/private/preboot/jb-agm", link)
    for marker in (".procursus_strapped", ".installed_dopamine"):
        open(os.path.join(jb, marker), "w").close()
    cache = os.path.join(jb, "var/cache/agm")
    os.makedirs(cache, exist_ok=True)
    with open(sileo_deb, "rb") as f, open(os.path.join(cache, "sileo.deb"), "wb") as o:
        o.write(f.read())
    script = os.path.join(jb, ".agm-firstboot.sh")
    with open(script, "w") as f:
        f.write(FIRSTBOOT)
    os.chmod(script, 0o755)
    stub = os.path.join(root, "Applications/Sileo.app")
    os.makedirs(stub, exist_ok=True)
    if not os.path.lexists(os.path.join(stub, "Sileo")):
        os.symlink("/var/jb/Applications/Sileo.app/Sileo", os.path.join(stub, "Sileo"))
    print("bootstrap: %d entries in /private/preboot/jb-agm, /var/jb, Sileo at the first boot" % n)


def shell(root, shell_dir):
    for name in ("toybox", "agm-shelld", "hfsgrow"):
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
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
    apps = opts.get("apps", "copy")
    if ("bootstrap" in opts) != ("sileo" in opts):
        sys.exit("--bootstrap and --sileo go together")
    if len(args) not in (1, 2) or apps not in ("copy", "migrate") or \
            not os.path.isdir(os.path.join(args[0], "System/Library/xpc")):
        sys.exit(__doc__)
    if os.geteuid() != 0:
        sys.exit("run as root (files must be owned by mobile/root)")
    root = args[0]
    build = build_version(root)
    print("build", build)
    if apps == "migrate":
        migration_on(root)
    else:
        migration(root, build)
        apps_copy(root)
    launchd(root)
    plugins_off(root)
    status_bar(root)
    mobile_tmp(root)
    if "bootstrap" in opts:
        bootstrap(root, opts["bootstrap"], opts["sileo"])
    if len(args) == 2:
        shell(root, args[1])


if __name__ == "__main__":
    main()
