**Forword:**

**Hello! Please go and say a big thank you to everyone who is involded in Monado, Ultradevs Basalt and Envision. Without their groundwork slopviber like me couldnt do things like these. The second their Work is at the same feature list as this the "Fork" will be closed.**

# p3cExtended

Run the **Pimax Crystal Super** VR headset natively on Linux (Wayland), without proprietary software.

This repo contains patches, scripts, and configs on top of:
- [Monado](https://monado.freedesktop.org/) (community fork with the Rust `pimax_p3c` driver)
- [Basalt](https://gitlab.freedesktop.org/ultradev/basalt) (`pose-covariance` branch + patches) for 4-camera inside-out SLAM
- [Envision](https://gitlab.com/gabmus/envision) patched with Pimax pages
- OpenXR test tools (`tools/`)

Upstream sources are cloned automatically at the commits pinned in `sources.lock`. **Status: experimental** (works end to end on the Crystal Super).

![Envision with the Pimax pages: home screen](docs/screenshots/envision-home.png)

## What works
Full breakdown in [docs/FEATURES.md](docs/FEATURES.md).

- **Display & headset controls**: direct mode at **90 Hz** and **72 Hz** (7680×3744, EDID-checked), full stereo (requires the NVIDIA patch below), live panel brightness (`PIMAX_BRIGHTNESS`), and live headset restart.
- **Motorized IPD**: live lens IPD control (58–72 mm) with live readout, plus live render IPD offset.
- **Head tracking**: 4-camera inside-out SLAM with Basalt (multi-threaded, 12.5 cm near-depth triangulation, pairwise stereo, anchor-preserving runaway auto-recovery), plus optional live still smoothing.
- **Controllers**: buttons, haptics, optical tracking with anti-merge protection and high-speed swing support, separate grip/aim offsets, and live still smoothing.
- **Eye tracking & foveation**: `XR_EXT_eye_gaze_interaction`, in-headset calibration (`pimax-et-calibrate`, ~0.44° mean error), gaze test, gaze-steered quad views / `XR_VARJO_foveated_rendering` (driver ready; in-game testing still ongoing).
- **Headset audio**: `config/51-pimax-crystal-audio.conf` enables `pro-audio` on the Crystal Super (`34a4:0040`, exposing `Pimax Crystal Super DMAS Speakers`) and disables UCM on `3654:5678`. Headset audio devices only appear and play while the XR service is running and the headset is awake. Auto-switching in Envision requires `pactl`.
- **Envision UI**: Home, Headset, IPD & Eye Tracking, Controllers, Devices & Apps, and Logs & Debugging.

**Not yet implemented:** local dimming, controller battery level (needs protocol reverse engineering). SteamVR support (`scripts/steamvr-monado.sh`) is experimental; OpenVR games can run via xrizer.

## Requirements
- **Headset**: Pimax Crystal Super (P3C10). Other P3C models (Crystal Light, Dream Air) share the driver but are untested.
- **GPU**: NVIDIA with the **open** kernel modules (RTX 40-series tested; needs two free display heads — unplug extra monitors if it fails to start). AMD has `PIMAX_GPU=amd` (untested).
- **System**: Linux on Wayland (tested on Fedora 44 / KDE Plasma 6), build tools (`scripts/install.sh --check` lists any missing packages), and optionally WayVR (`~/.local/opt/wayvr/WayVR-x86_64.AppImage` or `PIMAX_WAYVR`).

### NVIDIA patch (required for stereo)
At 7680×3744 NVIDIA drives the headset with two display heads ("2Heads1OR"), which start at x=0 and show the left eye in both eyes. Apply `patches/nvidia/` (NVIDIA open-gpu-kernel-modules PR #1262) to your DKMS source and rebuild (re-apply after NVIDIA driver updates):

```sh
sudo patch -p1 -d /usr/src/nvidia-<version> < patches/nvidia/0001-nvkms-2heads1or-viewport-origin-PR1262.patch
sudo dkms build --force nvidia/<version> -k <kernel>
sudo dkms install --force nvidia/<version> -k <kernel>
# reboot
```

### Kernel patch (optional)
If your desktop treats the headset as a regular monitor, `patches/kernel/` adds Pimax EDIDs (`PVR` 0x1019–0x101c) to the kernel's `non-desktop` quirk list. `scripts/install-kernel.sh` installs an already-built patched kernel tree.

## Quick start
```sh
git clone https://github.com/DByte308/p3cExtended.git && cd p3cExtended
scripts/install.sh --check        # list missing dependencies, change nothing
scripts/install.sh --udev --system
```

- `--udev`: installs USB/HID udev rules from `config/` (sudo).
- `--system`: installs Monado and Basalt into `/usr` (sudo; required because Envision's system profile runs `/usr/bin/monado-service`). Re-run after rebuilds.
- `--no-envision`: skip building the patched Envision.

Log out and back in after installing so `~/.config/environment.d/90-vr-pimax.conf` (including `OXR_PARALLEL_VIEWS=1`) takes effect.

## First start
1. Power on the headset and controllers.
2. Launch `envision-pimax`, enable **WayVR** in Plugins, and click **Start**.
3. Put the headset on and look around briefly so tracking settles.

Use Envision **or** `scripts/run-vr.sh [seconds]`, never both at once. Run `scripts/pimax-headset-test` (~45 s) for a quick automated check of view sizes and controller alignment.

## Configuration
Most options live in Envision (saved to `~/.config/envision/envision.json`). Changes marked **Applies live** take effect immediately; others show a **Restart** banner.

| Variable | Meaning |
|---|---|
| `PIMAX_REFRESH_HZ` | Refresh rate: `90` (default) or `72` on Crystal Super (EDID-checked; unlisted rates rejected) |
| `XRT_COMPOSITOR_SCALE_PERCENTAGE` | Render scale (`100` = native; Envision's default `140` is very heavy) |
| `PIMAX_FOV_PERCENT` | FOV level `50`–`100` (untested in headset) |
| `PIMAX_IPD_MM`, `PIMAX_IPD_OFFSET_MM` | Live motorized lens IPD (`58`–`72` mm); live render-only IPD offset |
| `PIMAX_EYE_TRACKING`, `PIMAX_EYE_CALIB` | Eye tracking toggle; calibration coefficients (written by `pimax-et-calibrate`) |
| `PIMAX_FOVEATION` | Gaze-steered quad views (`0` = off) |
| `XRT_FOVEATION_INSET_DEG`, `_INSET_DENSITY_PERCENT`, `_CONTEXT_PERCENT` | Foveation inset size, sharp-zone resolution, and periphery resolution |
| `PIMAX_HMD_SMOOTHING`, `PIMAX_CTRL_SMOOTHING` | Live 1-Euro still smoothing for head and controllers |
| `PIMAX_STICK_DEADZONE` | Live thumbstick dead zone (%) |
| `PIMAX_GPU` | Set to `amd` for AMD GPUs |

Do not set `XRT_COMPOSITOR_DESIRED_MODE` or leave `PIMAX_BLANK_EYE` set.

## Known issues
- **Dark rooms**: inside-out SLAM requires visible room features. If SLAM ever diverges, the runaway watchdog (`SLAM_RUNAWAY_WATCHDOG=1`) resets it automatically while preserving your floor and heading.
- **Stale IPC socket**: a hard-killed `monado-service` can leave `$XDG_RUNTIME_DIR/monado_comp_ipc` behind (`run-vr.sh` cleans it automatically).
- **External apps after stopping Envision**: Envision removes `~/.config/openxr/1/active_runtime.json` on stop; `run-vr.sh` and `pimax-headset-test` recreate it.
- **Still untested**: FOV levels, foveation in games, AMD GPUs.

## Repository layout
| Path | Contents |
|---|---|
| `patches/monado/` | Monado fork patches (stereo, 72 Hz, live IPD, eye tracking, foveation, tracking, diagnostics) |
| `patches/envision/` | Envision patches (sidebar shell, Pimax pages, Logs & Debugging) |
| `patches/basalt/` | Basalt multi-camera & near-depth SLAM fixes |
| `patches/nvidia/`, `patches/kernel/` | NVIDIA 2Heads1OR stereo fix; optional kernel non-desktop EDID quirk |
| `scripts/`, `config/`, `tools/` | Installer, session/test scripts, WirePlumber/udev/env configs, OpenXR test tools |
| `docs/` | [FEATURES](docs/FEATURES.md), [TRACKING-CHANGES](docs/TRACKING-CHANGES.md), [ROADMAP](docs/ROADMAP.md), [LICENSING](docs/LICENSING.md) |

## Licences
Each component keeps its own licence (see [docs/LICENSING.md](docs/LICENSING.md) and `sources.lock`): Monado BSL-1.0, Basalt BSD-3-Clause, Envision AGPL-3.0 (including our patches), kernel patch GPL-2.0, NVIDIA patch MIT/GPL-2.0. No vendor firmware, SDK, or proprietary software is included.
