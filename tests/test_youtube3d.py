"""The layout of a video for YouTube's 3D (tools/video/youtube3d.py): the filters, without running ffmpeg."""

import argparse
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools', 'video'))
import youtube3d  # noqa: E402


class Layout(unittest.TestCase):
    def test_even(self):
        self.assertEqual([youtube3d.even(x) for x in (1462.5, 1463.1, 0.2, 1920)], [1462, 1464, 2, 1920])

    def test_whole_square_eye(self):
        # a square eye is shown 2160 x 2160: stored half as wide, in the middle of its half
        self.assertEqual(youtube3d.eye_filter(2600, 2600, 0, 2160, 'whole', 0),
                         'crop=2600:2600:0:0,scale=1080:2160:flags=lanczos,pad=1920:2160:420:0:black')
        self.assertEqual(youtube3d.eye_filter(2600, 2600, 2600, 4320, 'whole', 0),
                         'crop=2600:2600:2600:0,scale=2160:4320:flags=lanczos,pad=3840:4320:840:0:black')

    def test_whole_wide_eye(self):
        # an eye wider than 16:9 fills the width, black above and below
        self.assertEqual(youtube3d.eye_filter(4000, 2000, 0, 2160, 'whole', 0),
                         'crop=4000:2000:0:0,scale=1920:1920:flags=lanczos,pad=1920:2160:0:120:black')

    def test_fill(self):
        # 16:9 of a square eye: its whole width, 1462 of its 2600 rows - the middle ones, the top, the bottom
        self.assertEqual(youtube3d.eye_filter(2600, 2600, 0, 2160, 'fill', 0), 'crop=2600:1462:0:569,scale=1920:2160:flags=lanczos')
        self.assertEqual(youtube3d.eye_filter(2600, 2600, 2600, 2160, 'fill', -1), 'crop=2600:1462:2600:0,scale=1920:2160:flags=lanczos')
        self.assertEqual(youtube3d.eye_filter(2600, 2600, 0, 2160, 'fill', 1), 'crop=2600:1462:0:1138,scale=1920:2160:flags=lanczos')
        self.assertEqual(youtube3d.eye_filter(2600, 2600, 0, 2160, 'fill', 7), youtube3d.eye_filter(2600, 2600, 0, 2160, 'fill', 1))
        # of a wider eye: its whole height, the middle of its width
        self.assertEqual(youtube3d.eye_filter(4000, 2000, 4000, 2160, 'fill', 0), 'crop=3556:2000:4222:0,scale=1920:2160:flags=lanczos')

    def test_graph_has_both_eyes(self):
        g = youtube3d.graph(2600, 2600, 2160, 'whole', 0)
        self.assertTrue(g.startswith('[0:v]split[a][b];[a]crop=2600:2600:0:0,'))
        self.assertIn('[b]crop=2600:2600:2600:0,', g)
        self.assertTrue(g.endswith('[l][r]hstack,setsar=1[v]'))


class Command(unittest.TestCase):
    def args(self, **kw):
        a = argparse.Namespace(fit='whole', height=2160, down=0.0, start=None, end=None, encoder='hevc_nvenc')
        a.__dict__.update(kw)
        return a

    def test_marks_the_file_as_3d(self):
        c = youtube3d.command('in.mp4', 'out.mkv', 2600, 2600, self.args())
        self.assertEqual(c[-3:], ['-metadata:s:v:0', 'stereo_mode=left_right', 'out.mkv'])
        self.assertEqual(c[c.index('-maxrate') + 1], '80M')
        self.assertNotIn('-ss', c)

    def test_part_and_size(self):
        c = youtube3d.command('in.mp4', 'out.mkv', 3200, 3200, self.args(start=12.5, end=95, height=4320, encoder='libx265'))
        self.assertEqual(c[c.index('-ss') + 1], '12.5')
        self.assertEqual(c[c.index('-to') + 1], '95')
        self.assertLess(c.index('-ss'), c.index('-i'))
        self.assertIn('-crf', c)
        self.assertNotIn('-maxrate', c)
        big = youtube3d.command('i', 'o', 3200, 3200, self.args(height=4320))
        self.assertEqual(big[big.index('-maxrate') + 1], '320M')


class Sizes(unittest.TestCase):
    def test_from_the_mods_json(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, 'v.json'), 'w', encoding='utf-8') as f:
                json.dump({'layout': 'left_right', 'eye_width': 3200, 'eye_height': 3200}, f)
            self.assertEqual(youtube3d.eye_size(os.path.join(d, 'v.mp4')), (3200, 3200))


if __name__ == '__main__':
    unittest.main()
