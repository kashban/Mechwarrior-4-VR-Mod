"""The layout of a video for YouTube's 3D (tools/video/youtube3d.py): the filters and the marks, without ffmpeg."""

import argparse
import json
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools', 'video'))
import youtube3d  # noqa: E402

T = 1.20629  # the tangent of half of 100.683 degrees
SQUARE = (-T, T, T, -T)


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

    def test_flat_is_one_eye(self):
        # an ordinary 16:9 video: the same rows as fill takes, of the left eye or the right, over the whole width
        self.assertEqual(youtube3d.flat_graph(3200, 3200, 0, 2160, 0),
                         '[0:v]crop=3200:1800:0:700,scale=3840:2160:flags=lanczos,setsar=1[v]')
        self.assertEqual(youtube3d.flat_graph(3200, 3200, 3200, 1080, 1),
                         '[0:v]crop=3200:1800:3200:1400,scale=1920:1080:flags=lanczos,setsar=1[v]')
        self.assertEqual(youtube3d.flat_graph(4000, 2000, 0, 1440, 0),
                         '[0:v]crop=3556:2000:222:0,scale=2560:1440:flags=lanczos,setsar=1[v]')

    def test_graph_has_both_eyes(self):
        g = youtube3d.graph('crop=1', 'crop=2')
        self.assertEqual(g, '[0:v]split[a][b];[a]crop=1[l];[b]crop=2[r];[l][r]hstack,setsar=1[v]')


class Sphere(unittest.TestCase):
    def test_square_eye(self):
        # a picture whose middle is straight ahead goes onto the sphere with 16 black pixels around it (v360 paints
        # what lies outside in the colour of the picture's edge): 3232 pixels stand for a little more than 3200
        self.assertEqual(youtube3d.sphere_filter(3200, 3200, 3200, SQUARE, 4096),
                         'crop=3200:3200:3200:0,pad=3232:3232:16:16:black,'
                         'v360=input=flat:ih_fov=101.243:iv_fov=101.243:output=hequirect:w=4096:h=4096:interp=lanczos')

    def test_eye_that_looks_to_a_side(self):
        # 1 to the left, 0.5 to the right, 0.5 up, 1 down: 1000 pixels for a tangent of 1.5. Widened to 1 each way
        # (1334 pixels, 90 degrees), the picture at the left and at the bottom of it - and the black edge
        self.assertEqual(youtube3d.sphere_filter(1000, 1000, 0, (-1.0, 0.5, 0.5, -1.0), 2048),
                         'crop=1000:1000:0:0,pad=1366:1366:16:349:black,'
                         'v360=input=flat:ih_fov=91.387:iv_fov=91.387:output=hequirect:w=2048:h=2048:interp=lanczos')
        self.assertIn('pad=1366:1366:349:16:black', youtube3d.sphere_filter(1000, 1000, 0, (-0.5, 1.0, 1.0, -0.5), 2048))

    def test_size(self):
        # 3200 rows over a tangent of 2.41: 1326 a radian, 4167 for the half circle - 4096 is the most
        self.assertEqual(youtube3d.sphere_size(3200, SQUARE), 4096)
        self.assertEqual(youtube3d.sphere_size(2600, SQUARE), 3392)
        self.assertEqual(youtube3d.sphere_size(400, SQUARE), 1024)


class Command(unittest.TestCase):
    def args(self, **kw):
        a = argparse.Namespace(start=None, end=None, encoder='hevc_nvenc', hwaccel='auto')
        a.__dict__.update(kw)
        return a

    def test_whole_video(self):
        c = youtube3d.command('in.mp4', 'out.mp4', 'G', 3840 * 2160, self.args())
        self.assertEqual(c[c.index('-filter_complex') + 1], 'G')
        self.assertEqual(c[-1], 'out.mp4')
        self.assertEqual(c[c.index('-maxrate') + 1], '80M')
        self.assertEqual(c[c.index('-tag:v') + 1], 'hvc1')
        self.assertNotIn('-ss', c)

    def test_what_reads_the_video(self):
        c = youtube3d.command('in.mp4', 'out.mp4', 'G', 3840 * 2160, self.args())
        self.assertEqual(c[c.index('-hwaccel') + 1], 'auto')
        self.assertLess(c.index('-hwaccel'), c.index('-i'))
        self.assertNotIn('-hwaccel', youtube3d.command('i', 'o', 'G', 3840 * 2160, self.args(hwaccel='none')))

    def test_part_and_size(self):
        c = youtube3d.command('in.mp4', 'out.mp4', 'G', 7680 * 4320, self.args(start=12.5, end=95, encoder='libx264'))
        self.assertEqual(c[c.index('-ss') + 1], '12.5')
        self.assertEqual(c[c.index('-to') + 1], '95')
        self.assertLess(c.index('-ss'), c.index('-i'))
        self.assertIn('-crf', c)
        self.assertNotIn('-maxrate', c)
        self.assertNotIn('-tag:v', c)
        big = youtube3d.command('i', 'o', 'G', 7680 * 4320, self.args())
        self.assertEqual(big[big.index('-maxrate') + 1], '320M')
        self.assertEqual(youtube3d.command('i', 'o', 'G', 8192 * 4096, self.args())[big.index('-maxrate') + 1], '324M')


