# Tests of tools/ci/guard.py: what may never be in the repository.
import importlib.util, os, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('guard', os.path.join(HERE, '..', 'tools', 'ci', 'guard.py'))
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)

# the private things of these tests are put together here, so that this file itself passes the guard
USERS = 'Users'
NET = '192.' + '168.'
AT = '@'


class Names(unittest.TestCase):
    def test_assets(self):
        for p in ('a/Atlas.mwc', 'x/mw4vr_paint.MWD', 'Mech_SKM.uasset', 'm.gltf', 'm.bin', 'Dash_MTL.props.txt', 'r/MW4VR.zip', 'game/d3d11.dll',
                  'p.pak', 'a.psk', 'tools/umodel.exe'):
            self.assertIsNotNone(guard.asset(p), p)
        for p in ('tools/cockpit_convert.py', 'docs/logo/logo_512.png', 'defaults/mw4vr_cockpits/atlas.ini', 'mwc.txt', 'docs/mwc', 'a.binx'):
            self.assertIsNone(guard.asset(p), p)
        self.assertEqual(guard.asset('A.Props.TXT'), '.props.txt')

    def test_check_names(self):
        mb = guard.MAX_BYTES
        self.assertEqual(mb, 1024 * 1024)
        found = guard.check_names([('a.py', 10), ('b.mwc', 10), ('c.png', mb), ('d.png', mb + 1), ('old/e.uasset', None), ('old/f.png', None),
                                   ('g.gltf', 3 * mb)])
        self.assertEqual(found, [('b.mwc', 'a .mwc file'), ('d.png', '1.0 MB - more than 1 MB'), ('old/e.uasset', 'a .uasset file'),
                                 ('g.gltf', 'a .gltf file')])


class Text(unittest.TestCase):
    def found(self, text):
        return [what for _, what in guard.check_text('f', text)]

    def test_user_paths(self):
        for p in ('C:\\%s\\somebody\\x' % USERS, 'c:/%s/somebody' % USERS, '/c/%s/somebody/x' % USERS, '/home' + '/somebody/x',
                  'C:\\\\%s\\\\somebody' % USERS):
            self.assertEqual(len(self.found('in ' + p)), 1, p)
            self.assertIn("a path into a user's folder", self.found(p)[0])
        for p in ('C:\\%s\\Public\\x' % USERS, 'C:\\%s\\<you>' % USERS, 'C:\\%s\\you\\x' % USERS, 'C:\\Program Files (x86)\\Windows Kits', 'the users of it', '/home' + '/runner/work'):
            self.assertEqual(self.found(p), [], p)

    def test_private_networks(self):
        for a in (NET + '0.22', '10.' + '0.0.5', '172.' + '16.4.1', '172.' + '31.255.254'):
            self.assertEqual(self.found('ssh pi@host at %s now' % a), ['an address of a private network: ' + a], a)
        for a in ('8.8.8.8', '172.' + '32.0.1', '10.0.26300', '10.0.22621.12345', '1.' + NET + '0.1', 'v11.46', '110.' + '1.2.3'):
            self.assertEqual(self.found(a), [], a)

    def test_mail(self):
        self.assertEqual(self.found('write to some.body+x' + AT + 'mail.example.org.'), ['a mail address: some.body+x' + AT + 'mail.example.org'])
        self.assertEqual(self.found('Co-Authored-By: Claude <noreply' + AT + 'anthropic.com>'), [])
        self.assertEqual(self.found('git' + AT + 'github.com:owner/repo.git, NoReply' + AT + 'Anthropic.com'), [])
        self.assertEqual(self.found('d = a @ b; uses: actions/checkout' + AT + 'v7; x' + AT + 'y'), [])

    def test_lines(self):
        got = guard.check_text('docs/x.md', 'fine\nssh ' + NET + '1.2\n\nand me' + AT + 'home.example and ' + NET + '1.3')
        self.assertEqual([w for w, _ in got], ['docs/x.md:2', 'docs/x.md:4', 'docs/x.md:4'])


class Repository(unittest.TestCase):
    def test_this_repository_is_clean(self):
        self.assertEqual(guard.scan(), [])

    def test_main(self):
        self.assertEqual(guard.main(['guard.py']), 0)


if __name__ == '__main__':
    unittest.main()
