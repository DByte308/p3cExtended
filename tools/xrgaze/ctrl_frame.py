#!/usr/bin/env python3
"""Find the controller aim frame from one xrctrl capture.

The user holds both controllers still, pointed straight ahead like laser pointers (thumb up), while xrctrl logs
poses (XRCTRL_CSV). In that hold the aim frame should look along the head's horizontal forward with +Y up, so
    device_from_aim = inverse(world_from_device) * world_from_target
is measured directly. It is compared with the candidate frames below.

Usage: ctrl_frame.py capture.csv [--undo-l x,y,z,w] [--undo-r x,y,z,w] [--from s] [--to s] [--json out.json]
  --from/--to: the hold period in capture seconds (the stillest 2 s inside it are used).
  --undo-*: an aim offset the service applied (PIMAX_AIM_OFFSET_L/R); it is removed first.
"""
import argparse, json, math, sys
import numpy as np


def rx(deg):
    a = math.radians(deg); c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def q2m(q):
    x, y, z, w = q / np.linalg.norm(q)
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def m2q(m):
    w = math.sqrt(max(0.0, 1 + m[0, 0] + m[1, 1] + m[2, 2])) / 2
    x = math.copysign(math.sqrt(max(0.0, 1 + m[0, 0] - m[1, 1] - m[2, 2])) / 2, m[2, 1] - m[1, 2])
    y = math.copysign(math.sqrt(max(0.0, 1 - m[0, 0] + m[1, 1] - m[2, 2])) / 2, m[0, 2] - m[2, 0])
    z = math.copysign(math.sqrt(max(0.0, 1 - m[0, 0] - m[1, 1] + m[2, 2])) / 2, m[1, 0] - m[0, 1])
    return np.array([x, y, z, w])


def angle(a, b):
    return math.degrees(math.acos(max(-1.0, min(1.0, (np.trace(a.T @ b) - 1) / 2))))


def vangle(a, b):
    return math.degrees(math.acos(max(-1.0, min(1.0, a @ b / np.linalg.norm(a) / np.linalg.norm(b)))))


def mean_rot(ms):
    u, _, vt = np.linalg.svd(sum(ms))
    r = u @ vt
    return r if np.linalg.det(r) > 0 else u @ np.diag([1, 1, -1]) @ vt


# Candidate device -> aim rotations (left hand; the right hand is the mirror about device X).
CANDIDATES = {
    "raw LED frame": np.eye(3),
    "driver default (-48.4 deg about X)": rx(-48.4),
}
MIRROR = np.diag([-1.0, 1.0, 1.0])
FWD = np.array([0.0, 0.0, -1.0])


def parse_q(s):
    v = np.array([float(c) for c in s.split(",")])
    if v.size != 4 or np.linalg.norm(v) < 1e-6:
        sys.exit(f"bad quaternion {s!r}")
    return v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv"); ap.add_argument("--undo-l"); ap.add_argument("--undo-r"); ap.add_argument("--json")
    ap.add_argument("--upside-down", action="store_true", help="controllers lie on their rings, fronts facing the way the headset looks")
    ap.add_argument("--from", dest="t_from", type=float, default=-1e9)
    ap.add_argument("--to", dest="t_to", type=float, default=1e9)
    a = ap.parse_args()
    undo = [q2m(parse_q(a.undo_l)).T if a.undo_l else np.eye(3), q2m(parse_q(a.undo_r)).T if a.undo_r else np.eye(3)]

    rows = []
    for line in open(a.csv):
        v = line.strip().split(",")
        if len(v) == 28:
            rows.append([float(c) for c in v])
    if not rows:
        sys.exit("no samples in the capture: did the session start?")
    d = np.array(rows)
    t = d[:, 0]
    poses = [d[:, 1 + 9 * k:10 + 9 * k] for k in range(3)]  # head, L, R: valid, tracked, p(3), q(4)
    ok = (t >= a.t_from) & (t <= a.t_to) & (poses[0][:, 0] > 0) & (poses[1][:, 1] > 0) & (poses[2][:, 1] > 0)
    print(f"{len(d)} frames, {ok.sum()} in the hold period with head valid and both controllers tracked")

    # The stillest 2 s window with everything tracked.
    best = None
    for start in np.arange(t[0], t[-1] - 2.0, 0.25):
        sel = ok & (t >= start) & (t < start + 2.0)
        if sel.sum() < 40:
            continue
        motion = max(np.ptp(poses[k][sel, 2:5], axis=0).max() for k in (1, 2))
        if best is None or motion < best[0]:
            best = (motion, start, sel)
    if best is None:
        sys.exit("never 2 s with head and both controllers tracked: are the controllers on and in view?")
    motion, start, sel = best
    print(f"hold window {start:.1f}-{start + 2:.1f} s, controller movement {motion * 100:.1f} cm"
          + ("  (WARNING: not still, result is rough)" if motion > 0.05 else ""))

    head = mean_rot([q2m(q) for q in poses[0][sel, 5:9]])
    f = head @ FWD
    f[1] = 0.0
    if np.linalg.norm(f) < 0.3:
        sys.exit("head looks straight up or down during the hold; look ahead and repeat")
    f /= np.linalg.norm(f)
    up = np.array([0.0, -1.0 if a.upside_down else 1.0, 0.0])  # upside down = rolled 180 deg about forward
    target = np.column_stack([np.cross(up, -f), up, -f])  # world_from_target: -Z = forward, +Y = up

    out = {"window_s": [round(start, 2), round(start + 2, 2)], "motion_cm": round(motion * 100, 1), "hands": {}}
    for k, hand in ((1, "left"), (2, "right")):
        dev = mean_rot([q2m(q) @ undo[k - 1] for q in poses[k][sel, 5:9]])
        meas = dev.T @ target  # device_from_aim
        spread = max(angle(q2m(q) @ undo[k - 1], dev) for q in poses[k][sel, 5:9])
        pt = dev @ FWD
        print(f"\n{hand}: pointing {vangle(pt, -target[:, 2]):.0f} deg away from head forward with the raw frame; "
              f"rotation jitter in the window {spread:.1f} deg")
        q = m2q(meas)
        print(f"  measured device->aim quaternion (x,y,z,w) {np.round(q, 4).tolist()}")
        res = {}
        for name, c in CANDIDATES.items():
            c = c if hand == "left" else MIRROR @ c @ MIRROR
            res[name] = {"forward_deg": round(vangle(meas @ FWD, c @ FWD), 1), "rotation_deg": round(angle(meas, c), 1)}
            print(f"  {name:36} forward off {res[name]['forward_deg']:5.1f} deg, full rotation off {res[name]['rotation_deg']:5.1f} deg")
        win = min(res, key=lambda n: res[n]["forward_deg"])
        print(f"  closest: {win}")
        out["hands"][hand] = {"measured_quat_xyzw": [round(float(c), 5) for c in q], "candidates": res, "closest": win,
                              "jitter_deg": round(spread, 1)}
    if a.json:
        json.dump(out, open(a.json, "w"), indent=1)
        print(f"\nresult: {a.json}")


if __name__ == "__main__":
    main()
