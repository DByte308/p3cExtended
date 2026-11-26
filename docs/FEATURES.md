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
| Audio: switch sound output and microphone on Start (headset automatic or any device), restore on Stop; via `pactl` (PulseAudio or PipeWire); waits up to 20 s for the headset's audio to appear. Note: the headset's true sound options (`Pimax Crystal Super DMAS Speakers` / mic) only appear and play once the XR service is running and the headset is awake | built & tested (patches 0017, 0018, `config/51-pimax-crystal-audio.conf`) |
| Per-second controller diagnostics in the log | debug level only: `PIMAX_LOG=debug` (patch 0030) |

## Display
| Feature | How | Status |
|---|---|---|
| Refresh rate 72 / 90 / 120 Hz | `PIMAX_REFRESH_HZ` → vision cmd 0x1A SetRate + direct-mode Vulkan refresh matching (`monado/0067`) | **done (72 & 90 Hz tested on Crystal Super)**. Only rates listed in the headset display's EDID are sent (EDID checksums verified); anything else falls back to the model default or a listed rate (patches 0029, 0031). The driver stores the EDID per headset (`~/.config/monado/pimax_p3c/display/`) and Monado's direct-mode picker (`comp_window_direct.c`, patch 0067) matches the requested nominal refresh rate instead of always choosing 90 Hz. Envision greys out unlisted rates. |
| Render resolution | `XRT_COMPOSITOR_SCALE_PERCENTAGE` (Monado) | done (variable existed) |
| Brightness 1–100 | command 0x2A, live setting `PIMAX_BRIGHTNESS`, Envision spin row | **done** (patches 0032/0033, envision 0019) |
| Display upscaling | command 0x5A to the display unit, live setting `PIMAX_UPSCALE` (bring-up leaves it off) | showed no visible effect on a Crystal Super in testing: the driver ignores the setting on that series (monado patch 0034) and Envision greys it out while one is connected, re-checked live (USB product 0040, envision patches 0021–0022) |
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
| Set IPD (move lens motors) | `PIMAX_IPD_MM` 58–72 → cmd `0x5C`, opt `0x05` (Vision), `[0x01, u16 LE mm*100]` at bring-up and live (`monado/0068`, `envision/0028`) | **done**: moves the motorized lenses live from Envision's **IPD & Eye Tracking** page and updates the live readout |
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
| Controllers merge when held close together | **fixed** (`monado/0053`–`0063`, `0066`): merged upstream Monado constellation MRs (`!3099`, `!3104`, `!3117`, `!3122`), added `in_gate_steals_other_device` and `restore_rejected_blobs()` on rejected solves, and tightened 2D orientation checks |
| Left controller lost on fast shakes | **fixed** (`monado/0066`): raised fast-shake thresholds (`CONSTELLATION_FAST_SHAKE_M_S=12`, `CONSTELLATION_FAST_SHAKE_RAD_S=18`); verified on fast swings |
| Grip/aim orientation offset | **done**: separate grip (+34° + lever) and aim (−48.4°) offsets (`PIMAX_AIM_OFFSET_L/R` still override). |
| Basalt abort on a repeated camera frame | **fixed** (patch 0026): a frame 16 ns older than the previous one aborted the service; such frames are now dropped before SLAM and logged |
| Head SLAM runs away mid-session | **fixed** (`basalt/0001`–`0003`, `monado/0064`–`0065`): side cameras keep keypoints (`0001`), near-depth triangulation lowered from 33 cm to 12.5 cm (`0002`), pairwise epipolar matrices & stationary non-cam0 stereo fixed (`0003`), Basalt runs with 4 TBB threads (`monado/0064`), and `SLAM_RUNAWAY_WATCHDOG=1` (`monado/0065`) detects divergence and resets SLAM while preserving the floor/heading anchor. |
| Beam jumps to the other controller | **fix built, untested** (patch 0028): rotate one hand as if looking at a watch and point at it with the other, and the beam came out of the tilted controller. Cause (code review): every accept path ends in `pushPose`, which never compared the solve with the device's own fused prior, and an accepted wrong solve seeds the next search and the blob labels, so it sticks. Also bypassed: `kAlwaysTrustRansac` in blob recovery, `prior_must_match=false` on the last-known and slow paths, and the gravity check never runs (`gravity_error_rad` fixed at 30°, needs < 25°). Now solves more than `CONSTELLATION_PRIOR_GATE_DEG` (25) / `_CM` (25) from the prior are rejected unless the device was unseen for `_GAP_MS` (500). Still open: contested blobs are given to the nearest device first; arbitration by fit is needed for controllers held together with the same orientation |
| Controllers drift / rubberband | **fixed** (monado patches 0035–0038). Two causes. (1) The loader applied the controller calibration's `imu_to_device` directly where the tracker needs its inverse, so the IMU reached the tracker rotated by about 176° against the LED model (this was the "opposite x sign" seen earlier); measured with the gyro against optical-only solves: 176° before, 6°/14° (right/left) after 0035; the true rotation is 1–2°, and 0038 keeps it fixed. (2) The sensor fusion re-estimated the model→IMU rotation in every 3-keyframe window, where it is barely observable; it drifted to 77° and then 178°, so the gyro was applied about the wrong axes, the prediction diverged in fast motion and the prior gate rejected the correct optical solves (rubberbanding, orientation stuck upright). The rotation is now kept at its calibrated value for these controllers. Prior-gate rejections went from up to 100 % in fast motion to 0–2 % once tracking has settled. Also dropped: the placeholder gyro samples a controller sends right after connecting. Still open: the first ~10 s after start, while the fusion settles, the gate still rejects about half the solves (short snap-backs) | The -48.4° aim offset was measured before these fixes; the beam was confirmed to point right afterwards. |
| Controller orientation stuck for a whole session | **open, not reproduced with data**: reported on the left, later on the right controller. In the logged sessions of 2026-11-25 both controllers' orientation followed every twist; a grabbed WayVR window keeps facing you, which can look frozen. See ROADMAP | |
| Crystal speakers | **fixed (config)**: on Crystal Super, the DMAS speakers live on USB `34a4:0040` ALSA device 1 (`hw:X,1`, while device 0 is capture-only), so PipeWire's default profile hides the output until switched to `pro-audio`; on Crystal / Crystal Light, `3654:5678` can get a motherboard UCM profile. `config/51-pimax-crystal-audio.conf` (installed to `~/.config/wireplumber/wireplumber.conf.d/`) selects `pro-audio` on `34a4:0040` (`Pimax Crystal Super DMAS Speakers`) and sets `api.alsa.use-ucm = false` on `3654:5678`. Note: the headset's true audio devices only activate and appear while the XR service (Envision / Monado) is running |
| Controller IMU axes | `PIMAX_CTRL_IMU_AXES` switch (default raw). Raw is right once the calibration transform is used the right way round (patch 0035); setting the switch now double-corrects |

