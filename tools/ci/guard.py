# What must never be in this repository, checked by the pipeline (and by hand: python tools/ci/guard.py):
#   - files of MechWarrior 5 or of its mods (Piranha Games' and the mod makers' work), or anything made from them
#     that still holds their shapes and pictures: by the file's ending, in the files of now and in all of history
#   - large files (a picture, a model, a package that belongs into a release, not into the source)
#   - private data in a text: a path into a user's folder, an address of a home network, a mail address
# python tools/ci/guard.py [--history]     exit 1 with the list of what was found
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# endings of the game's files, of what the export and the converter make of them, and of packages
ASSETS = (
    '.mwc',
    '.mwd',
    '.uasset',
    '.uexp',
    '.ubulk',
    '.umap',
    '.pak',
    '.utoc',
    '.ucas',
    '.psk',
    '.pskx',
    '.psa',
    '.gltf',
    '.glb',
    '.bin',
    '.props.txt',
    '.zip',
    '.7z',
    '.dll',
    '.exe',
)
MAX_BYTES = 1024 * 1024
MAILS_ALLOWED = ('noreply@anthropic.com', 'git@github.com')
PRIVATE = [
    (
        "a path into a user's folder",
        re.compile(
            r'(?i)(?:(?:[a-z]:[\\/]+|/[a-z]/)users[\\/]+|/home/)(?!public\b|you\b|name\b|user\b|runner\b)[\w.-]+'
        ),
    ),
    (
        'an address of a private network',
        re.compile(r'(?<![\d.])(?:192\.168|10\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01]))\.\d{1,3}\.\d{1,3}(?![\d.])'),
    ),
    ('a mail address', re.compile(r'[\w.+-]+@[\w-]+(?:\.[\w-]+)*\.[a-z]{2,}\b', re.IGNORECASE)),
]


def asset(path):
    """the ending that makes the file one that may not be here, or None"""
    p = path.lower()
    for e in ASSETS:
        if p.endswith(e):
            return e
    return None


def check_names(files):
    """files: (path, bytes or None for one of history) -> findings (path, what)"""
    found = []
    for path, size in files:
        e = asset(path)
        if e:
            found.append((path, 'a {} file'.format(e)))
        elif size is not None and size > MAX_BYTES:
            found.append((path, '%.1f MB - more than %d MB' % (size / MAX_BYTES, MAX_BYTES // (1024 * 1024))))
    return found


def check_text(path, text):
    """-> findings (path:line, what) of private data in a text"""
    found = []
    for n, line in enumerate(text.split('\n'), 1):
        for what, rx in PRIVATE:
            for m in rx.finditer(line):
                if m.group(0).lower() in MAILS_ALLOWED:
                    continue
                found.append(('%s:%d' % (path, n), '{}: {}'.format(what, m.group(0))))
    return found


def git(*args):
    return subprocess.run(('git', *args), cwd=ROOT, capture_output=True, check=True).stdout.decode('utf-8', 'replace')


def tracked():
    return [p for p in git('ls-files', '-z').split('\0') if p]


def history_names():
    """every path a commit of any branch or tag ever held"""
    return sorted({p for p in git('log', '--all', '--format=', '--name-only', '-z').split('\0') if p.strip()})


def scan(history=False):
    paths = tracked()
    found = check_names(
        [(p, os.path.getsize(os.path.join(ROOT, p))) for p in paths if os.path.exists(os.path.join(ROOT, p))]
    )
    for p in paths:
        try:
            with open(os.path.join(ROOT, p), encoding='utf-8') as f:
                text = f.read()
        except (OSError, UnicodeDecodeError):  # no text: a picture
            continue
        found += check_text(p, text)
    if history:
        now = set(paths)
        found += [
            (p + ' (in history)', what) for p, what in check_names([(p, None) for p in history_names() if p not in now])
        ]
    return found


def main(argv):
    found = scan('--history' in argv)
    for where, what in found:
        print('{}  {}'.format(where, what))
    print('guard: %d found' % len(found))
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
