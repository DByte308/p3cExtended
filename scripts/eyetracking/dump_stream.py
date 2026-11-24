#!/usr/bin/env python3
"""dump_stream.py <stream_id> <seconds> [out.pkl]: subscribe, save raw notification payloads, print the first one's TLV tree."""
import sys, time, pickle; sys.path.insert(0, __file__.rsplit("/", 1)[0]); import ttp
sid, secs = int(sys.argv[1], 0), float(sys.argv[2]); out = sys.argv[3] if len(sys.argv) > 3 else None
c = ttp.EyeChip(); c.hello(); c.subscribe(sid); got = []; t0 = time.time()
try:
    while time.time() - t0 < secs:
        m = c.message(300)
        if m and m[0] == 0x53 and m[2] == sid: got.append((time.time() - t0, m[3]))
finally:
    c.unsubscribe(sid); c.close()
print(len(got), "notifications in", secs, "s =", round(len(got) / secs, 1), "Hz")
if out: pickle.dump(got, open(out, "wb"))
if got:
    p = got[len(got)//2][1]; print("payload", len(p), "B; TLV items:")
    for off, t, v in ttp.tlv_walk(p, 2):
        print(f"  @{off:4d} t={t} ", (v.hex()[:60] + ("..." if len(v) > 30 else "")) if isinstance(v, (bytes, bytearray)) else (v if not isinstance(v, list) else f"vec[{len(v)}] {v[:6]}"))
