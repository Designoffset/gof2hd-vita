# Make X_ipad.aei (half size) from X_ipad_large.aei (RGBA8888 AEimage).
import sys, os, glob, struct
src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
n = 0
for f in sorted(glob.glob(os.path.join(src, '*_ipad_large.aei'))):
    d = open(f, 'rb').read()
    assert d[:8] == b'AEimage\0' and d[8] == 1, f
    w, h = struct.unpack('<HH', d[9:13])
    hdr = len(d) - w * h * 4
    assert hdr == 25, (f, hdr)
    rw, rh = struct.unpack('<HH', d[19:23])
    px = d[hdr:]
    nw, nh = w // 2, h // 2
    out = bytearray(nw * nh * 4)
    for y in range(nh):
        for x in range(nw):
            o = (y * nw + x) * 4
            for c in range(4):  # plain 2x2 average, channel-order agnostic
                o00 = ((2 * y) * w + 2 * x) * 4 + c
                o10 = o00 + w * 4
                out[o + c] = (px[o00] + px[o00 + 4] + px[o10] + px[o10 + 4] + 2) // 4
    nh_ = bytearray(d[:hdr])
    nh_[9:13] = struct.pack('<HH', nw, nh)
    nh_[19:23] = struct.pack('<HH', (rw + 1) // 2, (rh + 1) // 2)
    name = os.path.basename(f).replace('_ipad_large.aei', '_ipad.aei')
    open(os.path.join(dst, name), 'wb').write(bytes(nh_) + bytes(out))
    n += 1
print(n, 'written')
