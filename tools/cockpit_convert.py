# Converts a cockpit exported by UE Viewer (glTF + .props.txt + PNG) into the mod's .mwc file.
#   python cockpit_convert.py <mesh.gltf> <export root> <out.mwc> [max texture size]
# .mwc (little endian): 'MWC1', u32 groups; per group: char name[48], u32 verts, u32 indices,
# u32 colour size, u32 emissive size (0: none), float emissive strength; verts (pos3 nrm3 uv2, floats;
# x right, y up, z forward, metres, origin = the mesh's own); u32 indices; colour mips RGBA8 down to
# 1x1; emissive mips likewise. The faces of a material named Panels* (the monitors) become one group
# each, named SCREEN:<n> from left to right: uv 0..1 over the monitor (v down), the strength field its
# width / height; the mod shows parts of the game's HUD on them. Only parts of at least SCREEN_MIN metres
# each way count as monitors (the SCREEN_MAX largest); the others (buttons, lamps) stay with their material.
# Beside the .mwc a <name>.ini is written once - which monitor shows which part of the HUD, the eye's place -
# for the mod to read and for you to change.
import json, struct, sys, os, re
import numpy as np
from PIL import Image

CT = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}
HEADER = '<48sIIIIf'
TMAX = 1024  # the largest texture size written
SCREEN_MIN = 0.07  # metres: what is smaller is no monitor
SCREEN_MAX = 8  # as many as the mod shows
# the parts of the game's HUD picture (u0 v0 u1 v1, 0..1 from the top left) a monitor can show
HUD_PARTS = {'lance': '0.000 0.000 0.243 0.108', 'target': '0.000 0.772 0.180 1.000', 'radar': '0.320 0.770 0.690 0.990',
             'damage': '0.820 0.772 1.005 1.000', 'weapons': '0.716 0.000 1.000 0.124'}
LANCE_ALT = '0.000 0.000 0.243 0.435 0.210 0.125'  # the lance list with the open orders menu, and where that menu shows


def acc(g, buf, i):
    """accessor i of the glTF as an array (count, components)"""
    a = g['accessors'][i]
    v = g['bufferViews'][a['bufferView']]
    n = NC[a['type']]
    dt = np.dtype('<' + CT[a['componentType']])
    off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
    st = v.get('byteStride', 0)
    if st:  # a view shared by several accessors
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


