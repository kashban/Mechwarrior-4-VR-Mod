# Tests of tools/cockpit_convert.py on a small made-up cockpit (no MechWarrior 5 files needed).
#   python -m unittest discover -s tests
# MW4VR_CONVERT names another file to test in its place (tests/mutate.py: a copy with one planted error).
import importlib.util, json, os, struct, tempfile, unittest
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.environ.get('MW4VR_CONVERT') or os.path.join(HERE, '..', 'tools', 'cockpit_convert.py')
spec = importlib.util.spec_from_file_location('cockpit_convert', SRC)
cc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cc)


def put(path, text):
    with open(path, 'w') as f:
        f.write(text)


def get(path, mode='rb'):
    with open(path, mode) as f:
        return f.read()


def quad(x0, y0, x1, y1, z, flip=False):
    """a rectangle in the plane z, facing the viewer at the origin's -z side of the glTF (normal +z there)"""
    P = np.array([[x0, y0, z], [x1, y0, z], [x1, y1, z], [x0, y1, z]], np.float32)
    N = np.tile(np.array([0, 0, 1], np.float32), (4, 1))
    UV = np.array([[0, 1], [1, 1], [1, 0], [0, 0]], np.float32)
    I = np.array([0, 1, 2, 0, 2, 3], np.uint16)
    return P, N, UV, I


def write_gltf(folder, prims, interleaved=False):
    """prims: [(material name, P, N, UV, I)] -> path of a glTF with one mesh"""
    blob, views, accs, mats, gprims = b'', [], [], [], []

    def add(arr, ctype, kind):
        nonlocal blob
        while len(blob) % 4:
            blob += b'\0'
        views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': arr.nbytes})
        accs.append({'bufferView': len(views) - 1, 'componentType': ctype, 'count': len(arr), 'type': kind})
        blob += arr.tobytes()
        return len(accs) - 1

    for name, P, N, UV, I, *C in prims:  # C: vertex colours, n x 4 (bytes or floats)
        mats.append({'name': name})
        if interleaved:  # positions and normals share a view with a stride
            both = np.hstack([P, N]).astype('<f4')
            while len(blob) % 4:
                blob += b'\0'
            views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': both.nbytes, 'byteStride': 24})
            accs.append({'bufferView': len(views) - 1, 'componentType': 5126, 'count': len(P), 'type': 'VEC3'})
            accs.append({'bufferView': len(views) - 1, 'byteOffset': 12, 'componentType': 5126, 'count': len(P), 'type': 'VEC3'})
            ap, an = len(accs) - 2, len(accs) - 1
            blob += both.tobytes()
        else:
            ap, an = add(P.astype('<f4'), 5126, 'VEC3'), add(N.astype('<f4'), 5126, 'VEC3')
        at = add(UV.astype('<f4'), 5126, 'VEC2')
        ai = add(I.astype('<u2'), 5123, 'SCALAR')
        gprims.append({'attributes': {'POSITION': ap, 'NORMAL': an, 'TEXCOORD_0': at}, 'indices': ai, 'material': len(mats) - 1})
        if C:
            floats = C[0].dtype.kind == 'f'
            gprims[-1]['attributes']['COLOR_0'] = add(C[0].astype('<f4' if floats else 'u1'), 5126 if floats else 5121, 'VEC4')
    g = {'buffers': [{'uri': 'mesh.bin', 'byteLength': len(blob)}], 'bufferViews': views, 'accessors': accs, 'materials': mats,
         'meshes': [{'primitives': gprims}]}
    with open(os.path.join(folder, 'mesh.bin'), 'wb') as f:
        f.write(blob)
    path = os.path.join(folder, 'mesh.gltf')
    put(path, json.dumps(g))
    return path


def two_monitors():
    """the right monitor first in the file: 0.4 x 0.2 m at x = +1; the left one 0.2 x 0.2 m at x = -1"""
    Pr, Nr, _, Ir = quad(0.8, 0.0, 1.2, 0.2, -1.0)
    Pl, Nl, _, Il = quad(-1.1, 0.5, -0.9, 0.7, -1.0)
    P, N = np.vstack([Pr, Pl]), 2 * np.vstack([Nr, Nl])  # normals need not have length 1
    I = np.concatenate([Ir, Il + 4]).astype(np.uint16)
    return P, N, np.zeros((8, 2), np.float32), I


class Props(unittest.TestCase):
    TEXT = """TextureParameterValues[0] =
    {
        ParameterInfo = { Name=BaseColor }
        ParameterValue = Texture2D'/Game/A/B/Hull_CLR.Hull_CLR'
    }
    TextureParameterValues[1] =
    {
        ParameterInfo = { Name=Emissive }
        ParameterValue = Texture2D'/Game/A/B/Hull_EMS.Hull_EMS'
    }
    ScalarParameterValues[0] =
    {
        ParameterInfo = { Name=Emissive Strength }
        ParameterValue = 2.5
    }"""

    def test_textures_and_strength(self):
        tex, strength = cc.parse_props(self.TEXT)
        self.assertEqual(tex, {'basecolor': '/Game/A/B/Hull_CLR', 'emissive': '/Game/A/B/Hull_EMS'})
        self.assertEqual(strength, 2.5)

    def test_no_strength_is_one(self):
        tex, strength = cc.parse_props("ParameterInfo = { Name=BaseColor }\n ParameterValue = Texture2D'/Game/X.X'")
        self.assertEqual(tex, {'basecolor': '/Game/X'})
        self.assertEqual(strength, 1.0)

    def test_find_props_by_lower_case_name(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, 'a', 'b'))
            put(os.path.join(d, 'a', 'b', 'Hull_MTI.props.txt'), 'x')
            put(os.path.join(d, 'a', 'other.txt'), 'x')
            props = cc.find_props(d)
            self.assertEqual(list(props), ['hull_mti'])
            self.assertTrue(props['hull_mti'].endswith('Hull_MTI.props.txt'))

    def test_material_loads_images(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, 'Game', 'A', 'B'))
            Image.new('RGB', (8, 8), (10, 20, 30)).save(os.path.join(d, 'Game', 'A', 'B', 'Hull_CLR.png'))
            put(os.path.join(d, 'Hull_MTI.props.txt'), self.TEXT)
            said = []
            clr, ems, strength = cc.material(cc.find_props(d), d, 'HULL_mti', log=lambda *a: said.append(a))
            self.assertEqual(clr.mode, 'RGBA')
            self.assertEqual(clr.getpixel((0, 0)), (10, 20, 30, 255))
            self.assertIsNone(ems)  # named in the props, the file is missing
            self.assertEqual(len(said), 1)
            self.assertEqual(strength, 2.5)

    def test_material_unknown(self):
        self.assertEqual(cc.material({}, '.', 'Nothing'), (None, None, 1.0))


class Mips(unittest.TestCase):
    def test_sizes(self):
        self.assertEqual(cc.mip_bytes(1), 4)
        self.assertEqual(cc.mip_bytes(4), (16 + 4 + 1) * 4)
        self.assertEqual(cc.mip_bytes(0), 0)

    def test_chain_of_a_flat_image(self):
        data = cc.mips(Image.new('RGBA', (4, 4), (1, 2, 3, 255)), 4)
        self.assertEqual(len(data), cc.mip_bytes(4))
        self.assertEqual(data, bytes([1, 2, 3, 255]) * 21)

    def test_resized_to_the_wanted_size(self):
        data = cc.mips(Image.new('RGBA', (8, 8), (9, 9, 9, 255)), 2)
        self.assertEqual(len(data), (4 + 1) * 4)

    def test_mips_average(self):
        im = Image.new('RGBA', (2, 2), (0, 0, 0, 255))
        im.putpixel((0, 0), (200, 0, 0, 255))
        im.putpixel((1, 1), (200, 0, 0, 255))
        data = cc.mips(im, 2)
        self.assertEqual(data[:4], bytes([200, 0, 0, 255]))  # the image itself first
        self.assertEqual(data[16:], bytes([100, 0, 0, 255]))  # then the 1x1 average

    def test_emissive_size(self):
        self.assertEqual(cc.emissive_size(None, 1024), 0)
        self.assertEqual(cc.emissive_size(Image.new('RGBA', (16, 16), (0, 0, 0, 255)), 1024), 0)
        lit = Image.new('RGBA', (16, 16), (0, 0, 0, 255))
        lit.putpixel((3, 3), (255, 255, 255, 255))
        self.assertEqual(cc.emissive_size(lit.resize((64, 64)), 1024), 64)
        self.assertEqual(cc.emissive_size(lit.resize((64, 64)), 32), 32)
        self.assertEqual(cc.emissive_size(lit, 1024), 16)  # one lit texel is enough

    def test_flat(self):
        im = cc.flat(1, 2, 3)
        self.assertEqual((im.mode, im.size), ('RGBA', (4, 4)))
        self.assertEqual(im.tobytes(), bytes([1, 2, 3, 255]) * 16)


