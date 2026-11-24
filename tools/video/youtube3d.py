"""A video of the mod (Ctrl+F5: both eyes side by side, each at its full size) made into what YouTube shows as 3D.

    python tools/video/youtube3d.py <mw4vr_videos/mw4vr_....mp4> [--fit vr180|whole|fill] [--height 4096]
                                    [--fov 100.7] [--down 0.3] [--from 12.5] [--to 95] [--out file.mp4]
                                    [--encoder hevc_nvenc]

YouTube knows a video as 3D by two boxes in the .mp4 (Google's "Spherical Video V2": st3d says left | right, sv3d
says which part of a sphere the picture is); this script writes them. A mark in an .mkv is not enough.
    --fit vr180   (the default) what a headset shows around the viewer: each eye's picture laid onto the half of a
                  sphere in front of them ("VR180"), black where the game drew nothing. It needs the eyes' fields
                  of view: from the .json the mod wrote beside the video, or --fov (degrees, across and up alike).
    --fit whole   a flat 16:9 picture with depth: all of the eye's square picture, black to its left and right
    --fit fill    the same with a 16:9 part of the eye's picture over the whole width; top and bottom are cut off.
                  --down says which part: -1 the top, 0 the middle, 1 the bottom (the cockpit's dashboard)
--height: the picture's height. vr180: by default what keeps the video's sharpness, 4096 at most (8192 x 4096).
whole and fill: 2160 (3840 x 2160, the default) or 4320 (7680 x 4320, for a video of render_height 3200 and more).
Needs ffmpeg and ffprobe.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
import subprocess
import sys
from collections.abc import Iterator

Tans = tuple[float, float, float, float]  # an eye's field of view: the tangents left (negative), right, up, down


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


EDGE = 16  # black around an eye's picture before it goes onto the sphere


def sphere_filter(eye_w: int, eye_h: int, x0: int, tans: Tans, size: int) -> str:
    """The filters that lay one eye's part of the video (it begins at x0) onto its half of a sphere.

    The result is size x size and stands for 180 degrees across and up. ffmpeg's v360 takes a picture whose middle
    is straight ahead, so one that reaches further to one side is first widened with black to the other. And it
    paints all that lies outside the picture in the colour of a pixel at its edge - a colour that changes with every
    picture - so the picture gets a black edge first.
    """
    left, right, up, down = tans
    th, tv = max(-left, right), max(up, -down)
    ph, pv = eye_w / (right - left), eye_h / (up - down)  # pixels for a tangent of 1
    w, h = max(eye_w, even(2 * th * ph)), max(eye_h, even(2 * tv * pv))
    x, y = min(w - eye_w, round((th + left) * ph)), min(h - eye_h, round((tv - up) * pv))
    w, h = w + 2 * EDGE, h + 2 * EDGE
    return (
        'crop=%d:%d:%d:0,pad=%d:%d:%d:%d:black,v360=input=flat:ih_fov=%.3f:iv_fov=%.3f:output=hequirect:w=%d:h=%d:interp=lanczos'
        % (
            eye_w,
            eye_h,
            x0,
            w,
            h,
            x + EDGE,
            y + EDGE,
            2 * math.degrees(math.atan(w / 2 / ph)),
            2 * math.degrees(math.atan(h / 2 / pv)),
            size,
            size,
        )
    )


def sphere_size(eye_h: int, tans: Tans) -> int:
    """The height of the half sphere that keeps what the video shows straight ahead: 4096 at most, a multiple of 16."""
    per_radian = eye_h / (tans[2] - tans[3])
    return max(1024, min(4096, 16 * round(per_radian * math.pi / 16)))


def graph(left: str, right: str) -> str:
    """ffmpeg's filter graph: the video split into its eyes, each through its filters, the results side by side."""
    return '[0:v]split[a][b];[a]%s[l];[b]%s[r];[l][r]hstack,setsar=1[v]' % (left, right)


