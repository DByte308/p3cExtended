# Roadmap

Open work, roughly in priority order. Status of every feature: [FEATURES.md](FEATURES.md).

## 1. Tracking
- **Head SLAM runaway.** Partly fixed (patches/basalt 0001: the side cameras now contribute landmarks). Left:
  Basalt drops landmarks closer than 0.33 m, so facing a wall or table can still lose the visual anchor; a still
  headset only gets points from the front stereo pair; and Basalt never resets once it has lost the anchor.
  Plan: a runaway detector in the driver (speed, jump and still-IMU checks) that calls Basalt's reset
  (`vit_tracker_reset`) and keeps the world from jumping.
- **Controller start-up snaps.** Rubberbanding is fixed (patches 0035–0038). In the first ~10 s the prior gate
  still rejects about half the solves while the fusion settles, and an accepted solve does not pull the fusion's
  prediction back quickly. Candidate: gate against the last accepted solve carried
  forward by the raw gyro.
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
