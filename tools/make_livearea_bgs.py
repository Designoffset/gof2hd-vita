# Builds livearea/bg/bg_NN.png (840x500, 256 colours) from the source art.
# ax/ay: where to anchor the crop (0 = left/top, 0.5 = centre, 1 = right/bottom),
# chosen so the Galaxy on Fire 2 logos stay in frame.
import sys
from PIL import Image
src_dir = sys.argv[1]
srcs = [("d8724e3f-image.webp", 1, 0.5), ("670cc179-image.webp", 0.5, 0), ("ba7805a0-image.png", 0.3, 0.5),
        ("d3a1ffef-image.webp", 0.5, 0.5), ("952f315b-image.jpg", 0.5, 0.5), ("6ffda733-image.jpg", 0.5, 0.5),
        ("01eaebe1-image.jpg", 0, 0.5), ("84519bf3-image.webp", 1, 0.5), ("19c33e0c-image.jpg", 0.5, 0.5),
        ("960d01d5-image.jpg", 1, 0.5), ("060647a3-image.jpg", 1, 0.5)]
W, H = 840, 500
for i, (f, ax, ay) in enumerate(srcs, 1):
    im = Image.open(f"{src_dir}/{f}").convert("RGB")
    w, h = im.size
    s = max(W / w, H / h)
    im = im.resize((round(w * s), round(h * s)), Image.LANCZOS)
    x = round((im.width - W) * ax); y = round((im.height - H) * ay)
    im = im.crop((x, y, x + W, y + H))
    im.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG).save(f"livearea/bg/bg_{i:02d}.png", optimize=True)
