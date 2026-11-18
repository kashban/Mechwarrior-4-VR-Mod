"""Judges an end-to-end test run of the mod in the game.

    python tools/e2e/judge.py <mw4vr_e2e.csv> <mw4vr.log> [--cockpit NAME] [--seconds N]
        [--walk A:B] [--fire A:B] [--outside A:B] [--menu A:B]

The four windows (seconds of the plan, from a key going down to the next thing) say what the plan's keys were to
do there: the mech walks, it fires, the camera is outside, the paused battle's menu is up.

The mod writes a line for every frame to mw4vr_e2e.csv while a test runs (mod/src/e2e.inc): the time, both eyes'
mean colours, whether the camera sits in the mech, whether a cockpit of ours is drawn, the head's turn the plan
asked for, and the cockpit's sway. This script reads it and the mod's log and says what is wrong.
Exit code: 0 all checks passed, 1 at least one failed, 2 the files could not be read.
"""
import argparse
import csv
import statistics
import sys

FLASH = 18.0       # an eye's brightness this far from its neighbours' median, for one or two frames: a flash
DARK = 3.0         # an eye's brightness below this: black
STILL_SWAY = 0.01  # metres the cockpit may wander while only the head turns
WALK_SWAY = 0.004  # metres the cockpit sways at least with the mech's steps
FIRE_GLOW = 1.5    # the least an eye brightens while the mech fires
SETTLE = 1.0       # seconds a key may take to show its effect
SHARE = 0.8        # of a window's frames must show what the window is for


def window(text):
    """'13:18' -> (13.0, 18.0); None stays None."""
    if not text:
        return None
    a, b = text.split(':')
    return float(a), float(b)


def within(frames, a, b):
    return [f for f in frames if a <= f['t'] <= b]


def brightness(rgb):
    """What the eye takes as brightness, 0..255, from mean red, green and blue."""
    r, g, b = rgb
    return 0.299 * r + 0.587 * g + 0.114 * b


def read_frames(path):
    """The frames of the csv as dicts with numbers; the cockpit's name stays text."""
    frames = []
    with open(path, newline='', encoding='utf-8', errors='replace') as f:
        for row in csv.DictReader(f):
            try:
                frame = {k: float(v) for k, v in row.items() if k != 'cockpit'}
            except (TypeError, ValueError):
                continue  # the last line may be cut off: the game was ended while writing
            frame['cockpit'] = row.get('cockpit') or ''
            frame['left'] = brightness((frame['lr'], frame['lg'], frame['lb']))
            frame['right'] = brightness((frame['rr'], frame['rg'], frame['rb']))
            frames.append(frame)
    return frames


def flashes(frames, eye, limit=FLASH):
    """Frames whose brightness jumps away from the frames around them and comes back (the flicker).

    A frame counts when it is further than `limit` from the median of the three frames before and after it while
    those two groups agree with each other - a cut to another view moves both, a flash only the frame itself.
    """
    found = []
    values = [f[eye] for f in frames]
    for i in range(3, len(values) - 3):
        before, after = values[i - 3:i], values[i + 1:i + 4]
        around = statistics.median(before + after)
        steady = abs(statistics.median(before) - statistics.median(after)) < limit / 2
        if steady and abs(values[i] - around) > limit:
            found.append(int(frames[i]['frame']))
    return found


