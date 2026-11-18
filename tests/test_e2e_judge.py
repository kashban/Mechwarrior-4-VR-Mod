"""The judge of the end-to-end runs (tools/e2e/judge.py), on frames made up here."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools', 'e2e'))
import judge  # noqa: E402

LOG = '12:00:00.000  MW4VR v11.59 (debug level 0) loaded into game\n'


def frames(n=900, fps=60.0, own=1, cockpit='atlas', bright=80.0):
    out = []
    for i in range(n):
        out.append({'frame': 1000.0 + i, 'ms': i * 1000.0 / fps, 't': i / fps, 'seated': 1.0, 'own': float(own), 'yaw': 0.0,
                    'left': bright, 'right': bright + 1, 'sx': 0.0, 'sy': 0.0, 'sz': 0.0, 'outside': 0.0, 'menu': 0.0, 'draws': 600.0,
                    'cockpit': cockpit})
    return out


def acted(walk=True, fire=True, outside=True, menu=True, back=True, menu_at=6.05):
    """15 s: the mech walks from second 1 to 3, fires from 4 to 5, the camera is outside from 6 to 8, the menu up from 10 to 12."""
    f = frames()
    for x in f:
        t = x['t']
        if walk and 1.5 <= t <= 3:
            x['sy'] = 0.01
        if fire and 4.2 <= t <= 4.8:
            x['left'] = x['right'] = 84.0
        if outside and 6.3 <= t <= (8.2 if back else 99):
            x['outside'], x['own'], x['seated'] = 1.0, 0.0, 0.0
        if menu and menu_at + 4 <= t <= 12.1:
            x['menu'] = 1.0
    return f


WINDOWS = dict(walk=(1, 3), fire=(4, 5), outside=(6, 8), menu=(10, 12))


class Acting(unittest.TestCase):
    def failed(self, **how):
        return judge.judge(acted(**how), LOG, cockpit='Atlas', **WINDOWS)[1]

    def test_a_run_that_does_all_passes(self):
        passed, failed = judge.judge(acted(), LOG, cockpit='Atlas', **WINDOWS)
        self.assertEqual(failed, [])
        self.assertGreaterEqual(len(passed), 18)

    def test_a_mech_that_stands(self):
        failed = self.failed(walk=False)
        self.assertEqual(len(failed), 1)
        self.assertIn('the mech walks', failed[0])

    def test_no_shot(self):
        failed = self.failed(fire=False)
        self.assertEqual(len(failed), 1)
        self.assertIn('the mech fires', failed[0])

    def test_the_view_does_not_change(self):
        failed = self.failed(outside=False)
        self.assertEqual(len(failed), 1)
        self.assertIn('seen from outside', failed[0])

    def test_the_view_does_not_come_back(self):
        failed = self.failed(back=False)
        self.assertTrue(any('back in the mech' in x for x in failed), failed)

    def test_no_menu(self):
        failed = self.failed(menu=False)
        self.assertEqual(len(failed), 2)
        self.assertIn('menu is shown as one screen', failed[0])

    def test_a_menu_that_comes_late(self):
        failed = self.failed(menu_at=6.7)
        self.assertEqual(len(failed), 1)
        self.assertIn('within half a second', failed[0])

    def test_windows(self):
        self.assertEqual(judge.window('13:18.5'), (13.0, 18.5))
        self.assertIsNone(judge.window(None))


class Judge(unittest.TestCase):
    def test_a_clean_run_passes(self):
        passed, failed = judge.judge(frames(), LOG, cockpit='Atlas')
        self.assertEqual(failed, [])
        self.assertGreaterEqual(len(passed), 9)

    def test_one_bright_frame_is_a_flash(self):
        f = frames()
        f[400]['left'] = 200.0
        _, failed = judge.judge(f, LOG)
        self.assertEqual(len(failed), 1)
        self.assertIn('left eye does not flash', failed[0])
        self.assertIn('1400', failed[0])

    def test_a_cut_to_another_view_is_no_flash(self):
        f = frames()
        for x in f[400:]:
            x['left'] = x['right'] = 160.0
        self.assertEqual(judge.flashes(f, 'left'), [])

    def test_black_eye(self):
        f = frames()
        for x in f[100:110]:
            x['right'] = 0.0
        _, failed = judge.judge(f, LOG)
        self.assertTrue(any('right eye is never black' in line for line in failed))

    def test_too_short_and_too_slow(self):
        _, failed = judge.judge(frames(n=100, fps=20.0), LOG, seconds=10)
        self.assertTrue(any('ran for' in line for line in failed))
        self.assertTrue(any('frames a second' in line for line in failed))

    def test_never_seated(self):
        f = frames()
        for x in f:
            x['seated'] = 0.0
        _, failed = judge.judge(f, LOG)
        self.assertEqual(len(failed), 1)

    def test_wrong_or_late_cockpit(self):
        _, failed = judge.judge(frames(cockpit='nova'), LOG, cockpit='atlas')
        self.assertTrue(any("it is 'atlas'" in line for line in failed))
        f = frames()
        for x in f[:300]:
            x['own'] = 0.0
        _, failed = judge.judge(f, LOG, cockpit='atlas')
        self.assertTrue(any('within 2 s' in line for line in failed))

    def test_cockpit_wandering_with_the_head(self):
        f = frames()
        for x in f[300:600]:
            x['yaw'] = 45.0
            x['sx'] = 0.03
        _, failed = judge.judge(f, LOG)
        self.assertTrue(any('stays put' in line and '3.0 cm' in line for line in failed))

    def test_errors_in_the_log(self):
        _, failed = judge.judge(frames(), LOG + 'mw5: exception 0xc0000005 while loading\n')
        self.assertTrue(any('no error in the log' in line for line in failed))
        _, failed = judge.judge(frames(), 'nothing\n')
        self.assertTrue(any('was loaded' in line for line in failed))

    def test_csv_with_a_cut_last_line(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'e.csv')
            with open(p, 'w', encoding='utf-8') as f:
                f.write('frame,ms,t,seated,own,yaw,lr,lg,lb,rr,rg,rb,sx,sy,sz,cockpit\n')
                f.write('5,10,0.5,1,1,0.00,100.0,100.0,100.0,50.0,50.0,50.0,0.0,0.0,0.0,atlas\n')
                f.write('6,20,0.6,1,1,0.0')
            got = judge.read_frames(p)
        self.assertEqual(len(got), 1)
        self.assertAlmostEqual(got[0]['left'], 100.0, places=3)
        self.assertEqual(got[0]['cockpit'], 'atlas')


if __name__ == '__main__':
    unittest.main()
