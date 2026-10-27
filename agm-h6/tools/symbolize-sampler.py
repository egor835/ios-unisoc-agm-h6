#!/usr/bin/env python3
"""Symbolicate the last sampler snapshot (patches 0054/0961/0963) of a SWEET log.

  symbolize-sampler.py LOG.txt [PROC ...]

For each sampled process of the last "SMP --- sample N" block: its slide, then
per thread pc/lr and the user backtrace ("ubt"), symbolicated on the Mac with
`ipsw dyld a2s` against the iOS 20H392 shared cache (~/dsc-20H392). Addresses
outside the shared cache (the main binary, dyld) are left as hex.
"""
import os
import re
import subprocess
import sys

MAC = os.environ.get("MAC") or sys.exit("set MAC=user@host of the Mac build host")
IPSW = "~/tools/ipsw/ipsw"
CACHE = "~/dsc-20H392/dyld_shared_cache_arm64"
DSC_LO, DSC_HI = 0x180000000, 0x300000000    # slid shared-cache range on iOS 16


def last_sample(lines):
    start = max((i for i, l in enumerate(lines) if l.startswith("SMP --- sample")), default=None)
    if start is None:
        sys.exit("no sampler block in the log")
    return [l for l in lines[start + 1:] if l.startswith("SMP ")]


def main():
    lines = open(sys.argv[1], encoding="utf-8", errors="replace").read().replace("\0", "").splitlines()
    want = set(sys.argv[2:])
    block = last_sample(lines)
    procs = {}      # name -> {"slide": int, "threads": {tN: {"regs": str, "ubt": [int]}}}
    cur = None
    for l in block:
        m = re.match(r"SMP (\S+)\[(\d+)\] slide (0x[0-9a-f]+)", l)
        if m:
            cur = m.group(1)
            procs[cur] = {"slide": int(m.group(3), 16), "threads": {}}
            continue
        m = re.match(r"SMP (\S+) (t\d+) ubt(.*)", l)
        if m and m.group(1) in procs:
            # lines can be cut short where the log interleaves: whole hex tokens only
            procs[m.group(1)]["threads"].setdefault(m.group(2), {})["ubt"] = [
                int(x, 16) for x in m.group(3).split() if re.fullmatch(r"0x[0-9a-f]+", x)]
            continue
        m = re.match(r"SMP (\S+) (t\d+) (.*)", l)
        if m and m.group(1) in procs:
            procs[m.group(1)]["threads"].setdefault(m.group(2), {})["regs"] = m.group(3)
    if want:
        procs = {k: v for k, v in procs.items() if k in want}

    jobs = set()
    for p in procs.values():
        for t in p["threads"].values():
            regs = t.get("regs", "")
            addrs = [int(x, 16) for x in re.findall(r"(?:pc|lr) (0x[0-9a-f]+)", regs)] + t.get("ubt", [])
            t["addrs"] = addrs
            for a in addrs:
                if DSC_LO <= a < DSC_HI:
                    jobs.add((a, p["slide"]))
    script = "; ".join("echo %#x %#x $(%s dyld a2s %s %#x --slide %#x --cache /tmp/a2s.cache 2>/dev/null | tail -1)"
                       % (a, s, IPSW, CACHE, a, s) for a, s in sorted(jobs))
    out = subprocess.run(["ssh", "-o", "BatchMode=yes", MAC, script or "true"],
                         capture_output=True, text=True, timeout=1800).stdout
    sym = {}
    for l in out.splitlines():
        parts = l.split(" ", 2)
        if len(parts) == 3:
            sym[(int(parts[0], 16), int(parts[1], 16))] = parts[2].split(": ", 1)[-1]
    for name, p in procs.items():
        print("== %s (slide %#x)" % (name, p["slide"]))
        for tn, t in sorted(p["threads"].items(), key=lambda kv: int(kv[0][1:])):
            state = re.search(r"state (0x[0-9a-f]+).*hint (\d+)", t.get("regs", ""))
            print("  %s state %s hint %s" % (tn, state.group(1) if state else "?", state.group(2) if state else "?"))
            for a in t["addrs"]:
                print("     %#x  %s" % (a, sym.get((a, p["slide"]), "")))


if __name__ == "__main__":
    main()
