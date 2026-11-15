# What the pilot sees of each MW5 cockpit: a map of directions from the eye (the mesh's origin), the frame
# shaded by its distance, the open windows light blue.   python mw5windows.py <cockpits folder> <out folder>
import os, struct, sys
import numpy as np
from PIL import Image, ImageDraw

AZ, EL0, EL1, STEP, PX = 80, -45, 55, 2.0, 3
W, H = int(2 * AZ / STEP), int((EL1 - EL0) / STEP)


def groups(path):
    f = open(path, 'rb')
    assert f.read(4) == b'MWC1'
    for _ in range(struct.unpack('<I', f.read(4))[0]):
        name, nv, ni, cs, es, k = struct.unpack('<48sIIIIf', f.read(68))
        V = np.frombuffer(f.read(nv * 32), '<f4').reshape(nv, 8)
        I = np.frombuffer(f.read(ni * 4), '<u4').reshape(-1, 3)
        for s in (cs, es):
            while s:
                f.seek(s * s * 4, 1)
                s //= 2
        yield name.split(b'\0')[0].decode(), V[:, :3], I


def tile(path, rng):
    near = np.full((H, W), 1e9)
    names = []
    for name, P, I in groups(path):
        names.append(name)
        if 'glass' in name.lower():
            continue
        T = P[I].astype(np.float64)
        area = 0.5 * np.linalg.norm(np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]), axis=1)
        if area.sum() <= 0:
            continue
        n = 400000
        pick = rng.choice(len(T), n, p=area / area.sum())
        a, b = rng.random(n), rng.random(n)
        flip = a + b > 1
        a[flip], b[flip] = 1 - a[flip], 1 - b[flip]
        Q = T[pick, 0] + a[:, None] * (T[pick, 1] - T[pick, 0]) + b[:, None] * (T[pick, 2] - T[pick, 0])
        d = np.linalg.norm(Q, axis=1)
        az = np.degrees(np.arctan2(Q[:, 0], Q[:, 2]))
        el = np.degrees(np.arctan2(Q[:, 1], np.hypot(Q[:, 0], Q[:, 2])))
        i, j = ((az + AZ) / STEP).astype(int), ((EL1 - el) / STEP).astype(int)
        ok = (i >= 0) & (i < W) & (j >= 0) & (j < H) & (d > 0.05)
        np.minimum.at(near, (j[ok], i[ok]), d[ok])
    img = np.zeros((H, W, 3), np.uint8)
    open_ = near > 1e8
    img[open_] = (150, 200, 255)
    g = np.clip(200 - 70 * np.log2(np.maximum(near, 0.3) / 0.3), 30, 200)
    img[~open_] = np.stack([g, g, g], -1)[~open_]
    im = Image.fromarray(img).resize((W * PX, H * PX), Image.NEAREST)
    return im, names


def main(src, out):
    os.makedirs(out, exist_ok=True)
    rng = np.random.default_rng(1)
    for f in sorted(os.listdir(src)):
        if not f.endswith('.mwc'):
            continue
        im, names = tile(os.path.join(src, f), rng)
        d = ImageDraw.Draw(im)
        d.rectangle((0, 0, 110, 13), fill=(0, 0, 0))
        d.text((3, 1), f[:-4], fill=(255, 255, 120))
        im.save(os.path.join(out, f[:-4] + '.png'))
        print(f[:-4], [n for n in names if 'lass' in n])


main(sys.argv[1], sys.argv[2])
