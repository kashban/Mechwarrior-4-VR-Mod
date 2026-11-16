# Trees for mw4hook's clutter (Content\Clutter\clutter.txt): a few kinds of cheap card trees.
#
# The firs come from Poly Haven's CC0 fir_sapling (https://polyhaven.com/a/fir_sapling, Rico Cilliers): that model
# is far too fine to draw by the hundred (needles as geometry), so it is drawn once into two pictures - from the
# side and from above - and each tree is made of cards that show them: discs of branches up a trunk, and upright
# cards for the outline. The broad-leaved trees need no source at all: their crowns are grown here, leaf by leaf,
# and drawn into the same two pictures.
#
#   python build_trees.py <fir_sapling folder with the glTF and its textures> <out folder> [<folder of fetch_polyhaven.py>]
#
# With the third folder (jacaranda_tree, island_tree_02, pine_sapling_small, quiver_tree_02, searsia_lucida,
# wild_rooibos_bush, shrub_02 in it) those plants are drawn into pictures the same way and become card trees too.
#
# Writes <kind>.obj / .mtl / .material and the pictures they use, <kind>_preview.png (the tree as built, from the
# side), sheet.png (all of them) and clutter_trees.txt (the kinds for clutter.txt).
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SIZE = 1024


# ---------------------------------------------------------------- the sapling's pictures

def gltf_mesh(folder, mesh_index):
    j = json.load(open(os.path.join(folder, 'fir_sapling_1k.gltf')))
    raw = open(os.path.join(folder, j['buffers'][0]['uri']), 'rb').read()

    def acc(i):
        a = j['accessors'][i]
        v = j['bufferViews'][a['bufferView']]
        n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[a['type']]
        t = {5126: np.float32, 5125: np.uint32, 5123: np.uint16}[a['componentType']]
        off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
        stride = v.get('byteStride', 0)
        if stride and stride != n * np.dtype(t).itemsize:
            out = np.zeros((a['count'], n), t)
            for k in range(a['count']):
                out[k] = np.frombuffer(raw, t, n, off + k * stride)
            return out
        return np.frombuffer(raw, t, a['count'] * n, off).reshape(a['count'], n)

    parts = []
    for p in j['meshes'][mesh_index]['primitives']:
        at = p['attributes']
        parts.append((j['materials'][p['material']]['name'], acc(at['POSITION']), acc(at['NORMAL']), acc(at['TEXCOORD_0']),
                      acc(p['indices']).reshape(-1, 3).astype(np.int64)))
    return parts


def points(parts, folder):
    """coloured points all over the mesh: position, colour, normal"""
    P, C, N = [], [], []
    for name, pos, nrm, uv, tri in parts:
        tex = np.asarray(Image.open(os.path.join(folder, name + '_diff_1k.png')).convert('RGB'), np.float32) / 255
        h, w = tex.shape[:2]
        rng = np.random.default_rng(1)
        for _ in range(24 if 'twigs' in name else 400):  # points within every triangle
            b = rng.random((len(tri), 2))
            flip = b.sum(1) > 1
            b[flip] = 1 - b[flip]
            b = np.concatenate([1 - b.sum(1, keepdims=True), b], 1)
            p = (pos[tri] * b[:, :, None]).sum(1)
            n = (nrm[tri] * b[:, :, None]).sum(1)
            t = (uv[tri] * b[:, :, None]).sum(1)
            x = np.clip((t[:, 0] % 1) * w, 0, w - 1).astype(int)
            y = np.clip((t[:, 1] % 1) * h, 0, h - 1).astype(int)
            P.append(p), N.append(n), C.append(tex[y, x])
    return np.concatenate(P), np.concatenate(C), np.concatenate(N)


def qrot(q, v):
    """the vectors v turned by the quaternion q (x y z w)"""
    u, w = np.array(q[:3], np.float64), q[3]
    return v + 2 * np.cross(u, np.cross(u, v) + w * v)


