#!/bin/bash
# Capture USB traffic for the eye tracker's bus with usbmon.
# Usage: capture.sh <label> [seconds=30] [bus]   -> captures/<label>.pcap
set -euo pipefail
. "$(dirname "$(readlink -f "$0")")/../lib.sh"
label=${1:?label}; secs=${2:-30}
detect_bus() {
    local d
    for d in /sys/bus/usb/devices/*; do
        [ -f "$d/idVendor" ] && [ -f "$d/idProduct" ] && [ -f "$d/busnum" ] || continue
        if [ "$(cat "$d/idVendor")" = "2104" ] && [ "$(cat "$d/idProduct")" = "0220" ]; then
            cat "$d/busnum"; return 0
        fi
    done
    echo 1
}
bus=${3:-${USBMON_BUS:-$(detect_bus)}}
dir=$PIMAX_REPO/captures; mkdir -p "$dir"
timeout "$secs" tcpdump -i "usbmon$bus" -s 0 -U -w "$dir/$label.pcap" 2>&1 | tail -1 || true
echo "wrote $dir/$label.pcap (bus $bus)"
