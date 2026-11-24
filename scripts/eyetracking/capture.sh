#!/bin/bash
# Capture bus-1 USB traffic with usbmon.   capture.sh <label> <seconds>   -> captures/<label>.pcap
set -euo pipefail
. "$(dirname "$(readlink -f "$0")")/../lib.sh"
label=${1:?label}; secs=${2:-30}
dir=$PIMAX_REPO/captures; mkdir -p "$dir"
timeout "$secs" tcpdump -i usbmon1 -s 0 -U -w "$dir/$label.pcap" 2>&1 | tail -1 || true
echo "wrote $dir/$label.pcap"
