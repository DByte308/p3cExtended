#!/bin/bash
# ROOT, run once per boot session:  sudo scripts/eyetracking/setup-access.sh [bus]
#  - installs a narrow udev rule so the seated user can open the Tobii EyeChip (2104:0220)
#  - loads usbmon and grants THE USER read access to /dev/usbmon<bus> until reboot (ACL, not a group)
set -euo pipefail
. "$(dirname "$(readlink -f "$0")")/../lib.sh"
U=${SUDO_USER:?run with sudo}
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
BUS=${1:-${USBMON_BUS:-$(detect_bus)}}
install -m644 "$PIMAX_REPO/config/99-tobii-eyechip.rules" /etc/udev/rules.d/99-tobii-eyechip.rules
udevadm control --reload
udevadm trigger --subsystem-match=usb --attr-match=idVendor=2104
modprobe usbmon
for i in 1 2 3 4 5 6 7 8 9 10; do [ -e "/dev/usbmon$BUS" ] && break; sleep 0.3; done
setfacl -m "u:$U:r" "/dev/usbmon$BUS"
getfacl -p "/dev/usbmon$BUS" | grep "user:$U"
ls -l /dev/bus/usb/*/$(printf '%03d' "$(cat /sys/bus/usb/devices/$(grep -l 0220 /sys/bus/usb/devices/*/idProduct | head -1 | xargs dirname | xargs basename)/devnum)") 2>/dev/null || true
echo "ok: usbmon$BUS readable by $U until reboot; EyeChip rule installed"
