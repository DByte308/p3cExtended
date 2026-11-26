# Tracking Changes vs. Stock Monado & Basalt

This page documents the specific changes in this repository that **actually improved head SLAM and controller tracking** compared to stock Monado (`pimax-p3c-monado-rust`) and Basalt (`pose-covariance`), what each change does in the code, and why it was needed.

> **Clean standalone patches:** [`patches/upstream-tracking/`](../patches/upstream-tracking/) contains minimal, single-purpose `git format-patch` files split out from our working series with **all troubleshooting logs, diagnostic counters, and debug env switches stripped out**. Every patch in `patches/upstream-tracking/` is verified to apply cleanly with `git am` directly onto stock Basalt (`456836260ab0`) and stock Monado (`ae0895d415e3`), without needing any of our other patches.
>
> *(Envision does not perform SLAM or controller tracking — it only launches `monado-service` with environment variables.)*

---

## Clean standalone patch index (`patches/upstream-tracking/`)

### Generic fixes (benefit any multi-camera headset)

| Clean patch | Component | What it fixes |
|---|---|---|
| [`basalt/0001`](../patches/upstream-tracking/basalt/0001-optical-flow-take-a-new-keypoint-s-patch-from-its-ow.patch) | Basalt optical flow | Side cameras sampled tracking patches from `cam0` instead of their own image (3-line fix) |
| [`monado-constellation/0001`](../patches/upstream-tracking/monado-constellation/0001-t-constellation-hold-position-after-0.5-s-without-an.patch) | Monado `t_constellation` | Caps IMU position dead-reckoning at 0.5 s so occluded controllers stop flying away |
| [`monado-constellation/0002`](../patches/upstream-tracking/monado-constellation/0002-t-constellation-reject-optical-solves-far-from-the-d.patch) | Monado `t_constellation` | Gates optical solves against the fused prior to stop cross-controller jumps |
| [`monado-constellation/0003`](../patches/upstream-tracking/monado-constellation/0003-t-constellation-keep-a-device-s-known-model-to-IMU-r.patch) | Monado `t_constellation` | Adds `imu_extrinsics_known` to keep known model→IMU rotation constant in Ceres fusion |

### `pimax_p3c` driver fixes (no diagnostic counters/logs)

| Clean patch | Scope | What it fixes |
|---|---|---|
| [`monado-pimax-p3c/0001`](../patches/upstream-tracking/monado-pimax-p3c/0001-d-pimax_p3c-fix-HMD-IMU-per-sample-timestamp-us-ns-f.patch) | HMD SLAM | Fixes HMD IMU per-sample timestamp (`* 100` → `* 1000`, 1 line) |
| [`monado-pimax-p3c/0002`](../patches/upstream-tracking/monado-pimax-p3c/0002-d-pimax_p3c-filter-stale-HMD-reports-and-future-IMU-.patch) | HMD + Controllers | Discards 300 ms stale USB-buffer HMD reports, gates clock jumps, drops future IMU samples |
| [`monado-pimax-p3c/0003`](../patches/upstream-tracking/monado-pimax-p3c/0003-d-pimax_p3c-drop-non-monotonic-visual-frames-before-.patch) | HMD SLAM | Drops repeated/non-monotonic visual frames before Basalt |
| [`monado-pimax-p3c/0004`](../patches/upstream-tracking/monado-pimax-p3c/0004-d-pimax_p3c-invert-controller-imu_to_device-transfor.patch) | Controllers | Inverts controller `imu_to_device` transform (`pose_invert`) |
| [`monado-pimax-p3c/0005`](../patches/upstream-tracking/monado-pimax-p3c/0005-d-pimax_p3c-mark-controller-IMU-extrinsics-as-known.patch) | Controllers | Sets `imu_extrinsics_known: true` on Pimax controllers *(requires `monado-constellation/0003`)* |
| [`monado-pimax-p3c/0006`](../patches/upstream-tracking/monado-pimax-p3c/0006-d-pimax_p3c-drop-placeholder-and-empty-controller-IM.patch) | Controllers | Drops post-connect `-4000 deg/s` placeholder and all-zero controller IMU samples |

---

## Part 1: Head SLAM (Basalt + `pimax_p3c`)

