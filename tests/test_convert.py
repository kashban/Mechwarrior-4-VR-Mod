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

    for name, P, N, UV, I in prims:
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
    P, N = np.vstack([Pr, Pl]), np.vstack([Nr, Nl])
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
        for p, n, uv, i, asp in parts:
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
        (p, n, uv, i, asp), = cc.screens(P, N, np.array([0, 1, 2, 0, 2, 3], np.uint32))
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

    def test_tilted_monitor_keeps_its_shape(self):
        # leaning back by 45 degrees: 0.3 wide, 0.2 along its slope
        s = 0.2 / np.sqrt(2)
        P = np.array([[0, 0, 1], [0.3, 0, 1], [0.3, s, 1 + s], [0, s, 1 + s]], np.float32)
        nrm = np.array([0, 1, -1], np.float32) / np.sqrt(2)
        (p, n, uv, i, asp), = cc.screens(P, np.tile(nrm, (4, 1)), np.array([0, 1, 2, 0, 2, 3], np.uint32))
        self.assertAlmostEqual(asp, 1.5, places=3)


class Accessors(unittest.TestCase):
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
            self.assertEqual(b[ems + cc.mip_bytes(8):ems + cc.mip_bytes(8) + 8], b'SCREEN:0')

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
            with self.assertRaises(ValueError):
                cc.read_mwc(p)
            out, _ = self.run_convert(d)
            with open(out, 'ab') as f:
                f.write(b'\0')
            with self.assertRaises(ValueError):
                cc.read_mwc(out)


if __name__ == '__main__':
    unittest.main()
