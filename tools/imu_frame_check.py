#!/usr/bin/env python3
"""Check that the headset IMU frame the driver feeds to SLAM is a proper (not mirrored) frame.

Physics, no camera data needed: for a right-handed frame, the gravity direction g seen by a rotating body obeys
    dg/dt = -omega x g
In a mirrored frame the cross product flips sign. So: hold still, tip the headset over (any rotation that is not
only about the vertical), hold still again. Gravity before and after must be connected by the integrated gyro.
Both hypotheses (proper / mirrored) are fitted; the gyro scale is fitted too, so it need not be known.

Reads the headset's status HID interface directly; no XR session may be running. Usage:
    tools/imu_frame_check.py [seconds]        (default 20)
Then: headset still on the table for 3 s, tip it about 90 deg onto its side (not upside down), hold it there as still
as you can (or lean it against something) for 3 s.
The driver's conversion (-x, y, -z) is a proper rotation, so the verdict is about the raw sensor axes as the
driver reads them.
"""
import glob, math, os, struct, sys, time

DRIVER_FRAME = (-1.0, 1.0, -1.0)  # the driver's conversion of the raw axes, see to_headset_frame_vec


def find_node():
    for d in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        try:
            ue = open(os.path.join(d, "device/uevent")).read()
        except OSError:
            continue
        props = dict(l.split("=", 1) for l in ue.splitlines() if "=" in l)
        if props.get("HID_ID") == "0003:000034A4:00000040" and props.get("HID_PHYS", "").endswith("/input2"):
            return "/dev/" + os.path.basename(d)
    return None


def parse(report):
    """Two (t_us, accel_raw[3], gyro_raw[3]) samples from one 64-byte status report, or []."""
    if len(report) < 42 or report[0] != 0x53 or report[1] != 0x31:
        return []
    t = struct.unpack_from("<Q", report, 4)[0]
    off = struct.unpack_from("<H", report, 26)[0]
    out = []
    for base, ts in ((14, t), (30, t + off)):
        v = struct.unpack_from(">6h", report, base)
        out.append((ts, [DRIVER_FRAME[i] * v[i] for i in range(3)], [DRIVER_FRAME[i] * v[3 + i] for i in range(3)]))
    return out


def norm(v):
    n = math.sqrt(sum(c * c for c in v))
    return [c / n for c in v] if n else v


def angle(a, b):
    a, b = norm(a), norm(b)
    return math.degrees(math.acos(max(-1.0, min(1.0, sum(x * y for x, y in zip(a, b))))))


def rot_apply(m, v):
    return [sum(m[i][j] * v[j] for j in range(3)) for i in range(3)]


