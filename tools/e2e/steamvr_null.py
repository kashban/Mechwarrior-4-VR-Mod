"""Sets SteamVR's settings file up for a run without a headset: its "null" driver as the only one.

    python tools/e2e/steamvr_null.py <steamvr.vrsettings> [width height]

The file is changed in place; tools/e2e/run.ps1 keeps a copy and puts it back after the run.
"""
import json
import sys


def with_null_driver(settings, width=1512, height=1680):
    """The settings with the null driver switched on and no headset asked for (the argument is not changed)."""
    out = json.loads(json.dumps(settings))
    steamvr = out.setdefault('steamvr', {})
    steamvr.update({'requireHmd': False, 'forcedDriver': 'null', 'activateMultipleDrivers': False, 'enableHomeApp': False,
                    'startMonitorOnStartup': False, 'showMirrorView': False})
    out.setdefault('driver_null', {}).update({'enable': True, 'renderWidth': width, 'renderHeight': height, 'displayFrequency': 90.0})
    return out


def main(argv):
    if len(argv) not in (2, 4):
        print(__doc__)
        return 2
    with open(argv[1], encoding='utf-8') as f:
        settings = json.load(f)
    size = [int(a) for a in argv[2:4]]
    with open(argv[1], 'w', encoding='utf-8') as f:
        json.dump(with_null_driver(settings, *size), f, indent=3)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