class Screens(unittest.TestCase):
    def test_names(self):
        self.assertTrue(cc.is_screen_material('Panels_MTI'))
        self.assertTrue(cc.is_screen_material('panelsLeft'))
        self.assertFalse(cc.is_screen_material('Hull_Panels'))

    def test_two_parts_left_to_right(self):
        P, N, _, I = two_monitors()
        P, N = P.copy(), N.copy()
        P[:, 2] *= -1  # as build_groups hands them over: z forward
        N[:, 2] *= -1
        parts = cc.screens(P, N, I.astype(np.uint32))
        self.assertEqual(len(parts), 2)
        left, right = parts
        self.assertLess(left[0][:, 0].mean(), 0)
        self.assertGreater(right[0][:, 0].mean(), 0)
        self.assertAlmostEqual(left[4], 1.0, places=4)   # 0.2 x 0.2
        self.assertAlmostEqual(right[4], 2.0, places=4)  # 0.4 x 0.2
        for p, n, uv, i, asp, size in parts:
            self.assertEqual(len(p), 4)
            self.assertEqual(len(i), 6)
            self.assertEqual(i.dtype, np.uint32)
            self.assertLess(i.max(), 4)  # indices into the part's own vertices
            self.assertEqual(uv.dtype, np.float32)
            self.assertAlmostEqual(float(uv.min()), 0, places=5)
            self.assertAlmostEqual(float(uv.max()), 1, places=5)

    def test_uv_runs_right_and_down_for_the_viewer(self):
        # one monitor facing the viewer (normal -z in the mod's space: towards the eye at the origin)
        P = np.array([[-0.2, 0.0, 1.0], [0.2, 0.0, 1.0], [0.2, 0.1, 1.0], [-0.2, 0.1, 1.0]], np.float32)
        N = np.tile(np.array([0, 0, -1], np.float32), (4, 1))
        (p, n, uv, i, asp, size), = cc.screens(P, N, np.array([0, 1, 2, 0, 2, 3], np.uint32))
        by = {tuple(round(float(x), 3) for x in q): t for q, t in zip(p, uv)}
        np.testing.assert_allclose(by[(-0.2, 0.1, 1.0)], [0, 0], atol=1e-5)  # top left
        np.testing.assert_allclose(by[(0.2, 0.1, 1.0)], [1, 0], atol=1e-5)   # top right
        np.testing.assert_allclose(by[(-0.2, 0.0, 1.0)], [0, 1], atol=1e-5)  # bottom left
        self.assertAlmostEqual(asp, 4.0, places=4)

    def test_split_vertices_at_one_place_join_a_part(self):
        # two triangles that share no vertex index but two positions: one monitor
        P = np.array([[0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 0, 1], [1, 1, 1], [0, 1, 1]], np.float32)
        N = np.tile(np.array([0, 0, -1], np.float32), (6, 1))
        parts = cc.screens(P, N, np.arange(6, dtype=np.uint32))
        self.assertEqual(len(parts), 1)
        self.assertEqual(len(parts[0][3]), 6)

    def test_places_closer_than_a_tenth_of_a_millimetre_are_one(self):
        P = np.array([[0, 0, 1], [1, 0, 1], [1, 1, 1], [0.00002, 0, 1], [1, 1.00002, 1], [0, 1, 1]], np.float32)
        N = np.tile(np.array([0, 0, -1], np.float32), (6, 1))
        self.assertEqual(len(cc.screens(P, N, np.arange(6, dtype=np.uint32))), 1)
        P[3, 0] = P[4, 1] = 0.5  # a real gap
        P[4, 0] = 0.4
        self.assertEqual(len(cc.screens(P, N, np.arange(6, dtype=np.uint32))), 2)

    def test_triangles_joined_at_any_corner(self):
        # a chain of triangles, each sharing one vertex with the next, at every corner position in turn
        P = np.array([[x, y, 1] for x in range(7) for y in (0, 1)][:7], np.float32)
        N = np.tile(np.array([0, 0, -1], np.float32), (len(P), 1))
        for order in ([0, 1, 2, 3, 4, 2, 5, 4, 6], [0, 1, 2, 3, 2, 4, 4, 5, 6], [0, 1, 2, 2, 3, 4, 5, 6, 4],
                      [2, 0, 1, 3, 4, 2, 4, 5, 6], [1, 2, 0, 3, 4, 2, 6, 4, 5], [1, 0, 2, 3, 4, 0, 5, 6, 3]):
            parts = cc.screens(P, N, np.array(order, np.uint32))
            self.assertEqual(len(parts), 1, order)
            self.assertEqual(len(parts[0][3]), 9)

    def test_turned_and_tilted_monitor(self):
        # 0.5 x 0.2 m, turned by 30 degrees about the vertical, leaning by 20, away from the origin; normals of length 3
        yaw, pitch, W, H = np.radians(30), np.radians(20), 0.5, 0.2
        r = np.array([np.cos(yaw), 0, np.sin(yaw)])
        n0 = np.cross([0, 1, 0], r)
        u = np.cos(pitch) * np.array([0, 1.0, 0]) + np.sin(pitch) * n0
        n = np.cross(u, r)
        c = np.array([0.7, -0.4, 1.3])
        corners = {(0, 0): c - r * W / 2 + u * H / 2, (1, 0): c + r * W / 2 + u * H / 2,
                   (0, 1): c - r * W / 2 - u * H / 2, (1, 1): c + r * W / 2 - u * H / 2}
        want = list(corners)
        P = np.array([corners[k] for k in want], np.float32)
        (p, nn, uv, i, asp, size), = cc.screens(P, np.tile(3 * n, (4, 1)).astype(np.float32), np.array([0, 1, 2, 1, 3, 2], np.uint32))
        self.assertAlmostEqual(asp, W / H, places=3)
        for q, t in zip(p, uv):
            k = int(np.argmin(np.abs(P - q).sum(1)))
            np.testing.assert_allclose(t, want[k], atol=1e-4)
        np.testing.assert_allclose(nn, np.tile(3 * n, (4, 1)), atol=1e-6)  # the normals pass through unchanged

    def test_bent_monitor_is_measured_in_its_mean_plane(self):
        # two halves of 0.1 m, the lower leaning by 40 degrees, the upper upright; the normals say 20 degrees
        def along(deg):
            return np.array([0, np.cos(np.radians(deg)), np.sin(np.radians(deg))])
        low, mid = np.array([0, 0, 1.0]), np.array([0, 0, 1.0]) + 0.1 * along(40)
        top, w = mid + 0.1 * along(0), np.array([0.5, 0, 0])
        P = np.array([low, low + w, mid, mid + w, top, top + w], np.float32)
        n = np.cross(along(20), [1, 0, 0])
        (p, nn, uv, i, asp, size), = cc.screens(P, np.tile(n, (6, 1)).astype(np.float32), np.array([0, 1, 2, 1, 3, 2, 2, 3, 4, 3, 5, 4], np.uint32))
        self.assertAlmostEqual(asp, 0.5 / (0.2 * np.cos(np.radians(20))), places=3)
        np.testing.assert_allclose(uv[:, 1], [1, 1, 0.5, 0.5, 0, 0], atol=1e-4)

    def test_tilted_monitor_keeps_its_shape(self):
        # leaning back by 45 degrees: 0.3 wide, 0.2 along its slope
        s = 0.2 / np.sqrt(2)
        P = np.array([[0, 0, 1], [0.3, 0, 1], [0.3, s, 1 + s], [0, s, 1 + s]], np.float32)
        nrm = np.array([0, 1, -1], np.float32) / np.sqrt(2)
        (p, n, uv, i, asp, size), = cc.screens(P, np.tile(nrm, (4, 1)), np.array([0, 1, 2, 0, 2, 3], np.uint32))
        self.assertAlmostEqual(asp, 1.5, places=3)


class Accessors(unittest.TestCase):
    def test_component_types(self):
        for ctype, dt in [(5120, '<i1'), (5121, '<u1'), (5122, '<i2'), (5123, '<u2'), (5125, '<u4'), (5126, '<f4')]:
            arr = np.array([[1, 2, 3, 4], [5, 6, 7, 100]], dt)
            g = {'accessors': [{'bufferView': 0, 'componentType': ctype, 'count': 2, 'type': 'VEC4'}], 'bufferViews': [{'buffer': 0}]}
            got = cc.acc(g, arr.tobytes(), 0)  # a view without byteOffset starts at 0
            self.assertEqual(got.dtype, np.dtype(dt))
            np.testing.assert_array_equal(got, arr)

    def test_stride_equal_to_the_element(self):
        arr = np.arange(6, dtype='<f4').reshape(2, 3)
        g = {'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': 2, 'type': 'VEC3'}], 'bufferViews': [{'buffer': 0, 'byteStride': 12}]}
        np.testing.assert_array_equal(cc.acc(g, arr.tobytes(), 0), arr)

    def test_interleaved_view(self):
        with tempfile.TemporaryDirectory() as d:
            P, N, UV, I = quad(0, 0, 1, 1, -2)
            N = N * 0.5
            path = write_gltf(d, [('Hull_MTI', P, N, UV, I)], interleaved=True)
            g = json.loads(get(path, 'r'))
            buf = get(os.path.join(d, 'mesh.bin'))
            np.testing.assert_array_equal(cc.acc(g, buf, 0), P)
            np.testing.assert_array_equal(cc.acc(g, buf, 1), N)
            self.assertEqual(cc.acc(g, buf, 3).shape, (6, 1))


