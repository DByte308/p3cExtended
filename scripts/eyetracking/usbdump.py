#!/usr/bin/env python3
"""Decode a usbmon pcap (linktype 220/189) for one USB device.
usage: usbdump.py file.pcap --dev N [--summary] [--first K] [--ep 0x83]
Prints direction, endpoint, length, payload hex. Completion events only for IN, submit for OUT/control."""
import struct, sys, argparse, collections
ap = argparse.ArgumentParser(); ap.add_argument("pcap"); ap.add_argument("--dev", type=int, required=True)
ap.add_argument("--summary", action="store_true"); ap.add_argument("--first", type=int, default=0)
ap.add_argument("--ep", type=lambda x: int(x, 0)); ap.add_argument("--width", type=int, default=64)
a = ap.parse_args()
f = open(a.pcap, "rb"); gh = f.read(24)
magic = struct.unpack("<I", gh[:4])[0]; assert magic in (0xa1b2c3d4, 0xa1b23c4d), "not a little-endian pcap"
link = struct.unpack("<I", gh[20:24])[0]; hl = {220: 64, 189: 48}[link]
t0 = None; rows = []
while True:
    h = f.read(16)
    if len(h) < 16: break
    sec, us, incl, orig = struct.unpack("<IIII", h); d = f.read(incl)
    urb_id, typ, xfer, ep, dev, bus, fs, fd = struct.unpack("<QBBBBHBB", d[:16])
    ts = struct.unpack("<q", d[16:24])[0] + struct.unpack("<i", d[24:28])[0] / 1e6
    status, ulen, dlen = struct.unpack("<iII", d[28:40]); setup = d[40:48]; payload = d[hl:hl + dlen]
    if dev != a.dev: continue
    if a.ep is not None and ep != a.ep: continue
    t = chr(typ); isin = bool(ep & 0x80)
    if xfer == 2:  # control: setup on submit, data on complete for IN
        if t == "S" and not isin and not payload and fd != 0: pass
    keep = (t == "C" and isin) or (t == "S" and not isin) or xfer == 2 and (t == "S" or (t == "C" and payload))
    if not keep: continue
    t0 = ts if t0 is None else t0
    rows.append((ts - t0, ep, xfer, t, status, ulen, payload, setup if xfer == 2 and t == "S" else None))
if a.summary:
    c = collections.Counter(); b = collections.Counter()
    for r in rows: k = (hex(r[1]), ["iso","int","ctl","bulk"][r[2]]); c[k] += 1; b[k] += len(r[6])
    span = rows[-1][0] - rows[0][0] if rows else 0
    print(f"{len(rows)} events over {span:.2f}s")
    for k, n in sorted(c.items()): print(f"  ep {k[0]:>5} {k[1]:5} {n:6} pkts {b[k]:8} B  {n/max(span,1e-9):8.1f} pkt/s")
else:
    for i, (t, ep, xf, ty, st, ul, p, su) in enumerate(rows):
        if a.first and i >= a.first: break
        s = f" setup={su.hex()}" if su else ""
        print(f"{t:9.4f} {'IN ' if ep & 0x80 else 'OUT'} ep{ep & 0x7f} {['iso','int','ctl','blk'][xf]} st={st} len={ul}{s} {p[:a.width].hex()}")
