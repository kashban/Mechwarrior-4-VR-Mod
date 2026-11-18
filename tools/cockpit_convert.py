# Converts a cockpit exported by UE Viewer (glTF + .props.txt + PNG) into the mod's .mwc file.
#   python cockpit_convert.py <mesh.gltf> <export root> <out.mwc> [max texture size]
# .mwc (little endian): 'MWC1', u32 groups; per group: char name[48], u32 verts, u32 indices,
# u32 colour size, u32 emissive size (0: none), float emissive strength; verts (pos3 nrm3 uv2, floats;
# x right, y up, z forward, metres, origin = the mesh's own); u32 indices; colour mips RGBA8 down to
# 1x1; emissive mips likewise. The faces of a material named Panels* (the monitors) become one group
# each, named SCREEN:<n> from left to right: uv 0..1 over the monitor (v down), the strength field its
# width / height; the mod shows parts of the game's HUD on them. Only parts of at least SCREEN_MIN metres
# each way count as monitors (the SCREEN_MAX largest); the others (buttons, lamps) stay with their material.
# Cockpits made by players (MechWarrior 5 mods) are often built otherwise: a material without a colour texture
# takes its paint colour (PaintColorPrimary), a cockpit without Panels* has its monitors in the materials made
# from the game's Projection_Hologram, and the canopy's glass - a sheet over the windows - is left out.
# Beside the .mwc a <name>.ini is written once - which monitor shows which part of the HUD, the eye's place -
# for the mod to read and for you to change.
import json
import os
import re
import struct
import sys

import numpy as np
from PIL import Image

CT = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}
HEADER = '<48sIIIIf'
TMAX = 1024  # the largest texture size written
SCREEN_MIN = 0.07  # metres: what is smaller is no monitor
SCREEN_MAX = 8  # as many as the mod shows
DISPLAYS = ('projection_hologram', 'widget3d_', 'minimapgenerator')  # the game's materials that show a picture
QUARTERS = range(1, 181)  # the turns tried to stand a monitor upright: quarter degrees up to 45
WHITE = (255, 255, 255)
VC_SHARE = 0.5  # how much of the vertex colours' shade a painted part takes
METAL = np.array([0.15, 0.15, 0.16])  # bare metal, linear light
PAINT = 'PAINT:'  # before the name of a group of painted metal: the mod draws the game's patches, wear and gloss on it
DETAIL = 1024  # the size of the picture those come from
DETAIL_FILE = 'mw4vr_paint.mwd'
GRAIN = 3  # the grain's slopes are written this much steeper than the game's
DETAIL_PATCHES = 'Game/Objects/_common/Materials/Textures/MacroVariation_CLR.png'
DETAIL_GRAIN = 'Game/Objects/Mechs/_common/Material/Textures/CastMetal_NRM.png'
# the parts of the game's HUD picture (u0 v0 u1 v1, 0..1 from the top left) a monitor can show
HUD_PARTS = {
    'lance': '0.000 0.000 0.243 0.108',
    'target': '0.000 0.772 0.180 1.000',
    'radar': '0.320 0.770 0.690 0.990',
    'damage': '0.820 0.772 1.005 1.000',
    'weapons': '0.716 0.000 1.000 0.124',
}
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
        raw = np.frombuffer(buf, np.uint8, st * a['count'], off).reshape(a['count'], st)[:, : dt.itemsize * n]
        return np.ascontiguousarray(raw).view(dt).reshape(a['count'], n)
    return np.frombuffer(buf, dt, a['count'] * n, off).reshape(a['count'], n)


def find_props(root):
    """material name (lower case) -> its .props.txt under root"""
    props = {}
    for dp, _, fs in os.walk(root):
        for f in fs:
            if f.endswith('.props.txt'):
                props[f[: -len('.props.txt')].lower()] = os.path.join(dp, f)
    return props