class Convert(unittest.TestCase):
    def run_convert(self, d, tmax=1024):
        hull = quad(-1, -1, 1, 1, -3)
        path = write_gltf(d, [('Hull_MTI',) + hull, ('Panels_MTI',) + two_monitors()])
        os.makedirs(os.path.join(d, 'Game', 'T'))
        Image.new('RGB', (16, 16), (50, 60, 70)).save(os.path.join(d, 'Game', 'T', 'Hull_CLR.png'))
        ems = Image.new('RGB', (8, 8), (0, 0, 0))
        ems.putpixel((1, 1), (255, 0, 0))
        ems.save(os.path.join(d, 'Game', 'T', 'Hull_EMS.png'))
        put(os.path.join(d, 'Hull_MTI.props.txt'), "ParameterInfo = { Name=BaseColor }\n ParameterValue = Texture2D'/Game/T/Hull_CLR.Hull_CLR'\n"
            "ParameterInfo = { Name=Emissive }\n ParameterValue = Texture2D'/Game/T/Hull_EMS.Hull_EMS'\n"
            "ParameterInfo = { Name=Emissive Strength }\n ParameterValue = 3\n")
        out = os.path.join(d, 'out.mwc')
        said = []
        cc.convert(path, d, out, tmax, log=lambda *a: said.append(' '.join(str(x) for x in a)))
        return out, said

    def test_file(self):
        with tempfile.TemporaryDirectory() as d:
            out, said = self.run_convert(d)
            groups = cc.read_mwc(out)
            self.assertEqual([g['name'] for g in groups], ['Hull_MTI', 'SCREEN:0', 'SCREEN:1'])
            hull, left, right = groups
            # the hull: z turned to forward, normals with it, uv as given
            np.testing.assert_allclose(hull['verts'][:, 2], 3.0)
            np.testing.assert_allclose(hull['verts'][:, 5], -1.0)
            np.testing.assert_allclose(hull['verts'][0], [-1, -1, 3, 0, 0, -1, 0, 1])
            np.testing.assert_array_equal(hull['indices'], [0, 1, 2, 0, 2, 3])
            self.assertEqual((hull['colour_size'], hull['emissive_size'], hull['strength']), (16, 8, 3.0))
            # the monitors: left first, a black 4x4 texture, the aspect in the strength field
            self.assertLess(left['verts'][:, 0].max(), 0)
            self.assertGreater(right['verts'][:, 0].min(), 0)
            self.assertEqual((left['colour_size'], left['emissive_size']), (4, 0))
            self.assertAlmostEqual(left['strength'], 1.0, places=4)
            self.assertAlmostEqual(right['strength'], 2.0, places=4)
            np.testing.assert_allclose(right['verts'][:, 2], 1.0)
            self.assertTrue(any('bounds x -1.10..1.20  y -1.00..1.00  z 1.00..3.00 m' in s for s in said), said)
            self.assertTrue(any(s.startswith('  screen 1: centre 1.00 0.10 1.00 m, 40 x 20 cm') for s in said), said)

    def test_bytes(self):
        with tempfile.TemporaryDirectory() as d:
            out, _ = self.run_convert(d)
            b = get(out)
            self.assertEqual(b[:8], b'MWC1' + struct.pack('<I', 3))
            name, nv, ni, cs, es, k = struct.unpack_from(cc.HEADER, b, 8)
            self.assertEqual((name.rstrip(b'\0'), nv, ni, cs, es, k), (b'Hull_MTI', 4, 6, 16, 8, 3.0))
            tex = 8 + struct.calcsize(cc.HEADER) + 4 * 32 + 6 * 4
            self.assertEqual(b[tex:tex + 4], bytes([50, 60, 70, 255]))  # the colour texture's first texel
            ems = tex + cc.mip_bytes(16)
            self.assertEqual(b[ems + 4 * 9:ems + 4 * 9 + 4], bytes([255, 0, 0, 255]))  # the lit texel at 1,1
            # the next group starts right after the emissive mips
            scr = ems + cc.mip_bytes(8)
            self.assertEqual(b[scr:scr + 8], b'SCREEN:0')
            tex = scr + struct.calcsize(cc.HEADER) + 4 * 32 + 6 * 4
            self.assertEqual(b[tex:tex + cc.mip_bytes(4)], bytes([0, 0, 0, 255]) * 21)  # a monitor's own texture: black

    def group(self, name='A', clr=(4, 4), ems=None):
        P, N, UV, I = quad(0, 0, 1, 1, 1)
        e = Image.new('RGBA', ems, (9, 9, 9, 255)) if ems else None
        return (name, P, N, UV, I.astype(np.uint32), Image.new('RGBA', clr, (1, 2, 3, 255)), e, 1.0)

    def test_texture_size_is_the_width(self):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, 'o.mwc')
            cc.write_mwc(out, [self.group(clr=(16, 8), ems=(8, 2))], log=lambda *a: None)
            (g,) = cc.read_mwc(out)
            self.assertEqual((g['colour_size'], g['emissive_size']), (16, 8))
            self.assertEqual(len(get(out)), 8 + struct.calcsize(cc.HEADER) + 4 * 32 + 6 * 4 + cc.mip_bytes(16) + cc.mip_bytes(8))

    def test_long_name_is_cut(self):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, 'o.mwc')
            cc.write_mwc(out, [self.group(name='x' * 60), self.group(name='y' * 47)], log=lambda *a: None)
            self.assertEqual([g['name'] for g in cc.read_mwc(out)], ['x' * 47, 'y' * 47])

    def test_command_line(self):
        with tempfile.TemporaryDirectory() as d:
            path = write_gltf(d, [('Big_MTI',) + quad(0, 0, 1, 1, -1)])
            os.makedirs(os.path.join(d, 'Game'))
            Image.new('RGB', (2048, 2048), (5, 5, 5)).save(os.path.join(d, 'Game', 'Big.png'))
            put(os.path.join(d, 'Big_MTI.props.txt'), "ParameterInfo = { Name=BaseColor }\n ParameterValue = Texture2D'/Game/Big.Big'\n")
            out = os.path.join(d, 'o.mwc')
            quiet = lambda *a: None
            with self.assertRaises(SystemExit) as e:
                cc.main(['x', path, d], quiet)
            self.assertIn('cockpit_convert.py <mesh.gltf>', str(e.exception))
            self.assertFalse(os.path.exists(out))
            cc.main(['x', path, d, out], quiet)
            self.assertEqual(cc.read_mwc(out)[0]['colour_size'], 1024)  # the limit when none is given
            cc.main(['x', path, d, out, '8'], quiet)
            self.assertEqual(cc.read_mwc(out)[0]['colour_size'], 8)

    def test_texture_size_limit(self):
        with tempfile.TemporaryDirectory() as d:
            out, _ = self.run_convert(d, tmax=4)
            hull = cc.read_mwc(out)[0]
            self.assertEqual((hull['colour_size'], hull['emissive_size']), (4, 4))

    def test_material_without_textures_is_grey(self):
        with tempfile.TemporaryDirectory() as d:
            path = write_gltf(d, [('Bare_MTI',) + quad(0, 0, 1, 1, -1)])
            out = os.path.join(d, 'o.mwc')
            cc.convert(path, d, out, log=lambda *a: None)
            b = get(out)
            (g,) = cc.read_mwc(out)
            self.assertEqual((g['colour_size'], g['emissive_size'], g['strength']), (4, 0, 1.0))
            tex = 8 + struct.calcsize(cc.HEADER) + 4 * 32 + 6 * 4
            self.assertEqual(b[tex:tex + 4], bytes([80, 80, 84, 255]))
            self.assertEqual(len(b), tex + cc.mip_bytes(4))

    def test_read_rejects_other_files(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'x.mwc')
            with open(p, 'wb') as f:
                f.write(b'NOPE' + bytes(8))
            with self.assertRaisesRegex(ValueError, '^not an .mwc file$'):
                cc.read_mwc(p)
            out, _ = self.run_convert(d)
            with open(out, 'ab') as f:
                f.write(b'\0')
            with self.assertRaisesRegex(ValueError, '^bytes after the last group$'):
                cc.read_mwc(out)


def rect(x, w, h, z=1.0, y=0.0):
    """a rectangle w x h facing the viewer (z forward), its left edge at x; indices for vertices from 0"""
    P = np.array([[x, y, z], [x + w, y, z], [x + w, y + h, z], [x, y + h, z]], np.float32)
    return P, np.tile(np.array([0, 0, -1], np.float32), (4, 1)), np.array([0, 1, 2, 0, 2, 3], np.uint32)


def join(rects):
    P = np.vstack([r[0] for r in rects])
    N = np.vstack([r[1] for r in rects])
    I = np.concatenate([r[2] + 4 * k for k, r in enumerate(rects)]).astype(np.uint32)
    return P, N, I


