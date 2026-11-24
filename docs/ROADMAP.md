# Roadmap

Open work, roughly in priority order. Status of every feature: [FEATURES.md](FEATURES.md).

## 1. Tracking
- **Head SLAM runaway mid-session.** Basalt can degenerate (`Unusually small det(Q1Jl)`) and not recover.
  Plan: a runaway detector in the driver (speed, jump and still-IMU checks) that calls Basalt's reset
  (`vit_tracker_reset`) and keeps the world from jumping. Then record a session with `SLAM_EUROC_RECORD`
  (6 still orientations, then normal use) to check the IMU model offline: accel scale terms are ignored,
  the bias sign may be wrong, noise values are generic.
- **Controller drift.** The controller IMU's gravity x sign disagrees with the optical orientation on both
  hands. Plan: a table test (rings down, then on the side) to choose the right `PIMAX_CTRL_IMU_AXES`
  default. Also log the gyro full scale, and size the fusion's keyframe buffer for 500 Hz controller IMUs.
- **Controllers held close together.** The left and right LED rings are mirror images, so blobs can be
  given to the wrong controller. Needs arbitration by fit quality instead of "nearest device first".
- **Fast shakes** can lose a controller (the fast path is skipped above 5 m/s or 1 rev/s).
- Optional: Basalt drops ~10–15 "slow samples" per second. Not limited by VIO iterations; look at
  optical flow and frame feeding.

## 2. Eye tracking and foveation
How it works: the eye tracker is a separate USB device (Tobii EyeChip, `2104:0220`). It only appears on
USB after the driver sends a HID bring-up sequence to the headset's display sub-device. The chip then speaks
a framed request/response protocol over USB bulk; the driver subscribes to a gaze stream (combined gaze) and
a per-eye stream (eye origin, per-eye gaze, pupil size, validity) at ~120 Hz. Raw gaze needs a per-user
calibration (`PIMAX_EYE_CALIB`, see FEATURES).

Open:
- Test foveation with `xrfovea` and a real game.
- Foveated rendering in games that do not use quad views (per-game or layer approach).
- Auto IPD from the measured eye origins (validate against a ruler first).
- VRChat OSC gaze output (`xrosc`): test with real gaze and VRChat.
- SteamVR eye tracking path: binding works (`/user/head/eyetracking`), data not yet verified while worn.

## 3. Display and headset
- Test 72 Hz (`PIMAX_REFRESH_HZ=72`); the NVIDIA direct-mode mode list must offer it.
- Test the FOV levels (`PIMAX_FOV_PERCENT`) in the headset; add a hidden-area mask.
- Test `PIMAX_IPD_MM` (lens motors); make the lens-centre distance follow the IPD.
- Brightness and local dimming (key/format still to be confirmed on the device).
- Controller trigger value and battery level.

## 4. Release
- Settle the licence questions in [LICENSING.md](LICENSING.md); send driver fixes upstream.
- Test the installer on a clean machine and on non-Fedora distributions.
