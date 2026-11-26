#!/bin/bash
# Run SteamVR with Monado's SteamVR driver (pimax_p3c devices incl. eye tracking) instead of CustomHeadsetOpenVR.
# - The driver needs host libraries, so SteamVR must run outside the Steam Linux Runtime (--valve-skip-runtime-safety).
# - Outside the runtime, vrmonitor's bundled Qt 5.5 (built for OpenSSL 1.0) dlopens the host's unversioned libssl.so
#   (OpenSSL 3), half-binds it and segfaults while loading CA certificates -> SteamVR quits ("Lost master process").
#   Inside the runtime there is no libssl.so, so Qt just runs without SSL. Recreate that with empty stub
#   libssl.so/libcrypto.so in a private LD_LIBRARY_PATH entry (~/.local/share/pimax-linux/steamvr-compat).
# - openvrpaths.vrpath / steamvr.vrsettings are switched for the session and restored when SteamVR exits.
# Usage: scripts/steamvr-monado.sh          (blocks until SteamVR has exited)
set -euo pipefail
. "$(dirname "$(readlink -f "$0")")/lib.sh"
SVB=$HOME/.local/share/Steam/steamapps/common/SteamVR/bin
COMPAT=$HOME/.local/share/pimax-linux/steamvr-compat
P=$HOME/.config/openvr/openvrpaths.vrpath
S=$HOME/.local/share/Steam/config/steamvr.vrsettings
DRV=$PIMAX_PREFIX/share/steamvr-monado
mkdir -p "$PIMAX_CACHE"
LOG="$PIMAX_CACHE/steamvr-monado.log"

mkdir -p "$COMPAT"
for l in libssl.so libcrypto.so; do
    [ -f "$COMPAT/$l" ] || echo "/* empty stub */" | gcc -shared -fPIC -x c - -o "$COMPAT/$l" || exit 1
done

if pgrep -x vrserver >/dev/null; then echo "SteamVR is already running; quit it first." >&2; exit 1; fi
# SteamVR's room setup tool opens the headset cameras as plain UVC devices; a leftover instance (from a killed
# SteamVR) makes the driver's tracking camera stream fail ("Could not set up format") -> no tracking -> grey view.
if RS=$(pgrep -x steamvr_room_se 2>/dev/null); then
    for p in $RS; do pimax_stop_pid "$p"; done; sleep 1
fi
if pgrep -x monado-service >/dev/null; then
    echo "stopping monado-service (it would hold the headset)"
    for p in $(pgrep -x monado-service || true); do pimax_stop_pid "$p"; done; sleep 2
fi

[ -f "$P" ] || { echo "missing $P (run SteamVR once first)" >&2; exit 1; }
[ -f "$S" ] || { echo "missing $S (run SteamVR once first)" >&2; exit 1; }
# Keep any leftover backup from an earlier crashed run instead of overwriting it with the modified config.
[ -f "$P.steamvr-monado.bak" ] || cp -p "$P" "$P.steamvr-monado.bak"
[ -f "$S.steamvr-monado.bak" ] || cp -p "$S" "$S.steamvr-monado.bak"
restore() {
    [ -f "$P.steamvr-monado.bak" ] && mv -f "$P.steamvr-monado.bak" "$P"
    [ -f "$S.steamvr-monado.bak" ] && mv -f "$S.steamvr-monado.bak" "$S"
    echo "restored SteamVR driver config"
}
trap restore EXIT
python3 -I - "$P" "$S" "$DRV" <<'PY'
import json, os, sys
p, s, drv = sys.argv[1:]
def update(path, fn, indent):
    with open(path) as f:
        d = json.load(f)
    fn(d)
    tmp = path + ".steamvr-monado.tmp"
    with open(tmp, "w") as f:
        json.dump(d, f, indent=indent)
    os.replace(tmp, path)
update(p, lambda d: d.__setitem__("external_drivers", [drv]), "\t")
def enable_monado(d):
    d.setdefault("driver_CustomHeadsetOpenVR", {})["enable"] = False
    d.setdefault("driver_monado", {})["enable"] = True
update(s, enable_monado, 3)
PY

export VIT_SYSTEM_LIBRARY_PATH="$(pimax_libdir "$PIMAX_PREFIX" libbasalt.so || echo "$PIMAX_PREFIX/lib")/libbasalt.so"
export LD_LIBRARY_PATH="$COMPAT${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$SVB/vrstartup.sh" --valve-skip-runtime-safety >"$LOG" 2>&1 &
# wait for vrserver to appear, then for it to exit
for _ in $(seq 1 60); do pgrep -x vrserver >/dev/null && break; sleep 1; done
echo "SteamVR started (log: $LOG)"
while pgrep -x vrserver >/dev/null; do sleep 2; done