class Monitors(unittest.TestCase):
    def test_size_in_metres(self):
        s = 0.2 / np.sqrt(2)  # leaning back by 45 degrees: 0.3 wide, 0.2 along its slope
        P = np.array([[0, 0, 1], [0.3, 0, 1], [0.3, s, 1 + s], [0, s, 1 + s]], np.float32)
        nrm = np.array([0, 1, -1], np.float32) / np.sqrt(2)
        m = cc.screen(P, np.tile(nrm, (4, 1)), np.array([0, 1, 2, 0, 2, 3], np.uint32), np.array([0, 1]))
        self.assertAlmostEqual(m[5][0], 0.3, places=5)
        self.assertAlmostEqual(m[5][1], 0.2, places=5)
        self.assertAlmostEqual(m[4], 1.5, places=4)
        self.assertEqual(len(m[3]), 6)

    def test_one_triangle_of_a_part(self):
        P, N, I = rect(0, 0.4, 0.2)
        m = cc.screen(P, N, I, np.array([1]))  # the second triangle alone: three vertices, its own numbers
        self.assertEqual(len(m[0]), 3)
        self.assertEqual(sorted(m[3].tolist()), [0, 1, 2])

    def test_parts(self):
        P, N, I = join([rect(0, 0.1, 0.1), rect(0.5, 0.1, 0.1), rect(0.6, 0.1, 0.1)])  # the last two share an edge
        got = sorted(sorted(ts.tolist()) for ts in cc.parts(P, I))
        self.assertEqual(got, [[0, 1], [2, 3, 4, 5]])

    def test_small_parts_are_no_monitors(self):
        # left to right: a button, a monitor, a strip too low, a strip too narrow, a monitor
        P, N, I = join([rect(-0.9, 0.05, 0.05), rect(-0.5, 0.3, 0.2), rect(0.0, 0.3, 0.06), rect(0.4, 0.06, 0.3), rect(0.6, 0.08, 0.08)])
        found, rest = cc.monitors(P, N, I)
        self.assertEqual([round(float(m[0][:, 0].min()), 2) for m in found], [-0.5, 0.6])
        self.assertEqual(sorted(rest.tolist()), sorted(list(range(0, 4)) * 1 + [0, 2] + [8, 9, 10, 8, 10, 11] + [12, 13, 14, 12, 14, 15]))
        self.assertEqual(rest.dtype, np.uint32)
        for m in found:
            self.assertLess(int(m[3].max()), 4)

    def test_the_limit_counts(self):
        old = cc.SCREEN_MIN
        try:
            cc.SCREEN_MIN = 0.25  # (a number a float holds exactly)
            P, N, I = join([rect(0, 0.25, 0.5), rect(1, 0.5, 0.25), rect(2, 0.5, 0.2499)])
            found, rest = cc.monitors(P, N, I)
            self.assertEqual(len(found), 2)
            self.assertEqual(len(rest), 6)
        finally:
            cc.SCREEN_MIN = old
        self.assertEqual(old, 0.07)

    def test_the_largest_eight(self):
        # ten monitors, by area: the two smallest stay with the material (0.08 x 0.3 and 0.1 x 0.1)
        sizes = [(0.2, 0.2), (0.3, 0.31), (0.21, 0.2), (0.08, 0.3), (0.4, 0.08), (0.22, 0.2), (0.1, 0.1), (0.23, 0.2), (0.24, 0.2), (0.25, 0.2)]
        P, N, I = join([rect(k - 5.0, w, h) for k, (w, h) in enumerate(sizes)])
        found, rest = cc.monitors(P, N, I)
        self.assertEqual(cc.SCREEN_MAX, 8)
        self.assertEqual([round(float(m[0][:, 0].min())) for m in found], [-5, -4, -3, -1, 0, 2, 3, 4])
        self.assertEqual(sorted(set(rest.tolist())), [12, 13, 14, 15, 24, 25, 26, 27])

    def test_a_part_without_height(self):
        P, N, I = join([rect(0, 0.3, 0.0), rect(1, 0.3, 0.2)])
        found, rest = cc.monitors(P, N, I)  # no warning, and no monitor of the flat one
        self.assertEqual(len(found), 1)
        self.assertEqual(len(rest), 6)


class Assign(unittest.TestCase):
    def test_five_in_a_row(self):
        places = [(-0.64, 0.11, 0.024), (-0.22, 0.57, 0.016), (-0.01, 0.68, 0.037), (0.24, 0.68, 0.019), (0.69, 0.16, 0.024)]
        self.assertEqual(cc.assign(places), {'lance': 0, 'target': 1, 'radar': 2, 'damage': 3, 'weapons': 4})

    def test_behind_does_not_count(self):
        places = [(-0.5, 0.4, 0.01), (0.0, 0.4, 0.01), (0.5, 0.0, 0.01), (0.6, -0.3, 0.05)]  # two ahead, one beside, one behind
        self.assertEqual(cc.assign(places), {})
        self.assertEqual(cc.assign([]), {})
        places[2] = (0.5, 0.01, 0.01)
        self.assertEqual(cc.assign(places), {'target': 0, 'radar': 1, 'damage': 2})

    def test_the_largest_further_out(self):
        # left of the target three: the largest shows the lance list, not the nearest; right of the damage two
        places = [(-0.9, 0.3, 0.006), (-0.8, 0.3, 0.015), (-0.7, 0.3, 0.007), (-0.3, 0.4, 0.03), (0.02, 0.4, 0.016), (0.3, 0.4, 0.02),
                  (0.6, 0.3, 0.004), (0.7, 0.3, 0.009), (0.75, -0.2, 0.5)]
        self.assertEqual(cc.assign(places), {'lance': 1, 'target': 3, 'radar': 4, 'damage': 5, 'weapons': 7})

    def test_radar_keeps_a_neighbour_on_each_side(self):
        self.assertEqual(cc.assign([(0.0, 0.5, 0.01), (0.3, 0.5, 0.01), (0.6, 0.5, 0.01)]), {'target': 0, 'radar': 1, 'damage': 2})
        self.assertEqual(cc.assign([(-0.6, 0.5, 0.01), (-0.3, 0.5, 0.01), (-0.05, 0.5, 0.01)]), {'target': 0, 'radar': 1, 'damage': 2})
        # four, all to the right of the middle: the second is the radar, the last the weapons
        self.assertEqual(cc.assign([(0.1, 0.5, 0.01), (0.3, 0.5, 0.01), (0.6, 0.5, 0.01), (0.9, 0.5, 0.01)]),
                         {'target': 0, 'radar': 1, 'damage': 2, 'weapons': 3})
        self.assertEqual(cc.assign([(-0.9, 0.5, 0.01), (-0.6, 0.5, 0.01), (-0.3, 0.5, 0.01), (-0.2, 0.5, 0.01)]),
                         {'lance': 0, 'target': 1, 'radar': 2, 'damage': 3})

    def test_order_given_does_not_matter(self):
        places = [(0.24, 0.68, 0.019), (-0.64, 0.11, 0.024), (-0.01, 0.68, 0.037), (0.69, 0.16, 0.024), (-0.22, 0.57, 0.016)]
        self.assertEqual(cc.assign(places), {'lance': 1, 'target': 4, 'radar': 2, 'damage': 0, 'weapons': 3})

    def test_ini_text(self):
        text = cc.ini_text([(-0.64, 0.11, 0.024), (-0.22, 0.57, 0.016), (-0.01, 0.68, 0.0371), (0.24, 0.68, 0.019)])
        lines = text.split('\n')
        self.assertEqual(lines[1], '[cockpit]')
        self.assertEqual(lines[2], '; the eye in the mesh (metres) and the size of the mesh; in a battle: Ctrl+Alt+arrows, Ctrl+Alt+PgUp / PgDn')
        self.assertEqual(lines[7], '; 1 = the monitors show the HUD; 0 = they stay dark. What no monitor shows is on the glass (glass_<part>)')
        self.assertEqual(lines[3:7], ['scale=1.000', 'eye_right_m=0.000', 'eye_up_m=0.000', 'eye_forward_m=0.000'])
        self.assertEqual(lines[8], 'screens=1')
        self.assertEqual(lines[10:], [';   0: -0.64 m to the right, 0.11 m ahead, 240 cm2', ';   1: -0.22 m to the right, 0.57 m ahead, 160 cm2',
                                      ';   2: -0.01 m to the right, 0.68 m ahead, 371 cm2', ';   3: 0.24 m to the right, 0.68 m ahead, 190 cm2',
                                      '; lance', 'screen_0=0.000 0.000 0.243 0.108', 'screen_0_alt=0.000 0.000 0.243 0.435 0.210 0.125',
                                      '; target', 'screen_1=0.000 0.772 0.180 1.000', '; radar', 'screen_2=0.320 0.770 0.690 0.990',
                                      '; damage', 'screen_3=0.820 0.772 1.005 1.000', ''])
        for line in lines:
            self.assertTrue(line == '' or line[0] in ';[' or '=' in line, line)

    def test_ini_text_without_monitors(self):
        lines = cc.ini_text([(0.5, 0.3, 5.0)]).split('\n')
        self.assertEqual(lines[8], 'screens=0')
        self.assertEqual(lines[10:], [';   0: 0.50 m to the right, 0.30 m ahead, 50000 cm2', ''])
        self.assertEqual(cc.HUD_PARTS['weapons'], '0.716 0.000 1.000 0.124')


