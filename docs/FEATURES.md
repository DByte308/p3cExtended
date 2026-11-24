# Features: list and to-dos

Goal: the useful headset features in our own stack (Envision + the pimax_p3c Monado driver), without depending
on proprietary software. RGB/LED colour features are out of scope.
Sources: our driver, the `pimax-protocol` crate (rev `00e0f99`, see `sources.lock`), our own USB measurements
on the headset, public user documentation (IPD).

Status: **done** = works and verified in the headset; **built** = code exists, not tested on hardware;
**todo** = not started. Effort S/M/L.

## Envision UI (branch `pimax` of `src/envision-src`, patches in `patches/envision/`)
**Redesigned 2026-11-23:** sidebar app. Pages: Home (Start/Stop, status, summary tiles, warnings), Headset (device
info + self-test, display, IPD, tracking, advanced), Eye Tracking (status, Recalibrate / Gaze / Foveation test,
settings, foveation detail), Controllers (status, dead zone, tracking), Devices & Apps, Logs; Settings opens the
preferences dialog.
**Updated 2026-11-24:** Headset → Display gets the FOV level; Headset → Advanced (lens centre distance, view cant,
lighthouse faceplate, capture headset view); Eye Tracking → Foveation detail (sharp area size and resolution,
periphery resolution); Home gets a Rendering tile (foveated or standard, FOV level from the driver's status file).
Not yet seen on screen: the Home tiles only show with the headset plugged in. Settings are
edited on the pages and saved immediately; a banner offers "Restart" when the XR service is running. Narrow windows
collapse to one pane. Verified: builds without warnings, Home screenshot; other pages verified per component.
Audited 2026-11-24 (shell, Pimax pages, eye page) and fixed; see patches 0011 (Envision) and 0023 (Monado).
To do: try every page and the restart banner with a running session.
Run it with `envision-pimax` (installed to `~/.local/opt/envision-pimax`; a distribution Envision is untouched).
Envision's system profile starts `/usr/bin/monado-service`: after a driver build, refresh `/usr` with
`scripts/install.sh --system` (copies the `~/.local` build).
Settings are stored as global env vars in `~/.config/envision/envision.json` and apply on the next service start.

| Item | Status |
|---|---|
| Pimax settings on the Headset, Eye Tracking and Controllers pages (moved out of the Preferences dialog, Envision patch 0007) | built |
| Eye Tracking page: Recalibrate / Gaze Test / Foveation Test buttons | built |
| Recalibrate saves `PIMAX_EYE_CALIB` and offers "Restart" | built |
| Pimax pages shown only when a Pimax (USB 34a4) is plugged in; headset info from the driver's status file | built, verified with a screenshot |
| Advanced page env-var list refreshes when shown | built |
| Live options without a restart: render IPD offset, view cant, eye calibration and flip, stick dead zone. Envision writes the changed ones to `$XDG_RUNTIME_DIR/pimax-p3c/settings.json`; the driver re-reads it (files older than the service process are ignored) and reports `live_settings` in its status file, and only then does Envision skip the Restart banner. IPD offset and cant reach SteamVR games only on restart | built (patches 0024–0025, Envision 0012–0013), untested in the headset |
| Pimax pages refresh when Preferences closes; edits are saved 0.5 s after the last change | built |
| Render resolution slider (1.00× = native, 0.30×–1.50×, shows the share of native pixels) | built (patch 0015) |
| Audio: switch sound output and microphone on Start (headset automatic or any device), restore on Stop; via `pactl` (PulseAudio or PipeWire); waits up to 20 s for the headset's audio to appear | built (patches 0017, 0018); tested switch and restore of the microphone, not yet in a full session |
| Per-second controller diagnostics in the log | debug level only: `PIMAX_LOG=debug` (patch 0030) |

## Display
| Feature | How | Status |
|---|---|---|
| Refresh rate 72 / 90 / 120 Hz | `PIMAX_REFRESH_HZ` → vision cmd 0x1A SetRate | driver support exists; only rates listed in the headset display's EDID are sent, checked right before the rate is sent (EDID checksums verified); anything else falls back to the model default, or to a listed rate if the default is not listed (patches 0029, 0031); Envision greys out unlisted rates (patches 0016, 0018). The Crystal Super tested here lists 72 and 90 Hz. **todo: test 72 Hz** |
| Render resolution | `XRT_COMPOSITOR_SCALE_PERCENTAGE` (Monado) | done (variable existed) |
| Brightness 1–100 | command 0x2A, live setting `PIMAX_BRIGHTNESS`, Envision spin row | built (patches 0032/0033, envision 0019); **todo: test in the headset** |
| Display upscaling | command 0x5A to the display unit, live setting `PIMAX_UPSCALE` (bring-up leaves it off) | built; **todo: test** |
| Headset volume keys → system volume | `pactl` on a worker thread, 5 % steps, repeat while held, never above 100 %; `PIMAX_VOLUME_KEYS=0` turns it off | built; **todo: test** |
| Local dimming (OLED/mini-LED models) | KV `ld_level`, separate HID 34a4:0041, `local_dimming` table in the config | todo M–L |
| FOV levels (smaller field of view for less GPU load) | `PIMAX_FOV_PERCENT` 50–100: the driver narrows the view tangents; the compositor scales the recommended render size by the same tangent ratio (same sharpness, fewer pixels) and keeps its own scratch images at full size; the cropped edge is black. Envision: Headset → Display → Field of view | built (patch 0021), **untested in the headset**; no hidden-area mask yet |
| Panel on/off command | `04 29 00 01` | todo S (only if panels fail to wake) |
| Smart Smoothing / Brainwarp | runtime reprojection | out of scope for now (L) |

## IPD
The Crystal Super moves its lenses with motors (58–72 mm); the headset reports the current lens IPD (HMD status,
byte 45, /100 mm), and our views already use it, so the rendered eye distance follows the lenses.
Why it matters: lens IPD wrong → blur, a small sweet spot, eye strain; render IPD wrong → world scale and depth off.

| Feature | How | Status |
|---|---|---|
| Set IPD (move lens motors) | `PIMAX_IPD_MM` 58–72 → CmdSetIpd 0x5C `[01, i16 mm*100]` at bring-up | built, **untested**: the command meaning is inferred; test with the headset on and watch the lenses |
| Render IPD offset | `PIMAX_IPD_OFFSET_MM` −5…5, render only | built |
| Don't fail before the first status report | 63 mm fallback | built |
| Lens-centre separation follows the lenses | distortion uses a fixed 68.81 mm (tuned at one lens position) | todo M: measure fusion at 2 IPDs, then use `ipd_mm − ipd_at_tuning` |
| Auto IPD from the eye tracker | Tobii stream 1285 eye origins (mm) → mean distance → 0x5C | todo M: validate against a ruler measurement first |

## Eye tracking / foveation
| Feature | Status |
|---|---|
| Eye tracking on/off (`PIMAX_EYE_TRACKING`) | done |
| Calibration: in-headset dots (`xrcalib`), fit (`fit_calib.py`), driver correction (`PIMAX_EYE_CALIB`) | done, 0.44° mean error |
| Calibration from Envision (`pimax-et-calibrate`) | built; a recalibration is combined with the active correction (the driver reports it in the status file), Stop ends `xrcalib` too, and a run cut short is not saved (`xrcalib` exits 3/4) |
| Gaze test app (`pimax-gaze` = `xrcalib --live`) | done |
| Quad views / gaze-steered foveation (`PIMAX_FOVEATION`) | driver done; **todo: test with `xrfovea`** (built, never run) and a real game |
| Foveation detail (patch 0021): `XRT_FOVEATION_INSET_DEG` (sharp area size, default 40°), `XRT_FOVEATION_INSET_DENSITY_PERCENT` (sharp area resolution, 100 = native density), `XRT_FOVEATION_CONTEXT_PERCENT` (periphery resolution). All three are read by the service (driver/compositor), so they apply to every app started while the service runs. Envision: Eye Tracking → Settings → Foveation detail | built, **untested in the headset**. Behaviour change: the inset image used to be a full-resolution copy of each eye; it is now sized from the inset angle at the eye's pixel density. Check the sizes with `xrsizes` |
| Foveated rendering in games (OpenXR-Toolkit-style for Linux / per-game) | todo L |
| VRChat OSC gaze output (`xrosc`, `/tracking/eye/CenterPitchYaw`) | built; OSC verified with a local listener; real gaze + VRChat untested |

## Controllers
| Feature | Status |
|---|---|
| Tracking + buttons | buttons done; tracking partly (see Tracking below); **todo: confirm the trigger value** |
| Haptics | done (driver) |
| Joystick dead zone | `PIMAX_STICK_DEADZONE` + Envision row; 5 unit tests pass; not tried in the headset |
| Controller connected state in Envision | done (status file) |
| Controller battery | todo M: no battery field decoded in the protocol; needs reverse engineering |

## Tracking (status 2026-11-24: controller tracking about 90 %)
| Item | Status |
|---|---|
| HMD IMU per-sample timestamp ×1000 (µs → ns) | done; ×100 (upstream) leaves SLAM with no position at all (A/B via `PIMAX_IMU_TS_SCALE`) |
| Stale/corrupted HMD reports poisoning the shared clock | **fixed** (patch 0017): the first HMD reports after start-up are up to 6 s old (USB buffer) and seeded the skew tracker that times IMU, cameras and controllers. Now flushed for 300 ms and gated on device-vs-host clock consistency; future IMU samples dropped. Explains the head runaways on about half the starts, controller depth jumps and a Basalt abort (IMU sample 380 s in the future). Confirm over more starts |
| Head SLAM runs away in a dark room | expected: not enough visual features |
| Controller dead reckoning without limit (ran away by km after leaving the cameras' view) | fixed (patch 0014): position held after `CONSTELLATION_MAX_DEAD_RECKON_S` (0.5 s), orientation keeps following the gyro |
| Controller depth errors per camera | ruled out with `CONSTELLATION_POSE_LOG`: all 4 cameras agree with the prior (ratio 1.00) |
| Controller orientation jumping while held still | not seen after the clock fix (2° spread over 8 s) |
| Controllers merge when held close together | **open**: the left/right LED rings are mirror images; blob ownership is per camera and only set after a device is solved there. A blob-reservation attempt did not help (wipe on failure, labels carried across frames) and is not included |
| Left controller lost on fast shakes | **open**: fast path is skipped above 5 m/s or 1 rev/s |
| Grip/aim orientation offset | **done** (patch 0027): default −48.4° about device X for both hands (quaternion −0.410, 0, 0, 0.912). Measured 2026-11-24 with the controllers resting on their rings (`scripts/pimax-headset-test` analysis, `--upside-down`); the hand-held capture of 2026-11-23 gave the same value; in WayVR the beam now follows the controller. `PIMAX_AIM_OFFSET_L/R` still override it. |
| Basalt abort on a repeated camera frame | **fixed** (patch 0026): a frame 16 ns older than the previous one aborted the service; such frames are now dropped before SLAM and logged |
| Head SLAM runs away mid-session | **open**: after ~4 min Basalt degenerated (`Unusually small det(Q1Jl)`) and never recovered; another start ran away at once while lying still. Leads (code review, not proven): visual dropout bridged by an IMU model with errors: the calibration's accel scale terms are ignored, the bias sign may be doubled instead of cancelled, noise values are generic. Basalt has a reset (`vit_tracker_reset`) that only a debug button calls. Next: runaway detector in `flush_poses` (speed/jump/still-IMU checks) that resets and keeps the world from jumping; then a 90 s `SLAM_EUROC_RECORD` capture (6 still orientations, then normal use) to settle the bias sign and scale offline | IMU frame checked with `tools/imu_frame_check.py` (gravity vs integrated gyro over a 90° tip): proper, not mirrored (0.4° fit; gyro 16.4 LSB per °/s), so the axis order is not the cause.
| Beam jumps to the other controller | **fix built, untested** (patch 0028): rotate one hand as if looking at a watch and point at it with the other, and the beam came out of the tilted controller. Cause (code review): every accept path ends in `pushPose`, which never compared the solve with the device's own fused prior, and an accepted wrong solve seeds the next search and the blob labels, so it sticks. Also bypassed: `kAlwaysTrustRansac` in blob recovery, `prior_must_match=false` on the last-known and slow paths, and the gravity check never runs (`gravity_error_rad` fixed at 30°, needs < 25°). Now solves more than `CONSTELLATION_PRIOR_GATE_DEG` (25) / `_CM` (25) from the prior are rejected unless the device was unseen for `_GAP_MS` (500). Still open: contested blobs are given to the nearest device first; arbitration by fit is needed for controllers held together with the same orientation |
| Controllers drift slowly | **open, lead found**: in the table capture the controller IMU's gravity has the opposite x sign from the optical orientation on both hands (y, z agree within ~1°), contradicting the "raw stays" note on `PIMAX_CTRL_IMU_AXES`. Between optical fixes the gyro is then integrated about wrong axes. Next: 40 s table test (rings down, then on the side) to choose `-x,-y,z` or `-x,y,z`, then make it the default. Also: gyro full scale (2000 vs 4000 dps) is not logged; the fusion's 512-sample keyframe buffer assumes 1 kHz, controllers send 500 Hz, so a >1 s optical gap leaves the interval unconstrained |
| Crystal speakers | **fixed (config)**: the headset's built-in audio (USB device 3654:5678 behind the headset hub) can get a motherboard's onboard-audio UCM profile when both are named "USB Audio", so its outputs play through the motherboard jacks. The WirePlumber rule `config/51-pimax-crystal-audio.conf` (`api.alsa.use-ucm = false`), installed by `scripts/install.sh` to `~/.config/wireplumber/wireplumber.conf.d/`, fixes the mapping |
| Controller IMU axes | `PIMAX_CTRL_IMU_AXES` switch (default raw); a gravity fit found no simple axis swap that fits both hands, so raw stays |

### Diagnostics
- `xrsizes`: prints the recommended stereo and quad view sizes and the quad view angles (needs the service running).
- Envision → Headset → Advanced → Capture headset view sets `XRT_MIRROR_DUMP` to `~/.cache/pimax-linux/view`. It turns off the compositor's fast path and does a blocking readback, so use it for checks, not for play.
- `XRT_MIRROR_DUMP=<dir>`: the user's left-eye view as JPGs, about 5 per second (patch 0020; costs compositor time).
- `CONSTELLATION_POSE_LOG=<file>`: one CSV line per optical controller solve (patch 0019).
- The 1 s `controllers` log line includes the mean raw controller accelerometer.

### Record and replay a session (no headset needed for the replay)
```sh
# record: everything SLAM receives, plus the generated Basalt config
SLAM_EUROC_RECORD=/path/rec PIMAX_BASALT_SAVE=/path/cfg scripts/run-vr.sh 600
# replay (the folder gets a timestamp suffix); keep stdin open, slambatch quits on EOF
sleep 900 | VIT_SYSTEM_LIBRARY_PATH=<prefix>/lib64/libbasalt.so SLAM_CAM_COUNT=4 \
  monado-cli slambatch /path/rec_<timestamp> /path/cfg/basalt.toml /path/out
# trajectory: /path/out/tracking.csv
```
`xrctrl` with `XRCTRL_CSV=file` logs head and both controllers (validity, position, orientation) every frame.
Recordings contain camera images of the room: keep them local.

## Device info / maintenance
| Feature | How | Status |
|---|---|---|
| Firmware version, serial, self-test | CmdSoftVer (HMD), SN, DeviceCheck → log + `$XDG_RUNTIME_DIR/pimax-p3c/status.json` → Envision | done (tested on firmware 00 01 1D 01, all subsystems ok); no hw-version command in the crate |
| GPU work mode (AMD) | `PIMAX_GPU=amd` + Envision row | built, untested on AMD (NVIDIA default path verified) |
| Firmware update (DFU) | vendor tools | out of scope (risky) |
| Restart headset | command 0x28, live setting `PIMAX_RESTART_AT` (live file only, once per session), Envision button | built; **todo: test** |

## How to test the untested items
**Start here:** `scripts/pimax-headset-test` (headset on, controllers on, follow the voice; ~45 s). Runs `xrsizes`, then captures one hold with both controllers pointed straight ahead and measures the controller aim frame (`tools/xrgaze/ctrl_frame.py`, prints the measured device→aim quaternion and how far it is from the raw frame and from the current default). Results stay local in `~/.cache/pimax-linux/headset-test/`.

0. Headset plugged in: start the service with defaults and run `xrsizes`; restart the service with `PIMAX_FOV_PERCENT=80 XRT_FOVEATION_CONTEXT_PERCENT=50` (service env, e.g. in Envision) and run `xrsizes` again. Then `xrfovea` with view capture on: inset size and FOV crop in the images (capture takes the compositor's slow path).
1. Start `envision-pimax`: Pimax pages visible, settings saved.
2. Recalibrate from the Eye Tracking page → new value saved → Restart Session → gaze test.
3. Foveation test (`xrfovea`): the green fine-checker inset follows the gaze.
4. `PIMAX_REFRESH_HZ=72`: panels at 72 Hz?
5. `PIMAX_IPD_MM`: do the lenses move? Does the reported IPD follow? Does stereo still fuse?
6. Controller trigger value.
