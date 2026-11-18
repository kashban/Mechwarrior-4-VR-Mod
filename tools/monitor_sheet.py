# A picture of a cockpit's monitors as the pilot sees them, with what each shows: to check a <name>.ini.
#   python tools/monitor_sheet.py <name>.mwc [<name>.mwc ...] <out.png>
# The mesh as grey dots, each monitor filled, with its number and the part of the HUD its .ini gives it.
import configparser
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cockpit_convert as cc

W, H, FOV, PITCH = 640, 480, 115.0, -22.0
ROLE = {v: k for k, v in cc.HUD_PARTS.items()}
COLOUR = {
    'radar': (60, 200, 60),
    'target': (220, 170, 40),
    'damage': (220, 80, 80),
    'lance': (80, 160, 240),
    'weapons': (200, 90, 220),
    None: (70, 70, 70),
}


def project(P, eye):
    """view space (x right, y up, z forward) -> pixels; None where behind the eye"""
    p = P - eye
    c, s = math.cos(math.radians(PITCH)), math.sin(math.radians(PITCH))
    y = p[:, 1] * c - p[:, 2] * s
    z = p[:, 1] * s + p[:, 2] * c
    f = 0.5 * W / math.tan(math.radians(FOV) / 2)
    ok = z > 0.05
    zz = np.where(ok, z, 1)
    return np.stack([W / 2 + f * p[:, 0] / zz, H / 2 - f * y / zz], 1), ok


def sheet(path):
    name = os.path.splitext(os.path.basename(path))[0]
    ini = configparser.ConfigParser()
    ini.read(os.path.splitext(path)[0] + '.ini')

    def get(k, d):
        return ini.get('cockpit', k, fallback=d)

    eye = np.array([float(get('eye_right_m', 0)), float(get('eye_up_m', 0)), float(get('eye_forward_m', 0))])
    im = Image.new('RGB', (W, H), (12, 14, 18))
    d = ImageDraw.Draw(im)
    groups = cc.read_mwc(path)
    for g in groups:
        if g['name'].startswith('SCREEN:'):
            continue
        xy, ok = project(g['verts'][:, :3], eye)
        for (x, y), o in zip(xy[::2], ok[::2], strict=False):
            if o and 0 <= x < W and 0 <= y < H:
                im.putpixel((int(x), int(y)), (95, 100, 110))
    n = 0
    for g in groups:
        if not g['name'].startswith('SCREEN:'):
            continue
        k = int(g['name'][7:])
        n += 1
        role = ROLE.get(get('screen_%d' % k, ''))
        xy, ok = project(g['verts'][:, :3], eye)
        T = g['indices'].reshape(-1, 3)
        for t in T:
            if ok[t].all():
                d.polygon([tuple(xy[i]) for i in t], fill=COLOUR[role])
        if ok.any():
            cx, cy = xy[ok].mean(0)
            d.text((cx - 12, cy - 6), '%d %s' % (k, role or '-'), fill=(255, 255, 255))
    d.text((6, 4), '%s   %d monitors, screens=%s' % (name, n, get('screens', '?')), fill=(255, 255, 0))
    return im


def main(argv):
    if len(argv) < 3:
        sys.exit('python tools/monitor_sheet.py <name>.mwc [<name>.mwc ...] <out.png>')
    ims = [sheet(p) for p in argv[1:-1]]
    cols = min(3, len(ims))
    rows = (len(ims) + cols - 1) // cols
    out = Image.new('RGB', (cols * W, rows * H))
    for i, im in enumerate(ims):
        out.paste(im, ((i % cols) * W, (i // cols) * H))
    out.save(argv[-1])


if __name__ == '__main__':
    main(sys.argv)
