#!/usr/bin/env python3
"""Fit PIMAX_EYE_CALIB from an xrcalib CSV.

Usage: fit_calib.py calib.csv [--skip 0]
Prints per-target error, the residual after correction and the PIMAX_EYE_CALIB value.
--skip: comma-separated target indices to leave out (default 0: the first target, while the eyes settle).
"""
import csv, math, statistics as st, sys


def angles(r):
    x, y, z = float(r["gaze_x"]), float(r["gaze_y"]), float(r["gaze_z"])
    return math.degrees(math.atan2(x, -z)), math.degrees(math.atan2(y, math.hypot(x, z)))


def lstsq3(X, Y):
    M = [[sum(a[i] * a[j] for a in X) for j in range(3)] + [sum(a[i] * y for a, y in zip(X, Y))] for i in range(3)]
    for i in range(3):
        p = max(range(i, 3), key=lambda r: abs(M[r][i]))
        M[i], M[p] = M[p], M[i]
        for r in range(3):
            if r != i:
                f = M[r][i] / M[i][i]
                M[r] = [a - f * b for a, b in zip(M[r], M[i])]
    return [M[i][3] / M[i][i] for i in range(3)]


def main():
    path = sys.argv[1]
    skip = {0}
    if "--skip" in sys.argv:
        arg = sys.argv[sys.argv.index("--skip") + 1]
        skip = {int(v) for v in arg.split(",") if v}
    by = {}
    for r in csv.DictReader(open(path)):
        k = int(r["target"])
        e = by.setdefault(k, [float(r["target_yaw_deg"]), float(r["target_pitch_deg"]), [], []])
        my, mp = angles(r)
        e[2].append(my)
        e[3].append(mp)
    T = []
    print("tgt  true(yaw,pitch)  measured(yaw,pitch)  error   spread")
    for k in sorted(by):
        ty, tp, ys, ps = by[k]
        my, mp = st.median(ys), st.median(ps)
        mark = " (skipped)" if k in skip else ""
        print(f"{k:3d}  ({ty:+4.0f},{tp:+4.0f})    ({my:+6.1f},{mp:+6.1f})     {math.hypot(my - ty, mp - tp):5.1f}  "
              f"{st.pstdev(ys):4.1f},{st.pstdev(ps):4.1f}{mark}")
        if k not in skip:
            T.append((ty, tp, my, mp))
    X = [[t[2], t[3], 1.0] for t in T]
    cy, cp = lstsq3(X, [t[0] for t in T]), lstsq3(X, [t[1] for t in T])
    raw = [math.hypot(t[2] - t[0], t[3] - t[1]) for t in T]
    res = [math.hypot(cy[0] * t[2] + cy[1] * t[3] + cy[2] - t[0], cp[0] * t[2] + cp[1] * t[3] + cp[2] - t[1]) for t in T]
    print(f"\nerror before: mean {st.mean(raw):.2f} deg, max {max(raw):.2f} deg ({len(T)} targets)")
    print(f"error after:  mean {st.mean(res):.2f} deg, max {max(res):.2f} deg")
    print("PIMAX_EYE_CALIB=" + ",".join(f"{v:.4f}" for v in cy + cp))


if __name__ == "__main__":
    main()
