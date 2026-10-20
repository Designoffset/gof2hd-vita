# Make X_ipad.aei from X_ipad_large.aei (RGBA8888 AEimage), scaled to the
# iPad size the game's IMAGE_OFFSETS_IPAD table expects: 125/200 = 0.625.
import sys, os, glob, struct
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
S = 125 / 200
os.makedirs(dst, exist_ok=True)
n = 0
for f in sorted(glob.glob(os.path.join(src, '*_ipad_large.aei'))):
    d = open(f, 'rb').read()
    assert d[:8] == b'AEimage\0' and d[8] == 1, f
    w, h = struct.unpack('<HH', d[9:13])
    hdr = len(d) - w * h * 4
    assert hdr == 25, (f, hdr)
    rw, rh = struct.unpack('<HH', d[19:23])
    nw, nh = round(w * S), round(h * S)
    px = d[hdr:]
    # resize each channel on its own so the channel order doesn't matter
    chans = [Image.frombytes('L', (w, h), px[c::4]).resize((nw, nh), Image.LANCZOS) for c in range(4)]
    out = bytearray(nw * nh * 4)
    for c in range(4):
        out[c::4] = chans[c].tobytes()
    hd = bytearray(d[:hdr])
    hd[9:13] = struct.pack('<HH', nw, nh)
    hd[19:23] = struct.pack('<HH', round(rw * S), round(rh * S))
    name = os.path.basename(f).replace('_ipad_large.aei', '_ipad.aei')
    open(os.path.join(dst, name), 'wb').write(bytes(hd) + bytes(out))
    n += 1
print(n, 'written')
