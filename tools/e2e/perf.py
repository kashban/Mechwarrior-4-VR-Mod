"""Where the frames' time goes: reads mw4vr_perf.csv of a performance run (tools/e2e/run.ps1 -Perf).

    python tools/e2e/perf.py <mw4vr_perf.csv> [--from 2] [--to 60] [--json]

The mod writes a line for every frame (mod/src/perf.cpp): its length and the time of its parts in microseconds.
Printed per part: mean, median, 95th and 99th percentile in milliseconds over the frames in the mech between the
two times of the test plan's clock, and the frames a second that the mean frame length gives.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys

PARTS = ['us', 'eye0', 'eye1', 'grab', 'ours_only', 'wait', 'sys', 'rest', 'hook_only', 'd3d', 'map', 'exec', 'draws']
NAMES = {
    'us': 'the frame',
    'eye0': "left eye's pass",
    'eye1': "right eye's pass",
    'grab': "eyes' pictures copied",
    'ours_only': 'our work at Present',
    'wait': 'xrWaitFrame',
    'sys': "the system's Present",
    'rest': 'the game beside the passes',
    'hook_only': 'of that: our draw hooks',
    'd3d': "Direct3D's draw calls",
    'map': "Direct3D's Map",
    'exec': 'ExecuteCommandList',
    'draws': 'draw calls',
}


def read(path: str, t0: float, t1: float) -> list[dict[str, float]]:
    """The frames in the mech between t0 and t1 of the plan's clock; 'ours_only' and 'rest' worked out."""
    rows = []
    with open(path, newline='') as f:
        for r in csv.DictReader(f):
            try:
                v = {k: float(x) for k, x in r.items()}
            except (TypeError, ValueError):
                continue  # a line cut off when the game was ended
            if not (t0 <= v['t'] < t1) or not v['seated'] or v['outside'] or v['menu']:
                continue
            v['ours_only'] = v['ours'] - v['wait']
            v['hook_only'] = v['hook'] - v['d3d']
            v['rest'] = v['us'] - v['eye0'] - v['eye1'] - v['grab'] - v['ours'] - v['sys']
            rows.append(v)
    return rows


def summary(rows: list[dict[str, float]]) -> dict[str, dict[str, float]]:
    out = {}
    for p in PARTS:
        vals = sorted(r[p] for r in rows)
        n = len(vals)
        out[p] = {
            'mean': sum(vals) / n,
            'median': vals[n // 2],
            'p95': vals[min(n - 1, int(n * 0.95))],
            'p99': vals[min(n - 1, int(n * 0.99))],
        }
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('csv')
    ap.add_argument('--from', dest='t0', type=float, default=2.0)
    ap.add_argument('--to', dest='t1', type=float, default=1e9)
    ap.add_argument('--json', action='store_true')
    a = ap.parse_args()
    rows = read(a.csv, a.t0, a.t1)
    if len(rows) < 30:
        print(f'only {len(rows)} frames in the mech - nothing to say')
        return 2
    s = summary(rows)
    if a.json:
        json.dump({'frames': len(rows), 'parts': s}, sys.stdout, indent=1)
        return 0
    print(f'{len(rows)} frames in the mech, {1e6 / s["us"]["mean"]:.1f} a second')
    print(f'{"":28}{"mean":>8}{"median":>8}{"p95":>8}{"p99":>8}')
    for p in PARTS:
        k = 1.0 if p == 'draws' else 1e-3
        fmt = '{:8.0f}' if p == 'draws' else '{:8.2f}'
        print(
            f'{NAMES[p]:28}'
            + ''.join(fmt.format(s[p][q] * k) for q in ('mean', 'median', 'p95', 'p99'))
            + ('' if p == 'draws' else '  ms')
        )
    return 0


if __name__ == '__main__':
    sys.exit(main())
