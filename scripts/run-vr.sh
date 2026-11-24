#!/bin/bash
# Start a complete VR session: Monado (pimax-p3c, Basalt SLAM) + WayVR guest app.
# Keeps monado-service alive for the whole session (its stdin must stay a valid fd).
# Usage: run-vr.sh [seconds]        default 120
set -uo pipefail
. "$(dirname "$(readlink -f "$0")")/lib.sh"

DUR=${1:-120}
# MONADO_PREFIX=<dir>: use monado-service + client libs from that install prefix (e.g. a test branch build).
# Default: service from $PIMAX_PREFIX (~/.local), client libs from /usr (the regular setup).
MONADO_PREFIX=${MONADO_PREFIX:-}
if [ -n "$MONADO_PREFIX" ]; then
    SERVICE="$MONADO_PREFIX/bin/monado-service"; CLIENT_LIB=$(pimax_libdir "$MONADO_PREFIX" libopenxr_monado.so) || { echo "no libopenxr_monado.so under $MONADO_PREFIX" >&2; exit 1; }
else
    SERVICE="$PIMAX_PREFIX/bin/monado-service"
    CLIENT_LIB=$(pimax_libdir /usr libopenxr_monado.so) || { echo "no libopenxr_monado.so under /usr" >&2; exit 1; }
fi
MAN="$HOME/.config/openxr/1/active_runtime.json"
LOG=/tmp/vr-session.log

export VIT_SYSTEM_LIBRARY_PATH="$(pimax_libdir "$PIMAX_PREFIX" libbasalt.so || echo "$PIMAX_PREFIX/lib")/libbasalt.so"
# Envision General-tab equivalents (Envision src/config.rs get_env_vars)
export XRT_COMPOSITOR_SCALE_PERCENTAGE=100
export XRT_COMPOSITOR_COMPUTE=1
export U_PACING_APP_USE_MIN_FRAME_PERIOD=0
export XRT_DEBUG_GUI=0
export XRT_CURATED_GUI=0
export ORT_DISABLE_TELEMETRY=1
export U_PACING_COMP_TIME_FRACTION_PERCENT=95
export XRT_COMPOSITOR_USE_PRESENT_WAIT=1
# 72 Hz instead of 90: drop both for 90 Hz
export XR_RUNTIME_JSON="$MAN"
# canted panels: report parallel views to the app so the eyes line up
export OXR_PARALLEL_VIEWS=1

# 1. runtime manifest: real file, absolute lib path (Envision deletes it on session stop)
python3 - "$MAN" "$CLIENT_LIB" <<'PY'
import json, pathlib, sys
dst = pathlib.Path(sys.argv[1])
src = json.loads(pathlib.Path('/usr/share/openxr/1/openxr_monado.json').read_text())
src['runtime']['library_path'] = sys.argv[2] + '/libopenxr_monado.so'
src['runtime']['MND_libmonado_path'] = sys.argv[2] + '/libmonado.so'
dst.parent.mkdir(parents=True, exist_ok=True)
dst.unlink(missing_ok=True)
dst.write_text(json.dumps(src, indent=4) + "\n")
PY

# 2. reuse a running service, else start one with a stdin that never closes
if pgrep -x monado-service >/dev/null; then
    echo "reusing running monado-service (pid $(pgrep -x monado-service))"
    OWN_SERVICE=0
else
    # a killed service leaves its socket behind and the next bind fails
    rm -f "$PIMAX_RUNTIME_DIR/monado_comp_ipc" "$PIMAX_RUNTIME_DIR/monado.pid"
    echo "starting monado-service, log: $LOG"
    ( tail -f /dev/null | XRT_LOG=info "$SERVICE" > "$LOG" 2>&1 ) &
    SUB=$!
    OWN_SERVICE=1
    for _ in $(seq 1 20); do pgrep -x monado-service >/dev/null && break; sleep 0.5; done
    sleep 3
fi

echo "service: $(pgrep -x monado-service | head -1) | runtime: $MAN"
echo "starting WayVR for ${DUR}s ..."
timeout "$DUR" "$PIMAX_WAYVR" --openxr

# 3. cleanup only what we started
if [ "$OWN_SERVICE" = 1 ]; then
    echo "stopping monado-service"
    pimax_stop_pid "$(pgrep -P "$SUB" -x monado-service)" "$(pgrep -P "$SUB" -x tail)"
fi
