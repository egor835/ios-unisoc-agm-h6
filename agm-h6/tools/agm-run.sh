#!/bin/bash
# Boot iOS once on the AGM H6 and follow it over USB:
#   tools/agm-run.sh BOOT_B.img [LOGFILE]
# From Android on slot A: writes boot_b and boots it once (tools/run-b-once.sh; any reset returns to
# Android). Then waits for the phone's USB Ethernet (192.168.7.2; the PC side 192.168.7.1, see
# tools/agm-usbnet.sh) and prints the live kernel log (to LOGFILE too, default logs/usb-<time>.txt).
# Shell: tools/agm-sh.sh; back to Android: tools/agm-reboot.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
img=${1:?usage: tools/agm-run.sh BOOT_B.img [LOGFILE]}
mkdir -p logs
log=${2:-logs/usb-$(date +%Y%m%d-%H%M%S).txt}
tools/run-b-once.sh "$img"
echo "== waiting for the phone on USB (192.168.7.2)"
until ping -c1 -W1 192.168.7.2 >/dev/null 2>&1; do sleep 2; done
echo "== up; live log -> $log (Ctrl-C to stop; the phone keeps running)"
exec tools/agm-usbnet.sh "$log"
