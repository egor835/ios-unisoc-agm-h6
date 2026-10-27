#!/bin/bash
# Restart the phone from iOS over USB (kernel: sync, unmount, restart); the bootloader then starts
# Android on slot A again.   tools/agm-reboot.sh
exec sh -c 'echo reboot | socat - UDP:${AGM_HOST:-192.168.7.2}:5141'
