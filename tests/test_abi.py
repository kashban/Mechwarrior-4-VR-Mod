# Is mod/src/mw4hook_abi.inc still true? Reads the constants out of the file and holds them against the programs
# themselves: mw4hook.dll and MW4Mercs.exe in the folder the environment variable MW4_DIR names (the game's
# folder). Without MW4_DIR only the file's own consistency is tested; the rest is skipped.
#   set MW4_DIR=<the game's folder>
#   python -m unittest discover -s tests
# A new build of mw4hook.dll fails here first - and tells which of the places the mod relies on have moved.
import os, re, struct, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
INC = os.path.join(HERE, '..', 'mod', 'src', 'mw4hook_abi.inc')
GAME = os.environ.get('MW4_DIR')


def read_inc():
    """-> ({NAME: number}, {NAME: label text}, {NAME: (stamp, image, name)})"""
    with open(INC, encoding='utf-8') as f:
        text = f.read()
    nums = {m.group(1): int(m.group(2), 16) for m in re.finditer(r'\b([A-Z][A-Z0-9_]+) = (0x[0-9a-fA-F]+)(?![0-9a-zA-Z])', text)}  # (not the shader hashes: ...ull)
    labels = {m.group(1): m.group(2) for m in re.finditer(r'#define (HK_LBL_\w+) L"([^"]*)"', text)}
    builds = {m.group(1): (int(m.group(2), 16), int(m.group(3), 16), m.group(4))
              for m in re.finditer(r'AbiBuild (\w+) = \{(0x[0-9a-fA-F]+), (0x[0-9a-fA-F]+), "([^"]*)"\}', text)}
    return nums, labels, builds


class PE:
    """a 32-bit PE file: its header's numbers and the bytes at an address relative to the image's base"""

    def __init__(self, path):
        with open(path, 'rb') as f:
            self.data = f.read()
        nt = struct.unpack_from('<I', self.data, 0x3c)[0]
        assert self.data[nt:nt + 4] == b'PE\0\0'
        nsec, self.stamp = struct.unpack_from('<HI', self.data, nt + 6)
        optsize = struct.unpack_from('<H', self.data, nt + 20)[0]
        opt = nt + 24
        assert struct.unpack_from('<H', self.data, opt)[0] == 0x10b  # PE32
        self.base = struct.unpack_from('<I', self.data, opt + 28)[0]
        self.image = struct.unpack_from('<I', self.data, opt + 56)[0]
        self.sections = []
        for i in range(nsec):
            o = opt + optsize + 40 * i
            vsize, va, rsize, raw = struct.unpack_from('<IIII', self.data, o + 8)
            self.sections.append((self.data[o:o + 8].rstrip(b'\0').decode(), va, vsize, raw, rsize))

    def section(self, rva):
        for name, va, vsize, raw, rsize in self.sections:
            if va <= rva < va + max(vsize, rsize):
                return name
        return None

    def at(self, rva, n):
        """n bytes of the file at rva (None: not in the file)"""
        for name, va, vsize, raw, rsize in self.sections:
            if va <= rva and rva + n <= va + rsize:
                return self.data[raw + rva - va:raw + rva - va + n]
        return None


