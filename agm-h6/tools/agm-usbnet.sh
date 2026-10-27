#!/bin/bash
# Host side of the phone's USB Ethernet (kernel boot-arg agm_usb=1, patch 0988).
#   tools/agm-usbnet.sh [LOGFILE]
# One-time setup (NetworkManager gives the phone's CDC-ECM interface 192.168.7.1/24 whenever it appears):
#   sudo nmcli con add type ethernet con-name agm-usb ifname '*' ethernet.mac-address 02:00:5E:77:EE:01 \
#        ipv4.method manual ipv4.addresses 192.168.7.1/24 ipv4.never-default yes ipv6.method disabled
# Then this prints the live kernel console the phone streams to UDP port 5140 (phone = 192.168.7.2),
# appending it to LOGFILE if given.
set -eu
if [ $# -ge 1 ]; then
	exec socat -u UDP-RECV:5140,reuseaddr - | tee -a "$1"
else
	exec socat -u UDP-RECV:5140,reuseaddr -
fi
