"""The reader of the performance runs' records (tools/e2e/perf.py), on frames made up here."""

import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools', 'e2e'))
import perf  # noqa: E402

HEAD = 'frame,t,us,eye0,eye1,ours,wait,sys,grab,hook,d3d,map,exec,draws,seated,outside,menu\n'


def line(t, us=20000, eye0=8000, eye1=6000, ours=1500, wait=1000, sys_=100, grab=10, hook=900, d3d=100, seated=1, outside=0, menu=0):
    return f'7,{t:.3f},{us},{eye0},{eye1},{ours},{wait},{sys_},{grab},{hook},{d3d},120,0,700,{seated},{outside},{menu}\n'


class Record(unittest.TestCase):
    def write(self, text):
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        path = os.path.join(d.name, 'mw4vr_perf.csv')
        with open(path, 'w') as f:
            f.write(text)
        return path

    def test_parts_are_worked_out(self):
        rows = perf.read(self.write(HEAD + line(3.0)), 2.0, 60.0)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['ours_only'], 500)  # our work at Present without the wait in it
        self.assertEqual(rows[0]['hook_only'], 800)  # our draw hooks without Direct3D's own calls
        self.assertEqual(rows[0]['rest'], 20000 - 8000 - 6000 - 10 - 1500 - 100)

    def test_only_frames_in_the_mech_between_the_times_count(self):
        text = HEAD + line(1.0) + line(3.0) + line(4.0, seated=0) + line(5.0, outside=1) + line(6.0, menu=1) + line(70.0) + line(2.0)
        rows = perf.read(self.write(text), 2.0, 60.0)
        self.assertEqual([r['t'] for r in rows], [3.0, 2.0])

    def test_a_line_cut_off_is_passed_over(self):
        rows = perf.read(self.write(HEAD + line(3.0) + '8,4.000,2000'), 2.0, 60.0)
        self.assertEqual(len(rows), 1)

    def test_summary(self):
        rows = perf.read(self.write(HEAD + ''.join(line(2.0 + i * 0.01, us=10000 + i * 100) for i in range(100))), 2.0, 60.0)
        s = perf.summary(rows)
        self.assertAlmostEqual(s['us']['mean'], 10000 + 99 * 100 / 2)
        self.assertEqual(s['us']['median'], 15000)
        self.assertEqual(s['us']['p95'], 19500)
        self.assertEqual(s['us']['p99'], 19900)
        self.assertEqual(s['draws']['mean'], 700)

    def run_main(self, argv):
        out = io.StringIO()
        old = sys.argv
        sys.argv = ['perf.py', *argv]
        try:
            with redirect_stdout(out):
                code = perf.main()
        finally:
            sys.argv = old
        return code, out.getvalue()

    def test_too_few_frames_say_nothing(self):
        code, text = self.run_main([self.write(HEAD + line(3.0))])
        self.assertEqual(code, 2)
        self.assertIn('only 1 frames', text)

    def test_the_table(self):
        path = self.write(HEAD + ''.join(line(2.0 + i * 0.01) for i in range(50)))
        code, text = self.run_main([path])
        self.assertEqual(code, 0)
        self.assertIn('50 frames in the mech, 50.0 a second', text)
        self.assertRegex(text, r"left eye's pass\s+8\.00\s+8\.00\s+8\.00\s+8\.00  ms")
        self.assertRegex(text, r'draw calls\s+700\s+700\s+700\s+700\n')


if __name__ == '__main__':
    unittest.main()