class Cockpit(unittest.TestCase):
    def convert(self, d, prims):
        path = write_gltf(d, prims)
        out = os.path.join(d, 'c.mwc')
        said = []
        groups = cc.convert(path, d, out, log=lambda *a: said.append(' '.join(str(x) for x in a)))
        return out, said, groups

    def panel(self, rects):
        """rects in the mod's space (z forward) as a glTF primitive (z back)"""
        P, N, I = join(rects)
        P, N = P.copy(), N.copy()
        P[:, 2] *= -1
        N[:, 2] *= -1
        UV = np.tile(np.array([[0.25, 0.75]], np.float32), (len(P), 1))
        return P, N, UV, I.astype(np.uint16)

    def test_buttons_stay_with_their_material(self):
        with tempfile.TemporaryDirectory() as d:
            prims = [('Panels_MTI',) + self.panel([rect(-0.5, 0.3, 0.2), rect(0.0, 0.05, 0.05), rect(0.3, 0.2, 0.2)]),
                     ('PanelsRight_MTI',) + self.panel([rect(0.8, 0.2, 0.1)])]
            out, said, groups = self.convert(d, prims)
            got = cc.read_mwc(out)
            self.assertEqual([g['name'] for g in got], ['SCREEN:0', 'SCREEN:1', 'Panels_MTI', 'SCREEN:2'])
            rest = got[2]
            self.assertEqual(rest['indices'].tolist(), [4, 5, 6, 4, 6, 7])  # the button's two triangles, the vertices as they were
            np.testing.assert_allclose(rest['verts'][:, 6:8], [[0.25, 0.75]] * 12)
            self.assertEqual((rest['colour_size'], rest['strength']), (4, 1.0))
            np.testing.assert_allclose(got[3]['verts'][:, 0].min(), 0.8)
            self.assertAlmostEqual(got[3]['strength'], 2.0, places=4)
            self.assertTrue(any(s.startswith('  screen 2: centre 0.90 0.05 1.00 m, 20 x 10 cm') for s in said), said)
            self.assertTrue(any(s.startswith('  screen 0: centre -0.35 0.10 1.00 m, 30 x 20 cm') for s in said), said)

    def test_ini_is_written_once(self):
        with tempfile.TemporaryDirectory() as d:
            prims = [('Panels_MTI',) + self.panel([rect(-0.5, 0.3, 0.2), rect(-0.1, 0.2, 0.2), rect(0.3, 0.2, 0.1)])]
            out, said, groups = self.convert(d, prims)
            ini = os.path.join(d, 'c.ini')
            text = get(ini, 'r')
            self.assertEqual(text, cc.ini_text([(-0.35, 1.0, 0.06), (0.0, 1.0, 0.04), (0.4, 1.0, 0.02)]))
            self.assertIn('screen_1=' + cc.HUD_PARTS['radar'], text)
            put(ini, 'mine')
            self.convert(d, prims)
            self.assertEqual(get(ini, 'r'), 'mine')

    def test_ini_without_monitors(self):
        with tempfile.TemporaryDirectory() as d:
            out, said, groups = self.convert(d, [('Hull_MTI',) + quad(0, 0, 1, 1, -1)])
            self.assertIn('screens=0', get(os.path.join(d, 'c.ini'), 'r'))


PAINT = """Parent = MaterialInstanceConstant'/Game/Objects/Mechs/X/Body_LOD.Body_LOD'
VectorParameterValues[0] =
{
    ParameterInfo = { Name=PaintColorPrimary }
    ParameterValue = { R=0.5, G=2, B=-1, A=1 }
}"""
HOLOGRAM = """ScalarParameterValues[0] = {}
Parent = Material3'/Game/Objects/Mechs/_common/Cockpit/Material/Projection_Hologram_MTL.Projection_Hologram_MTL'"""


class PlayerMade(unittest.TestCase):
    """cockpits of MechWarrior 5 mods: paint colours, monitors as holograms, the canopy's glass"""

    def test_paint_colour(self):
        self.assertEqual(cc.paint_colour(PAINT), (186, 255, 0))  # 255 * 0.5 ** (1 / 2.2) = 186.1; cut to 0..1
        self.assertEqual(cc.paint_colour(PAINT.replace('R=0.5', 'R=5e-1')), (186, 255, 0))
        self.assertIsNone(cc.paint_colour(PAINT.replace('PaintColorPrimary', 'Fringe Color')))
        self.assertIsNone(cc.paint_colour(''))

    def test_hologram(self):
        self.assertTrue(cc.is_hologram(HOLOGRAM))
        self.assertTrue(cc.is_hologram(HOLOGRAM.replace('Projection_Hologram', 'PROJECTION_hologram')))
        self.assertIs(cc.is_hologram(PAINT), False)
        self.assertIs(cc.is_hologram('Name=Projection_Hologram'), False)  # no parent named

    def test_props_text(self):
        with tempfile.TemporaryDirectory() as d:
            put(os.path.join(d, 'Body_MTI.props.txt'), PAINT)
            props = cc.find_props(d)
            self.assertEqual(cc.props_text(props, 'BODY_mti'), PAINT)
            self.assertEqual(cc.props_text(props, 'Other'), '')

    def test_material_without_a_texture_takes_its_paint(self):
        with tempfile.TemporaryDirectory() as d:
            put(os.path.join(d, 'Body_MTI.props.txt'), PAINT)
            clr, ems, strength = cc.material(cc.find_props(d), d, 'Body_MTI')
            self.assertEqual(clr.getpixel((0, 0)), (186, 255, 0, 255))
            self.assertEqual((ems, strength), (None, 1.0))

    def test_a_texture_wins_over_the_paint(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, 'Game', 'A', 'B'))
            Image.new('RGB', (8, 8), (10, 20, 30)).save(os.path.join(d, 'Game', 'A', 'B', 'Hull_CLR.png'))
            put(os.path.join(d, 'Hull_MTI.props.txt'), Props.TEXT + '\n' + PAINT)
            clr, ems, strength = cc.material(cc.find_props(d), d, 'Hull_MTI', log=lambda *a: None)
            self.assertEqual(clr.getpixel((0, 0)), (10, 20, 30, 255))

    def test_canopy_glass(self):
        self.assertTrue(cc.is_canopy_glass('TimberwolfGlass_CockpitTP_MTL'))
        self.assertFalse(cc.is_canopy_glass('Glass_CockpitTP_Opaque_MTI'))  # the game's own, kept
        self.assertFalse(cc.is_canopy_glass('Glass_Equipment_MTI'))
        self.assertFalse(cc.is_canopy_glass('Opaque_MTI'))

    def cockpit(self, d, prims):
        put(os.path.join(d, 'Monitor_MTI1.props.txt'), HOLOGRAM)
        put(os.path.join(d, 'Dash_LOD.props.txt'), PAINT)
        path = write_gltf(d, prims)
        out = os.path.join(d, 'c.mwc')
        said = []
        cc.convert(path, d, out, log=lambda *a: said.append(' '.join(str(x) for x in a)))
        return [g['name'] for g in cc.read_mwc(out)], said

    def test_holograms_are_the_monitors_of_a_cockpit_without_panels(self):
        with tempfile.TemporaryDirectory() as d:
            names, said = self.cockpit(d, [('Dash_LOD',) + quad(0, 0, 1, 1, -1), ('Monitor_MTI1',) + quad(0, 0, 0.3, 0.2, -1),
                                           ('MyGlass_CockpitTP_MTL',) + quad(-1, -1, 1, 1, -0.5)])
            self.assertEqual(names, ['Dash_LOD', 'SCREEN:0'])
            self.assertIn('  MyGlass_CockpitTP_MTL: the glass of the canopy, left out', said)
            self.assertTrue(any(s.startswith('  screen 0: centre 0.15 0.10 1.00 m, 30 x 20 cm') for s in said), said)

    def test_with_panels_a_hologram_is_no_monitor(self):
        with tempfile.TemporaryDirectory() as d:
            names, said = self.cockpit(d, [('Monitor_MTI1',) + quad(0, 0, 0.3, 0.2, -1), ('Panels_MTI',) + quad(0.5, 0, 0.8, 0.2, -1)])
            self.assertEqual(names, ['Monitor_MTI1', 'SCREEN:0'])


MASKED = PAINT.replace('}\n', '}\n', 1) + """
        ParameterInfo = { Name=PaintColorSecondary }
        ParameterValue = { R=0, G=0, B=1, A=1 }
        ParameterInfo = { Name=RGBPaintMask }
        ParameterValue = Texture2D'/Game/A/Mask_MSK.Mask_MSK'
"""