class File(unittest.TestCase):
    def setUp(self):
        self.nums, self.labels, self.builds = read_inc()

    def test_everything_is_read(self):
        for name in ('HK_SCREEN_MODE', 'HK_WATER_REFLECT', 'HK_PICTURE', 'HK_PICTURE_KNOWN', 'HK_PICTURE_RECT', 'HK_OUT_WINDOW', 'HK_OUT_RECT',
                     'HK_OUT_WIDTH', 'HK_PTR_GETCURSORPOS', 'HK_PTR_SETCURSORPOS', 'HK_PTR_CLIPCURSOR', 'HK_IAT_GETCLIENTRECT',
                     'HK_AT_PICTURE_READ', 'HK_AT_WIDTH_STORE', 'HK_AT_SIZE_CALL', 'HK_AT_WATER_CHECK', 'HK_AT_GETCURSORPOS', 'HK_AT_CLIPCURSOR',
                     'GM_CAMERA_DRAW', 'GM_HWND', 'EL_PARENT', 'EL_LOCAL', 'EL_LOCAL_COPY', 'EL_WORLD', 'CAM_PLANES', 'CAM_TANGENTS', 'CAM_VIEWPORT'):
            self.assertIn(name, self.nums)
        self.assertEqual(set(self.builds), {'HK_KNOWN', 'GM_KNOWN'})
        self.assertEqual(len(self.labels), 8)

    def test_layouts_fit_together(self):
        n = self.nums
        self.assertEqual(n['HK_OUT_WIDTH'], n['HK_OUT_RECT'] + 8)    # a RECT's right
        self.assertEqual(n['HK_OUT_RECT'], n['HK_OUT_WINDOW'] + 4)
        self.assertEqual(n['HK_PTR_SETCURSORPOS'], n['HK_PTR_GETCURSORPOS'] + 4)
        self.assertEqual(n['HK_PTR_CLIPCURSOR'], n['HK_PTR_GETCURSORPOS'] + 8)
        self.assertEqual(n['EL_LOCAL_COPY'] - n['EL_LOCAL'], 48)     # a 3x4 float matrix each
        self.assertEqual(n['EL_WORLD'] - n['EL_LOCAL_COPY'], 48)
        self.assertGreaterEqual(n['CAM_TANGENTS'], n['CAM_PLANES'] + 6 * 16)
        for name, v in n.items():
            if name.startswith('HK_'):
                self.assertLess(v, self.builds['HK_KNOWN'][1], name)  # inside the image
        self.assertLess(n['GM_CAMERA_DRAW'] - 0x400000, self.builds['GM_KNOWN'][1])
        self.assertLess(n['GM_HWND'] - 0x400000, self.builds['GM_KNOWN'][1])


@unittest.skipUnless(GAME and os.path.exists(os.path.join(GAME, 'mw4hook.dll')), 'MW4_DIR does not name a folder with mw4hook.dll')
class Mw4hook(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.nums, cls.labels, cls.builds = read_inc()
        cls.pe = PE(os.path.join(GAME, 'mw4hook.dll'))

    def test_build(self):
        stamp, image, name = self.builds['HK_KNOWN']
        self.assertEqual((hex(self.pe.stamp), hex(self.pe.image)), (hex(stamp), hex(image)), 'not build ' + name)

    def check(self, site, opcode, target, tail=b''):
        """the instruction at site: opcode, then the address of target as the file has it (the preferred base)"""
        n = self.nums
        want = opcode + struct.pack('<I', self.pe.base + n[target]) + tail
        got = self.pe.at(n[site], len(want))
        self.assertIsNotNone(got, site + ' is not in the file')
        self.assertEqual(got.hex(), want.hex(), '%s: not the instruction known' % site)
        self.assertEqual(self.pe.section(n[site]), '.text')

    def test_code_sites(self):
        self.check('HK_AT_PICTURE_READ', b'\xa1', 'HK_PICTURE')            # mov eax, [..]
        self.check('HK_AT_WIDTH_STORE', b'\xa3', 'HK_OUT_WIDTH')           # mov [..], eax
        self.check('HK_AT_SIZE_CALL', b'\xff\x15', 'HK_IAT_GETCLIENTRECT')  # call [..]
        self.check('HK_AT_WATER_CHECK', b'\x80\x3d', 'HK_WATER_REFLECT', b'\0')  # cmp byte ptr [..], 0
        self.check('HK_AT_GETCURSORPOS', b'\xa1', 'HK_PTR_GETCURSORPOS')
        self.check('HK_AT_CLIPCURSOR', b'\xa1', 'HK_PTR_CLIPCURSOR')

    def test_data_is_data(self):
        for name, v in self.nums.items():
            if name.startswith('HK_') and not name.startswith('HK_AT_'):
                self.assertNotIn(self.pe.section(v), (None, '.text'), name)

    def test_labels(self):
        # the annotation labels' beginnings, as mw4hook holds them (wide or narrow text)
        for name, text in self.labels.items():
            found = text.encode('utf-16-le') in self.pe.data or text.encode('ascii') in self.pe.data
            self.assertTrue(found, '%s: "%s" is not in mw4hook.dll' % (name, text))


@unittest.skipUnless(GAME and os.path.exists(os.path.join(GAME, 'MW4Mercs.exe')), 'MW4_DIR does not name a folder with MW4Mercs.exe')
class Game(unittest.TestCase):
    def test_build(self):
        # the file is packed: only its header can be held against what is known
        nums, labels, builds = read_inc()
        pe = PE(os.path.join(GAME, 'MW4Mercs.exe'))
        stamp, image, name = builds['GM_KNOWN']
        self.assertEqual((hex(pe.stamp), hex(pe.image), hex(pe.base)), (hex(stamp), hex(image), hex(0x400000)), 'not ' + name)


if __name__ == '__main__':
    unittest.main()
