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
  # Only sync /usr/local if a copy already exists there; avoid creating a second stale copy.
  if [ "$dst" = "/usr/local" ] && [ ! -e "$dst/bin/monado-service" ]; then
    continue
  fi
  DL=$(pimax_libdir "$dst" libopenxr_monado.so || echo "$dst/${SL#"$L"/}")
  mkdir -p "$dst/bin" "$DL"
  install -m755 "$L/bin/monado-service" "$dst/bin/monado-service"
  install -m755 "$SL/libopenxr_monado.so" "$DL/libopenxr_monado.so"
  # Copy libmonado.so and all versioned symlinks/targets without hardcoding a soversion,
  # and without preserving non-root ownership or user_home_t SELinux labels.
  cp -P --no-preserve=ownership,context "$SL"/libmonado.so* "$DL/"
  for f in "$DL"/libmonado.so*; do
    [ -f "$f" ] && [ ! -L "$f" ] && chmod 755 "$f"
  done
  md5sum "$dst/bin/monado-service" "$DL/libopenxr_monado.so"
done
ldconfig
md5sum "$L/bin/monado-service" "$SL/libopenxr_monado.so"