def command(src: str, dst: str, filters: str, pixels: int, a: argparse.Namespace) -> list[str]:
    """The call of ffmpeg: filters is the filter graph, pixels the size of a picture it makes."""
    cmd = ['ffmpeg', '-hide_banner', '-y']
    if a.start is not None:
        cmd += ['-ss', '%g' % a.start]
    if a.end is not None:
        cmd += ['-to', '%g' % a.end]
    cmd += ['-i', src, '-filter_complex', filters, '-map', '[v]', '-map', '0:a?', '-c:v', a.encoder]
    if a.encoder.endswith('_nvenc'):
        cmd += ['-preset', 'p6', '-rc', 'vbr', '-cq', '20', '-b:v', '0']
        cmd += ['-maxrate', '%dM' % round(80 * pixels / (3840 * 2160))]
    else:
        cmd += ['-crf', '18']
    if 'hevc' in a.encoder or '265' in a.encoder:
        cmd += ['-tag:v', 'hvc1']
    return [*cmd, '-pix_fmt', 'yuv420p', '-c:a', 'copy', dst]


# --- the boxes that say "3D" in an .mp4 (https://github.com/google/spatial-media, docs/spherical-video-v2-rfc.md)


def box(kind: bytes, payload: bytes) -> bytes:
    return struct.pack('>I4s', 8 + len(payload), kind) + payload


def marks(sphere: bool) -> bytes:
    """st3d: the picture is left eye | right eye. With sphere, sv3d too: each eye's is the front half of a sphere."""
    out = box(b'st3d', bytes(4) + bytes([2]))
    if sphere:
        # equirectangular, of which a quarter is left out at the left and at the right (0.32 fixed point)
        equi = box(b'equi', bytes(4) + struct.pack('>IIII', 0, 0, 0x40000000, 0x40000000))
        proj = box(b'proj', box(b'prhd', bytes(4) + struct.pack('>iii', 0, 0, 0)) + equi)
        out += box(b'sv3d', box(b'svhd', bytes(4) + b'mw4vr\0') + proj)
    return out


def children(buf: bytes) -> Iterator[tuple[bytes, int, int]]:
    """The boxes in a box's content: kind, where it begins, its size."""
    pos = 0
    while pos + 8 <= len(buf):
        size, kind = struct.unpack_from('>I4s', buf, pos)
        if size < 8 or pos + size > len(buf):
            raise ValueError('a box of the .mp4 is not as expected (%r, size %d)' % (kind, size))
        yield kind, pos, size
        pos += size


def find(buf: bytes, kind: bytes) -> bytes:
    """The content of the first box of this kind, or nothing."""
    for k, pos, size in children(buf):
        if k == kind:
            return buf[pos + 8 : pos + size]
    return b''


def mark_content(buf: bytes, path: list[bytes], extra: bytes) -> bytes | None:
    """buf (a box's content) with extra added to the video track's picture description; path leads to its list."""
    for kind, pos, size in children(buf):
        if kind != path[0]:
            continue
        inner: bytes | None = buf[pos + 8 : pos + size]
        assert inner is not None
        if kind == b'trak' and find(find(inner, b'mdia'), b'hdlr')[8:12] != b'vide':
            continue
        if len(path) > 1:
            inner = mark_content(inner, path[1:], extra)
        else:
            # stsd: version and flags, the number of descriptions, the descriptions - the first one grows
            (n,) = struct.unpack_from('>I', inner, 8)
            if b'st3d' in inner[8 : 8 + n]:
                return buf
            inner = inner[:8] + struct.pack('>I', n + len(extra)) + inner[12 : 8 + n] + extra + inner[8 + n :]
        if inner is not None:
            return buf[:pos] + box(kind, inner) + buf[pos + size :]
    return None


def mark_file(path: str, sphere: bool) -> None:
    """Writes the boxes into an .mp4 whose index (moov) is its last part, as ffmpeg writes it: only that changes."""
    with open(path, 'r+b') as f:
        end = f.seek(0, os.SEEK_END)
        pos = 0
        while pos + 8 <= end:
            f.seek(pos)
            size, kind = struct.unpack('>I4s', f.read(8))
            if size == 1:
                (size,) = struct.unpack('>Q', f.read(8))
            elif size == 0:
                size = end - pos
            if size < 8:
                break
            if kind == b'moov':
                if pos + size != end:
                    raise ValueError("the .mp4's index is not at its end")
                f.seek(pos + 8)
                new = mark_content(f.read(size - 8), [b'trak', b'mdia', b'minf', b'stbl', b'stsd'], marks(sphere))
                if new is None:
                    raise ValueError('the .mp4 has no video track')
                f.seek(pos)
                f.write(box(b'moov', new))
                return
            pos += size
    raise ValueError('the .mp4 has no index (moov)')