def parts(P, I):
    """the triangles' connected parts (vertices at one place are one): a list of arrays of triangle numbers"""
    key, w = {}, np.zeros(len(P), int)
    for i, p in enumerate(P):
        w[i] = key.setdefault(tuple(np.round(p, 4)), len(key))
    par = list(range(len(key)))

    def find(x):
        while par[x] != x:
            par[x] = par[par[x]]
            x = par[x]
        return x
    T = I.reshape(len(I) // 3, 3)
    for a, b, c in w[T]:  # the triangles' corners as welded places
        r = find(a)
        par[find(b)] = r
        par[find(c)] = r
    comps = {}
    for ti, (a, b, c) in enumerate(w[T]):
        comps.setdefault(find(a), []).append(ti)
    return [np.array(ts) for ts in comps.values()]


def screen(P, N, I, ts):
    """the triangles ts as a monitor: (P, N, UV, I, aspect, (width, height) in metres); UV runs 0..1 over its
    own rectangle, u to the viewer's right, v down"""
    T = I.reshape(len(I) // 3, 3)
    vi = np.unique(T[ts])
    n = N[vi].mean(0)
    n /= np.linalg.norm(n)
    up = np.array([0, 1.0, 0]) - n * n[1]  # the world's up within the monitor's plane
    up /= np.linalg.norm(up)
    right = np.cross(n, up)
    a, b = P[vi] @ right, P[vi] @ up
    w, h = a.max() - a.min(), b.max() - b.min()
    uv = np.stack([(a - a.min()) / w, (b.max() - b) / h], 1)
    remap = np.zeros(len(P), int)
    remap[vi] = np.arange(len(vi))
    return P[vi], N[vi], uv.astype(np.float32), remap[T[ts].ravel()].astype(np.uint32), w / h, (float(w), float(h))


def screens(P, N, I):
    """every connected part of the monitor faces as a monitor, left to right"""
    T = I.reshape(len(I) // 3, 3)
    return [screen(P, N, I, ts) for ts in sorted(parts(P, I), key=lambda ts: P[T[ts].ravel()][:, 0].mean())]


def monitors(P, N, I):
    """-> (the monitors among the parts, left to right; the indices of the triangles that are none)"""
    T = I.reshape(len(I) // 3, 3)
    found = []
    for ts in parts(P, I):
        with np.errstate(all='ignore'):  # a part without width or height: its size says so
            m = screen(P, N, I, ts)
        if min(m[5]) >= SCREEN_MIN:
            found.append((m[5][0] * m[5][1], float(m[0][:, 0].mean()), ts, m))
    found.sort(key=lambda f: -f[0])
    kept = sorted(found[:SCREEN_MAX], key=lambda f: f[1])
    used = np.zeros(len(T), bool)
    for f in kept:
        used[f[2]] = True
    return [f[3] for f in kept], T[~used].ravel()


def assign(places):
    """which monitor shows which part of the HUD: {part: monitor}, from the monitors' (x, z, area) - x to the
    right, z forward. Of those ahead, the one nearest the middle (with a neighbour on each side) shows the
    radar, its neighbours target and damage; the largest further left the lance list, the largest further right
    the weapons. Fewer than three ahead: {}"""
    ahead = sorted((k for k in range(len(places)) if places[k][1] > 0), key=lambda k: places[k][0])
    if len(ahead) < 3:
        return {}
    c = min(range(len(ahead)), key=lambda j: abs(places[ahead[j]][0]))
    c = min(max(c, 1), len(ahead) - 2)
    res = {'radar': ahead[c], 'target': ahead[c - 1], 'damage': ahead[c + 1]}
    for part, rest in (('lance', ahead[:c - 1]), ('weapons', ahead[c + 2:])):
        if rest:
            res[part] = max(rest, key=lambda k: places[k][2])
    return res


def ini_text(places):
    """the text of a cockpit's .ini for monitors at places (see assign)"""
    roles = assign(places)
    lines = ['; Settings of this cockpit for MW4VR. Written once by cockpit_convert.py; change it as you like.',
             '[cockpit]',
             '; the eye in the mesh (metres) and the size of the mesh; in a battle: Ctrl+Alt+arrows, Ctrl+Alt+PgUp / PgDn',
             'scale=1.000', 'eye_right_m=0.000', 'eye_up_m=0.000', 'eye_forward_m=0.000',
             '; 1 = the monitors show the HUD; 0 = the HUD floats in front as without a cockpit of our own',
             'screens=%d' % bool(roles),
             '; screen_<n> = u0 v0 u1 v1: the part of the HUD picture monitor n shows (none: dark). The monitors, left to right:']
    for k, (x, z, area) in enumerate(places):
        lines.append(';   %d: %.2f m to the right, %.2f m ahead, %.0f cm2' % (k, x, z, area * 10000))
    for part, k in sorted(roles.items(), key=lambda r: r[1]):
        lines.append('; %s' % part)
        lines.append('screen_%d=%s' % (k, HUD_PARTS[part]))
        if part == 'lance':
            lines.append('screen_%d_alt=%s' % (k, LANCE_ALT))
    return '\n'.join(lines) + '\n'


def flat(r, g, b):
    """a small opaque texture of one colour"""
    return Image.new('RGB', (4, 4), (r, g, b)).convert('RGBA')


def is_screen_material(name):
    return name.lower().startswith('panels')


def build_groups(g, buf, props, root, log=print):
    """-> (groups, lo, hi, places); a group: (name, P, N, UV, I, colour image, emissive image or None, emissive
    strength - for a screen its width / height); places: the monitors' (x, z, area)"""
    groups, places = [], []
    lo, hi = np.full(3, 1e9), np.full(3, -1e9)
    for prim in g['meshes'][0]['primitives']:
        name = g['materials'][prim['material']]['name']
        P = acc(g, buf, prim['attributes']['POSITION']).astype(np.float32).copy()
        N = acc(g, buf, prim['attributes']['NORMAL']).astype(np.float32).copy()
        UV = acc(g, buf, prim['attributes']['TEXCOORD_0']).astype(np.float32)
        I = acc(g, buf, prim['indices']).astype(np.uint32).ravel()
        P[:, 2] = -P[:, 2]  # glTF looks down -z; the mod's view space looks down +z
        N[:, 2] = -N[:, 2]
        lo, hi = np.minimum(lo, P.min(0)), np.maximum(hi, P.max(0))
        if is_screen_material(name):  # the monitors: one group each, shown with the game's HUD
            found, I = monitors(P, N, I)
            for p, n, uv, i, asp, (w, h) in found:
                c = p.mean(0)
                log('  screen %d: centre %.2f %.2f %.2f m, %.0f x %.0f cm' % (len(places), c[0], c[1], c[2], 100 * w, 100 * h))
                groups.append(('SCREEN:%d' % len(places), p, n, uv, i, flat(0, 0, 0), None, float(asp)))
                places.append((float(c[0]), float(c[2]), w * h))
            if not len(I):
                continue
        clr, ems, strength = material(props, root, name, log)
        if clr is None:
            clr = flat(80, 80, 84)
        groups.append((name, P, N, UV, I, clr, ems, strength))
    return groups, lo, hi, places


def emissive_size(ems, tmax):
    """the emissive texture's size in the file: 0 without one or with an all-black one"""
    if ems is None:
        return 0
    if not ems.convert('L').getbbox():
        return 0
    return min(tmax, ems.size[0])


def write_mwc(out, groups, tmax=TMAX, log=print):
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
        if f.read():
            raise ValueError('bytes after the last group')
        return res


def convert(gltf, root, out, tmax=TMAX, log=print):
    with open(gltf) as f:
        g = json.load(f)
    with open(os.path.join(os.path.dirname(gltf), g['buffers'][0]['uri']), 'rb') as f:
        buf = f.read()
    groups, lo, hi, places = build_groups(g, buf, find_props(root), root, log)
    write_mwc(out, groups, tmax, log)
    ini = os.path.splitext(out)[0] + '.ini'
    if not os.path.exists(ini):  # yours once it is there
        with open(ini, 'w') as f:
            f.write(ini_text(places))
    log('bounds x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f m' % (lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
    log('written', out, os.path.getsize(out) // 1024, 'KB')
    return groups


def main(argv, log=print):
    if len(argv) < 4:
        sys.exit('python cockpit_convert.py <mesh.gltf> <export root> <out.mwc> [max texture size]')
    convert(argv[1], argv[2], argv[3], int(argv[4]) if len(argv) > 4 else TMAX, log)


if __name__ == '__main__':
    main(sys.argv)
