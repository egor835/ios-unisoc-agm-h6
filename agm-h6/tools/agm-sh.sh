#!/bin/bash
# Root shell on the phone (agm-shelld, toybox sh) over USB Ethernet:  tools/agm-sh.sh [COMMAND...]
# Without arguments: an interactive terminal (raw mode; leave with "exit"). With: runs COMMAND, prints
# its output and returns its exit status.
set -euo pipefail
host=${AGM_HOST:-192.168.7.2}
if [ $# -eq 0 ]; then
	exec socat -,raw,echo=0 "TCP:$host:2323"
fi
exec python3 -I -c '
import socket, sys
host, cmd = sys.argv[1], sys.argv[2]
s = socket.create_connection((host, 2323), timeout=600)
# the markers are split in the command line, so the echoed input never matches them
s.sendall(b"echo @@AGM-B\"\"EGIN@@; " + cmd.encode() + b"; echo @@AGM-E\"\"ND@@ $?\nexit\n")
buf = b""
while True:
    d = s.recv(65536)
    if not d:
        break
    buf += d
    tail = buf.split(b"@@AGM-BEGIN@@", 1)
    if len(tail) == 2 and b"@@AGM-END@@ " in tail[1] and tail[1].endswith(b"\n"):
        break
out = buf.replace(b"\r", b"").split(b"@@AGM-BEGIN@@\n", 1)[-1]
body, _, rest = out.partition(b"@@AGM-END@@ ")
sys.stdout.buffer.write(body)
try:
    sys.exit(int(rest.split()[0]))
except (ValueError, IndexError):
    sys.exit(1)
' "$host" "$*"
