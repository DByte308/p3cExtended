#!/bin/bash
# Installs the patched Pimax kernel alongside the stock ones. Root required.
# Revert: rm -rf /lib/modules/7.3.1-pimaxq /boot/vmlinuz-7.3.1-pimaxq* \
#                /boot/initramfs-7.3.1-pimaxq* /boot/System.map-7.3.1-pimaxq \
#                /boot/config-7.3.1-pimaxq /boot/loader/entries/*7.3.1-pimaxq*
#         then: grubby --remove-kernel=/boot/vmlinuz-7.3.1-pimaxq ; dnf reinstall grub2-... not needed
set -euo pipefail
. "$(dirname "$(readlink -f "$0")")/lib.sh"
# Usage: install-kernel.sh [kernel-tree]   default: <repo>/build/kernel/linux-7.3.1
# (absolute path: it becomes the target of the /lib/modules/$KREL/build symlinks)
TREE=$(readlink -f "${1:-$PIMAX_REPO/build/kernel/linux-7.3.1}")
cd "$TREE"
KREL=$(make -s kernelrelease EXTRAVERSION=-pimaxq)
[ -n "$KREL" ] || { echo "could not determine kernelrelease in $TREE" >&2; exit 1; }
echo "Installing kernel $KREL from $TREE"

echo "== 1/4 modules_install =="
make modules_install EXTRAVERSION=-pimaxq

echo "== 2/4 build/source symlinks (needed by DKMS) =="
ln -sfn "$TREE" "/lib/modules/$KREL/build"
ln -sfn "$TREE" "/lib/modules/$KREL/source"
depmod -a "$KREL"

echo "== 3/4 make install (vmlinuz, initramfs, grub entry, dkms hook) =="
make install EXTRAVERSION=-pimaxq

echo "== 4/4 dkms for the new kernel =="
dkms autoinstall -k "$KREL" || echo "dkms autoinstall reported errors"
dkms status | grep "$KREL" || true

echo "== done =="
