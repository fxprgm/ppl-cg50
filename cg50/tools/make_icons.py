#!/usr/bin/env python3
"""Draw the add-in's MAIN MENU icons (92x64, fxsdk's assets-cg/icon-uns.png / icon-sel.png)
with Pillow: a spirograph curve and "PPL"."""
import math
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def font(size):
    for f in ("/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/System/Library/Fonts/Helvetica.ttc"):
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


def icon(selected):
    bg = (20, 30, 60) if not selected else (30, 80, 170)
    im = Image.new("RGB", (92, 64), (255, 255, 255))
    d = ImageDraw.Draw(im)
    # the OS writes the name over the bottom ~16 rows: the picture stays above
    d.rounded_rectangle((2, 2, 89, 47), 7, fill=bg)
    # a hypotrochoid, R=5 r=3 d=5 (as spiro.ppl draws)
    R, r, dd, s = 5.0, 3.0, 5.0, 2.0
    pts = []
    for i in range(0, 1900):
        t = i * 0.01
        x = (R - r) * math.cos(t) + dd * math.cos((R - r) / r * t)
        y = (R - r) * math.sin(t) - dd * math.sin((R - r) / r * t)
        pts.append((24 + x * s * 2.6, 25 + y * s * 2.6))
    d.line(pts, fill=(255, 200, 60), width=1)
    d.text((47, 5), "HP", font=font(13), fill=(200, 210, 255))
    d.text((44, 20), "PPL", font=font(17), fill=(255, 255, 255))
    return im


os.makedirs(os.path.join(HERE, "assets-cg"), exist_ok=True)
icon(False).save(os.path.join(HERE, "assets-cg", "icon-uns.png"))
icon(True).save(os.path.join(HERE, "assets-cg", "icon-sel.png"))
print("wrote assets-cg/icon-uns.png, icon-sel.png")
