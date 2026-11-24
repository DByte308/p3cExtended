#!/usr/bin/env python3
"""Probe the Tobii EyeChip (2104:0220) with the TTP hello used by desktop Tobii trackers.
Framing (from Aetherall/tobiifree, GPL-3.0, for the Eye Tracker 5; we only reuse the observed wire format):
  host->dev : [0x00,0,0,0][len_LE u32 = ttp_len][TTP header 24B big-endian: magic,seq,flag,op,0,plen][payload]
  dev->host : [0x01,0,0,0][len_LE u32 INCLUDING the 8-byte envelope][TTP header][payload]
usage: probe_ttp.py [out_ep=0x05]"""
import struct, sys, usb.core, usb.util
OUT = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x05
IN = 0x83
HELLO = bytes([0,0,0x17,0,0,0,0x28,0,0,0,0x09, 0,1,0,0,0,1,0,1,0,1,0,2, 0,1,0,3,0,1,0,4,0,1,0,5,0,1,0,6,0,1,0,7,0,1,0,8])
def frame(seq, op, payload=b""):
    ttp = struct.pack(">6I", 0x51, seq, 0, op, 0, len(payload)) + payload
    return b"\x00\x00\x00\x00" + struct.pack("<I", len(ttp)) + ttp
def parse(buf):
    out = []
    while len(buf) >= 8:
        n = struct.unpack("<I", buf[4:8])[0]
        if n < 8 + 24 or len(buf) < n: break
        magic, seq, flag, op, z, plen = struct.unpack(">6I", buf[8:32])
        out.append(dict(dir=buf[0], magic=hex(magic), seq=seq, flag=hex(flag), op=hex(op), plen=plen, payload=buf[32:n]))
        buf = buf[n:]
    return out, buf
def read_all(d, ms=1500):
    acc = b""
    while True:
        try: acc += bytes(d.read(IN, 4096, timeout=ms)); ms = 300
        except usb.core.USBTimeoutError: return acc
d = usb.core.find(idVendor=0x2104, idProduct=0x0220)
usb.util.claim_interface(d, 0)
try:
    pkt = frame(1, 0x3e8, HELLO)
    print("send hello", len(pkt), "bytes on EP", hex(OUT)); d.write(OUT, pkt, timeout=2000)
    raw = read_all(d)
    print("received", len(raw), "bytes:", raw[:96].hex())
    msgs, rest = parse(raw)
    for m in msgs: print({k: (v.hex() if isinstance(v, bytes) else v) for k, v in m.items()})
finally:
    usb.util.release_interface(d, 0)