### 1. Side cameras were sampling keypoint patches from `cam0`
* **Clean patch:** [`patches/upstream-tracking/basalt/0001`](../patches/upstream-tracking/basalt/0001-optical-flow-take-a-new-keypoint-s-patch-from-its-ow.patch)
* **Files:** `include/basalt/optical_flow/frame_to_frame_optical_flow.h` (lines 560, 593), `include/basalt/optical_flow/patch_optical_flow.h` (line 428)
* **What stock Basalt did:** When creating or recalling a tracking patch for a newly detected keypoint in camera `cam_id`, Basalt hardcoded `pyramid->at(0).lvl(l)` instead of `pyramid->at(cam_id).lvl(l)`. Any keypoint detected outside `cam0`'s overlap stored a patch cropped from `cam0` at the same pixel coordinates, so optical flow immediately lost it.
* **What we changed:** Replaced `pyramid->at(0)` with `pyramid->at(cam_id)` in all three places.
* **Measured effect:** On a 4-camera headset, the two side cameras had **0 landmarks** before; after the fix they keep **40–70** and **10–60** landmarks while moving, with unchanged runtime and trajectory within 3 cm on replay.

### 2. HMD IMU per-sample timestamp was off by 10×
* **Clean patch:** [`patches/upstream-tracking/monado-pimax-p3c/0001`](../patches/upstream-tracking/monado-pimax-p3c/0001-d-pimax_p3c-fix-HMD-IMU-per-sample-timestamp-us-ns-f.patch)
* **File:** `src/xrt/drivers/pimax_p3c/src/device/session/status.rs` (`OnReport for HmdStatus`)
* **What stock did:** Computed `host_ns + (sample.timestamp_us as i64 * 100) - device_ns` (multiplying microseconds by `100` instead of `1000`).
* **What we changed:** Changed `* 100` to `* 1000`.
* **Measured effect:** Stock put every HMD IMU sample ~0.9× the device clock in the past and advanced it at 0.1× real time, leaving Basalt with **no position tracking at all**.

### 3. Stale USB-buffer HMD reports poisoned the shared clock
* **Clean patch:** [`patches/upstream-tracking/monado-pimax-p3c/0002`](../patches/upstream-tracking/monado-pimax-p3c/0002-d-pimax_p3c-filter-stale-HMD-reports-and-future-IMU-.patch) *(split from `patches/monado/0017` without diagnostic switches or logging)*
* **File:** `src/xrt/drivers/pimax_p3c/src/device/session/status.rs` (`hmd_timestamp_plausible`, `HmdStatus::on_status`, `ControllerStatus::on_status`)
* **What stock did:** Fed every HMD report straight into `tracking.clock.observe(device_ns, now_ns())`. The first reports after startup sit in the USB buffer (seen up to **6 s old**, arriving back-to-back) and seeded the 2048-sample skew tracker that timestamps **IMU samples, camera frames, and controllers**.
* **What we changed:**
  * Discard HMD reports during the first **300 ms** of the stream.
  * Accept a report only when `device_ns` advanced within **100 ms** of the host clock since the last accepted report (outliers become a one-step candidate before rebasing).
  * Drop any HMD or controller IMU sample more than **20 ms in the future**.
* **Measured effect:** Eliminated startup head-SLAM runaways (~50 % of starts), controller depth jumps, and Basalt aborts caused by future-dated IMU samples.

### 4. Drop non-monotonic camera frames before Basalt
* **Clean patch:** [`patches/upstream-tracking/monado-pimax-p3c/0003`](../patches/upstream-tracking/monado-pimax-p3c/0003-d-pimax_p3c-drop-non-monotonic-visual-frames-before-.patch)
* **File:** `src/xrt/drivers/pimax_p3c/src/device/camera/mod.rs` (`CameraSource::on_visual_frame`)
* **What stock did:** Forwarded every visual frame unconditionally; a repeated frame or a tiny clock-estimate shift yielding a timestamp even 16 ns older than the previous frame caused Basalt to `abort()`.
* **What we changed:** Track `last_visual = (device_us, timestamp_ns)` and drop any frame where `device_us <= last_us || timestamp_ns <= last_ns`.

---

## Part 2: Controller Tracking (`pimax_p3c` + Monado `t_constellation`)

