#!/bin/bash
# ROOT, run once per boot session:  sudo scripts/eyetracking/setup-access.sh
#  - installs a narrow udev rule so the seated user can open the Tobii EyeChip (2104:0220)
#  - loads usbmon and grants THE USER read access to /dev/usbmon1 until reboot (ACL, not a group)
set -euo pipefail
. "$(dirname "$(readlink -f "$0")")/../lib.sh"
U=${SUDO_USER:?run with sudo}
install -m644 "$PIMAX_REPO/config/99-tobii-eyechip.rules" /etc/udev/rules.d/99-tobii-eyechip.rules
udevadm control --reload
udevadm trigger --subsystem-match=usb --attr-match=idVendor=2104
modprobe usbmon
for i in 1 2 3 4 5 6 7 8 9 10; do [ -e /dev/usbmon1 ] && break; sleep 0.3; done
setfacl -m "u:$U:r" /dev/usbmon1
getfacl -p /dev/usbmon1 | grep "user:$U"
ls -l /dev/bus/usb/*/$(printf '%03d' "$(cat /sys/bus/usb/devices/$(grep -l 0220 /sys/bus/usb/devices/*/idProduct | head -1 | xargs dirname | xargs basename)/devnum)") 2>/dev/null || true
echo "ok: usbmon1 readable by $U until reboot; EyeChip rule installed"