def parse_props(text):
    """-> ({parameter name (lower case): texture path}, emissive strength)"""
    tex, strength = {}, 1.0
    for m in re.finditer(r"Name=([\w ]+?) \}\s*ParameterValue = Texture2D'([^'.]+)", text):
        tex[m.group(1).strip().lower()] = m.group(2)
    found = re.search(r'Name=Emissive Strength \}\s*ParameterValue = ([-\d.eE]+)', text)
    if found:
        strength = float(found.group(1))
    return tex, strength


def paint_light(text, which='Primary'):
    """one of the material's paint colours (PaintColorPrimary, Secondary, Tertiary) in linear light, (r, g, b) 0..1,
    or None"""
    m = re.search(
        r'Name=PaintColor{} \}}\s*ParameterValue = \{{ R=([-\d.eE]+), G=([-\d.eE]+), B=([-\d.eE]+)'.format(which), text
    )
    if not m:
        return None
    return tuple(min(1.0, max(0.0, float(v))) for v in m.groups())


def to_screen(light):
    """linear light 0..1 -> 0..255 for the screen"""
    return tuple(round(255 * v ** (1 / 2.2)) for v in light)


def paint_colour(text):
    """the material's paint colour (PaintColorPrimary, linear light) as (r, g, b) 0..255 for the screen, or None"""
    light = paint_light(text)
    return None if light is None else to_screen(light)


def masked_paint(mask, paints):
    """a paint mask's picture (red, green, blue: how much of the first paint lies on the bare metal, of the second
    over that, of the third over both) painted with paints - three (r, g, b) in linear light, None: not given, left
    out - as an image for the screen"""
    m = np.asarray(mask.convert('RGB'), np.float64) / 255
    out = np.zeros(m.shape) + METAL
    for k in range(3):
        if paints[k] is not None:
            out = out + m[..., k : k + 1] * (np.array(paints[k]) - out)
    out = np.round(255 * out ** (1 / 2.2)).astype(np.uint8)
    return Image.fromarray(out, 'RGB').convert('RGBA')


def is_paint(text):
    """is the material painted metal - no picture of its own, its colour a paint (the game's PaintedMetal: the look
    comes from patches, wear and gloss its shader adds)?"""
    tex = parse_props(text)[0]
    return 'basecolor' not in tex and ('rgbpaintmask' in tex or paint_light(text) is not None)


def detail_map(root, size=DETAIL):
    """the picture the mod draws painted metal with, from the game's own two under the export root: red and green
    the fine grain's slope to the right and up (steeper, see GRAIN), blue the patches' lightness; None while the
    export has them not"""
    try:
        patches = (
            Image.open(os.path.join(root, *DETAIL_PATCHES.split('/'))).convert('L').resize((size, size), Image.LANCZOS)
        )
        grain = (
            Image.open(os.path.join(root, *DETAIL_GRAIN.split('/'))).convert('RGB').resize((size, size), Image.LANCZOS)
        )
    except OSError:
        return None
    slope = np.asarray(grain, np.float64)[..., :2]  # faint, and not about the middle: about it, GRAIN times as steep
    slope = np.clip(np.round(127.5 + GRAIN * (slope - slope.mean((0, 1)))), 0, 255).astype(np.uint8)
    full = np.full((size, size, 1), 255, np.uint8)
    return Image.fromarray(np.concatenate([slope, np.asarray(patches)[..., None], full], 2), 'RGBA')


def write_detail(path, im):
    with open(path, 'wb') as o:
        o.write(b'MWD1' + struct.pack('<I', im.width) + mips(im, im.width))


def is_hologram(text):
    """is the material made from one of the game's that show a picture (a monitor)?"""
    m = re.search(r"^Parent = \w+'([^']*)'", text, re.MULTILINE)
    return m is not None and any(k in m.group(1).lower() for k in DISPLAYS)


def is_display(name):
    """is it one of the game's own picture materials, used as it is?"""
    return name.lower().startswith(DISPLAYS)


def props_text(props, name):
    """the text of the material's .props.txt, '' without one"""
    p = props.get(name.lower())
    if not p:
        return ''
    with open(p, encoding='utf-8', errors='replace') as f:
        return f.read()