### 1. Inverted controller `imu_to_device` transform *(root cause #1 of rubberbanding)*
* **Clean patch:** [`patches/upstream-tracking/monado-pimax-p3c/0004`](../patches/upstream-tracking/monado-pimax-p3c/0004-d-pimax_p3c-invert-controller-imu_to_device-transfor.patch)
* **Files:** `src/xrt/drivers/pimax_p3c/src/device/constellation/led_model.rs`, `slot/mod.rs`
* **What stock did:** Applied the calibration file's `imu_to_device` (which maps IMU → LED-model) directly to transform LEDs into the IMU frame (`model_to_imu_frame`). As a result, IMU samples reached the tracker rotated by **~176°** relative to the LED model.
* **What we changed:** Invert the transform with `pose_invert(...)` (`device_to_imu`) before transforming LED positions and normals.
* **Measured effect:** Gyro-vs-optical rotation agreement improved from **176°** off to **0.9° median** between consecutive solves.

### 2. Keep known model→IMU rotation fixed in Ceres fusion *(root cause #2 of rubberbanding)*
* **Clean patches:** [`patches/upstream-tracking/monado-constellation/0003`](../patches/upstream-tracking/monado-constellation/0003-t-constellation-keep-a-device-s-known-model-to-IMU-r.patch) (generic Monado) + [`patches/upstream-tracking/monado-pimax-p3c/0005`](../patches/upstream-tracking/monado-pimax-p3c/0005-d-pimax_p3c-mark-controller-IMU-extrinsics-as-known.patch) (`pimax_p3c` opt-in)
* **Files:** `src/xrt/tracking/constellation/optimizer/sensor_fusion.cpp`, `sensor_fusion.hpp`, `t_constellation_tracker.h`, `t_constellation_tracker.cpp`, `pimax_p3c/.../slot/mod.rs`
* **What stock Monado did:** Solved for `ImuExtrinsics` (model→IMU rotation) online in every 3-keyframe Ceres window. Three keyframes barely observe it, so it drifted from the true 1–2° out to **77°** and then **178°** within a minute — applying every gyro sample about the wrong axes, diverging in fast motion, and causing rubberbanding and stuck-upright orientation.
* **What we changed:** Added `bool imu_extrinsics_known` to `t_constellation_tracker_device_params`. When set, `SensorFusionProblem` calls `SetParameterBlockConstant(imu_extrinsics_block.data())` and `run_room_calibration_func` keeps `Qcv_model_imu` at identity. Existing drivers leave it `false`; `pimax_p3c` sets it `true`.
* **Measured effect:** Prior-gate rejections during fast motion dropped from **up to 100 %** to **0–2 %** once settled.

### 3. Cap IMU position dead-reckoning at 0.5 s
* **Clean patch:** [`patches/upstream-tracking/monado-constellation/0001`](../patches/upstream-tracking/monado-constellation/0001-t-constellation-hold-position-after-0.5-s-without-an.patch)
* **File:** `src/xrt/tracking/constellation/optimizer/sensor_fusion.cpp` (`integrateToTimestampHistory`, `SensorFusion::reckonToLatestImu`)
* **What stock Monado did:** Dead-reckoned position from double-integrated accelerometer noise indefinitely after leaving camera view while keeping `POSITION_TRACKED` set. Occluded controllers flew away by metres per second (kilometres within minutes) and dragged the blob-search prior with them.
* **What we changed:** Past `CONSTELLATION_MAX_DEAD_RECKON_S` (default `0.5 s`) since `device.latest_solve->time_ns`, hold `T_world_device`, zero `world_velocity`, and clear `POSITION_TRACKED` + `LINEAR_VELOCITY_VALID` while continuing to integrate gyro orientation.
* **Measured effect:** Verified in a 30 s occlusion test — no runaway, 97 % / 94 % tracked.

### 4. Reject optical solves far from the device's fused prior
* **Clean patch:** [`patches/upstream-tracking/monado-constellation/0002`](../patches/upstream-tracking/monado-constellation/0002-t-constellation-reject-optical-solves-far-from-the-d.patch)
* **Files:** `src/xrt/tracking/constellation/t_constellation_tracker.cpp`, `t_constellation_tracker_internal.hpp`
* **What stock Monado did:** Every accept path ended in `Camera::pushPose()` without comparing the solve to the device's own fused prior, allowing overlapping mirror-image LED rings to steal each other's blobs and stick there.
* **What we changed:** Gate `pushPose()` against `device_state.Txr_world_device_prior` (`CONSTELLATION_PRIOR_GATE_DEG=25`, `CONSTELLATION_PRIOR_GATE_CM=25`), bypassed when unseen for longer than `CONSTELLATION_PRIOR_GATE_GAP_MS=500` so occluded controllers can always re-acquire. On rejection, release the claimed blobs and return `false` so callers try fallback paths.

