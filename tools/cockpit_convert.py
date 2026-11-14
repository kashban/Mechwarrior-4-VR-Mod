# Converts a cockpit exported by UE Viewer (glTF + .props.txt + PNG) into the probe's .mwc file.
#   python cockpit_convert.py <mesh.gltf> <export root> <out.mwc> [max texture size]
# .mwc (little endian): 'MWC1', u32 groups; per group: char name[48], u32 verts, u32 indices,
# u32 colour size, u32 emissive size (0: none), float emissive strength; verts (pos3 nrm3 uv2, floats;
# x right, y up, z forward, metres, origin = the mesh's own); u32 indices; colour mips RGBA8 down to
# 1x1; emissive mips likewise. The faces of a material named Panels* (the monitors) become one group
# each, named SCREEN:<n> from left to right: uv 0..1 over the monitor (v down), the strength field its
# width / height; the probe shows parts of the game's HUD on them.
import json, struct, sys, os, re
import numpy as np
from PIL import Image

gltf, root, out = sys.argv[1], sys.argv[2], sys.argv[3]
TMAX = int(sys.argv[4]) if len(sys.argv) > 4 else 1024
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
        return np.ascontiguousarray(raw).view(dt).reshape(a['count'], n)
    return np.frombuffer(buf, dt, a['count'] * n, off).reshape(a['count'], n)


props = {}
for dp, _, fs in os.walk(root):
    for f in fs:
        if f.endswith('.props.txt'):
            props[f[:-len('.props.txt')].lower()] = os.path.join(dp, f)


def material(name):
    """-> (colour image or None, emissive image or None, emissive strength)"""
    tex, strength = {}, 1.0
    p = props.get(name.lower())
    if p:
        s = open(p, encoding='utf-8', errors='replace').read()
        for m in re.finditer(r"Name=([\w ]+?) \}\s*ParameterValue = Texture2D'([^'.]+)", s):
            tex[m.group(1).strip().lower()] = m.group(2)
        m = re.search(r"Name=Emissive Strength \}\s*ParameterValue = ([-\d.eE]+)", s)
        if m:
            strength = float(m.group(1))

    def load(key):
        t = tex.get(key)
        if not t:
            return None
        f = os.path.join(root, t.lstrip('/').replace('/', os.sep) + '.png')
        try:
            return Image.open(f).convert('RGBA')
        except Exception as e:
            print('   texture not usable:', f, e)
            return None
    return load('basecolor'), load('emissive'), strength


def mips(im, size):
    im = im.resize((size, size), Image.LANCZOS) if im.size != (size, size) else im
    data = b''
    while True:
        data += im.tobytes()
        if size == 1:
            return data
        size //= 2
        im = im.resize((size, size), Image.BOX)


def screens(P, N, I):
    """the monitor faces' connected parts, left to right: (P, N, UV, I, aspect); UV runs 0..1 over each
    part's own rectangle, u to the viewer's right, v down"""
    key, w = {}, np.zeros(len(P), int)
    for i, p in enumerate(P):
        w[i] = key.setdefault(tuple(np.round(p, 4)), len(key))
    par = list(range(len(key)))

    def find(x):
        while par[x] != x:
            par[x] = par[par[x]]
            x = par[x]
        return x
    T = I.reshape(-1, 3)
    for t in T:
        par[find(w[t[1]])] = find(w[t[0]])
        par[find(w[t[2]])] = find(w[t[0]])
    comps = {}
    for ti, t in enumerate(T):
        comps.setdefault(find(w[t[0]]), []).append(ti)
    res = []
    for ts in sorted(comps.values(), key=lambda ts: P[T[ts].reshape(-1)][:, 0].mean()):
        vi = np.unique(T[ts].reshape(-1))
        n = N[vi].mean(0)
        n /= np.linalg.norm(n)
        up = np.array([0, 1.0, 0]) - n * n[1]
        up /= np.linalg.norm(up)
        right = np.cross(n, up)
        a, b = P[vi] @ right, P[vi] @ up
        uv = np.stack([(a - a.min()) / (a.max() - a.min()), (b.max() - b) / (b.max() - b.min())], 1)
        remap = np.full(len(P), -1)
        remap[vi] = np.arange(len(vi))
        res.append((P[vi], N[vi], uv.astype(np.float32), remap[T[ts].reshape(-1)].astype(np.uint32), (a.max() - a.min()) / (b.max() - b.min())))
    return res


# groups: (name, P, N, UV, I, colour image, emissive image or None, emissive strength - for a screen its width / height)
groups = []
lo, hi = np.full(3, 1e9), np.full(3, -1e9)
for prim in g['meshes'][0]['primitives']:
    name = g['materials'][prim['material']]['name']
    P = acc(prim['attributes']['POSITION']).astype(np.float32).copy()
    N = acc(prim['attributes']['NORMAL']).astype(np.float32).copy()
    UV = acc(prim['attributes']['TEXCOORD_0']).astype(np.float32)
    I = acc(prim['indices']).astype(np.uint32).reshape(-1)
    P[:, 2] *= -1  # glTF looks down -z; the probe's view space looks down +z
    N[:, 2] *= -1
    lo, hi = np.minimum(lo, P.min(0)), np.maximum(hi, P.max(0))
    if name.lower().startswith('panels'):  # the monitors: one group each, shown with the game's HUD
        for k, (p, n, uv, i, asp) in enumerate(screens(P, N, I)):
            c = p.mean(0)
            print('  screen %d: centre %.2f %.2f %.2f m, %.0f x %.0f cm' % (k, c[0], c[1], c[2], 100 * np.ptp(p @ np.cross(n.mean(0) / np.linalg.norm(n.mean(0)), [0, 1, 0])), 100 * np.ptp(p[:, 1])))
            groups.append(('SCREEN:%d' % k, p, n, uv, i, Image.new('RGBA', (4, 4), (0, 0, 0, 255)), None, float(asp)))
        continue
    clr, ems, strength = material(name)
    if clr is None:
        clr = Image.new('RGBA', (4, 4), (80, 80, 84, 255))
    groups.append((name, P, N, UV, I, clr, ems, strength))

with open(out, 'wb') as o:
    o.write(b'MWC1' + struct.pack('<I', len(groups)))
    for name, P, N, UV, I, clr, ems, strength in groups:
        cs = min(TMAX, clr.size[0])
        es = min(TMAX, ems.size[0]) if ems is not None else 0
        if ems is not None and not np.asarray(ems.convert('L').resize((64, 64))).any():
            es = 0  # all black
        o.write(struct.pack('<48sIIIIf', name.encode()[:47], len(P), len(I), cs, es, strength))
        o.write(np.hstack([P, N, UV]).astype('<f4').tobytes())
        o.write(I.astype('<u4').tobytes())
        o.write(mips(clr, cs))
        if es:
            o.write(mips(ems, es))
        print('  %-34s %6d verts %6d tris, colour %d, emissive %d x %.2f' % (name, len(P), len(I) // 3, cs, es, strength))
print('bounds x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f m' % (lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
print('written', out, os.path.getsize(out) // 1024, 'KB')
