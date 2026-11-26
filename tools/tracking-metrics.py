#!/usr/bin/env python3
"""Analyze controller & SLAM tracking health from CONSTELLATION_POSE_LOG CSV and/or Monado logs.

Usage:
    tools/tracking-metrics.py [--csv /path/to/pose.csv] [--log /path/to/monado.log]
"""

from __future__ import annotations

import argparse
import csv
import math
import re
import sys
from collections import defaultdict
from pathlib import Path


def quat_angle_deg(q1: tuple[float, float, float, float], q2: tuple[float, float, float, float]) -> float:
    dot = abs(q1[0] * q2[0] + q1[1] * q2[1] + q1[2] * q2[2] + q1[3] * q2[3])
    return 2.0 * math.degrees(math.acos(min(1.0, dot)))


def analyze_csv(path: Path) -> None:
    per_dev: dict[str, list[tuple[int, tuple[float, float, float], tuple[float, float, float, float]]]] = defaultdict(list)
    with path.open("r", encoding="utf-8", errors="replace") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                dev = row.get("device") or row.get("device_id") or row.get("dev") or "0"
                ts = int(row.get("ts_ns") or row.get("timestamp_ns") or row.get("ts") or 0)
                # Prefer world-space pose (wx,wy,wz,wqx..wqw) from CONSTELLATION_POSE_LOG over camera-space (x,y,z)
                if row.get("wx") not in (None, ""):
                    pos = (float(row["wx"]), float(row["wy"]), float(row["wz"]))
                    quat = (float(row["wqx"]), float(row["wqy"]), float(row["wqz"]), float(row["wqw"]))
                else:
                    pos = (
                        float(row.get("px") or row.get("x") or 0.0),
                        float(row.get("py") or row.get("y") or 0.0),
                        float(row.get("pz") or row.get("z") or 0.0),
                    )
                    quat = (
                        float(row.get("qx") or 0.0),
                        float(row.get("qy") or 0.0),
                        float(row.get("qz") or 0.0),
                        float(row.get("qw") or 1.0),
                    )
                per_dev[dev].append((ts, pos, quat))
            except (ValueError, TypeError):
                continue

    if not per_dev:
        print(f"No pose rows parsed from {path}")
        return

    print(f"=== Pose CSV: {path} ===")
    for dev in sorted(per_dev):
        rows = sorted(per_dev[dev], key=lambda r: r[0])
        if len(rows) < 2:
            print(f"  device {dev}: {len(rows)} sample(s)")
            continue
        dur_s = max(1e-6, (rows[-1][0] - rows[0][0]) / 1e9)
        dur_min = dur_s / 60.0
        dropouts = 0
        max_gap_ms = 0.0
        jumps_5cm = 0
        jumps_10cm = 0
        ang_jumps_20 = 0
        still_deltas_mm: list[float] = []

        for (t0, p0, q0), (t1, p1, q1) in zip(rows, rows[1:]):
            dt_s = (t1 - t0) / 1e9
            if dt_s <= 0:
                continue
            dt_ms = dt_s * 1000.0
            if dt_ms > max_gap_ms:
                max_gap_ms = dt_ms
            if dt_ms > 200.0:
                dropouts += 1
            dp = math.dist(p0, p1)
            da = quat_angle_deg(q0, q1)
            if dt_s < 0.2:
                if dp > 0.05:
                    jumps_5cm += 1
                if dp > 0.10:
                    jumps_10cm += 1
                if da > 20.0:
                    ang_jumps_20 += 1
                if dp / dt_s < 0.02 and da / dt_s < 5.0:
                    still_deltas_mm.append(dp * 1000.0)

        rms_still_mm = (
            math.sqrt(sum(d * d for d in still_deltas_mm) / len(still_deltas_mm))
            if still_deltas_mm
            else 0.0
        )
        print(
            f"  device {dev}: {len(rows)} solves over {dur_s:.1f}s ({len(rows)/dur_s:.1f} Hz) | "
            f"dropouts(>200ms)={dropouts} max_gap={max_gap_ms:.0f}ms | "
            f"jumps >5cm={jumps_5cm} ({jumps_5cm/dur_min:.1f}/min), >10cm={jumps_10cm}, >20deg={ang_jumps_20} | "
            f"still_rms={rms_still_mm:.2f}mm"
        )


def analyze_log(path: Path) -> None:
    text = path.read_text(encoding="utf-8", errors="replace")
    rej_re = re.compile(r"Rejected solve #(\d+) of (\d+) gated for device (\d+)")
    esc_re = re.compile(r"Prior of device (\d+) lost \(#(\d+)\)")
    ref3d_re = re.compile(r"Refused re-acquisition #(\d+) of device (\d+)")
    ref2d_re = re.compile(r"Refused 2D re-acquisition #(\d+) of device (\d+)")
    steal_re = re.compile(r"Refused in-gate steal #(\d+) of device (\d+)")
    slam_re = re.compile(r"SLAM runaway detected \(([^)]+)\)")

    rej: dict[str, tuple[int, int]] = {}
    esc: dict[str, int] = {}
    ref3d: dict[str, int] = {}
    ref2d: dict[str, int] = {}
    steal: dict[str, int] = {}
    slam_reasons: dict[str, int] = defaultdict(int)

    for line in text.splitlines():
        if m := rej_re.search(line):
            rej[m.group(3)] = (int(m.group(1)), int(m.group(2)))
        elif m := esc_re.search(line):
            esc[m.group(1)] = max(esc.get(m.group(1), 0), int(m.group(2)))
        elif m := ref3d_re.search(line):
            ref3d[m.group(2)] = max(ref3d.get(m.group(2), 0), int(m.group(1)))
        elif m := ref2d_re.search(line):
            ref2d[m.group(2)] = max(ref2d.get(m.group(2), 0), int(m.group(1)))
        elif m := steal_re.search(line):
            steal[m.group(2)] = max(steal.get(m.group(2), 0), int(m.group(1)))
        elif m := slam_re.search(line):
            slam_reasons[m.group(1)] += 1

    print(f"=== Monado Log: {path} ===")
    devs = sorted(set(rej) | set(esc) | set(ref3d) | set(ref2d) | set(steal))
    if not devs:
        print("  no constellation gate events found")
    for dev in devs:
        r, c = rej.get(dev, (0, 0))
        pct = (100.0 * r / c) if c else 0.0
        print(
            f"  device {dev}: prior_gate_rejected={r}/{c} ({pct:.1f}%) | "
            f"escapes={esc.get(dev, 0)} | ref_3d={ref3d.get(dev, 0)} | "
            f"ref_2d={ref2d.get(dev, 0)} | ref_in_gate_steal={steal.get(dev, 0)}"
        )
    if slam_reasons:
        summary = ", ".join(f"{k}: {v}" for k, v in sorted(slam_reasons.items()))
        print(f"  SLAM runaway resets: {sum(slam_reasons.values())} ({summary})")
    else:
        print("  SLAM runaway resets: 0")


def main() -> int:
    ap = argparse.ArgumentParser(description="Summarize controller & SLAM tracking metrics")
    ap.add_argument("--csv", type=Path, help="CONSTELLATION_POSE_LOG CSV file")
    ap.add_argument("--log", type=Path, help="Monado service log file")
    args = ap.parse_args()
    if not args.csv and not args.log:
        ap.error("provide at least one of --csv or --log")
    if args.csv:
        analyze_csv(args.csv)
    if args.log:
        analyze_log(args.log)
    return 0


if __name__ == "__main__":
    sys.exit(main())