### 5. Drop bogus controller IMU packets
* **Clean patch:** [`patches/upstream-tracking/monado-pimax-p3c/0006`](../patches/upstream-tracking/monado-pimax-p3c/0006-d-pimax_p3c-drop-placeholder-and-empty-controller-IM.patch) *(combined & stripped from `patches/monado/0036` + `0043`, with no diagnostic counters or stall watchers)*
* **File:** `src/xrt/drivers/pimax_p3c/src/device/session/status.rs` (`is_placeholder_imu`)
* **What stock did:** Pushed the first ~30 post-connect samples where all 3 gyro axes carry `~-4000 deg/s` and ~1/s all-zero accel+gyro packets into sensor fusion as huge rotation kicks / sudden zero-gravity stops.
* **What we changed:** Filter out both before calling `constellation.push_controller_imu(...)`.

---

## Additional controller tracking & smoothing patches (`patches/monado/0046–0052`, `patches/envision/0027`)

These improvements are merged into `main` on top of the core series above:

* **Separate grip & aim offsets + grip origin lever ([`monado/0046`](../patches/monado/0046-d-pimax_p3c-separate-grip-and-aim-offsets-grip-origi.patch)):** Grip uses Pimax's `+34°` X offset + `(0, +20.2, −30.0) mm` lever (fixes VRChat hand angle), while aim keeps `−48.4°` X for the WayVR laser.
* **3D re-acquisition anti-merge & diverged-prior recovery ([`monado/0047`](../patches/monado/0047-t-constellation-refuse-a-re-acquisition-that-lands-o.patch), [`0048`](../patches/monado/0048-t-constellation-re-acquire-a-device-whose-prior-dive.patch), [`0049`](../patches/monado/0049-t-constellation-strict-re-acquisition-radius-8-cm-CO.patch)):** Refuses re-acquisition onto another tracked controller within `6 cm` (`8 cm` when the prior is untrusted) unless gyro orientation agrees, and resets a diverged fusion when consecutive rejected solves agree with each other.
* **2D cross-projection blob ownership check ([`monado/0050`](../patches/monado/0050-t-constellation-reject-re-acquisition-whose-blobs-be.patch)):** Generic `t_constellation` fix (`CONSTELLATION_REACQUIRE_2D_BLOB_CHECK=1`). Projects each other actively tracked device's LED model into the current camera via `pose_metrics_match_pose_to_blobs` and refuses re-acquisition if ≥ 3 claimed blobs match its LEDs or ≥ half fall inside its 2D LED bounds (+15 px margin). Eliminated `cam 3` mirror-image false solves that landed ~10–17 cm off the other controller (`11–14` per session → `0`).
* **Fast startup prior escape ([`monado/0051`](../patches/monado/0051-t-constellation-faster-prior-escape-during-startup.patch)):** During the first `5 s` of a device's lifetime (`CONSTELLATION_PRIOR_GATE_STARTUP_S`), triggers prior escape after 2 agreeing exposures (≥ 30 ms) while keeping 3D + 2D anti-merge guards active — eliminating the 2–3 s startup lock-in delay.
* **Adaptive 1-Euro still smoothing for controllers & HMD ([`monado/0052`](../patches/monado/0052-d-pimax_p3c-1-Euro-still-smoothing-for-controllers-a.patch), [`envision/0027`](../patches/envision/0027-Headset-Controllers-live-1-Euro-still-smoothing-rows.patch)):** Clean-room 1-Euro filter (position + SLERP quaternion, timestamp-deduplicated across grip/aim queries, auto-reset across >250 ms gaps) that calms micro-jitter when held still without adding lag in motion. Controlled via live settings `PIMAX_CTRL_SMOOTHING` (`gentle` default / `strong` / `off`) and `PIMAX_HMD_SMOOTHING` (`off` default / `gentle`).

---

## Excluded from `patches/upstream-tracking/`

* **Diagnostics & guards (kept only in `patches/monado/`):** `0015` (EuRoC SLAM recorder), `0016` / `0019` / `0030` / `0037` (pose/prior-gate diagnostic logging), `0044` (per-device IMU range sanity check).
* **Still open (see [ROADMAP.md](ROADMAP.md)):**
  * **Head SLAM:** Basalt still drops landmarks closer than 0.33 m (`inv_dist < 3`) and lacks a runaway detector/reset (`vit_tracker_reset`) when facing a close wall or table.
