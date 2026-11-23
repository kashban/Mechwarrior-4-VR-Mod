"""A video of the mod (Ctrl+F5: both eyes side by side, each at its full size) made into what YouTube shows as 3D.

    python tools/video/youtube3d.py <mw4vr_videos/mw4vr_....mp4> [--fit whole|fill] [--height 2160|4320]
                                    [--down 0.3] [--from 12.5] [--to 95] [--out file.mkv] [--encoder hevc_nvenc]

YouTube takes 3D as one 16:9 picture with the left eye in its left half and the right eye in its right half, each
half squeezed to half its width (the player pulls each to the full width again), in a file that says
stereo_mode=left_right. An eye's picture of the mod is square, so it does not fill 16:9:
    --fit whole   all of the eye's picture, black to its left and right (the default)
    --fit fill    a 16:9 part of it over the whole width; top and bottom are cut off. --down says which part:
                  -1 the top, 0 the middle, 1 the bottom (the cockpit's dashboard)
--height: the picture's height, 2160 (3840 x 2160) or 4320 (7680 x 4320, for a video of render_height 3200 and more).
The sizes are read from the .json beside the video, or from the video itself. Needs ffmpeg and ffprobe.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys


def even(x: float) -> int:
    """The even number nearest to x (video sizes are even), at least 2."""
    return max(2, 2 * round(x / 2))


def eye_filter(eye_w: int, eye_h: int, x0: int, out_h: int, fit: str, down: float) -> str:
    """The filters that make one eye's half of the 16:9 picture from its part of the video (it begins at x0).

    The half is out_h * 8 / 9 wide and stands for a picture twice as wide.
    """
    half_w = even(out_h * 8 / 9)
    if fit == 'fill':
        # the part of the eye's picture that is 16:9: as wide as the picture or, of a wider one, as high
        cw, ch = eye_w, even(eye_w * 9 / 16)
        if ch > eye_h:
            cw, ch = even(eye_h * 16 / 9), eye_h
        x = x0 + (eye_w - cw) // 2
        y = round((eye_h - ch) * (1 + max(-1.0, min(1.0, down))) / 2)
        return 'crop=%d:%d:%d:%d,scale=%d:%d:flags=lanczos' % (cw, ch, x, y, half_w, out_h)
    # all of it: as high as the picture or, of a wider one, as wide - and half as wide as it is shown
    w, h = even(out_h * eye_w / eye_h / 2), out_h
    if w > half_w:
        w, h = half_w, even(half_w * 2 * eye_h / eye_w)
    return 'crop=%d:%d:%d:0,scale=%d:%d:flags=lanczos,pad=%d:%d:%d:%d:black' % (
        eye_w,
        eye_h,
        x0,
        w,
        h,
        half_w,
        out_h,
        (half_w - w) // 2,
        (out_h - h) // 2,
    )


def graph(eye_w: int, eye_h: int, out_h: int, fit: str, down: float) -> str:
    """ffmpeg's filter graph: the video split into its eyes, each made into its half, the halves side by side."""
    left = eye_filter(eye_w, eye_h, 0, out_h, fit, down)
    right = eye_filter(eye_w, eye_h, eye_w, out_h, fit, down)
    return '[0:v]split[a][b];[a]%s[l];[b]%s[r];[l][r]hstack,setsar=1[v]' % (left, right)


def command(src: str, dst: str, eye_w: int, eye_h: int, a: argparse.Namespace) -> list[str]:
    """The call of ffmpeg."""
    cmd = ['ffmpeg', '-hide_banner', '-y']
    if a.start is not None:
        cmd += ['-ss', '%g' % a.start]
    if a.end is not None:
        cmd += ['-to', '%g' % a.end]
    cmd += ['-i', src, '-filter_complex', graph(eye_w, eye_h, a.height, a.fit, a.down), '-map', '[v]', '-map', '0:a?']
    cmd += ['-c:v', a.encoder]
    if a.encoder.endswith('_nvenc'):
        cmd += [
            '-preset',
            'p6',
            '-rc',
            'vbr',
            '-cq',
            '20',
            '-b:v',
            '0',
            '-maxrate',
            '%dM' % (80 * (a.height // 2160) ** 2),
        ]
    else:
        cmd += ['-crf', '18']
    return [*cmd, '-pix_fmt', 'yuv420p', '-c:a', 'copy', '-metadata:s:v:0', 'stereo_mode=left_right', dst]


def eye_size(src: str) -> tuple[int, int]:
    """One eye's part of the video: from the .json the mod wrote beside it, else half of the video's own size."""
    side = os.path.splitext(src)[0] + '.json'
    if os.path.exists(side):
        with open(side, encoding='utf-8') as f:
            d = json.load(f)
        if d.get('layout') == 'left_right' and d.get('eye_width') and d.get('eye_height'):
            return int(d['eye_width']), int(d['eye_height'])
    r = subprocess.run(
        [
            'ffprobe',
            '-v',
            'error',
            '-select_streams',
            'v:0',
            '-show_entries',
            'stream=width,height',
            '-of',
            'json',
            src,
        ],
        capture_output=True,
        text=True,
        check=True,
    )
    s = json.loads(r.stdout)['streams'][0]
    return int(s['width']) // 2, int(s['height'])


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description="A video of the mod made into YouTube's 3D layout.")
    p.add_argument('video')
    p.add_argument('--fit', choices=['whole', 'fill'], default='whole')
    p.add_argument('--height', type=int, choices=[2160, 4320], default=2160)
    p.add_argument(
        '--down', type=float, default=0.0, help='with --fit fill: -1 the top part, 0 the middle, 1 the bottom'
    )
    p.add_argument('--from', dest='start', type=float, help='seconds: where to begin')
    p.add_argument('--to', dest='end', type=float, help='seconds: where to end')
    p.add_argument('--out', help='the file to write (default: beside the video, <name>_3d_<fit>_<height>.mkv)')
    p.add_argument('--encoder', default='hevc_nvenc', help='an ffmpeg video encoder (hevc_nvenc, libx265, ...)')
    a = p.parse_args(argv)
    if not os.path.exists(a.video):
        print('no such file: %s' % a.video)
        return 2
    eye_w, eye_h = eye_size(a.video)
    dst = a.out or '%s_3d_%s_%d.mkv' % (os.path.splitext(a.video)[0], a.fit, a.height)
    print(
        "each eye %d x %d -> %s, %d x %d (YouTube's 3D: left | right)"
        % (eye_w, eye_h, dst, even(a.height * 16 / 9), a.height)
    )
    return subprocess.run(command(a.video, dst, eye_w, eye_h, a), check=False).returncode


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