def judge(frames, log_text, cockpit=None, seconds=10.0, walk=None, fire=None, outside=None, menu=None):
    """-> (passed, failed): lists of lines saying what was checked."""
    passed, failed = [], []

    def check(ok, text):
        (passed if ok else failed).append(text)

    check('MW4VR v' in log_text, 'the mod was loaded (its log has a version line)')
    bad = [line for line in log_text.splitlines() if 'ERROR' in line or 'exception 0x' in line]
    check(not bad, 'no error in the log' + (': ' + bad[0][:120] if bad else ''))

    timed = [f for f in frames if f['t'] >= 0]
    seated = [f for f in timed if f['seated']]
    check(bool(seated), 'the camera sat in the mech (%d of %d frames)' % (len(seated), len(frames)))
    if not seated:
        return passed, failed

    span = seated[-1]['t'] - seated[0]['t']
    check(span >= seconds, 'the battle ran for %.1f s (wanted: %.0f s)' % (span, seconds))
    fps = (len(seated) - 1) / span if span > 0 else 0.0
    check(fps >= 30, '%.0f frames a second with both eyes' % fps)

    for eye in ('left', 'right'):
        dark = [int(f['frame']) for f in seated if f[eye] < DARK]
        check(not dark, 'the %s eye is never black' % eye + (' - black at frames %s' % dark[:8] if dark else ''))
        hits = flashes(seated, eye)
        check(not hits, 'the %s eye does not flash' % eye + (' - %d flashes, at frames %s' % (len(hits), hits[:8]) if hits else ''))
    apart = statistics.mean(abs(f['left'] - f['right']) for f in seated)
    check(apart < 25, 'both eyes show the same scene (brightness %.1f apart on average)' % apart)

    if cockpit:
        own = [f for f in seated if f['own']]
        first = own[0]['t'] if own else None
        names = sorted({f['cockpit'].lower() for f in own})
        check(bool(own) and first <= 2.0, "a cockpit of ours is drawn within 2 s of sitting down" +
              (' (after %.1f s)' % first if own else ' (never)'))
        check(names == [cockpit.lower()], "it is '%s' (seen: %s)" % (cockpit, ', '.join(names) or 'none'))

    turned = [f for f in seated if abs(f['yaw']) > 1]
    if turned:
        # (at rest right before and after the turns - not while the cockpit still settles after the mech was set
        # down, some seconds, and not what the plan does later: walking sways it)
        still = [f for f in seated if abs(f['yaw']) <= 1 and turned[0]['t'] - 2.0 <= f['t'] <= turned[-1]['t'] + 1.0]
        rest = [statistics.mean(f[k] for f in still) if still else 0.0 for k in ('sx', 'sy', 'sz')]
        wander = max(max(abs(f[k] - r) for k, r in zip(('sx', 'sy', 'sz'), rest)) for f in turned)
        check(wander <= STILL_SWAY, 'the cockpit stays put while only the head turns (wandered %.1f cm)' % (wander * 100))

    if walk:
        part = within(seated, walk[0] + SETTLE, walk[1])
        most = max((max(abs(f[k]) for k in ('sx', 'sy', 'sz')) for f in part), default=0.0)
        check(most >= WALK_SWAY, 'the mech walks: the cockpit sways with its steps (%.1f mm at most)' % (most * 1000))
    if fire:
        before = within(seated, fire[0] - 2.0, fire[0] - 0.1)
        part = within(seated, fire[0], fire[1])
        # (the beams and flashes brighten the picture a little; the head is at rest here. Counting draws tells nothing:
        # a laser's beam adds none.)
        was = statistics.median(f['left'] for f in before) if before else 0.0
        now = max((f['left'] for f in part), default=0.0)
        check(bool(before) and now >= was + FIRE_GLOW, 'the mech fires: the picture brightens (%.1f before, up to %.1f while firing)' % (was, now))
    if outside:
        part = within(timed, outside[0] + SETTLE, outside[1])
        out = [f for f in part if f['outside'] and not f['own']]
        check(bool(part) and len(out) >= SHARE * len(part),
              'seen from outside no cockpit of ours is drawn (%d of %d frames)' % (len(out), len(part)))
        back = within(timed, outside[1] + SETTLE, outside[1] + SETTLE + 2.0)
        inside = [f for f in back if f['seated'] and (f['own'] or not cockpit)]
        check(bool(back) and len(inside) >= SHARE * len(back), 'back in the mech afterwards (%d of %d frames)' % (len(inside), len(back)))
    if menu:
        part = within(timed, menu[0] + SETTLE, menu[1])
        up = [f for f in part if f['menu']]
        check(bool(part) and len(up) >= SHARE * len(part), "the paused battle's menu is shown as one screen (%d of %d frames)" % (len(up), len(part)))
        first = within(timed, menu[0], menu[0] + SETTLE)
        late = [f for f in first if f['menu']]
        check(bool(late) and late[0]['t'] - menu[0] <= 0.5, 'it comes up within half a second' +
              (' (after %.2f s)' % (late[0]['t'] - menu[0]) if late else ' (not within %.0f s)' % SETTLE))
        after = within(timed, menu[1] + SETTLE, menu[1] + SETTLE + 2.0)
        gone = [f for f in after if not f['menu']]
        check(bool(after) and len(gone) >= SHARE * len(after), 'and is gone afterwards (%d of %d frames)' % (len(gone), len(after)))
    return passed, failed


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('csv')
    parser.add_argument('log')
    parser.add_argument('--cockpit', help="the cockpit file's name that must be drawn")
    parser.add_argument('--seconds', type=float, default=10.0, help='how long the battle must have run')
    for name, what in (('walk', 'the mech walks'), ('fire', 'it fires'), ('outside', 'the camera is outside'), ('menu', "the menu is up")):
        parser.add_argument('--' + name, metavar='A:B', help='seconds of the plan in which ' + what)
    args = parser.parse_args(argv)
    try:
        frames = read_frames(args.csv)
        with open(args.log, encoding='utf-8', errors='replace') as f:
            log_text = f.read()
    except OSError as e:
        print('cannot read: %s' % e)
        return 2
    passed, failed = judge(frames, log_text, args.cockpit, args.seconds, window(args.walk), window(args.fire), window(args.outside),
                           window(args.menu))
    for line in passed:
        print('ok    ' + line)
    for line in failed:
        print('FAIL  ' + line)
    print('%d checks, %d failed' % (len(passed) + len(failed), len(failed)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