def model_points(folder, name, meshes=None, total=5_000_000, scale=1.0, seed=1):
    """Any Poly Haven model (fetch_polyhaven.py) as coloured points spread evenly over its surface: position (the
    foot's middle at 0 0 0, times scale), colour, normal. meshes: which of its meshes (the variants a .. n), or all."""
    j = json.load(open(os.path.join(folder, name + '_1k.gltf'), encoding='utf-8'))
    raw = open(os.path.join(folder, j['buffers'][0]['uri']), 'rb').read()

    def acc(i):
        a = j['accessors'][i]
        v = j['bufferViews'][a['bufferView']]
        n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
        t = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a['componentType']]
        off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
        stride = v.get('byteStride', 0)
        if stride and stride != n * np.dtype(t).itemsize:
            b = np.frombuffer(raw, np.uint8, a['count'] * stride - (stride - n * np.dtype(t).itemsize), off)
            idx = (np.arange(a['count'])[:, None] * stride + np.arange(n * np.dtype(t).itemsize)[None, :]).ravel()
            return np.frombuffer(b[idx].tobytes(), t).reshape(a['count'], n)
        return np.frombuffer(raw, t, a['count'] * n, off).reshape(a['count'], n)

    parts = []
    for node in j['nodes']:
        if 'mesh' not in node or (meshes is not None and node['mesh'] not in meshes):
            continue
        for p in j['meshes'][node['mesh']]['primitives']:
            mat = j['materials'][p['material']]
            tex = mat.get('pbrMetallicRoughness', {}).get('baseColorTexture')
            if tex is None:
                continue
            pos, nrm = acc(p['attributes']['POSITION']).astype(np.float64), acc(p['attributes']['NORMAL']).astype(np.float64)
            if 'rotation' in node:
                pos, nrm = qrot(node['rotation'], pos), qrot(node['rotation'], nrm)
            uri = j['images'][j['textures'][tex['index']]['source']]['uri']
            parts.append((os.path.join(folder, *uri.split('/')), mat.get('alphaMode', 'OPAQUE') != 'OPAQUE', pos, nrm, acc(p['attributes']['TEXCOORD_0']),
                          acc(p['indices']).reshape(-1, 3).astype(np.int64)))
    areas = [np.linalg.norm(np.cross(pos[tri[:, 1]] - pos[tri[:, 0]], pos[tri[:, 2]] - pos[tri[:, 0]]), axis=1) for _, _, pos, _, _, tri in parts]
    whole = sum(a.sum() for a in areas)
    rng = np.random.default_rng(seed)
    P, C, N = [], [], []
    for (path, cut, pos, nrm, uv, tri), area in zip(parts, areas):
        n = int(total * area.sum() / whole)
        if n == 0:
            continue
        pick = tri[rng.choice(len(tri), n, p=area / area.sum())]
        b = rng.random((n, 2))
        flip = b.sum(1) > 1
        b[flip] = 1 - b[flip]
        b = np.concatenate([1 - b.sum(1, keepdims=True), b], 1)
        p = (pos[pick] * b[:, :, None]).sum(1)
        nn = (nrm[pick] * b[:, :, None]).sum(1)
        t = (uv[pick] * b[:, :, None]).sum(1)
        img = Image.open(path)
        tex = np.asarray(img.convert('RGBA'), np.float32) / 255
        h, w = tex.shape[:2]
        s = tex[np.clip((t[:, 1] % 1) * h, 0, h - 1).astype(int), np.clip((t[:, 0] % 1) * w, 0, w - 1).astype(int)]
        keep = s[:, 3] > 0.5
        if cut and img.mode not in ('RGBA', 'LA'):  # a cut-out without its mask: what is not leaf is black in the picture
            keep &= s[:, :3].max(1) > 0.07
        P.append(p[keep]), N.append(nn[keep]), C.append(s[keep, :3])
    P, C, N = np.concatenate(P), np.concatenate(C), np.concatenate(N)
    low = P[P[:, 1] < P[:, 1].min() + 0.03 * np.ptp(P[:, 1])]  # the foot
    P = (P - [low[:, 0].mean(), P[:, 1].min(), low[:, 2].mean()]) * scale
    return P, C, N


def model_sheet(P, C, N):
    """the cards' picture of a model's points, and what it spans: half its width, its height"""
    half = float(np.abs(P[:, [0, 2]]).max()) * 1.02
    h = float(P[:, 1].max()) * 1.01
    s = finish(view(P, C, N, 0, 1, 2, np.array([-half, 0.0]), np.array([half, h])))
    t = finish(view(P, C, N, 0, 2, 1, np.array([-half, -half]), np.array([half, half])))
    return sheet_of(s, t), half, h


def view(P, C, N, ax, ay, depth, lo, hi):
    """the points seen along the axis depth (the largest value nearest), ax across, ay up: RGBA, SIZE x SIZE"""
    span = hi - lo
    px = np.clip(((P[:, ax] - lo[0]) / span[0] * SIZE).astype(int), 0, SIZE - 1)
    py = np.clip(((hi[1] - P[:, ay]) / span[1] * SIZE).astype(int), 0, SIZE - 1)
    order = np.argsort(P[:, depth])  # the nearest written last
    lit = 0.62 + 0.38 * np.clip(N[:, 1] * 0.5 + 0.5, 0, 1)  # the sky's light, baked in lightly
    img = np.zeros((SIZE, SIZE, 4), np.float32)
    img[py[order], px[order], :3] = C[order] * lit[order, None]
    img[py[order], px[order], 3] = 1
    return img


def finish(img):
    """holes between the points closed, the colour carried out past the edge (no dark fringe), 8 bit"""
    a = Image.fromarray((img[:, :, 3] * 255).astype(np.uint8))
    a = a.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(3))
    return bleed(img[:, :, :3].copy(), img[:, :, 3] > 0, np.asarray(a, np.float32))