# --- the video's sizes


def probe_size(src: str) -> tuple[int, int]:
    cmd = ['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'stream=width,height', '-of', 'json']
    r = subprocess.run([*cmd, src], capture_output=True, text=True, check=True)
    s = json.loads(r.stdout)['streams'][0]
    return int(s['width']), int(s['height'])


def eyes(src: str) -> tuple[int, int, list[Tans]]:
    """One eye's part of the video and, where known, each eye's field of view: from the .json the mod wrote beside
    it, else half of the video's own size and nothing."""
    side = os.path.splitext(src)[0] + '.json'
    if os.path.exists(side):
        with open(side, encoding='utf-8') as f:
            d = json.load(f)
        if d.get('layout') == 'left_right' and d.get('eye_width') and d.get('eye_height'):
            tans: list[Tans] = []
            if d.get('fov_known') and len(d.get('eyes', [])) == 2:
                tans = [(e['tan_left'], e['tan_right'], e['tan_up'], e['tan_down']) for e in d['eyes']]
            return int(d['eye_width']), int(d['eye_height']), tans
    w, h = probe_size(src)
    return w // 2, h, []


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description="A video of the mod made into YouTube's 3D layout.")
    p.add_argument('video')
    p.add_argument('--fit', choices=['vr180', 'whole', 'fill'], default='vr180')
    p.add_argument('--height', type=int, help='vr180: 4096 at most; whole and fill: 2160 or 4320')
    p.add_argument('--fov', type=float, help="vr180: an eye's field of view in degrees, if no .json says it")
    p.add_argument(
        '--down', type=float, default=0.0, help='with --fit fill: -1 the top part, 0 the middle, 1 the bottom'
    )
    p.add_argument('--from', dest='start', type=float, help='seconds: where to begin')
    p.add_argument('--to', dest='end', type=float, help='seconds: where to end')
    p.add_argument('--out', help='the file to write (default: beside the video, <name>_3d_<fit>_<height>.mp4)')
    p.add_argument('--encoder', default='hevc_nvenc', help='an ffmpeg video encoder (hevc_nvenc, libx265, ...)')
    a = p.parse_args(argv)
    if not os.path.exists(a.video):
        print('no such file: %s' % a.video)
        return 2
    eye_w, eye_h, tans = eyes(a.video)
    if a.fit == 'vr180':
        if a.fov:
            t = math.tan(math.radians(min(a.fov, 170.0)) / 2)
            tans = [(-t, t, t, -t)] * 2
        if not tans:
            print("the eyes' field of view is not known (no .json beside the video): give --fov <degrees>")
            return 2
        h = a.height or sphere_size(eye_h, tans[0])
        if h > 4096 or h % 2:
            print('--height: an even number, 4096 at most')
            return 2
        w = 2 * h
        filters = graph(sphere_filter(eye_w, eye_h, 0, tans[0], h), sphere_filter(eye_w, eye_h, eye_w, tans[1], h))
    else:
        h = a.height or 2160
        if h not in (2160, 4320):
            print('--height: 2160 or 4320')
            return 2
        w = 2 * even(h * 8 / 9)
        filters = graph(
            eye_filter(eye_w, eye_h, 0, h, a.fit, a.down), eye_filter(eye_w, eye_h, eye_w, h, a.fit, a.down)
        )
    dst = a.out or '%s_3d_%s_%d.mp4' % (os.path.splitext(a.video)[0], a.fit, h)
    if not dst.lower().endswith('.mp4'):
        print('--out: an .mp4 (YouTube reads the 3D marks from that)')
        return 2
    print("each eye %d x %d -> %s, %d x %d (YouTube's 3D: left | right)" % (eye_w, eye_h, dst, w, h))
    code = subprocess.run(command(a.video, dst, filters, w * h, a), check=False).returncode
    if code == 0:
        mark_file(dst, a.fit == 'vr180')
    return code


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
