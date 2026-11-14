# Software preview of a cockpit exported by UE Viewer (glTF + PNG): python cockpit_preview.py <gltf> <export root> <out.png> ex ey ez [yaw_deg pitch_deg fov_deg]
import json, struct, sys, os, glob, math
import numpy as np
from PIL import Image

gltf, root, out = sys.argv[1], sys.argv[2], sys.argv[3]
eye = np.array([float(v) for v in sys.argv[4:7]])
yaw = math.radians(float(sys.argv[7])) if len(sys.argv) > 7 else 0
pitch = math.radians(float(sys.argv[8])) if len(sys.argv) > 8 else 0
fov = float(sys.argv[9]) if len(sys.argv) > 9 else 100
W, H = 800, 600

g = json.load(open(gltf))
buf = open(os.path.join(os.path.dirname(gltf), g['buffers'][0]['uri']), 'rb').read()
CT = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}


def acc(i):
    a = g['accessors'][i]
    v = g['bufferViews'][a['bufferView']]
    n = NC[a['type']]
    dt = np.dtype('<' + CT[a['componentType']])
    off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
    st = v.get('byteStride', 0)
    if st and st != dt.itemsize * n:
        raw = np.frombuffer(buf, np.uint8, st * a['count'], off).reshape(a['count'], st)[:, :dt.itemsize * n]
        arr = np.ascontiguousarray(raw).view(dt).reshape(a['count'], n)
    else:
        arr = np.frombuffer(buf, dt, a['count'] * n, off).reshape(a['count'], n)
    if a.get('normalized'):
        arr = arr.astype(np.float32) / np.iinfo(dt).max
    return arr


def tex(name, suffix):
    base = name.replace('_MTI', '').replace('_cockpit', '')
    for cand in (name.replace('_MTI', '') + suffix, base + suffix):
        hits = [p for p in glob.glob(os.path.join(root, '**', '*.png'), recursive=True) if os.path.basename(p).lower() == (cand + '.png').lower()]
        if hits:
            return np.asarray(Image.open(hits[0]).convert('RGB').resize((512, 512)), np.float32) / 255
    return None


# camera: +z forward, +y up, +x right (as exported); yaw about y, pitch about x
cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
fwd = np.array([sy * cp, sp, cy * cp])
right = np.array([cy, 0, -sy])
up = np.cross(fwd, right)
f = (W / 2) / math.tan(math.radians(fov) / 2)
img = np.zeros((H, W, 3), np.float32) + np.array([0.45, 0.6, 0.8], np.float32)
zb = np.full((H, W), 1e9, np.float32)
light = np.array([0.3, 0.8, -0.5])
light /= np.linalg.norm(light)

for prim in g['meshes'][0]['primitives']:
    name = g['materials'][prim['material']]['name']
    P = acc(prim['attributes']['POSITION']).astype(np.float64)
    N = acc(prim['attributes']['NORMAL']).astype(np.float64)
    UV = acc(prim['attributes']['TEXCOORD_0']).astype(np.float64)
    I = acc(prim['indices']).reshape(-1, 3)
    clr, ems = tex(name, '_CLR'), tex(name, '_EMS')
    d = P - eye
    cam = np.stack([d @ right, d @ up, d @ fwd], 1)
    for tri in I:
        c = cam[tri]
        if (c[:, 2] < 0.05).any():
            continue
        sx = W / 2 + f * c[:, 0] / c[:, 2]
        syy = H / 2 - f * c[:, 1] / c[:, 2]
        x0, x1 = int(max(0, math.floor(sx.min()))), int(min(W - 1, math.ceil(sx.max())))
        y0, y1 = int(max(0, math.floor(syy.min()))), int(min(H - 1, math.ceil(syy.max())))
        if x0 > x1 or y0 > y1:
            continue
        den = (syy[1] - syy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (syy[0] - syy[2])
        if abs(den) < 1e-9:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        a = ((syy[1] - syy[2]) * (xs - sx[2]) + (sx[2] - sx[1]) * (ys - syy[2])) / den
        b = ((syy[2] - syy[0]) * (xs - sx[2]) + (sx[0] - sx[2]) * (ys - syy[2])) / den
        cc = 1 - a - b
        m = (a >= 0) & (b >= 0) & (cc >= 0)
        if not m.any():
            continue
        iz = a / c[0, 2] + b / c[1, 2] + cc / c[2, 2]
        z = 1 / iz
        sub = zb[y0:y1 + 1, x0:x1 + 1]
        m &= z < sub
        if not m.any():
            continue
        wa, wb, wc = a / c[0, 2] * z, b / c[1, 2] * z, cc / c[2, 2] * z
        uv = wa[..., None] * UV[tri[0]] + wb[..., None] * UV[tri[1]] + wc[..., None] * UV[tri[2]]
        n = N[tri].mean(0)
        lam = 0.35 + 0.65 * abs(float(n @ light))
        if clr is not None:
            tx = (np.mod(uv[..., 0], 1) * 511).astype(int)
            ty = (np.mod(uv[..., 1], 1) * 511).astype(int)
            col = clr[ty, tx] * lam
            if ems is not None:
                col = col + ems[ty, tx] * 2
        else:
            col = np.zeros(uv.shape[:2] + (3,), np.float32) + 0.3 * lam
        sub[m] = z[m]
        img[y0:y1 + 1, x0:x1 + 1][m] = col[m]

Image.fromarray((np.clip(img, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)).save(out)
print('ok', out)
