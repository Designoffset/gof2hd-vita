# Builds data/cockpit_slim.png: an original slim cockpit frame (1280x725 RGBA,
# the game's framebuffer size) drawn over the first-person view.
from PIL import Image, ImageDraw, ImageFilter
S = 2
W, H = 1280 * S, 725 * S
im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
def P(pts): return [(x * S, y * S) for x, y in pts]
METAL, METAL2, EDGE, DARK = (32, 38, 46, 255), (46, 54, 64, 255), (92, 106, 120, 255), (14, 17, 21, 255)
CYAN, ORANGE = (57, 200, 255, 255), (255, 138, 42, 255)

# canopy struts: from the top, angling out to the lower corners
L = [(330, 0), (372, 0), (70, 600), (20, 600)]
R = [(908, 0), (950, 0), (1260, 600), (1210, 600)]
for q in (L, R):
    d.polygon(P(q), fill=METAL)
# inner edge highlight on each strut
d.line(P([(372, 0), (70, 600)]), fill=EDGE, width=3 * S)
d.line(P([(908, 0), (1210, 600)]), fill=EDGE, width=3 * S)
d.line(P([(330, 0), (20, 600)]), fill=DARK, width=2 * S)
d.line(P([(950, 0), (1260, 600)]), fill=DARK, width=2 * S)
# top canopy rail between the struts
d.polygon(P([(330, 0), (950, 0), (930, 18), (350, 18)]), fill=METAL)
d.line(P([(350, 18), (930, 18)]), fill=EDGE, width=2 * S)
# small cyan running lights along the struts
for t in (0.25, 0.45, 0.65):
    for (x0, y0), (x1, y1) in (((351, 0), (45, 600)), ((929, 0), (1235, 600))):
        x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
        d.ellipse(P([(x - 3, y - 3), (x + 3, y + 3)]), fill=CYAN)

# low dashboard: sides rise into the strut feet, centre dips to keep the view open
dash = [(0, 560), (90, 560), (170, 640), (470, 660), (560, 650), (720, 650), (810, 660),
        (1110, 640), (1190, 560), (1280, 560), (1280, 725), (0, 725)]
d.polygon(P(dash), fill=METAL)
d.line(P(dash[:10]), fill=EDGE, width=3 * S)
# a second, darker console lip
lip = [(170, 660), (470, 678), (810, 678), (1110, 660), (1280, 640), (1280, 725), (0, 725), (0, 640)]
d.polygon(P(lip), fill=METAL2)
d.line(P(lip[:5]), fill=DARK, width=2 * S)

# centre display (radar-style ring) and two side readouts
cx, cy = 640, 690
d.rounded_rectangle(P([(575, 655), (705, 722)]), radius=8 * S, fill=DARK, outline=EDGE, width=2 * S)
d.ellipse(P([(cx - 26, cy - 26), (cx + 26, cy + 26)]), outline=CYAN, width=2 * S)
d.ellipse(P([(cx - 12, cy - 12), (cx + 12, cy + 12)]), outline=(57, 200, 255, 140), width=1 * S)
d.line(P([(cx - 30, cy), (cx + 30, cy)]), fill=(57, 200, 255, 120), width=1 * S)
d.line(P([(cx, cy - 30), (cx, cy + 30)]), fill=(57, 200, 255, 120), width=1 * S)
for x0 in (380, 800):
    d.rounded_rectangle(P([(x0, 676), (x0 + 100, 716)]), radius=6 * S, fill=DARK, outline=EDGE, width=2 * S)
    for k in range(4):
        w = (90, 60, 75, 40)[k]
        d.line(P([(x0 + 8, 684 + k * 8), (x0 + 8 + w * 0.9, 684 + k * 8)]), fill=CYAN if k % 2 == 0 else (57, 200, 255, 150), width=2 * S)
# orange indicator lights
for x in (250, 275, 300, 980, 1005, 1030):
    d.ellipse(P([(x - 4, 690 - 4), (x + 4, 690 + 4)]), fill=ORANGE)

# soft glow on the lights
glow = im.filter(ImageFilter.GaussianBlur(6 * S))
im = Image.alpha_composite(glow, im)
im = im.resize((1280, 725), Image.LANCZOS)
im.save("data/cockpit_slim.png")
# preview over a dark starfield
bg = Image.new("RGBA", (1280, 725), (12, 18, 40, 255))
Image.alpha_composite(bg, im).convert("RGB").save("/tmp/claude-0/cockpit_preview.png")
