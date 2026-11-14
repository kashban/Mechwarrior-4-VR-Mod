# Converts a cockpit exported by UE Viewer (glTF + .props.txt + PNG) into the mod's .mwc file.
#   python cockpit_convert.py <mesh.gltf> <export root> <out.mwc> [max texture size]
# .mwc (little endian): 'MWC1', u32 groups; per group: char name[48], u32 verts, u32 indices,
# u32 colour size, u32 emissive size (0: none), float emissive strength; verts (pos3 nrm3 uv2, floats;
# x right, y up, z forward, metres, origin = the mesh's own); u32 indices; colour mips RGBA8 down to
# 1x1; emissive mips likewise. The faces of a material named Panels* (the monitors) become one group
# each, named SCREEN:<n> from left to right: uv 0..1 over the monitor (v down), the strength field its
# width / height; the mod shows parts of the game's HUD on them.
import json, struct, sys, os, re
import numpy as np
from PIL import Image

CT = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}
HEADER = '<48sIIIIf'


def acc(g, buf, i):
    """accessor i of the glTF as an array (count, components)"""
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


def find_props(root):
    """material name (lower case) -> its .props.txt under root"""
    props = {}
    for dp, _, fs in os.walk(root):
        for f in fs:
            if f.endswith('.props.txt'):
                props[f[:-len('.props.txt')].lower()] = os.path.join(dp, f)
    return props


def parse_props(text):
    """-> ({parameter name (lower case): texture path}, emissive strength)"""
    tex, strength = {}, 1.0
    for m in re.finditer(r"Name=([\w ]+?) \}\s*ParameterValue = Texture2D'([^'.]+)", text):
        tex[m.group(1).strip().lower()] = m.group(2)
    m = re.search(r"Name=Emissive Strength \}\s*ParameterValue = ([-\d.eE]+)", text)
    if m:
        strength = float(m.group(1))
    return tex, strength


def material(props, root, name, log=print):
    """-> (colour image or None, emissive image or None, emissive strength)"""
    tex, strength = {}, 1.0
    p = props.get(name.lower())
    if p:
        with open(p, encoding='utf-8', errors='replace') as f:
            tex, strength = parse_props(f.read())

    def load(key):
        t = tex.get(key)
        if not t:
            return None
        f = os.path.join(root, t.lstrip('/').replace('/', os.sep) + '.png')
        try:
            return Image.open(f).convert('RGBA')
        except Exception as e:
            log('   texture not usable:', f, e)
            return None
    return load('basecolor'), load('emissive'), strength


def mips(im, size):
    """the image at size x size and every mip down to 1x1, RGBA8, one after the other"""
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


def is_screen_material(name):
    return name.lower().startswith('panels')


def build_groups(g, buf, props, root, log=print):
    """-> (groups, lo, hi); a group: (name, P, N, UV, I, colour image, emissive image or None, emissive
    strength - for a screen its width / height)"""
    groups = []
    lo, hi = np.full(3, 1e9), np.full(3, -1e9)
    for prim in g['meshes'][0]['primitives']:
        name = g['materials'][prim['material']]['name']
        P = acc(g, buf, prim['attributes']['POSITION']).astype(np.float32).copy()
        N = acc(g, buf, prim['attributes']['NORMAL']).astype(np.float32).copy()
        UV = acc(g, buf, prim['attributes']['TEXCOORD_0']).astype(np.float32)
        I = acc(g, buf, prim['indices']).astype(np.uint32).reshape(-1)
        P[:, 2] *= -1  # glTF looks down -z; the mod's view space looks down +z
        N[:, 2] *= -1
        lo, hi = np.minimum(lo, P.min(0)), np.maximum(hi, P.max(0))
        if is_screen_material(name):  # the monitors: one group each, shown with the game's HUD
            for k, (p, n, uv, i, asp) in enumerate(screens(P, N, I)):
                c = p.mean(0)
                nm = n.mean(0) / np.linalg.norm(n.mean(0))
                log('  screen %d: centre %.2f %.2f %.2f m, %.0f x %.0f cm' % (k, c[0], c[1], c[2], 100 * np.ptp(p @ np.cross(nm, [0, 1, 0])), 100 * np.ptp(p[:, 1])))
                groups.append(('SCREEN:%d' % k, p, n, uv, i, Image.new('RGBA', (4, 4), (0, 0, 0, 255)), None, float(asp)))
            continue
        clr, ems, strength = material(props, root, name, log)
        if clr is None:
            clr = Image.new('RGBA', (4, 4), (80, 80, 84, 255))
        groups.append((name, P, N, UV, I, clr, ems, strength))
    return groups, lo, hi


def emissive_size(ems, tmax):
    """the emissive texture's size in the file: 0 without one or with an all-black one"""
    if ems is None:
        return 0
    if not np.asarray(ems.convert('L').resize((64, 64))).any():
        return 0
    return min(tmax, ems.size[0])


def write_mwc(out, groups, tmax=1024, log=print):
    with open(out, 'wb') as o:
        o.write(b'MWC1' + struct.pack('<I', len(groups)))
        for name, P, N, UV, I, clr, ems, strength in groups:
            cs = min(tmax, clr.size[0])
            es = emissive_size(ems, tmax)
            o.write(struct.pack(HEADER, name.encode()[:47], len(P), len(I), cs, es, strength))
            o.write(np.hstack([P, N, UV]).astype('<f4').tobytes())
            o.write(I.astype('<u4').tobytes())
            o.write(mips(clr, cs))
            if es:
                o.write(mips(ems, es))
            log('  %-34s %6d verts %6d tris, colour %d, emissive %d x %.2f' % (name, len(P), len(I) // 3, cs, es, strength))


def mip_bytes(size):
    """bytes of an RGBA8 mip chain from size x size down to 1x1"""
    n = 0
    while size:
        n += size * size * 4
        size //= 2
    return n


def read_mwc(path):
    """-> [{name, verts (n, 8), indices, colour_size, emissive_size, strength}] (textures skipped)"""
    with open(path, 'rb') as f:
        if f.read(4) != b'MWC1':
            raise ValueError('not an .mwc file')
        n = struct.unpack('<I', f.read(4))[0]
        res = []
        for _ in range(n):
            name, nv, ni, cs, es, k = struct.unpack(HEADER, f.read(struct.calcsize(HEADER)))
            V = np.frombuffer(f.read(nv * 32), '<f4').reshape(nv, 8)
            I = np.frombuffer(f.read(ni * 4), '<u4')
            f.seek(mip_bytes(cs) + mip_bytes(es), 1)
            res.append({'name': name.split(b'\0')[0].decode(), 'verts': V, 'indices': I, 'colour_size': cs, 'emissive_size': es, 'strength': k})
        if f.read(1):
            raise ValueError('bytes after the last group')
        return res


def convert(gltf, root, out, tmax=1024, log=print):
    with open(gltf) as f:
        g = json.load(f)
    with open(os.path.join(os.path.dirname(gltf), g['buffers'][0]['uri']), 'rb') as f:
        buf = f.read()
    groups, lo, hi = build_groups(g, buf, find_props(root), root, log)
    write_mwc(out, groups, tmax, log)
    log('bounds x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f m' % (lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
    log('written', out, os.path.getsize(out) // 1024, 'KB')
    return groups


if __name__ == '__main__':
    if len(sys.argv) < 4:
        sys.exit(__doc__ or 'python cockpit_convert.py <mesh.gltf> <export root> <out.mwc> [max texture size]')
    convert(sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 1024)
