# Licensing notes (not legal advice)

This repository is public. It holds patches, scripts, configs, small tools and docs. Upstream sources are
not included: `scripts/install.sh` clones them at the commits pinned in `sources.lock`.

| Component | Licence | Notes |
|---|---|---|
| Monado | BSL-1.0 (permissive) | `patches/monado/` may be published under terms compatible with BSL-1.0 |
| `pimax_p3c` driver (inside the Monado fork) | **no licence file, no SPDX headers** | by default "all rights reserved" by its author(s) (`ultradev`). Several of our Monado patches touch it. |
| `pimax-protocol` crate | **no licence file** | same issue |
| Basalt (`ultradev/basalt`) | BSD-3-Clause | used unmodified |
| Envision | AGPL-3.0 | **modified**: `patches/envision/` are AGPL-3.0 derived work; anyone who distributes the patched build must offer its source |
| NVIDIA open kernel modules patch | MIT / GPL-2.0 (dual, like the open modules) | `patches/nvidia/` is taken from NVIDIA open-gpu-kernel-modules PR #1262 (not merged yet); credit goes to its author |
| Linux kernel patch | GPL-2.0 | `patches/kernel/` must stay GPL-2.0 |
| Eye-tracking scripts and tools (`scripts/eyetracking/`, `tools/`) | ours | own implementations (see the file headers) |
| Tobii Stream Engine | proprietary | not used; **never commit the library or any licence key** |
| Our scripts/docs | undecided | pick a licence (BSL-1.0 or MIT would match Monado) |

## Open points
1. Ask the `pimax_p3c` / `pimax-protocol` authors to add a licence, or send our fixes upstream as merge requests
   instead of publishing derived patches. Publishing patch context lines from unlicensed code is the one real risk.
2. Add a licence file for our own scripts, tools and docs.
3. Keep personal data out: no `baseline/` snapshots, no headset calibration cache, no session recordings
   (they contain camera images of the room), no serial numbers.
4. Vendor firmware, SDK files or proprietary software must not be added, and nothing may be copied from them.