def pixels(im):
    return [im.getpixel((x, y)) for y in range(im.size[1]) for x in range(im.size[0])]


def tilted(deg, w=0.4, h=0.2, extra=()):
    """a w x h monitor facing the viewer (the mod's space), turned deg to the left about its middle; extra: more
    corners (x, y) within it -> P, N, I, {corner number: the uv it should get}"""
    c, s = np.cos(np.radians(deg)), np.sin(np.radians(deg))
    own = [(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)] + list(extra)
    # the viewer looks along +z: its right is +x, so a turn to the left for it takes x towards +y
    P = np.array([[x * c - y * s, x * s + y * c, 1.0] for x, y in own], np.float32)
    N = np.tile(np.array([0, 0, -1], np.float32), (len(P), 1))
    want = {k: (x / w + 0.5, 0.5 - y / h) for k, (x, y) in enumerate(own)}
    return P, N, np.array([0, 1, 2, 0, 2, 3], np.uint32), want


class Askew(unittest.TestCase):
    """a monitor that hangs askew shows its picture along its own edges"""

    def test_upright_stays(self):
        a, b = np.array([0, 1, 1, 0.0]), np.array([0, 0, 0.5, 0.5])
        self.assertEqual(cc.upright(a, b), 0.0)

    def test_turned_rectangle(self):
        for deg in (30, -30, 0.25, -0.25, 44.75, -44.75, 10.5):
            c, s = np.cos(np.radians(deg)), np.sin(np.radians(deg))
            x, y = np.array([-2, 2, 2, -2.0]), np.array([-1, -1, 1, 1.0])
            # (tiny turns: the upright rectangle around it is hardly larger - a long one shows it)
            x = x * 40 if abs(deg) < 1 else x
            a, b = x * c - y * s, x * s + y * c
            self.assertAlmostEqual(np.degrees(cc.upright(a, b)), deg, places=6, msg=deg)

    def test_45_degrees_goes_left(self):
        a, b = np.array([1, 0, -1, 0.0]), np.array([0, 1, 0, -1.0])  # a square on its corner: both ways are as good
        self.assertAlmostEqual(np.degrees(cc.upright(a, b)), 45, places=6)

    def test_the_smaller_turn_of_two_as_good(self):
        # a six-cornered shape fits its rectangle as well every 30 degrees
        t = np.radians(np.arange(6) * 60)
        self.assertEqual(cc.upright(np.cos(t), np.sin(t)), 0.0)
        a, b = np.cos(t + np.radians(10)), np.sin(t + np.radians(10))
        self.assertAlmostEqual(np.degrees(cc.upright(a, b)), 10, places=6)  # not -20
        a, b = np.cos(t - np.radians(10)), np.sin(t - np.radians(10))
        self.assertAlmostEqual(np.degrees(cc.upright(a, b)), -10, places=6)

    def test_two_per_cent(self):
        def turned(deg):  # a square, turned: the upright rectangle around it is (cos + sin)^2 as large
            t = np.radians(deg + np.arange(4) * 90 + 45)
            return np.cos(t), np.sin(t)
        self.assertEqual(cc.upright(*turned(0.5)), 0.0)                      # 1.75 % larger: left as it is
        self.assertAlmostEqual(np.degrees(cc.upright(*turned(0.75))), 0.75)  # 2.6 %
        # just at the limit: a 1 x 1 square against an upright rectangle of 1 / 0.98
        a, b = np.array([0, 1, 1, 0, 0.5]), np.array([0, 0, 1, 1, 0.5])
        self.assertEqual(cc.upright(a, b), 0.0)

    def test_upright_in_the_world_on_demand(self):
        P, N, I, want = tilted(20)
        p, n, uv, i, asp, size = cc.screen(P, N, I, np.array([0, 1]), False)
        c, s = np.cos(np.radians(20)), np.sin(np.radians(20))
        np.testing.assert_allclose(size, (0.4 * c + 0.2 * s, 0.4 * s + 0.2 * c), atol=1e-5)
        p, n, uv, i, asp, size = cc.screen(P, N, I, np.array([0, 1]))
        np.testing.assert_allclose(size, (0.4, 0.2), atol=1e-5)

    def test_a_monitor_is_one_by_its_upright_rectangle(self):
        # a strip of 30 x 5 cm, turned by 45 degrees: too narrow for a monitor, but its upright rectangle is not -
        # it stays the monitor it always was, and shows its picture along its edges
        P, N, I, want = tilted(45, 0.3, 0.05)
        (p, n, uv, i, asp, size), = cc.monitors(P, N, I)[0]
        np.testing.assert_allclose(size, (0.3, 0.05), atol=1e-5)
        # the largest by those rectangles are kept: nine upright ones of 0.2 x 0.2 and a longer strip, turned
        prims = [rect(x, 0.2, 0.2) for x in range(9)]
        Ps, Ns, Is, _ = tilted(45, 0.4, 0.02)  # 0.008 m2, but 0.088 m2 upright: the largest
        P9, N9, I9 = join(prims)
        P = np.vstack([P9, Ps + np.array([20, 0, 0], np.float32)])
        found, rest = cc.monitors(P, np.vstack([N9, Ns]), np.concatenate([I9, Is + len(P9)]).astype(np.uint32))
        self.assertEqual(len(found), 8)
        np.testing.assert_allclose(found[-1][5], (0.4, 0.02), atol=1e-5)  # the strip is among them, the last from the left

    def test_screen_follows_its_edges(self):
        for deg in (20, -20):
            P, N, I, want = tilted(deg)
            (p, n, uv, i, asp, size), = cc.screens(P, N, I)
            self.assertAlmostEqual(asp, 2.0, places=4)
            np.testing.assert_allclose(size, (0.4, 0.2), atol=1e-5)
            for q, t in zip(p, uv):
                k = int(np.argmin(np.abs(P - q).sum(1)))
                np.testing.assert_allclose(t, want[k], atol=1e-4, err_msg=str(deg))


