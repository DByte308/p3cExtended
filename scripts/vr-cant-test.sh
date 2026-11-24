#!/bin/bash
# Stereo-fusion test: restarts Monado with a given cant scale and runs WayVR.
#   scripts/vr-cant-test.sh 100   current behaviour (view rotated +-fov_fix AND lopsided fov)
#   scripts/vr-cant-test.sh 0     no view rotation (lopsided fov only)
#   scripts/vr-cant-test.sh -100  reversed rotation
# Look at the WayVR dock/desktop and say whether the two eyes fuse into one image.
set -u
. "$(dirname "$(readlink -f "$0")")/lib.sh"
SCALE=${1:-100}; DUR=${2:-90}
for p in $(pgrep -x monado-service); do pimax_stop_pid "$p"; done; sleep 2
export PIMAX_CANT_SCALE="$SCALE"
echo "PIMAX_CANT_SCALE=$SCALE for ${DUR}s"
exec "$(dirname "$0")/run-vr.sh" "$DUR"
