# Builds data/controls.png: the in-game controls card shown while Select is held.
# A line drawing of the Vita with call-outs, drawn at 2x and scaled down.
from PIL import Image, ImageDraw, ImageFont
S = 2
W, H = 960 * S, 544 * S
F = "/usr/share/fonts/truetype/dejavu/"
def font(n, s): return ImageFont.truetype(F + n, s * S)
title, lab, small, sym = font("DejaVuSans-Bold.ttf", 22), font("DejaVuSans.ttf", 15), font("DejaVuSans.ttf", 13), font("DejaVuSans-Bold.ttf", 13)
BG, GRID, LINE, TXT, DIM, ACC = (8, 20, 34), (16, 34, 54), (205, 222, 240), (235, 240, 248), (150, 170, 195), (255, 176, 40)
im = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(im)
for x in range(0, W, 40 * S): d.line([(x, 0), (x, H)], fill=GRID, width=1)
for y in range(0, H, 40 * S): d.line([(0, y), (W, y)], fill=GRID, width=1)
def P(x, y): return (x * S, y * S)
def box(x0, y0, x1, y1): return [x0 * S, y0 * S, x1 * S, y1 * S]
lw = 2 * S

d.text(P(480, 26), "CONTROLS", font=title, fill=TXT, anchor="mm")
d.text(P(480, 50), "hold SELECT to show this screen", font=small, fill=DIM, anchor="mm")

# --- Vita body
cx, cy = 480, 228
bx0, bx1, by0, by1 = 306, 654, 150, 306
d.rounded_rectangle(box(bx0, by0, bx1, by1), radius=78 * S, outline=LINE, width=lw)
d.rounded_rectangle(box(384, 164, 576, 292), radius=6 * S, outline=LINE, width=lw)        # screen
# shoulder buttons
d.arc(box(bx0 + 6, by0 - 8, bx0 + 120, by0 + 60), 200, 265, fill=LINE, width=lw * 2)
d.arc(box(bx1 - 120, by0 - 8, bx1 - 6, by0 + 60), 275, 340, fill=LINE, width=lw * 2)
# D-pad
dx, dy = 346, 202
for (ox, oy) in ((0, -14), (0, 14), (-14, 0), (14, 0)):
    d.rounded_rectangle(box(dx + ox - 7, dy + oy - 7, dx + ox + 7, dy + oy + 7), radius=2 * S, outline=LINE, width=S)
# sticks
lsx, lsy, rsx, rsy = 360, 260, 600, 260
for (x, y) in ((lsx, lsy), (rsx, rsy)):
    d.ellipse(box(x - 15, y - 15, x + 15, y + 15), outline=LINE, width=lw)
    d.ellipse(box(x - 8, y - 8, x + 8, y + 8), outline=LINE, width=S)
# face buttons
fx, fy = 614, 202
face = {"tri": (fx, fy - 17), "cir": (fx + 17, fy), "crs": (fx, fy + 17), "sqr": (fx - 17, fy)}
for k, (x, y) in face.items():
    d.ellipse(box(x - 8, y - 8, x + 8, y + 8), outline=LINE, width=S)
glyph = {"tri": "△", "cir": "○", "crs": "✕", "sqr": "□"}
for k, (x, y) in face.items():
    d.text(P(x, y), glyph[k], font=sym, fill=LINE, anchor="mm")
# start / select
st, se = (628, 290), (604, 290)
for (x, y) in (st, se):
    d.ellipse(box(x - 5, y - 4, x + 5, y + 4), outline=LINE, width=S)
# PS button
d.ellipse(box(326, 280, 340, 294), outline=LINE, width=S)

def callout(pts, text, anchor, color=TXT):
    d.line([P(*p) for p in pts], fill=ACC, width=S + 1)
    x, y = pts[-1]
    d.ellipse(box(pts[0][0] - 3, pts[0][1] - 3, pts[0][0] + 3, pts[0][1] + 3), fill=ACC)
    lines = text.split("\n")
    off = 6 if anchor[0] == "l" else -6
    for i, t in enumerate(lines):
        d.text(P(x + off, y + i * 19 - (len(lines) - 1) * 9.5), t, font=lab, fill=color, anchor=anchor)

# left side
callout([(336, 152), (290, 110), (270, 110)], "L  Barrel roll left", "rm")
callout([(dx - 21, dy), (270, 200)], "D-pad ↑  Autopilot\nD-pad ↓  Fast forward / jump\nD-pad →  Action menu", "rm")
callout([(lsx - 16, lsy + 6), (290, 286), (270, 286)], "Left stick  Steer", "rm")
# right side
callout([(624, 152), (670, 110), (690, 110)], "R  Barrel roll right", "lm")
callout([(face["tri"][0] + 8, face["tri"][1] - 6), (670, 160), (690, 160)], "△  Boost", "lm")
callout([(face["cir"][0] + 9, face["cir"][1]), (690, 196)], "○  Change camera", "lm", ACC)
callout([(face["crs"][0] + 8, face["crs"][1] + 5), (670, 226), (690, 226)], "✕  Fire", "lm")
callout([(face["sqr"][0] - 2, face["sqr"][1] + 9), (590, 232), (650, 254), (690, 254)], "□  Secondary weapon", "lm")
callout([(rsx + 16, rsy + 6), (670, 284), (690, 284)], "Right stick ↑↓  Speed up / down", "lm")
callout([(st[0] + 4, st[1] + 4), (660, 316), (690, 316)], "START  Pause menu", "lm")
callout([(se[0], se[1] + 5), (600, 342), (690, 342)], "SELECT (hold)  This screen", "lm")
# screen label
d.text(P(480, 220), "Touch screen", font=lab, fill=DIM, anchor="mm")
d.text(P(480, 240), "works too", font=small, fill=DIM, anchor="mm")

# menus line
d.text(P(480, 370), "IN MENUS & DIALOGUE:   Left stick / D-pad  move cursor     ✕ or R  select     ○  back",
       font=small, fill=DIM, anchor="mm")

# circle notice
y0 = 392
d.rounded_rectangle(box(30, y0, 930, 526), radius=10 * S, fill=(40, 30, 12), outline=ACC, width=S + 1)
d.text(P(50, y0 + 20), "○  CIRCLE CHANGES THE CAMERA", font=font("DejaVuSans-Bold.ttf", 16), fill=ACC, anchor="lm")
for i, t in enumerate([
    "Each press cycles:   Chase view  →  Turret view (if your ship has one)  →  Free look  →  back to Chase view.",
    "In free look the left stick moves the camera instead of the ship.",
    "If steering seems to stop working, press ○ until you are back in chase view. Every flight starts in chase view.",
]):
    d.text(P(50, y0 + 50 + i * 24), t, font=small, fill=TXT, anchor="lm")

im.resize((960, 544), Image.LANCZOS).save("data/controls.png")