class Shaded(unittest.TestCase):
    """one-coloured parts take the shade of their vertex colours"""

    def test_paint_light(self):
        self.assertEqual(cc.paint_light(PAINT), (0.5, 1.0, 0.0))
        self.assertEqual(cc.paint_light(MASKED, 'Secondary'), (0.0, 0.0, 1.0))
        self.assertIsNone(cc.paint_light(MASKED, 'Tertiary'))
        self.assertIsNone(cc.paint_light(PAINT, 'Secondary'))

    def test_to_screen(self):
        self.assertEqual(cc.to_screen((0, 1, 0.5)), (0, 255, 186))
        self.assertEqual(cc.to_screen((0.2,)), (123,))  # 255 * 0.2 ** (1 / 2.2) = 122.7

    def test_masked_paint(self):
        mask = Image.new('RGB', (2, 2))
        mask.putdata([(255, 0, 0), (0, 0, 0), (255, 255, 0), (255, 255, 255)])
        metal = cc.to_screen(cc.METAL)
        im = cc.masked_paint(mask, [(0.5, 1.0, 0.0), (0.0, 0.0, 1.0), None])
        self.assertEqual(im.mode, 'RGBA')
        self.assertEqual(pixels(im), [(186, 255, 0, 255), metal + (255,), (0, 0, 255, 255), (0, 0, 255, 255)])
        im = cc.masked_paint(mask, [None, None, (1.0, 0.0, 0.0)])
        self.assertEqual(pixels(im), [metal + (255,), metal + (255,), metal + (255,), (255, 0, 0, 255)])
        half = Image.new('RGB', (1, 1), (0, 128, 0))  # half of the second paint over the metal
        want = cc.to_screen(tuple(m + 128 / 255 * (p - m) for m, p in zip(cc.METAL, (1.0, 0.5, 0.0))))
        self.assertEqual(cc.masked_paint(half, [(0, 0, 0), (1.0, 0.5, 0.0), None]).getpixel((0, 0)), want + (255,))
        self.assertEqual(cc.masked_paint(Image.new('L', (1, 1), 255), [(1, 1, 1), (0, 0, 0), (0.5, 1, 0)]).getpixel((0, 0)), (186, 255, 0, 255))

    def test_metal(self):
        np.testing.assert_allclose(cc.METAL, [0.15, 0.15, 0.16])
        self.assertEqual(cc.VC_SHARE, 0.5)
        self.assertEqual(cc.WHITE, (255, 255, 255))
        self.assertEqual(cc.QUARTERS, range(1, 181))

    def test_material_with_a_paint_mask(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, 'Game', 'A'))
            Image.new('RGB', (8, 8), (0, 255, 0)).save(os.path.join(d, 'Game', 'A', 'Mask_MSK.png'))
            put(os.path.join(d, 'Door_MTI.props.txt'), MASKED)
            clr, ems, strength = cc.material(cc.find_props(d), d, 'Door_MTI')
            self.assertEqual(clr.size, (8, 8))
            self.assertEqual(clr.getpixel((3, 3)), (0, 0, 255, 255))  # the second paint
            # a colour picture wins over the mask
            Image.new('RGB', (4, 4), (10, 20, 30)).save(os.path.join(d, 'Game', 'A', 'Hull_CLR.png'))
            put(os.path.join(d, 'Both_MTI.props.txt'), MASKED + "\nParameterInfo = { Name=BaseColor }\nParameterValue = Texture2D'/Game/A/Hull_CLR.Hull_CLR'")
            self.assertEqual(cc.material(cc.find_props(d), d, 'Both_MTI')[0].getpixel((0, 0)), (10, 20, 30, 255))
            # the mask's picture is missing: the first paint, as without a mask
            os.remove(os.path.join(d, 'Game', 'A', 'Mask_MSK.png'))
            clr, ems, strength = cc.material(cc.find_props(d), d, 'Door_MTI', log=lambda *a: None)
            self.assertEqual(clr.getpixel((0, 0)), (186, 255, 0, 255))

    def test_displays(self):
        self.assertEqual(cc.DISPLAYS, ('projection_hologram', 'widget3d_', 'minimapgenerator'))
        for parent in ('Widget3D_Paperdoll1_MTL', 'MiniMapGenerator1_MTL'):
            self.assertTrue(cc.is_hologram(HOLOGRAM.replace('Projection_Hologram_MTL', parent)), parent)
        self.assertFalse(cc.is_hologram(HOLOGRAM.replace('Projection_Hologram', 'Annihilator')))
        self.assertTrue(cc.is_display('Widget3D_Paperdoll1_MTL'))
        self.assertTrue(cc.is_display('MINIMAPGenerator1_MTL'))
        self.assertTrue(cc.is_display('Projection_Hologram_MTL'))
        self.assertFalse(cc.is_display('My_Widget3D_Paperdoll1_MTL'))
        self.assertFalse(cc.is_display('Widget3D'))

    def gltf(self, d, colours):
        P, N, UV, I = quad(0, 0, 1, 1, -1)
        with open(write_gltf(d, [('A', P, N, UV, I) + (() if colours is None else (colours,))])) as f:
            g = json.load(f)
        return g, get(os.path.join(d, 'mesh.bin')), g['meshes'][0]['primitives'][0]

    def test_vertex_colours(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(cc.vertex_colours(*self.gltf(d, None)))
            c = np.array([[255, 255, 255, 0], [250, 255, 251, 255], [255, 252, 255, 9], [255, 255, 250, 255]], np.uint8)
            self.assertIsNone(cc.vertex_colours(*self.gltf(d, c)))  # nothing painted: 250 and more is white
            c[1, 0] = 249
            got = cc.vertex_colours(*self.gltf(d, c))
            self.assertEqual(got.dtype, np.float64)
            np.testing.assert_array_equal(got, c[:, :3])
            f = np.array([[1, 1, 1, 1], [0.2, 0.4, 1, 1], [1, 1, 1, 1], [1, 1, 1, 1]], np.float32)  # as numbers 0..1
            np.testing.assert_allclose(cc.vertex_colours(*self.gltf(d, f)), f[:, :3] * 255, atol=1e-4)
            self.assertIsNone(cc.vertex_colours(*self.gltf(d, np.full((4, 4), 0.99, np.float32))))  # 252

    def test_painted(self):
        # four triangles around the middle of a square; the first two get one colour, the others one each
        P = np.array([[0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1], [0.5, 0.5, 1]], np.float32)
        N = np.arange(15, dtype=np.float32).reshape(5, 3)
        I = np.array([0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4], np.uint32)
        VC = np.array([[30, 60, 90], [30, 60, 90], [30, 60, 90], [240, 240, 240], [30, 60, 90]], np.float64)
        p, n, uv, i, im, colours = cc.painted(P, N, I, VC, (200, 100, 255), 1.0)
        self.assertEqual(colours, 2)
        self.assertEqual((im.mode, im.size), ('RGBA', (4, 4)))
        self.assertEqual((uv.dtype, i.dtype), (np.float32, np.uint32))
        self.assertEqual(len(i), 12)
        self.assertEqual(len(p), 8)  # the corners 2 and 4 belong to triangles of both colours, 0 and 3 too... see below
        px = pixels(im)
        dark = (round(200 * 30 / 255), round(100 * 60 / 255), 90)
        light = (round(200 * 100 / 255), round(100 * 120 / 255), round(255 * 140 / 255))  # the mean of 240 and twice 30, 60, 90
        self.assertEqual(sorted(px[:2]), sorted([dark + (255,), light + (255,)]))
        self.assertEqual(px[2:], [(0, 0, 0, 255)] * 14)
        want = [dark, dark, light, light]
        for t in range(4):
            corners = i[3 * t:3 * t + 3]
            np.testing.assert_array_equal(p[corners], P[I[3 * t:3 * t + 3]])  # the triangle is where it was
            np.testing.assert_array_equal(n[corners], N[I[3 * t:3 * t + 3]])
            for c in corners:  # every corner points at the middle of its colour's texel
                x, y = uv[c] * 4 - 0.5
                self.assertEqual((x, y), (round(float(x)), round(float(y))))
                self.assertEqual(px[int(y) * 4 + int(x)][:3], want[t])

    def test_painted_share(self):
        P = np.zeros((3, 3), np.float32)
        I = np.array([0, 1, 2], np.uint32)
        VC = np.array([[0, 51, 255], [0, 51, 255], [0, 51, 255]], np.float64)
        for share, want in ((1.0, (0, 20, 100)), (0.5, (50, 60, 100)), (0.0, (100, 100, 100)), (0.25, (75, 80, 100))):
            im = cc.painted(P, P, I, VC, (100, 100, 100), share)[4]
            self.assertEqual(im.getpixel((0, 0)), want + (255,), share)
        self.assertEqual(cc.painted(P, P, I, VC, (10, 200, 30), 1.0)[4].getpixel((0, 0)), (0, 40, 30, 255))  # channel by channel

    def test_painted_many_colours(self):
        # 17 triangles, each of its own colour: a picture of 8 x 8, the 17th in its third row
        P = np.zeros((51, 3), np.float32)
        P[:, 0] = np.arange(51)
        VC = np.repeat(np.arange(17) * 10, 3)[:, None] * np.ones((1, 3))
        p, n, uv, i, im, colours = cc.painted(P, P, np.arange(51, dtype=np.uint32), VC, (255, 255, 255), 1.0)
        self.assertEqual((colours, im.size, len(p)), (17, (8, 8), 51))
        self.assertEqual(im.getpixel((0, 2)), (160, 160, 160, 255))
        last = uv[i[48]]  # the triangle of the colour 160
        np.testing.assert_allclose(last, [0.5 / 8, 2.5 / 8])
        np.testing.assert_allclose(uv[i[21]], [7.5 / 8, 0.5 / 8])  # the eighth colour: the end of the first row
        self.assertEqual(im.getpixel((7, 0)), (70, 70, 70, 255))
        p, n, uv, i, im, colours = cc.painted(P[:48], P[:48], np.arange(48, dtype=np.uint32), VC[:48], (255, 255, 255), 1.0)
        self.assertEqual((colours, im.size), (16, (4, 4)))  # sixteen just fit the smallest

    def cockpit(self, d, prims, shade=True):
        out = os.path.join(d, 'c.mwc')
        said = []
        groups = cc.convert(write_gltf(d, prims), d, out, log=lambda *a: said.append(' '.join(str(x) for x in a)), shade=shade)
        return groups, said

    def test_only_a_mods_cockpit_is_shaded(self):
        with tempfile.TemporaryDirectory() as d:
            put(os.path.join(d, 'Dash_LOD.props.txt'), PAINT)
            c = np.array([[0, 0, 0, 255]] * 4, np.uint8)
            prims = [('Dash_LOD',) + quad(0, 0, 1, 1, -1) + (c,)]
            groups, said = self.cockpit(d, prims, shade=False)
            self.assertEqual(groups[0][5].getpixel((0, 0)), (186, 255, 0, 255))
            self.assertEqual(cc.convert(write_gltf(d, prims), d, os.path.join(d, 'd.mwc'), log=lambda *a: None)[0][5].getpixel((0, 0)), (186, 255, 0, 255))
            g = json.loads(get(write_gltf(d, prims), 'r'))
            groups = cc.build_groups(g, get(os.path.join(d, 'mesh.bin')), cc.find_props(d), d, log=lambda *a: None)[0]
            self.assertEqual(groups[0][5].getpixel((0, 0)), (186, 255, 0, 255))

    def test_a_painted_part_is_shaded_by_half(self):
        with tempfile.TemporaryDirectory() as d:
            put(os.path.join(d, 'Dash_LOD.props.txt'), PAINT)
            c = np.array([[0, 0, 0, 255]] * 4, np.uint8)
            groups, said = self.cockpit(d, [('Dash_LOD',) + quad(0, 0, 1, 1, -1) + (c,)])
            self.assertEqual(groups[0][5].getpixel((0, 0)), (93, 128, 0, 255))  # half of 186, 255, 0
            self.assertIn('  Dash_LOD: shaded by its vertices, 1 colours', said)
            np.testing.assert_allclose(groups[0][3], np.full((4, 2), 0.125))  # all at the first texel's middle

    def test_a_part_without_material_takes_its_vertex_colours(self):
        with tempfile.TemporaryDirectory() as d:
            c = np.array([[40, 80, 120, 255]] * 4, np.uint8)
            groups, said = self.cockpit(d, [('dummy_material_0',) + quad(0, 0, 1, 1, -1) + (c,)])
            self.assertEqual(groups[0][5].getpixel((0, 0)), (40, 80, 120, 255))

    def test_white_vertices_change_nothing(self):
        with tempfile.TemporaryDirectory() as d:
            put(os.path.join(d, 'Dash_LOD.props.txt'), PAINT)
            c = np.array([[255, 255, 255, 255]] * 4, np.uint8)
            P, N, UV, I = quad(0, 0, 1, 1, -1)
            groups, said = self.cockpit(d, [('Dash_LOD', P, N, UV, I, c), ('Other', P, N, UV, I, c), ('Bare', P, N, UV, I)])
            self.assertEqual([g[5].getpixel((0, 0)) for g in groups], [(186, 255, 0, 255), (80, 80, 84, 255), (80, 80, 84, 255)])
            np.testing.assert_array_equal(groups[0][3], UV)
            self.assertFalse(any('shaded' in s for s in said))

    def test_a_picture_is_not_shaded(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, 'Game', 'A', 'B'))
            im = Image.new('RGB', (8, 8), (10, 20, 30))
            im.putpixel((5, 5), (200, 0, 0))  # a picture: more than one colour
            im.save(os.path.join(d, 'Game', 'A', 'B', 'Hull_CLR.png'))
            put(os.path.join(d, 'Hull_MTI.props.txt'), Props.TEXT)
            c = np.array([[0, 0, 0, 255]] * 4, np.uint8)
            P, N, UV, I = quad(0, 0, 1, 1, -1)
            groups, said = self.cockpit(d, [('Hull_MTI', P, N, UV, I, c)])
            self.assertEqual(groups[0][5].getpixel((0, 0)), (10, 20, 30, 255))
            np.testing.assert_array_equal(groups[0][3], UV)
            # of one colour all over it is shaded like a paint
            Image.new('RGB', (8, 8), (10, 20, 30)).save(os.path.join(d, 'Game', 'A', 'B', 'Hull_CLR.png'))
            groups, said = self.cockpit(d, [('Hull_MTI', P, N, UV, I, c)])
            self.assertEqual(groups[0][5].getpixel((0, 0)), (5, 10, 15, 255))

    def test_the_games_picture_materials_are_monitors(self):
        with tempfile.TemporaryDirectory() as d:
            put(os.path.join(d, 'Monitor_MTI.props.txt'), HOLOGRAM.replace('Projection_Hologram_MTL', 'Widget3D_Paperdoll1_MTL'))
            groups, said = self.cockpit(d, [('Monitor_MTI',) + quad(0, 0, 0.3, 0.2, -1), ('MiniMapGenerator1_MTL',) + quad(1, 0, 1.3, 0.2, -1),
                                            ('Dash',) + quad(2, 0, 3, 1, -1)])
            self.assertEqual([g[0] for g in groups], ['SCREEN:0', 'SCREEN:1', 'PAINT:Dash'])
            # with the game's own monitor material in the cockpit they are none
            groups, said = self.cockpit(d, [('MiniMapGenerator1_MTL',) + quad(1, 0, 1.3, 0.2, -1), ('Panels_MTI',) + quad(0.5, 0, 0.8, 0.2, -1)])
            self.assertEqual([g[0] for g in groups], ['MiniMapGenerator1_MTL', 'SCREEN:0'])