def material(props, root, name, log=print):
    """-> (colour image or None, emissive image or None, emissive strength)"""
    text = props_text(props, name)
    tex, strength = parse_props(text)

    def load(key):
        t = tex.get(key)
        if not t:
            return None
        f = os.path.join(root, t.lstrip('/').replace('/', os.sep) + '.png')
        try:
            return Image.open(f).convert('RGBA')
        except Exception as e:  # noqa: BLE001 - any picture that cannot be read is told, not fatal
            log('   texture not usable:', f, e)
            return None

    clr = load('basecolor')
    if clr is None:
        mask = load('rgbpaintmask')
        if mask is not None:
            clr = masked_paint(mask, [paint_light(text, w) for w in ('Primary', 'Secondary', 'Tertiary')])
    if clr is None:
        paint = paint_colour(text)
        if paint is not None:
            clr = flat(*paint)
    return clr, load('emissive'), strength


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
    key: dict[tuple, int] = {}
    w = np.zeros(len(P), int)
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
    comps: dict[int, list[int]] = {}
    for ti, (a, _b, _c) in enumerate(w[T]):
        comps.setdefault(find(a), []).append(ti)
    return [np.array(ts) for ts in comps.values()]


def upright(a, b):
    """points in a plane (a: to the right, b: up): the turn to the left, in radians within 45 degrees, that stands the
    smallest rectangle around them upright (a monitor that hangs askew); 0 while that one is not 2 % smaller"""
    best, turn = 0.98 * (a.max() - a.min()) * (b.max() - b.min()), 0.0
    for q in QUARTERS:  # the smaller turns first
        for t in (np.radians(q / 4), -np.radians(q / 4)):
            x, y = a * np.cos(t) + b * np.sin(t), b * np.cos(t) - a * np.sin(t)
            area = (x.max() - x.min()) * (y.max() - y.min())
            if area < best:
                best, turn = area, t
    return turn


def vertex_colours(g, buf, prim):
    """the primitive's vertex colours as n x 3 numbers 0..255; None without any, or with nothing painted (all white)"""
    i = prim['attributes'].get('COLOR_0')
    if i is None:
        return None
    c = acc(g, buf, i)[:, :3].astype(np.float64)
    if g['accessors'][i]['componentType'] == 5126:
        c = c * 255
    return None if c.min() >= 250 else c