def rot_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def rot_exp(w):
    th = math.sqrt(sum(c * c for c in w))
    if th < 1e-12:
        return [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
    k = [c / th for c in w]
    K = [[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]]
    K2 = rot_mul(K, K)
    s, c = math.sin(th), 1 - math.cos(th)
    return [[(1 if i == j else 0) + s * K[i][j] + c * K2[i][j] for j in range(3)] for i in range(3)]


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
    node = find_node()
    if not node:
        sys.exit("headset status interface not found (USB 34a4:0040, interface 2); is the headset on?")
    fd = os.open(node, os.O_RDONLY | os.O_NONBLOCK)
    samples, t_end = [], time.time() + secs
    print(f"reading {node} for {secs:.0f} s: still 3 s, tip the headset about 90 deg onto its side, still 3 s")
    while time.time() < t_end:
        try:
            samples += parse(os.read(fd, 128))
        except BlockingIOError:
            time.sleep(0.001)
    os.close(fd)
    if len(samples) < 1000:
        sys.exit(f"only {len(samples)} samples; is a session running (it owns the stream)?")
    samples.sort(key=lambda s: s[0])

    # Stillness per 0.5 s block: gyro magnitude spread.
    t0 = samples[0][0]
    blocks = {}
    for s in samples:
        blocks.setdefault(int((s[0] - t0) / 500_000), []).append(s)
    keys = sorted(blocks)
    gmag = {k: sum(math.sqrt(sum(c * c for c in s[2])) for s in blocks[k]) / len(blocks[k]) for k in keys}
    # Raw gyro units: about 16 or 8 LSB per deg/s. "Still" allows hand tremor (a few deg/s, the headset may be held
    # on its side); "moving" is the tip-over itself. Blocks in between are neither.
    quiet = sorted(gmag.values())[: max(1, len(keys) // 5)]
    still_max = max(3 * (sum(quiet) / len(quiet)) + 5, 130)
    move_min = max(2 * still_max, 400)
    still = [k for k in keys if gmag[k] < still_max]
    moving = [k for k in keys if gmag[k] >= move_min]
    if not moving:
        sys.exit("no movement found: tip the headset over during the recording")
    first_move, last_move = moving[0], moving[-1]
    before = [k for k in still if k < first_move][-4:]
    after = [k for k in still if k > last_move][:4]
    if len(before) < 2 or len(after) < 2:
        sys.exit("need about 2 s (roughly) still before and after the tip-over; please repeat")

    def mean(ks, idx):
        vals = [s[idx] for k in ks for s in blocks[k]]
        return [sum(v[i] for v in vals) / len(vals) for i in range(3)]

    g1, g2 = mean(before, 1), mean(after, 1)
    bias = [(a + b) / 2 for a, b in zip(mean(before, 2), mean(after, 2))]
    tilt = angle(g1, g2)
    print(f"{len(samples)} samples, gravity turned by {tilt:.0f} deg between the still periods")
    if tilt < 20:
        sys.exit("gravity barely changed: tip the headset onto its side, not only turn it on the table")
    if tilt > 110:
        sys.exit("tipped too far (over 110 deg): both hypotheses can fit then; tip it about 90 deg and repeat")

    seg = [s for s in samples if blocks[before[-1]][-1][0] <= s[0] <= blocks[after[0]][0][0]]

    def predict(lsb_per_dps, sign):
        R = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
        for a, b in zip(seg, seg[1:]):
            dt = (b[0] - a[0]) * 1e-6
            w = [sign * math.radians((a[2][i] - bias[i]) / lsb_per_dps) * dt for i in range(3)]
            R = rot_mul(R, rot_exp(w))
        # gravity in body frame after the motion: R^T g1
        Rt = [[R[j][i] for j in range(3)] for i in range(3)]
        return angle(rot_apply(Rt, g1), g2)

    # Only the two gyro ranges the sensor is configured with (2000 and 4000 deg/s full scale), +-10 %: a free scale
    # would let the mirrored hypothesis alias onto a larger rotation.
    scales = [base * (0.90 + 0.01 * i) for base in (32768 / 2000, 32768 / 4000) for i in range(21)]
    res = {}
    for name, sign in (("proper (right-handed)", 1.0), ("mirrored", -1.0)):
        best = min((predict(s, sign), s) for s in scales)
        res[name] = best
        print(f"  {name:22} best fit {best[0]:5.1f} deg off  (gyro scale {best[1]:.1f} LSB per deg/s)")
    (pe, ps), (me, _) = res["proper (right-handed)"], res["mirrored"]
    # Hand-held rests add a few degrees of error, so the pass limit is 12 deg with a clear margin to the other fit.
    if pe < 12 and me > 2 * pe + 10:
        print(f"RESULT: the driver's IMU frame is proper (right-handed); no axis change needed. (gyro {ps:.1f} LSB/dps)")
    elif me < 12 and pe > 2 * me + 10:
        print("RESULT: the driver's IMU frame is MIRRORED: the raw axes are not right-handed as read; SLAM gets a wrong frame.")
    else:
        print("RESULT: inconclusive; repeat with a clearer tip-over and stiller rests")


if __name__ == "__main__":
    main()