def track(handler, entry):
    """A track of an .mp4 with one description of its samples."""
    b = youtube3d.box
    stsd = b(b'stsd', bytes(4) + struct.pack('>I', 1) + entry)
    minf = b(b'minf', b(b'stbl', stsd + b(b'stts', bytes(8))))
    return b(b'trak', b(b'tkhd', bytes(20)) + b(b'mdia', b(b'hdlr', bytes(8) + handler + bytes(12)) + minf))


class Marks(unittest.TestCase):
    def setUp(self):
        b = youtube3d.box
        self.entry = b(b'hvc1', bytes(78) + b(b'hvcC', b'config'))
        self.sound = track(b'soun', b(b'mp4a', bytes(28)))
        self.moov = b(b'mvhd', bytes(100)) + self.sound + track(b'vide', self.entry) + b(b'udta', b'x')

    def write(self, d, parts):
        path = os.path.join(d, 'v.mp4')
        with open(path, 'wb') as f:
            f.write(parts)
        return path

    def test_the_boxes(self):
        flat = youtube3d.marks(False)
        self.assertEqual(flat, bytes.fromhex('0000000d') + b'st3d' + bytes.fromhex('0000000002'))
        ball = youtube3d.marks(True)
        self.assertTrue(ball.startswith(flat))
        sv3d = youtube3d.find(ball, b'sv3d')
        self.assertEqual(youtube3d.find(sv3d, b'svhd'), bytes(4) + b'mw4vr\0')
        proj = youtube3d.find(sv3d, b'proj')
        self.assertEqual(youtube3d.find(proj, b'prhd'), bytes(16))
        # nothing left out above and below, a quarter at the left and at the right: the front half
        self.assertEqual(youtube3d.find(proj, b'equi'), bytes(12) + bytes.fromhex('4000000040000000'))

    def test_into_the_video_tracks_description(self):
        extra = youtube3d.marks(True)
        new = youtube3d.mark_content(self.moov, [b'trak', b'mdia', b'minf', b'stbl', b'stsd'], extra)
        self.assertEqual(len(new), len(self.moov) + len(extra))
        # every box still ends where the next begins, the sound's track is as it was, the description has grown
        kinds = [k for k, _, _ in youtube3d.children(new)]
        self.assertEqual(kinds, [b'mvhd', b'trak', b'trak', b'udta'])
        self.assertIn(self.sound, new)
        grown = youtube3d.box(b'hvc1', self.entry[8:] + extra)
        self.assertIn(struct.pack('>I', 1) + grown + youtube3d.box(b'stts', bytes(8)), new)
        # a second time changes nothing
        self.assertEqual(youtube3d.mark_content(new, [b'trak', b'mdia', b'minf', b'stbl', b'stsd'], extra), new)

    def test_no_video_track(self):
        moov = youtube3d.box(b'mvhd', bytes(100)) + self.sound
        self.assertIsNone(youtube3d.mark_content(moov, [b'trak', b'mdia', b'minf', b'stbl', b'stsd'], b'x'))

    def test_file(self):
        b = youtube3d.box
        head = b(b'ftyp', b'isom') + b(b'free', b'') + struct.pack('>I4sQ', 1, b'mdat', 16 + 5) + b'video'
        with tempfile.TemporaryDirectory() as d:
            path = self.write(d, head + b(b'moov', self.moov))
            youtube3d.mark_file(path, False)
            with open(path, 'rb') as f:
                data = f.read()
            self.assertEqual(data[: len(head)], head)
            self.assertEqual(len(data), len(head) + 8 + len(self.moov) + 13)
            self.assertEqual(struct.unpack_from('>I4s', data, len(head)), (8 + len(self.moov) + 13, b'moov'))
            self.assertIn(b'st3d', data)
            self.assertNotIn(b'sv3d', data)
            # an index that is not the last part (the samples' places would move) is left alone
            path = self.write(d, b(b'moov', self.moov) + head)
            with self.assertRaises(ValueError):
                youtube3d.mark_file(path, True)
            path = self.write(d, head)
            with self.assertRaises(ValueError):
                youtube3d.mark_file(path, True)

    def test_broken_box(self):
        with self.assertRaises(ValueError):
            list(youtube3d.children(struct.pack('>I4s', 4000, b'trak') + bytes(20)))


class Sizes(unittest.TestCase):
    def test_from_the_mods_json(self):
        with tempfile.TemporaryDirectory() as d:
            side = {'layout': 'left_right', 'eye_width': 3200, 'eye_height': 3200}
            with open(os.path.join(d, 'v.json'), 'w', encoding='utf-8') as f:
                json.dump(side, f)
            self.assertEqual(youtube3d.eyes(os.path.join(d, 'v.mp4')), (3200, 3200, []))
            eye = {'tan_left': -1.0, 'tan_right': 0.5, 'tan_up': 0.75, 'tan_down': -0.25}
            side.update(fov_known=True, eyes=[eye, eye])
            with open(os.path.join(d, 'v.json'), 'w', encoding='utf-8') as f:
                json.dump(side, f)
            self.assertEqual(youtube3d.eyes(os.path.join(d, 'v.mp4')), (3200, 3200, [(-1.0, 0.5, 0.75, -0.25)] * 2))


if __name__ == '__main__':
    unittest.main()