### Diagnostics
- `xrsizes`: prints the recommended stereo and quad view sizes and the quad view angles (needs the service running).
- Envision → **Logs & Debugging** → Capture headset view sets `XRT_MIRROR_DUMP` to `~/.cache/pimax-linux/view`. It turns off the compositor's fast path and does a blocking readback, so use it for checks, not for play.
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
| Restart headset | command 0x28, live setting `PIMAX_RESTART_AT` (live file only, once per session), Envision button | **done** |

## How to test the untested items
**Start here:** `scripts/pimax-headset-test` (headset on, controllers on, follow the voice; ~45 s). Runs `xrsizes`, then captures one hold with both controllers pointed straight ahead and measures the controller aim frame (`tools/xrgaze/ctrl_frame.py`, prints the measured device→aim quaternion and how far it is from the raw frame and from the current default). Results stay local in `~/.cache/pimax-linux/headset-test/`.

0. Headset plugged in: start the service with defaults and run `xrsizes`; restart the service with `PIMAX_FOV_PERCENT=80 XRT_FOVEATION_CONTEXT_PERCENT=50` (service env, e.g. in Envision) and run `xrsizes` again. Then `xrfovea` with view capture on: inset size and FOV crop in the images (capture takes the compositor's slow path).
1. Start `envision-pimax`: Pimax pages visible, settings saved.
2. Recalibrate from the Eye Tracking page → new value saved → Restart Session → gaze test.
3. Foveation test (`xrfovea`): the green fine-checker inset follows the gaze.
4. Controller trigger value.