class PaintedMetal(unittest.TestCase):
    """a mod's painted metal is marked, and the picture the mod draws it with is made from the game's own"""

    def test_names(self):
        self.assertEqual((cc.PAINT, cc.DETAIL, cc.DETAIL_FILE, cc.GRAIN), ('PAINT:', 1024, 'mw4vr_paint.mwd', 3))
        self.assertEqual(cc.DETAIL_PATCHES, 'Game/Objects/_common/Materials/Textures/MacroVariation_CLR.png')
        self.assertEqual(cc.DETAIL_GRAIN, 'Game/Objects/Mechs/_common/Material/Textures/CastMetal_NRM.png')

    def test_is_paint(self):
        self.assertTrue(cc.is_paint(PAINT))
        self.assertTrue(cc.is_paint(MASKED))
        self.assertTrue(cc.is_paint(MASKED.replace('PaintColorPrimary', 'Other')))  # a mask alone
        self.assertFalse(cc.is_paint(''))
        self.assertFalse(cc.is_paint(Props.TEXT))
        self.assertFalse(cc.is_paint(PAINT + '\n' + Props.TEXT))  # a picture of its own
        self.assertFalse(cc.is_paint(PAINT.replace('PaintColorPrimary', 'PaintColorSecondary')))

    def pictures(self, d, patches=True, grain=True):
        if patches:
            os.makedirs(os.path.join(d, 'Game', 'Objects', '_common', 'Materials', 'Textures'))
            im = Image.new('RGB', (2, 2))
            im.putdata([(10, 10, 10), (20, 20, 20), (30, 30, 30), (40, 40, 40)])
            im.save(os.path.join(d, *cc.DETAIL_PATCHES.split('/')))
        if grain:
            os.makedirs(os.path.join(d, 'Game', 'Objects', 'Mechs', '_common', 'Material', 'Textures'))
            im = Image.new('RGB', (2, 2))
            im.putdata([(100, 0, 77), (111, 0, 77), (120, 255, 77), (131, 255, 77)])
            im.save(os.path.join(d, *cc.DETAIL_GRAIN.split('/')))

    def test_detail_map(self):
        with tempfile.TemporaryDirectory() as d:
            self.pictures(d)
            im = cc.detail_map(d, 2)
            self.assertEqual((im.mode, im.size), ('RGBA', (2, 2)))
            # the slopes about their mean (115.5; 127.5), three times as steep, about the middle; cut to 0..255
            self.assertEqual(pixels(im), [(81, 0, 10, 255), (114, 0, 20, 255), (141, 255, 30, 255), (174, 255, 40, 255)])
            self.assertEqual(cc.detail_map(d).size, (1024, 1024))
            self.assertEqual(cc.detail_map(d, 1).getpixel((0, 0))[2:], (25, 255))  # smaller: the mean
        for patches, grain in ((True, False), (False, True), (False, False)):
            with tempfile.TemporaryDirectory() as d:
                self.pictures(d, patches, grain)
                self.assertIsNone(cc.detail_map(d, 2))

    def test_write_detail(self):
        with tempfile.TemporaryDirectory() as d:
            im = Image.new('RGBA', (4, 4), (1, 2, 3, 255))
            cc.write_detail(os.path.join(d, 'p.mwd'), im)
            self.assertEqual(get(os.path.join(d, 'p.mwd')), b'MWD1' + struct.pack('<I', 4) + bytes([1, 2, 3, 255]) * 21)

    def names(self, d, prims, shade=True):
        return [g[0] for g in cc.convert(write_gltf(d, prims), d, os.path.join(d, 'c.mwc'), log=lambda *a: None, shade=shade)]

    def test_a_mods_painted_metal_is_marked(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, 'Game', 'A', 'B'))
            Image.new('RGB', (8, 8), (10, 20, 30)).save(os.path.join(d, 'Game', 'A', 'B', 'Hull_CLR.png'))
            Image.new('RGB', (8, 8), (200, 0, 0)).save(os.path.join(d, 'Game', 'A', 'B', 'Hull_EMS.png'))
            put(os.path.join(d, 'Dash_LOD.props.txt'), PAINT)
            put(os.path.join(d, 'Hull_MTI.props.txt'), Props.TEXT)
            put(os.path.join(d, 'Lamp_MTI.props.txt'), PAINT + '\n' + Props.TEXT.replace('BaseColor', 'Other'))  # paint that glows
            q = quad(0, 0, 1, 1, -1)
            prims = [('Dash_LOD',) + q, ('Hull_MTI',) + q, ('Lamp_MTI',) + q, ('dummy_material_0',) + q, ('Panels_MTI',) + quad(2, 0, 2.3, 0.2, -1),
                     ('Widget3D_Map_MTL',) + q]  # (with the game's monitors there, a picture material is a part like others)
            self.assertEqual(self.names(d, prims), ['PAINT:Dash_LOD', 'Hull_MTI', 'Lamp_MTI', 'PAINT:dummy_material_0', 'SCREEN:0', 'Widget3D_Map_MTL'])
            self.assertEqual(self.names(d, prims, shade=False), ['Dash_LOD', 'Hull_MTI', 'Lamp_MTI', 'dummy_material_0', 'SCREEN:0', 'Widget3D_Map_MTL'])
            self.assertEqual([g['name'] for g in cc.read_mwc(os.path.join(d, 'c.mwc'))][:4], ['Dash_LOD', 'Hull_MTI', 'Lamp_MTI', 'dummy_material_0'])


if __name__ == '__main__':
    unittest.main()
