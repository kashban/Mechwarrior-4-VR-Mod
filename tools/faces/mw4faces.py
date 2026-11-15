# Pictures of the MW4 mechs' torsos (mw4hook's model_dump): front and three-quarter view, textured.
#   python mw4faces.py <model_dump\mechs> <texture_dump> <out folder>
import os, sys, math
import numpy as np
from PIL import Image, ImageDraw

S = 360


def load(path):
    """lod 0 of an OBJ -> (positions n x 3, uvs n x 2, triangles [(i0, i1, i2, material)])"""
    P, T, F = [], [], []
    mtl, take = None, False
    for line in open(path, encoding='latin-1'):
        if line.startswith('o '):
            take = line.strip().endswith('lod0')
        elif line.startswith('v '):
            P.append([float(x) for x in line.split()[1:4]])
        elif line.startswith('vt '):
            T.append([float(x) for x in line.split()[1:3]])
        elif line.startswith('usemtl'):
            mtl = line.split(None, 1)[1].strip()
        elif line.startswith('f ') and take:
            c = [w.split('/') for w in line.split()[1:4]]
            F.append(([int(w[0]) - 1 for w in c], [int(w[1]) - 1 for w in c], mtl))
    return np.array(P), np.array(T), F


def materials(path, texroot):
    out, cur = {}, None
    if not os.path.exists(path):
        return out
    for line in open(path, encoding='latin-1'):
        if line.startswith('newmtl'):
            cur = line.split(None, 1)[1].strip()
        elif line.startswith('map_Kd') and cur:
            rel = line.split(None, 1)[1].strip().replace('\\', '/')
            name = rel.split('texture_dump/')[-1]
            p = os.path.join(texroot, *name.split('/'))
            if os.path.exists(p):
                out[cur] = np.asarray(Image.open(p).convert('RGB')).astype(np.float32)
    return out


def render(P, T, F, tex, yaw, pitch, lo, hi):
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    R = np.array([[cy, 0, -sy], [0, 1, 0], [sy, 0, cy]])
    R = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]]) @ R
    mid, ext = (lo + hi) / 2, (hi - lo).max() * 0.62
    Q = (P - mid) @ R.T
    img = np.full((S, S, 3), 46, np.float32)
    zb = np.full((S, S), -1e9, np.float32)
    X = (0.5 - Q[:, 0] / (2 * ext)) * S      # the viewer stands in front (+z), the mech's left is on the viewer's right
    Y = (0.5 - Q[:, 1] / (2 * ext)) * S
    Z = Q[:, 2]
    L = np.array([0.3, 0.5, 0.8])
    L /= np.linalg.norm(L)
    for vi, ti, m in F:
        x, y, z = X[vi], Y[vi], Z[vi]
        x0, x1 = max(int(x.min()), 0), min(int(x.max()) + 1, S - 1)
        y0, y1 = max(int(y.min()), 0), min(int(y.max()) + 1, S - 1)
        if x1 < x0 or y1 < y0:
            continue
        den = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
        if abs(den) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        a = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / den
        b = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / den
        c = 1 - a - b
        zz = a * z[0] + b * z[1] + c * z[2]
        ok = (a >= -1e-4) & (b >= -1e-4) & (c >= -1e-4) & (zz > zb[y0:y1 + 1, x0:x1 + 1])
        if not ok.any():
            continue
        n = np.cross(Q[vi[1]] - Q[vi[0]], Q[vi[2]] - Q[vi[0]])
        ln = np.linalg.norm(n)
        shade = 0.45 + 0.55 * abs(float(n @ L) / ln) if ln > 0 else 0.6
        t = tex.get(m)
        if t is not None and len(T):
            uv = T[ti]
            u = a * uv[0, 0] + b * uv[1, 0] + c * uv[2, 0]
            v = a * uv[0, 1] + b * uv[1, 1] + c * uv[2, 1]
            th, tw = t.shape[:2]
            col = t[((1 - v) % 1.0 * th).astype(int) % th, (u % 1.0 * tw).astype(int) % tw] * shade
        else:
            col = np.full(gx.shape + (3,), 150.0) * shade
        sub = img[y0:y1 + 1, x0:x1 + 1]
        sub[ok] = col[ok]
        zb[y0:y1 + 1, x0:x1 + 1][ok] = zz[ok]
    return Image.fromarray(np.clip(255 * (img / 255) ** 0.55, 0, 255).astype(np.uint8))  # the paint is dark: lifted


def main(root, texroot, out):
    os.makedirs(out, exist_ok=True)
    for mech in sorted(os.listdir(root)):
        d = os.path.join(root, mech)
        objs = [f for f in os.listdir(d) if f.endswith('.obj') and '_dam' not in f]
        # the torso, and what sits on it with a face of its own
        pick = [f for f in objs if any(k in f.lower() for k in ('torso', 'head', 'cockpit', 'canopy', 'nose')) and 'cage' not in f]
        if not pick:
            print(mech, 'no torso among', objs)
            continue
        tiles = []
        for f in sorted(pick, key=lambda s: ('torso' not in s, s))[:2]:
            P, T, F = load(os.path.join(d, f))
            if not F:
                continue
            tex = materials(os.path.join(d, f[:-4] + '.mtl'), texroot)
            used = np.unique(np.array([vi for vi, _, _ in F]))
            lo, hi = P[used].min(0), P[used].max(0)
            for yaw, pitch in ((0, 0.12), (0.7, 0.25)):
                im = render(P, T, F, tex, yaw, pitch, lo, hi)
                ImageDraw.Draw(im).text((6, 4), '%s  %s%s' % (mech, f[:-4], '' if tex else '  (no texture)'), fill=(255, 255, 120))
                tiles.append(im)
        sheet = Image.new('RGB', (S * len(tiles), S))
        for i, im in enumerate(tiles):
            sheet.paste(im, (i * S, 0))
        sheet.save(os.path.join(out, mech + '.png'))
        print(mech, pick, len(tiles))


main(*sys.argv[1:4])
