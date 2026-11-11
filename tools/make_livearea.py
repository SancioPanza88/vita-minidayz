#!/usr/bin/env python3
"""Builds the LiveArea images (build_assets/sce_sys) from the game's own icon/logo."""
import os, sys
from PIL import Image
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
game = os.path.join(root, 'game')
out = os.path.join(root, 'build_assets', 'sce_sys')
os.makedirs(os.path.join(out, 'livearea', 'contents'), exist_ok=True)
icon = Image.open(os.path.join(game, 'icon-256.png')).convert('RGBA')
logo = icon

def on_bg(size, img, scale, color=(20, 0, 0)):
    bg = Image.new('RGBA', size, color + (255,))
    w, h = img.size
    k = min(size[0] * scale / w, size[1] * scale / h)
    im = img.resize((max(1, int(w * k)), max(1, int(h * k))), Image.NEAREST)
    bg.alpha_composite(im, ((size[0] - im.width) // 2, (size[1] - im.height) // 2))
    return bg

def save8(img, path):  # Vita wants indexed (8-bit) PNGs for LiveArea
    img.convert('RGB').quantize(256).save(path, optimize=True)

save8(icon.resize((128, 128), Image.LANCZOS), os.path.join(out, 'icon0.png'))
save8(on_bg((960, 544), logo, 0.6), os.path.join(out, 'pic0.png'))
save8(on_bg((840, 500), logo, 0.6), os.path.join(out, 'livearea', 'contents', 'bg.png'))
save8(on_bg((280, 158), logo, 0.8), os.path.join(out, 'livearea', 'contents', 'startup.png'))
print('LiveArea assets written to', out)
