#!/bin/bash
# CPU use on the phone over an interval (default 30 s), by process: tools/agm-top.sh [SECONDS]
# (ps %CPU is a decaying average; this is the TIME delta)
set -euo pipefail
cd "$(dirname "$0")/.."
s=${1:-30}
tools/agm-sh.sh "/bin/ps -A -o pid=,time=,comm= > /var/agm/p1; sleep $s; /bin/ps -A -o pid=,time=,comm= > /var/agm/p2; cat /var/agm/p1; echo ====; cat /var/agm/p2" |
python3 -I -c '
import sys
t = sys.stdin.read().replace("\r", ""); a, _, b = t.partition("====")
def parse(s):
    d = {}
    for l in s.splitlines():
        p = l.split(None, 2)
        if len(p) == 3 and ":" in p[1]:
            m, sec = p[1].split(":"); d[p[0]] = (int(m) * 60 + float(sec), p[2])
    return d
A, B = parse(a), parse(b); S = float(sys.argv[1])
rows = sorted(((B[k][0] - A[k][0], B[k][1]) for k in B if k in A), reverse=True)
print("total %.0f%% of one core over %d s" % (sum(r[0] for r in rows) / S * 100, S))
for d, n in rows[:12]:
    print("%5.0f%%  %s" % (d / S * 100, n[:100]))
' "$s"
