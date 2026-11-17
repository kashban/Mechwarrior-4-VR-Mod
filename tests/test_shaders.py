# Do the mod's shaders compile? The game compiles them when it starts (D3DCompile); a slip in one would show
# only there, as a missing cockpit. Here the Windows SDK's fxc compiles the same texts with the same entry
# points and targets. Skipped where no fxc is found (FXC names one; otherwise the Windows Kits are searched).
import glob, os, re, subprocess, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', 'mod', 'src')


def find_fxc():
    if os.environ.get('FXC'):
        return os.environ['FXC']
    found = sorted(glob.glob(r'C:\Program Files (x86)\Windows Kits\10\bin\*\x86\fxc.exe'))
    return found[-1] if found else None


FXC = find_fxc()


def read(name):
    with open(os.path.join(SRC, name), encoding='utf-8') as f:
        return f.read()


def hlsl_in(name):
    """the text between R"HLSL( and )HLSL" of a source file"""
    parts = re.findall(r'R"HLSL\((.*?)\)HLSL"', read(name), re.S)
    assert len(parts) == 1, name
    return parts[0]


def compile_calls():
    """(text's variable, entry point, target) of every D3DCompile call in the mod's sources"""
    calls = []
    for name in sorted(os.listdir(SRC)):
        if name.endswith(('.cpp', '.inc')):
            calls += re.findall(r'g_compile\((\w+), sizeof\(\1\) - 1, "[^"]*", nullptr, nullptr, "(\w+)", "(\w+)"', read(name))
    return calls


class Sources(unittest.TestCase):
    def test_header_is_the_hlsl_file(self):
        # eye_tl_vs.h holds eye_tl_vs.hlsl as a string: the two must not drift apart
        self.assertEqual(hlsl_in('eye_tl_vs.h').strip(), read('eye_tl_vs.hlsl').strip())

    def test_every_compile_call_is_known(self):
        self.assertEqual(sorted(compile_calls()), [('g_crHlsl', 'ps_cross', 'ps_5_0'), ('g_crHlsl', 'ps_rest', 'ps_5_0'), ('g_crHlsl', 'vs', 'vs_5_0'),
                                                 ('g_eyeTlHlsl', 'main', 'vs_5_0'), ('g_m5Hlsl', 'ps', 'ps_5_0'), ('g_m5Hlsl', 'vs', 'vs_5_0')])


@unittest.skipUnless(FXC and os.path.exists(FXC), 'no fxc.exe (Windows SDK)')
class Compile(unittest.TestCase):
    TEXTS = {'g_eyeTlHlsl': 'eye_tl_vs.h', 'g_m5Hlsl': 'mw5.inc', 'g_crHlsl': 'cross.inc'}

    def test_all(self):
        for var, entry, target in compile_calls():
            with self.subTest(shader=var, entry=entry), tempfile.TemporaryDirectory() as d:
                src, out = os.path.join(d, 'shader.hlsl'), os.path.join(d, 'shader.cso')
                with open(src, 'w', encoding='ascii') as f:
                    f.write(hlsl_in(self.TEXTS[var]))
                r = subprocess.run([FXC, '/nologo', '/O3', '/T', target, '/E', entry, '/Fo', out, src], capture_output=True, text=True, errors='replace')
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                self.assertGreater(os.path.getsize(out), 100)
                self.assertNotIn('warning', (r.stdout + r.stderr).lower(), r.stdout + r.stderr)

    def test_a_broken_shader_fails(self):
        # the test itself: fxc's verdict is read rightly
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, 'shader.hlsl')
            with open(src, 'w', encoding='ascii') as f:
                f.write(hlsl_in('mw5.inc').replace('float4', 'float5', 1))
            r = subprocess.run([FXC, '/nologo', '/T', 'vs_5_0', '/E', 'vs', '/Fo', os.path.join(d, 'x.cso'), src], capture_output=True)
            self.assertNotEqual(r.returncode, 0)


if __name__ == '__main__':
    unittest.main()