def bleed(rgb, known, alpha):
    """rgb 0..1 where known; every empty pixel takes the mean of its known neighbours, a few times over"""
    known = known.copy()
    for _ in range(24):
        s = np.zeros_like(rgb)
        c = np.zeros(known.shape, np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            s += np.roll(rgb * known[:, :, None], (dy, dx), (0, 1))
            c += np.roll(known, (dy, dx), (0, 1))
        new = ~known & (c > 0)
        rgb[new] = s[new] / c[new][:, None]
        known = known | new
    out = np.dstack([np.clip(rgb, 0, 1) * 255, alpha]).astype(np.uint8)
    return Image.fromarray(out, 'RGBA')


def sheet_of(side, top):
    """the cards' picture: its left half the tree from the side, its right half from above"""
    cards = Image.new('RGBA', (SIZE * 2, SIZE))
    cards.paste(side, (0, 0)), cards.paste(top, (SIZE, 0))
    return cards


def recolour(cards, mul, grey=0.0, thin=0.0, seed=5):
    """another fir from the same pictures: the colour times mul, part of it greyed, a share of the needles gone"""
    a = np.asarray(cards, np.float32) / 255
    rgb = a[:, :, :3]
    g = rgb.mean(2, keepdims=True)
    rgb = (rgb * (1 - grey) + g * grey) * np.array(mul, np.float32)
    alpha = a[:, :, 3]
    if thin > 0:  # in clumps, not pixel by pixel: whole twigs bare
        rng = np.random.default_rng(seed)
        n = rng.random((SIZE // 16, SIZE * 2 // 16)).astype(np.float32)
        n = np.asarray(Image.fromarray((n * 255).astype(np.uint8)).resize((SIZE * 2, SIZE), Image.BICUBIC), np.float32) / 255
        alpha = alpha * (n > thin)
    return Image.fromarray((np.dstack([np.clip(rgb, 0, 1), alpha]) * 255).astype(np.uint8), 'RGBA')


# ---------------------------------------------------------------- a crown grown here

def crown(seed, rx, ry, cy, clusters, leaves, spread, droop, leaf, colour, vary, bare=0.0):
    """leaves of a broad-leaved crown: an uneven ellipsoid (radii rx across, ry up, centre cy up the tree) filled
    with clusters of leaves. Returns the leaves (position, size, colour) and the boughs (from the stem to a cluster)."""
    rng = np.random.default_rng(seed)
    lobes = rng.normal(size=(9, 3))
    lobes /= np.linalg.norm(lobes, axis=1, keepdims=True)
    amp = rng.uniform(0.1, 0.32, 9)
    d = rng.normal(size=(clusters, 3))
    d /= np.linalg.norm(d, axis=1, keepdims=True)
    d[:, 1] = np.abs(d[:, 1]) * 1.0 - 0.45 * rng.random(clusters)  # more of them above than below
    d /= np.linalg.norm(d, axis=1, keepdims=True)
    bump = 1 + (amp * np.clip(d @ lobes.T, 0, 1) ** 4).sum(1)
    r = bump * (0.5 + 0.5 * np.sqrt(rng.random(clusters)))  # towards the surface
    c = d * r[:, None] * np.array([rx, ry, rx]) + np.array([0, cy, 0])
    P = np.repeat(c, leaves, 0) + rng.normal(size=(clusters * leaves, 3)) * spread * np.array([1, 0.7, 1])
    P[:, 1] -= np.abs(rng.normal(size=len(P))) * droop  # hanging twigs
    if bare > 0:
        P = P[rng.random(len(P)) > bare]
    out = np.linalg.norm((P - [0, cy, 0]) / [rx, ry, rx], axis=1)
    shade = np.clip(0.45 + 0.4 * np.clip(out, 0, 1.2) + 0.25 * (P[:, 1] - cy) / ry, 0.25, 1.15)
    col = np.array(colour) * shade[:, None] * (1 + rng.normal(size=(len(P), 1)) * vary) + rng.normal(size=(len(P), 3)) * vary * 0.1
    size = leaf * rng.uniform(0.7, 1.3, len(P))
    # a few boughs, each leaving the stem a little below its cluster (many from one point make a brown funnel)
    boughs = [((0.0, max(cy - ry * 0.95, c[i][1] - ry * rng.uniform(0.25, 0.6)), 0.0), tuple(c[i])) for i in range(clusters) if rng.random() < 0.14]
    return P, size, np.clip(col, 0, 1), boughs, rng


def crown_view(P, size, col, boughs, rng, ax, ay, depth, lo, hi, bark):
    """the crown seen along the axis depth as an RGBA picture, boughs first, the nearest leaves last"""
    img = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)
    sx, sy = SIZE / (hi[0] - lo[0]), SIZE / (hi[1] - lo[1])

    def px(p):
        return ((p[ax] - lo[0]) * sx, (hi[1] - p[ay]) * sy)

    for a, b in boughs:
        dr.line([px(a), px(b)], fill=tuple(int(255 * v) for v in bark) + (255,), width=max(2, int(0.07 * sx)))
    order = np.argsort(P[:, depth])
    ang = rng.uniform(0, math.pi, len(P))
    near = (P[:, depth] - P[:, depth].min()) / max(np.ptp(P[:, depth]), 1e-6)
    for i in order:
        x, y = px(P[i])
        L, a = size[i] * sx, ang[i]
        dx, dy = math.cos(a) * L, math.sin(a) * L
        wx, wy = -dy * 0.42, dx * 0.42
        c = np.clip(col[i] * (0.82 + 0.3 * near[i]), 0, 1)
        dr.polygon([(x - dx, y - dy), (x + wx, y + wy), (x + dx, y + dy), (x - wx, y - wy)], fill=tuple(int(255 * v) for v in c) + (255,))
    a = np.asarray(img, np.float32) / 255
    return bleed(a[:, :, :3].copy(), a[:, :, 3] > 0, a[:, :, 3] * 255)


def birch_bark(seed=3):
    rng = np.random.default_rng(seed)
    n = rng.random((64, 32)).astype(np.float32)
    n = np.asarray(Image.fromarray((n * 255).astype(np.uint8)).resize((256, 512), Image.BICUBIC), np.float32) / 255
    img = Image.fromarray((np.dstack([0.80 + 0.14 * n, 0.79 + 0.14 * n, 0.74 + 0.14 * n]) * 255).astype(np.uint8), 'RGB')
    dr = ImageDraw.Draw(img)
    for _ in range(70):  # the dark lens-shaped marks
        x, y, w, h = rng.uniform(0, 256), rng.uniform(0, 512), rng.uniform(8, 46), rng.uniform(2, 7)
        g = int(rng.uniform(20, 70))
        dr.ellipse([x - w, y - h, x + w, y + h], fill=(g, g, g - 6))
    return img.filter(ImageFilter.GaussianBlur(0.8))


# ---------------------------------------------------------------- the model

class Obj:
    def __init__(self, name):
        self.name, self.v, self.vt, self.vn, self.groups = name, [], [], [], {}

    def tri(self, mat, pts):
        """pts: three (position, uv, normal)"""
        f = []
        for p, t, n in pts:
            self.v.append(p), self.vt.append(t), self.vn.append(n)
            f.append(len(self.v))
        self.groups.setdefault(mat, []).append(f)

    def quad(self, mat, a, b, c, d):
        self.tri(mat, (a, b, c)), self.tri(mat, (a, c, d))

    def count(self):
        return sum(len(g) for g in self.groups.values())

    def write(self, path, mtl):
        with open(path, 'w') as f:
            f.write('# %s: MW4VR tree, built by tools/trees/build_trees.py. Y up, metres.\n' % self.name)
            f.write('mtllib %s\no %s\n' % (mtl, self.name))
            for p in self.v:
                f.write('v %.4f %.4f %.4f\n' % tuple(p))
            for t in self.vt:
                f.write('vt %.4f %.4f\n' % tuple(t))
            for n in self.vn:
                f.write('vn %.3f %.3f %.3f\n' % tuple(n))
            for mat, faces in self.groups.items():
                f.write('usemtl %s\n' % mat)
                for a, b, c in faces:
                    f.write('f %d/%d/%d %d/%d/%d %d/%d/%d\n' % (a, a, a, b, b, b, c, c, c))


def unit(v):
    v = np.asarray(v, float)
    return v / max(np.linalg.norm(v), 1e-9)


def side(u, v):
    return (0.5 * u, v)


def top(u, v):
    return (0.5 + 0.5 * u, v)


def build(name, height, trunk, whorls, cards, lean=0.0, sides=8):
    """trunk: (radius at the foot, up to this height); whorls: [(height, radius, droop, turn)] - six-sided cones of
    the picture from above, hanging (droop > 0) or domed (< 0); cards: (from, to, half width, how many) - upright
    cards of the picture from the side. lean: how far the top stands off the foot's axis (m)."""
    o = Obj(name)
    bark, leaf = name + '_bark', name + '_cards'

    def off(y):
        return lean * (y / height) ** 2

    r_foot, t_top = trunk or (0, 0)  # (none: the stem is in the cards' picture)
    rings = 6 if trunk else 0
    for i in range(rings):
        y0, y1 = t_top * i / rings, t_top * (i + 1) / rings
        r0, r1 = r_foot * (1 - i / rings) + 0.03, r_foot * (1 - (i + 1) / rings) + 0.03
        for s in range(sides):
            a0, a1 = 2 * math.pi * s / sides, 2 * math.pi * (s + 1) / sides
            c0, s0, c1, s1 = math.cos(a0), math.sin(a0), math.cos(a1), math.sin(a1)
            o.quad(bark, ((off(y0) + r0 * c0, y0, r0 * s0), (s / 4, y0 / 2), (c0, 0, s0)), ((off(y0) + r0 * c1, y0, r0 * s1), ((s + 1) / 4, y0 / 2), (c1, 0, s1)),
                   ((off(y1) + r1 * c1, y1, r1 * s1), ((s + 1) / 4, y1 / 2), (c1, 0, s1)), ((off(y1) + r1 * c0, y1, r1 * s0), (s / 4, y1 / 2), (c0, 0, s0)))
    for y, r, droop, turn in whorls:
        centre = ((off(y), y, 0), top(0.5, 0.5), (0, 1, 0))
        for s in range(6):
            e = []
            for a in (turn + 2 * math.pi * s / 6, turn + 2 * math.pi * (s + 1) / 6):
                c, sn = math.cos(a), math.sin(a)
                e.append(((off(y) + r * c, y - droop, r * sn), top(0.5 + 0.5 * math.cos(a - turn), 0.5 + 0.5 * math.sin(a - turn)), tuple(unit((c, 0.9, sn)))))
            o.tri(leaf, (centre, e[0], e[1]))
    y0, y1, w, n = cards
    for s in range(n):
        a = math.pi * s / n + 0.3
        c, sn = math.cos(a), math.sin(a)
        nm = tuple(unit((-sn, 0.35, c)))
        o.quad(leaf, ((off(y0) - w * c, y0, -w * sn), side(0, 0), nm), ((off(y0) + w * c, y0, w * sn), side(1, 0), nm),
               ((off(y1) + w * c, y1, w * sn), side(1, 1), nm), ((off(y1) - w * c, y1, -w * sn), side(0, 1), nm))
    return o


def fir_whorls(height, n, lowest, radius, droop=0.32, power=0.8):
    out = []
    for w in range(n):
        k = w / (n - 1)
        r = radius * (1 - k) ** power + 0.35
        out.append((lowest + (height * 0.93 - lowest) * k ** 0.9, r, droop * r, w * 0.9))
    return out


# ---------------------------------------------------------------- a look at what was built

def preview(o, tex, size=384, yaw=0.5, pitch=0.2):
    """the model drawn as the game would (cut-out textures, no light), seen a little from above"""
    V = np.array(o.v, float)
    T = np.array(o.vt, float)
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    x = V[:, 0] * cy + V[:, 2] * sy
    z = -V[:, 0] * sy + V[:, 2] * cy
    y = V[:, 1] * cp + z * sp
    z = -V[:, 1] * sp + z * cp
    span = max(np.ptp(x), np.ptp(y)) * 1.05
    px = (x - (x.min() + x.max()) / 2) / span * size + size / 2
    py = size - 4 - (y - y.min()) / span * size
    img = np.zeros((size, size, 3), np.float32)
    img[:] = (0.42, 0.55, 0.72)
    zb = np.full((size, size), -1e9)
    for mat, faces in o.groups.items():
        t = np.asarray(tex[mat].convert('RGBA'), np.float32) / 255
        th, tw = t.shape[:2]
        for f in faces:
            i = [k - 1 for k in f]
            X, Y, Z, U = px[i], py[i], z[i], T[i]
            x0, x1 = int(max(0, math.floor(X.min()))), int(min(size - 1, math.ceil(X.max())))
            y0, y1 = int(max(0, math.floor(Y.min()))), int(min(size - 1, math.ceil(Y.max())))
            if x1 < x0 or y1 < y0:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            den = (Y[1] - Y[2]) * (X[0] - X[2]) + (X[2] - X[1]) * (Y[0] - Y[2])
            if abs(den) < 1e-9:
                continue
            b0 = ((Y[1] - Y[2]) * (gx - X[2]) + (X[2] - X[1]) * (gy - Y[2])) / den
            b1 = ((Y[2] - Y[0]) * (gx - X[2]) + (X[0] - X[2]) * (gy - Y[2])) / den
            b2 = 1 - b0 - b1
            zz = b0 * Z[0] + b1 * Z[1] + b2 * Z[2]
            u = (b0 * U[0, 0] + b1 * U[1, 0] + b2 * U[2, 0]) % 1
            v = (b0 * U[0, 1] + b1 * U[1, 1] + b2 * U[2, 1]) % 1
            s = t[np.clip(((1 - v) * th).astype(int), 0, th - 1), np.clip((u * tw).astype(int), 0, tw - 1)]
            ok = (b0 >= 0) & (b1 >= 0) & (b2 >= 0) & (s[:, :, 3] > 0.4) & (zz > zb[y0:y1 + 1, x0:x1 + 1])
            img[y0:y1 + 1, x0:x1 + 1][ok] = s[:, :, :3][ok]
            zb[y0:y1 + 1, x0:x1 + 1][ok] = zz[ok]
    return Image.fromarray((img * 255).astype(np.uint8), 'RGB')


# ---------------------------------------------------------------- the kinds

GRASS = ['eaton03_color_grass1_act', 'eaton03_color_grass2_act', 'trop01_color_grass1_act', 'trop01_color_grass2_act', 'trop01_color_grass3_act',
         'trop01_color_grass4_act', 'trop01_color_grass5_act', 'trop01_color_grass6_act', 'trop01_color_grass7_act', 'training_lightgrass_active']
# (material, weight for the firs, weight for the broad-leaved)
MIXED = [('arctic05_color_grass_active', 0.9, 0.2), ('alpine01_color_terraingrassshale_active', 0.8, 0.3), ('mountn03_color_terraingrassshale_active', 0.8, 0.3),
         ('alpine02_color_terraingrassbush1_active', 0.8, 0.4), ('alpinenew_color_terraingrassbush1_active', 0.8, 0.4),
         ('alpine03_color_rocksgreenshrub_active', 0.4, 0.15), ('alpine01_color_rockfacegreen_active', 0.4, 0.1), ('scrub01_color_grass_01', 0.3, 0.3),
         ('eaton02_color_mowedgrass', 0.1, 0.15), ('newav01_color_mowedgrass', 0.1, 0.15), ('cityroad_color_median_*', 0.2, 0.3)]


# where the dry grass grows: the desert's and the steppe's plants
DRY = [('desert01_color_grass02', 1), ('desert01_color_grass03', 1), ('desert01_color_grass04', 1), ('scrub01_color_browngrass_active', 1),
       ('scrub01_color_grass_01', 0.7), ('hallor03_color_deadgrass_active', 1), ('arctic01_color_deadgrass_active', 0.3)]
# the warm maps' grass alone (Exford, the jungle)
WARM = [(m, 1) for m in GRASS if m.startswith('trop01') or m.startswith('eaton03')]


# Half the densities the kinds below name: a wood of cards fills the picture many times over, and in a headset
# (two large pictures a frame) that cost three quarters of the frame rate. Trees (not the low bushes) are not drawn
# nearer than NEAR metres either: nothing stops a mech walking into one, and its crown would stand in the cockpit.
THIN, NEAR = 0.5, 14


def kind_text(name, model, density, size, patch, seed, wind, conifer=False, grass=1.0, where='green', distance=1000, sink=(-0.4, -0.15)):
    tree = distance >= 1000
    t = ['[%s]' % name, 'enabled = 1', 'model = clutter\\mw4vr\\%s.obj' % model, 'density = %g' % (density * THIN), 'size = %g %g' % size, 'rotate = 0 360',
         'sink = %g %g' % sink, 'jitter = 1', 'align = up', 'slope = 22 35', 'above_water = 0.5', 'distance = %d %d' % (NEAR if tree else 0, distance),
         'grow = %s 0.1' % ('0.01' if tree else '0'),
         'fade = dither', 'bands = auto', 'patch = %g %g' % patch, 'seed = %d' % seed, 'wind = %g %g' % wind, 'ground_tint = 0 1']
    if where == 'green':
        for m in GRASS:
            t.append('material = %s%s' % (m, '' if grass == 1 else ' %g' % grass))
        for m, wc, wb in MIXED:
            t.append('material = %s %g' % (m, wc if conifer else wb))
    else:
        for m, w in (DRY if where == 'dry' else WARM):
            t.append('material = %s%s' % (m, '' if w == 1 else ' %g' % w))
    return '\n'.join(t) + '\n'


def card_tree(name, half, h, domes, cards=3, lean=0.0):
    """a tree all of cards for a model's pictures: upright cards over its whole height, and domes (height share,
    radius share, how far the middle stands above the rim as a share of the radius) of the picture from above"""
    return build(name, h, None, [(h * y, half * r, -half * r * up, 0.4 + 1.1 * i) for i, (y, r, up) in enumerate(domes)], (0.0, h, half, cards), lean)


def main(src, out, ph=None):
    os.makedirs(out, exist_ok=True)
    parts = gltf_mesh(src, 0)
    P, C, N = points(parts, src)
    lo, hi = P.min(0), P.max(0)
    half = max(hi[0], -lo[0], hi[2], -lo[2])
    fir = sheet_of(finish(view(P, C, N, 0, 1, 2, np.array([-half, lo[1]]), np.array([half, hi[1]]))),
                   finish(view(P, C, N, 0, 2, 1, np.array([-half, -half]), np.array([half, half]))))
    fir_bark = Image.open(os.path.join(src, 'fir_sapling_branches_diff_1k.png')).convert('RGB')
    dark_bark = Image.eval(fir_bark, lambda v: int(v * 0.7))
    brown, grey = (0.30, 0.22, 0.15), (0.34, 0.31, 0.27)

    def grown(seed, half_w, y0, y1, bark, **kw):
        Pl, size, col, boughs, rng = crown(seed, **kw)
        s = crown_view(Pl, size, col, boughs, rng, 0, 1, 2, (-half_w, y0), (half_w, y1), bark)
        t = crown_view(Pl, size, col, boughs, rng, 0, 2, 1, (-half_w, -half_w), (half_w, half_w), bark)
        return sheet_of(s, t)

    oak = grown(11, 6.4, 3.0, 14.2, brown, rx=5.0, ry=4.6, cy=8.9, clusters=230, leaves=34, spread=0.62, droop=0.15, leaf=0.19,
                colour=(0.23, 0.36, 0.12), vary=0.16)
    oak2 = grown(29, 5.4, 4.4, 17.4, brown, rx=4.0, ry=5.6, cy=11.2, clusters=210, leaves=34, spread=0.6, droop=0.2, leaf=0.19,
                 colour=(0.27, 0.38, 0.13), vary=0.18)
    birch = grown(17, 3.6, 3.6, 15.2, grey, rx=2.5, ry=5.2, cy=9.6, clusters=150, leaves=26, spread=0.5, droop=0.9, leaf=0.12,
                  colour=(0.42, 0.55, 0.18), vary=0.2, bare=0.1)
    bush = grown(41, 2.6, 0.0, 3.4, brown, rx=2.0, ry=1.6, cy=1.5, clusters=120, leaves=30, spread=0.36, droop=0.05, leaf=0.12,
                 colour=(0.21, 0.33, 0.12), vary=0.18)

    # name: (model, cards picture, bark picture, clutter kind's values)
    trees = [
        ('fir_01', build('fir_01', 14.0, (0.24, 13.6), fir_whorls(14.0, 13, 2.2, 3.3), (1.2, 14.0, 3.56, 3)), fir, fir_bark,
         dict(density=0.0009, size=(0.8, 1.5), patch=(70, 0.5), seed=77, wind=(0.012, 0.35), conifer=True)),
        ('fir_02', build('fir_02', 21.0, (0.34, 20.4), fir_whorls(21.0, 16, 6.5, 3.0, 0.4, 0.7), (5.4, 21.0, 3.3, 3), lean=0.5),
         recolour(fir, (0.78, 0.9, 0.92)), dark_bark, dict(density=0.0006, size=(0.85, 1.3), patch=(70, 0.5), seed=78, wind=(0.01, 0.28), conifer=True)),
        ('fir_03', build('fir_03', 7.5, (0.14, 7.2), fir_whorls(7.5, 9, 0.7, 2.5, 0.22, 0.9), (0.3, 7.5, 2.7, 3)),
         recolour(fir, (1.06, 1.1, 0.9)), fir_bark, dict(density=0.0009, size=(0.6, 1.3), patch=(45, 0.55), seed=79, wind=(0.02, 0.5), conifer=True)),
        ('fir_04', build('fir_04', 15.0, (0.26, 14.6), fir_whorls(15.0, 8, 4.0, 2.4, 0.5, 0.6), (3.0, 15.0, 2.6, 2), lean=1.1),
         recolour(fir, (0.95, 0.74, 0.5), grey=0.75, thin=0.5), dark_bark,
         dict(density=0.0002, size=(0.7, 1.3), patch=(70, 0.5), seed=80, wind=(0.006, 0.3), conifer=True)),
        ('oak_01', build('oak_01', 14.0, (0.42, 9.5), [(7.2, 5.0, -0.9, 0.2), (10.0, 4.6, -1.3, 1.3), (12.4, 3.0, -0.9, 2.1)], (3.0, 14.2, 6.4, 3)),
         oak, dark_bark, dict(density=0.0006, size=(0.8, 1.4), patch=(80, 0.45), seed=91, wind=(0.008, 0.3), conifer=False)),
        ('oak_02', build('oak_02', 17.2, (0.38, 12.0), [(8.6, 3.9, -1.0, 0.5), (11.8, 4.0, -1.5, 1.7), (15.0, 2.6, -1.0, 2.6)], (4.4, 17.4, 5.4, 3), lean=0.4),
         oak2, dark_bark, dict(density=0.0005, size=(0.75, 1.3), patch=(80, 0.45), seed=92, wind=(0.009, 0.3), conifer=False)),
        ('birch_01', build('birch_01', 15.0, (0.2, 13.5), [(8.0, 2.4, 0.5, 0.4), (11.0, 2.3, 0.3, 1.5), (13.6, 1.5, 0.2, 2.4)], (3.6, 15.2, 3.6, 3), lean=0.6),
         birch, birch_bark(), dict(density=0.0006, size=(0.7, 1.25), patch=(55, 0.4), seed=93, wind=(0.016, 0.45), conifer=False)),
        ('bush_01', build('bush_01', 3.2, (0.1, 1.6), [(1.2, 2.0, -0.5, 0.3), (2.3, 1.5, -0.5, 1.4)], (0.0, 3.4, 2.6, 3), sides=5),
         bush, fir_bark, dict(density=0.0016, size=(0.6, 1.5), patch=(40, 0.5), seed=94, wind=(0.03, 0.7), conifer=False)),
    ]
    names = {'fir_01': 'tree_fir', 'fir_02': 'tree_fir_tall', 'fir_03': 'tree_fir_young', 'fir_04': 'tree_fir_dead', 'oak_01': 'tree_oak',
             'oak_02': 'tree_oak_tall', 'birch_01': 'tree_birch', 'bush_01': 'tree_bush'}
    if ph:
        # with Poly Haven's other plants at hand (fetch_polyhaven.py) the photographed ones take the drawn ones' places:
        # name, asset, which of its meshes, scale, domes, the clutter kind's name and values
        trees = [t for t in trees if t[0] not in ('oak_01', 'oak_02', 'birch_01', 'bush_01')]
        real = [
            ('leaf_01', 'tree_small_02', None, 3.4, [(0.5, 0.95, 0.1), (0.72, 0.85, 0.2), (0.9, 0.5, 0.25)], 'tree_leaf',
             dict(density=0.0006, size=(0.75, 1.25), patch=(80, 0.45), seed=108, wind=(0.008, 0.3))),
            ('leaf_02', 'island_tree_01', None, 3.2, [(0.5, 0.95, 0.1), (0.72, 0.85, 0.2), (0.9, 0.5, 0.25)], 'tree_leaf_wide',
             dict(density=0.0005, size=(0.75, 1.25), patch=(80, 0.45), seed=109, wind=(0.008, 0.3))),
            ('leaf_03', 'island_tree_03', None, 3.4, [(0.6, 0.95, 0.12), (0.85, 0.7, 0.25)], 'tree_leaf_small',
             dict(density=0.0006, size=(0.7, 1.3), patch=(55, 0.4), seed=110, wind=(0.012, 0.4))),
            ('jacaranda_01', 'jacaranda_tree', None, 0.85, [(0.5, 0.95, 0.1), (0.72, 0.85, 0.2), (0.9, 0.5, 0.25)], 'tree_jacaranda',
             dict(density=0.0005, size=(0.7, 1.1), patch=(80, 0.45), seed=101, wind=(0.006, 0.25))),
            ('island_01', 'island_tree_02', None, 3.2, [(0.6, 0.95, 0.12), (0.85, 0.7, 0.25)], 'tree_island',
             dict(density=0.0009, size=(0.75, 1.3), patch=(60, 0.5), seed=102, wind=(0.01, 0.35), where='warm')),
            ('pine_01', 'pine_sapling_small', [1], 11.0, [(0.3, 0.95, -0.2), (0.48, 0.9, -0.2), (0.66, 0.7, -0.2), (0.82, 0.5, -0.2), (0.94, 0.25, -0.2)],
             'tree_pine', dict(density=0.0007, size=(0.75, 1.35), patch=(70, 0.5), seed=103, wind=(0.012, 0.35), conifer=True)),
            ('quiver_01', 'quiver_tree_02', None, 3.6, [(0.82, 0.95, 0.1)], 'tree_quiver',
             dict(density=0.0005, size=(0.7, 1.4), patch=(70, 0.35), seed=104, wind=(0.002, 0.3), where='dry')),
            ('searsia_01', 'searsia_lucida', [0], 1.6, [(0.55, 0.9, 0.3)], 'bush_searsia',
             dict(density=0.002, size=(0.6, 1.4), patch=(40, 0.45), seed=105, wind=(0.02, 0.6), where='dry', distance=900, sink=(-0.1, 0))),
            ('rooibos_01', 'wild_rooibos_bush', [0], 2.4, [(0.6, 0.9, 0.3)], 'bush_rooibos',
             dict(density=0.006, size=(0.6, 1.5), patch=(25, 0.5), seed=106, wind=(0.03, 0.8), where='dry', distance=400, sink=(-0.05, 0))),
            ('shrub_01', 'shrub_02', [0], 1.5, [(0.5, 0.9, 0.3)], 'bush_green',
             dict(density=0.002, size=(0.6, 1.5), patch=(40, 0.5), seed=107, wind=(0.03, 0.7), distance=900, sink=(-0.1, 0))),
        ]
        none = Image.new('RGB', (8, 8), (70, 55, 40))
        for name, asset, meshes, scale, domes, kind, k in real:
            cards, half, h = model_sheet(*model_points(os.path.join(ph, asset), asset, meshes, scale=scale))
            trees.append((name, card_tree(name, half, h, domes), cards, none, k))
            names[name] = kind
            print('%-12s %.1f m high, %.1f m across' % (name, h, 2 * half))
    shots, kinds, total = [], [], 0
    for name, o, cards, bark, k in trees:
        cards.save(os.path.join(out, name + '_cards.png'))
        bark.save(os.path.join(out, name + '_bark.png'))
        o.write(os.path.join(out, name + '.obj'), name + '.mtl')
        with open(os.path.join(out, name + '.mtl'), 'w') as f:
            f.write('newmtl %s_bark\nmap_Kd %s_bark.png\nnewmtl %s_cards\nmap_Kd %s_cards.png\nmap_d %s_cards.png\n' % ((name,) * 5))
        with open(os.path.join(out, name + '.material'), 'w') as f:
            f.write('; MW4VR tree, made by tools/trees/build_trees.py%s\n' % ('; pictures from Poly Haven CC0 fir_sapling (Rico Cilliers)' if name.startswith('fir') else '')
                    + '[%s_bark]\nps = opaque\nalbedo = %s_bark.png\nroughness = 0.9\n\n' % (name, name)
                    + '[%s_cards]\nps = foliage\nalbedo = %s_cards.png\nroughness = 0.85\ntransmission = 0.2\ntwo_sided = 1\nalpha_cutoff = 0.4\n' % (name, name))
        p = preview(o, {name + '_bark': bark, name + '_cards': cards})
        p.save(os.path.join(out, name + '_preview.png'))
        shots.append(p)
        kinds.append(kind_text(names[name], name, grass=0.6 if name == 'fir_04' else 1.0, **k))
        total += k['density']
        print('%-9s %4d triangles' % (name, o.count()))
    sheet = Image.new('RGB', (384 * 5, 384 * ((len(shots) + 4) // 5)))
    for i, p in enumerate(shots):
        sheet.paste(p, (384 * (i % 5), 384 * (i // 5)))
    sheet.save(os.path.join(out, 'sheet.png'))
    with open(os.path.join(out, 'clutter_trees.txt'), 'w') as f:
        f.write('; MW4VR trees (Content\\Models\\clutter\\mw4vr, built by MW4VR tools\\trees\\build_trees.py). A kind off: enabled = 0.\n'
                '; All of them thinner or denser at once: [d3d11] clutter_density in mw4hook.ini, or the densities here.\n' + '\n'.join(kinds))
    print('%d kinds, %.4f trees per square metre of grass at most' % (len(kinds), total))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