def painted(P, N, I, VC, base, share):
    """the triangles in the colour base, shaded by their vertex colours VC (the mod makers' baked light; share: how
    much of it, 0..1), one colour a triangle (the mean of its corners): -> (P, N, UV, I, image, colours). The file
    has no vertex colours: the colours go into a small picture, a texel each, and the three corners of a triangle
    point at the middle of its texel (corners shared by triangles of other colours are doubled)"""
    T = I.reshape(len(I) // 3, 3)
    col = np.round(np.array(base) * (1 - share + share * VC[T].mean(1) / 255.0)).astype(np.uint8)
    pal, which = np.unique(col, axis=0, return_inverse=True)
    which = which.ravel()
    size = 4
    while size * size < len(pal):
        size *= 2
    px = np.zeros((size * size, 4), np.uint8)
    px[:, 3] = 255
    px[: len(pal), :3] = pal
    key = T.astype(np.int64) * len(pal) + which[:, None]
    uniq, index = np.unique(key.ravel(), return_inverse=True)
    v, k = uniq // len(pal), uniq % len(pal)
    uv = np.stack([(k % size + 0.5) / size, (k // size + 0.5) / size], 1).astype(np.float32)
    return P[v], N[v], uv, index.ravel().astype(np.uint32), Image.fromarray(px.reshape(size, size, 4), 'RGBA'), len(pal)


def screen(P, N, I, ts, turn=True):
    """the triangles ts as a monitor: (P, N, UV, I, aspect, (width, height) in metres); UV runs 0..1 over its
    own rectangle (turn: the smallest around it, see upright; else the one that stands upright in the world), u to
    the viewer's right, v down"""
    T = I.reshape(len(I) // 3, 3)
    vi = np.unique(T[ts])
    n = N[vi].mean(0)
    n /= np.linalg.norm(n)
    up = np.array([0, 1.0, 0]) - n * n[1]  # the world's up within the monitor's plane
    up /= np.linalg.norm(up)
    right = np.cross(n, up)
    t = upright(P[vi] @ right, P[vi] @ up) if turn else 0.0
    right, up = right * np.cos(t) + up * np.sin(t), up * np.cos(t) - right * np.sin(t)
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
        # what is a monitor, and which, is told by its rectangle upright in the world, as it always was: the
        # monitors keep their numbers (the cockpits' .ini files name them)
        with np.errstate(all='ignore'):  # a part without width or height: its size says so
            m = screen(P, N, I, ts, False)
        if min(m[5]) >= SCREEN_MIN:
            found.append((m[5][0] * m[5][1], float(m[0][:, 0].mean()), ts))
    found.sort(key=lambda f: -f[0])
    kept = sorted(found[:SCREEN_MAX], key=lambda f: f[1])
    used = np.zeros(len(T), bool)
    for f in kept:
        used[f[2]] = True
    return [screen(P, N, I, f[2]) for f in kept], T[~used].ravel()


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
    for part, rest in (('lance', ahead[: c - 1]), ('weapons', ahead[c + 2 :])):
        if rest:
            res[part] = max(rest, key=lambda k: places[k][2])
    return res


def ini_text(places):
    """the text of a cockpit's .ini for monitors at places (see assign)"""
    roles = assign(places)
    lines = [
        '; Settings of this cockpit for MW4VR. Written once by cockpit_convert.py; change it as you like.',
        '[cockpit]',
        '; the eye in the mesh (metres) and the size of the mesh; in a battle: Ctrl+Alt+arrows, Ctrl+Alt+PgUp / PgDn',
        'scale=1.000',
        'eye_right_m=0.000',
        'eye_up_m=0.000',
        'eye_forward_m=0.000',
        '; 1 = the monitors show the HUD; 0 = they stay dark. What no monitor shows is on the glass (glass_<part>)',
        'screens=%d' % bool(roles),
        '; screen_<n> = u0 v0 u1 v1: the part of the HUD picture monitor n shows (none: dark). '
        'The monitors, left to right:',
    ]
    for k, (x, z, area) in enumerate(places):
        lines.append(';   %d: %.2f m to the right, %.2f m ahead, %.0f cm2' % (k, x, z, area * 10000))
    for part, k in sorted(roles.items(), key=lambda r: r[1]):
        lines.append('; {}'.format(part))
        lines.append('screen_%d=%s' % (k, HUD_PARTS[part]))
        if part == 'lance':
            lines.append('screen_%d_alt=%s' % (k, LANCE_ALT))
    return '\n'.join(lines) + '\n'


def flat(r, g, b):
    """a small opaque texture of one colour"""
    return Image.new('RGB', (4, 4), (r, g, b)).convert('RGBA')


def is_screen_material(name):
    return name.lower().startswith('panels')


def is_canopy_glass(name):
    """the sheet of glass over a player-made cockpit's windows (the game's own cockpits have none of that name)"""
    n = name.lower()
    return 'glass_cockpittp' in n and 'opaque' not in n


def build_groups(g, buf, props, root, log=print, shade=False):
    """-> (groups, lo, hi, places); a group: (name, P, N, UV, I, colour image, emissive image or None, emissive
    strength - for a screen its width / height); places: the monitors' (x, z, area)"""
    groups: list = []
    places: list = []
    lo, hi = np.full(3, 1e9), np.full(3, -1e9)
    names = [g['materials'][prim['material']]['name'] for prim in g['meshes'][0]['primitives']]
    panels = any(is_screen_material(n) for n in names)  # the game's way of naming monitors; else: the holograms
    for prim, name in zip(g['meshes'][0]['primitives'], names):  # noqa: B905 - one name for each primitive, made above
        if is_canopy_glass(name):
            log('  {}: the glass of the canopy, left out'.format(name))
            continue
        P = acc(g, buf, prim['attributes']['POSITION']).astype(np.float32).copy()
        N = acc(g, buf, prim['attributes']['NORMAL']).astype(np.float32).copy()
        UV = acc(g, buf, prim['attributes']['TEXCOORD_0']).astype(np.float32)
        I = acc(g, buf, prim['indices']).astype(np.uint32).ravel()
        P[:, 2] = -P[:, 2]  # glTF looks down -z; the mod's view space looks down +z
        N[:, 2] = -N[:, 2]
        lo, hi = np.minimum(lo, P.min(0)), np.maximum(hi, P.max(0))
        text = props_text(props, name)
        if is_screen_material(name) or (
            not panels and (is_hologram(text) or is_display(name))
        ):  # the monitors: one group each
            found, I = monitors(P, N, I)
            for p, n, uv, i, asp, (w, h) in found:
                c = p.mean(0)
                log('  screen %d: centre %.2f %.2f %.2f m, %.0f x %.0f cm' % (len(places), *c, 100 * w, 100 * h))
                groups.append(('SCREEN:%d' % len(places), p, n, uv, i, flat(0, 0, 0), None, float(asp)))
                places.append((float(c[0]), float(c[2]), w * h))
            if not len(I):
                continue
        clr, ems, strength = material(props, root, name, log)
        vc = vertex_colours(g, buf, prim)
        one = None if clr is None else clr.getcolors(1)
        base = WHITE if clr is None else one and one[0][1][:3]
        if shade and vc is not None and base:
            # one colour all over, but painted vertices: the colour shaded by them (without a colour: they alone)
            P, N, UV, I, clr, n = painted(P, N, I, vc, base, 1.0 if clr is None else VC_SHARE)
            log('  %s: shaded by its vertices, %d colours' % (name, n))
        if clr is None:
            clr = flat(80, 80, 84)
        # (a mod's part without any material: bare metal - but for the game's own picture materials)
        if shade and ems is None and (is_paint(text) or not (text or is_display(name))):
            name = PAINT + name
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
            # (on one line: the mutation tests leave a log line alone, not a log call's later lines)
            log('  %-34s %6d verts %6d tris, colour %d, emissive %d x %.2f' % (name, len(P), len(I) // 3, cs, es, strength))  # fmt: skip  # noqa: E501


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
            res.append(
                {
                    'name': name.split(b'\0')[0].decode(),
                    'verts': V,
                    'indices': I,
                    'colour_size': cs,
                    'emissive_size': es,
                    'strength': k,
                }
            )
        if f.read():
            raise ValueError('bytes after the last group')
        return res


def convert(gltf, root, out, tmax=TMAX, log=print, shade=False):
    """shade: a cockpit of a mod - its one-coloured parts take the shade of their vertex colours (in the game's own
    cockpits those colours mean other things), and its painted metal is marked for the mod to draw as such"""
    with open(gltf) as f:
        g = json.load(f)
    with open(os.path.join(os.path.dirname(gltf), g['buffers'][0]['uri']), 'rb') as f:
        buf = f.read()
    groups, lo, hi, places = build_groups(g, buf, find_props(root), root, log, shade)
    write_mwc(out, groups, tmax, log)
    ini = os.path.splitext(out)[0] + '.ini'
    if not os.path.exists(ini):  # yours once it is there
        with open(ini, 'w') as f:
            f.write(ini_text(places))
    log(
        'bounds x {:.2f}..{:.2f}  y {:.2f}..{:.2f}  z {:.2f}..{:.2f} m'.format(lo[0], hi[0], lo[1], hi[1], lo[2], hi[2])
    )
    log('written', out, os.path.getsize(out) // 1024, 'KB')
    return groups


def main(argv, log=print):
    if len(argv) < 4:
        sys.exit('python cockpit_convert.py <mesh.gltf> <export root> <out.mwc> [max texture size]')
    convert(argv[1], argv[2], argv[3], int(argv[4]) if len(argv) > 4 else TMAX, log)


if __name__ == '__main__':
    main(sys.argv)
