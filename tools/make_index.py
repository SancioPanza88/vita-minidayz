#!/usr/bin/env python3
"""Writes game/mdz_images.json: image sizes, so the Vita does not open ~1300 files at startup."""
import os, json, struct
root = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'game')
idx = {}
for d, _, files in os.walk(root):
    for f in files:
        if not f.lower().endswith('.png'):
            continue
        p = os.path.join(d, f)
        with open(p, 'rb') as fh:
            h = fh.read(24)
        if h[:8] == b'\x89PNG\r\n\x1a\n':
            idx[os.path.relpath(p, root).replace(os.sep, '/')] = list(struct.unpack('>II', h[16:24]))
json.dump(idx, open(os.path.join(root, 'mdz_images.json'), 'w'), separators=(',', ':'))
print(len(idx), 'images indexed')
