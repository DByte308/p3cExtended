#!/bin/bash
# Copy the freshly built user-prefix Monado artifacts over the /usr and /usr/local copies (needs root).
# Only monado-service carries the pimax driver; the libs are shared and identical, but we sync them too.
# Usage: refresh-usr-copies.sh [SOURCE_PREFIX]
#   default: PIMAX_PREFIX if passed through (sudo, not pkexec: it clears the environment),
#   else ~/.local of the invoking user (SUDO_USER / PKEXEC_UID).
set -euo pipefail
L=${1:-${PIMAX_PREFIX:-}}
if [ -z "$L" ]; then
  U=${SUDO_USER:-${PKEXEC_UID:-}}
  [ -n "$U" ] && L="$(getent passwd "$U" | cut -d: -f6)/.local"
fi
[ -n "$L" ] && export PIMAX_PREFIX="$L"
. "$(dirname "$(readlink -f "$0")")/lib.sh"
L=$PIMAX_PREFIX
SL=$(pimax_libdir "$L" libopenxr_monado.so) || { echo "no libopenxr_monado.so under $L" >&2; exit 1; }
for dst in /usr /usr/local; do
  DL=$(pimax_libdir "$dst" libopenxr_monado.so || echo "$dst/${SL#"$L"/}")
  install -m755 "$L/bin/monado-service" "$dst/bin/monado-service"
  for f in libopenxr_monado.so libmonado.so.25.1.0; do install -m755 "$SL/$f" "$DL/$f"; done
  md5sum "$dst/bin/monado-service" "$DL/libopenxr_monado.so"
done
ldconfig
md5sum "$L/bin/monado-service" "$SL/libopenxr_monado.so"
