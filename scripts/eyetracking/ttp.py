"""Minimal Tobii TTP client for the Pimax Crystal Super's EyeChip (Tobii VR4, USB 2104:0220).
Own implementation from observed wire format (tobiifree README/ARCHITECTURE for framing; omarchyvr docs for the
opening control transfers). Needs pyusb and access to the device (config/99-tobii-eyechip.rules).
The headset must have received CmdEyeTrackEnable (0x3C) first (it powers the chip; Monado does it at bring-up)."""
import struct, time, usb.core, usb.util

HELLO = bytes.fromhex("0000170000002800000009000100000001000100010002000100030001000400010005000100060001000700010008")
OP_HELLO, OP_SUBSCRIBE, OP_UNSUBSCRIBE = 0x3E8, 0x4C4, 0x4CE
STREAM_WEARABLE, STREAM_GAZE, STREAM_IMAGE = 1285, 1289, 1291

def frame(seq, op, payload=b""):
    ttp = struct.pack(">6I", 0x51, seq, 0, op, 0, len(payload))
    return b"\0\0\0\0" + struct.pack("<I", len(ttp) + len(payload)) + ttp + payload

def stream_payload(sid):
    p = bytearray([0,0,2,0,0,0,4,0,0,0,0,0x17,0,0,0,4,0,0,0,0]); p[9] = sid >> 8; p[10] = sid & 255
    return bytes(p)

def tlv_walk(b, pos=0):
    """Yield (offset, type, value) for the TLV items of a TTP payload; stops at the first unknown type."""
    while pos < len(b):
        t = b[pos]
        if t not in (2, 3, 4, 5, 6, 21, 23) or pos + 5 > len(b): yield (pos, "?", b[pos:pos+8].hex()); return
        s = pos + 5
        if t == 2: v, e = struct.unpack(">I", b[s:s+4])[0], s + 4
        elif t == 3: v, e = struct.unpack(">i", b[s:s+4])[0] / 65536.0, s + 4
        elif t == 4: v, e = struct.unpack(">q", b[s:s+8])[0] / 4294967296.0, s + 8
        elif t == 5: v, e = struct.unpack(">I", b[s:s+4])[0], s + 4
        elif t == 6: v, e = struct.unpack(">q", b[s:s+8])[0], s + 8
        else:
            n = struct.unpack(">I", b[s:s+4])[0]; nb = n * 4 if t == 23 else n; e = s + 4 + nb
            v = b[s+4:e] if t == 21 else list(struct.unpack(">%dI" % n, b[s+4:e]))
        yield (pos, t, v); pos = e

class EyeChip:
    def __init__(self):
        self.d = usb.core.find(idVendor=0x2104, idProduct=0x0220)
        if self.d is None: raise RuntimeError("EyeChip not found (send CmdEyeTrackEnable first, check udev rule)")
        usb.util.claim_interface(self.d, 0); self.seq = 0; self.buf = b""
        self.d.ctrl_transfer(0xC1, 48, 0, 0, 512, timeout=1500)   # info text (read-only)
        self.d.ctrl_transfer(0xC1, 70, 1, 0, 8, timeout=1500)     # device time counter (read-only)
        self.d.ctrl_transfer(0x41, 65, 0, 0, None, timeout=1500)  # session open (needed on every connect)
        time.sleep(0.2)
    def send(self, op, payload=b""):
        self.seq += 1; self.d.write(0x05, frame(self.seq, op, payload), timeout=2000); return self.seq
    def _chunk(self, ms):
        try: self.buf += bytes(self.d.read(0x83, 16384, timeout=ms))
        except usb.core.USBTimeoutError: pass
    def message(self, ms=500):
        """Return (magic, seq, op, payload) of the next complete TTP message, or None."""
        self._chunk(ms)
        if len(self.buf) < 8: return None
        n = struct.unpack("<I", self.buf[4:8])[0]
        if len(self.buf) < n: self._chunk(ms)
        if len(self.buf) < n: return None
        m, seq, fl, op, z, pl = struct.unpack(">6I", self.buf[8:32]); payload = self.buf[32:32+pl]; self.buf = self.buf[n:]
        return m, seq, op, payload
    def request(self, op, payload=b"", ms=1500):
        s = self.send(op, payload)
        while True:
            m = self.message(ms)
            if m is None: return None
            if m[0] == 0x52 and m[1] == s: return m[3]
    def hello(self): return self.request(OP_HELLO, HELLO)
    def subscribe(self, sid): return self.request(OP_SUBSCRIBE, stream_payload(sid))
    def unsubscribe(self, sid): return self.request(OP_UNSUBSCRIBE, stream_payload(sid))
    def close(self): usb.util.release_interface(self.d, 0)
