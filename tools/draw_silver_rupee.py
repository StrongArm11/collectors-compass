"""Draw the mod's own Silver Rupee map icon (128x128 RGBA) in the style of the game's map icons: a flat, faceted
rupee with a thick dark-brown outline on a transparent background. Original artwork (simple geometry), not taken
from the game. usage: draw_silver_rupee.py <out.png>"""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

S = 4                      # supersampling
W = 128 * S
OUT = sys.argv[1]
cx = 64 * S
def P(x, y): return (cx + x * S, y * S)

# outer hexagon (vertical rupee): top and bottom points, straight sides
half_w, top, shoulder_t, shoulder_b, bottom = 24, 22, 42, 86, 106
outer = [P(0, top), P(half_w, shoulder_t), P(half_w, shoulder_b), P(0, bottom), P(-half_w, shoulder_b), P(-half_w, shoulder_t)]
# inner face (the flat centre of the gem)
iw, it, ist, isb, ib = 11, 36, 48, 80, 92
inner = [P(0, it), P(iw, ist), P(iw, isb), P(0, ib), P(-iw, isb), P(-iw, ist)]

img = Image.new('RGBA', (W, W), (0, 0, 0, 0))
dr = ImageDraw.Draw(img)
outline = (83, 28, 7, 255)
# thick outline: draw the outer shape grown by the outline width
grow = 7 * S
og = [P(0, top - 8), P(half_w + 7, shoulder_t - 4), P(half_w + 7, shoulder_b + 4), P(0, bottom + 8),
      P(-half_w - 7, shoulder_b + 4), P(-half_w - 7, shoulder_t - 4)]
dr.polygon(og, fill=outline)
# facets: each between an outer edge and the matching inner edge
light, mid, dark, darker, top_c, bot_c = (236, 241, 245), (198, 207, 216), (150, 161, 173), (118, 128, 141), (222, 229, 236), (132, 142, 156)
facets = [
    ([outer[5], outer[0], inner[0], inner[5]], top_c),     # upper left bevel
    ([outer[0], outer[1], inner[1], inner[0]], mid),       # upper right bevel
    ([outer[1], outer[2], inner[2], inner[1]], dark),      # right side
    ([outer[2], outer[3], inner[3], inner[2]], darker),    # lower right bevel
    ([outer[3], outer[4], inner[4], inner[3]], bot_c),     # lower left bevel
    ([outer[4], outer[5], inner[5], inner[4]], mid),       # left side
]
for poly, col in facets: dr.polygon(poly, fill=col + (255,))
dr.polygon(inner, fill=light + (255,))
# thin dark lines between facets, like the game's icons' inner strokes
for a, b in zip(outer, inner): dr.line([a, b], fill=(96, 52, 30, 255), width=int(1.6 * S))
dr.line(inner + [inner[0]], fill=(110, 70, 48, 255), width=int(1.6 * S))
# a small sparkle highlight on the face
dr.polygon([P(-5, 52), P(-2, 50), P(-2, 66), P(-5, 68)], fill=(255, 255, 255, 230))

small = img.resize((128, 128), Image.LANCZOS)
a = np.array(small).astype(np.float32)
# the game's icons have a faint painted grain: add a little noise to the colour (not the alpha)
rng = np.random.default_rng(7)
noise = rng.normal(0, 4.0, a.shape[:2])[..., None]
a[..., :3] = np.clip(a[..., :3] + noise, 0, 255)
# transparent pixels keep the outline colour (like the game's textures), so filtering at the edge stays dark
mask = a[..., 3] < 1
a[mask, 0], a[mask, 1], a[mask, 2] = 83, 28, 7
Image.fromarray(a.astype(np.uint8)).save(OUT)
print('wrote', OUT)
