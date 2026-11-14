# Do the tests find bugs? Plants one small error at a time into a copy of a source file and runs its tests:
# a mutant is killed when a test fails (or the copy no longer compiles), a survivor went unnoticed. The score
# is the share killed.
#   python tests/mutate.py            all scopes; fails (exit 1) when a score is below its floor
#   python tests/mutate.py core       the scopes named
# The floors (tests/mutation_floor.json) only rise: when new tests lift a score, raise its floor to it.
#
# A scope: the file; how its tests are run against a copy of it (command, environment); the comment sign;
# which operators; and what is left alone - lines (skip), blocks from a line to a line (skip_blocks), parts
# of lines (protect: (lines matching, parts matching)).
import json, os, re, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
VCVARS = r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat'

# (pattern, replacement): each match on a line is one mutant
COMMON = [
    (r' <= ', ' < '), (r' >= ', ' > '), (r' < ', ' <= '), (r' > ', ' >= '),
    (r' == ', ' != '), (r' != ', ' == '),
    (r' \+ ', ' - '), (r' - ', ' + '), (r' \* ', ' / '), (r' / ', ' * '),
    (r' \+= ', ' -= '), (r' -= ', ' += '), (r' \*= ', ' /= '), (r' /= ', ' *= '),
]
PYTHON = COMMON + [
    (r' // ', ' * '), (r' //= ', ' *= '),
    (r'\bmin\(', 'max('), (r'\bmax\(', 'min('),
    (r' and ', ' or '), (r' or ', ' and '), (r'\bnot ', ''),
    (r'\bTrue\b', 'False'), (r'\bFalse\b', 'True'),
    (r'\bis None\b', 'is not None'), (r'\bis not None\b', 'is None'),
    (r'(?<![\w.])(\d+)(?![\w.])', lambda m: str(int(m.group(1)) + 1)),  # an integer, one more
    (r'(?<![\w.])(\d+\.\d+)(?![\w.])', lambda m: repr(float(m.group(1)) + 1)),
    (r'\.min\(', '.max('), (r'\.max\(', '.min('),
    (r'\bcontinue\b', 'pass'), (r'\breturn None\b', 'return 0'),
]
CPP = COMMON + [
    (r' && ', ' || '), (r' \|\| ', ' && '),
    (r'\btrue\b', 'false'), (r'\bfalse\b', 'true'),
    (r'(?<![\w.])(\d+)(?![\w.])', lambda m: str(int(m.group(1)) + 1)),
    (r'(?<![\w.])(\d+\.\d+)f\b', lambda m: repr(float(m.group(1)) + 1) + 'f'),
    (r'(?<![\w.)\]] )(?<![\w.)\]])-(?=[a-zA-Z(])', ''),  # a sign dropped
    (r'\bfmaxf\(', 'fminf('), (r'\bfminf\(', 'fmaxf('), (r'\bfabsf\(', '('),
    (r'\+\+', '--'), (r'\bcontinue;', ';'),
    (r'if \(!\(', 'if (('), (r'if \(!(?!\()', 'if ('),
]

SCOPES = [
    {'name': 'cockpit_convert', 'file': 'tools/cockpit_convert.py', 'ops': PYTHON, 'comment': '#',
     'run': lambda path: ([sys.executable, 'tests/test_convert.py'], {'MW4VR_CONVERT': path} if path else {}),
     'skip': [r'^\s*log\(', r'^\s*#', r"^if __name__", r'^\s*""".*"""\s*$', r'^\s*main\(sys\.argv'],
     'skip_blocks': [(r'^\s*"""[^"]*$', r'"""')]},
    # core.h: the constants' own values (enum) and the structs' layouts are not behaviour a test of the header
    # alone could see; a declared array's size neither (one more element changes nothing)
    {'name': 'core', 'file': 'mod/src/core.h', 'ops': CPP, 'comment': '//', 'vcvars': True, 'timeout': 30,
     'run': lambda path: (['cmd', '/c', 'tests\\core_test.bat'] + ([os.path.dirname(path)] if path else []), {}),
     'skip': [r'^\s*//', r'^\s*#', r'^enum\b'],
     'skip_blocks': [(r'^struct\b', r'^\};')],
     'protect': [(r'^\s*(const |static )*(float|int|bool|unsigned)\b', r'\b\w+(\[\d+\])+(?= = \{|,|;)')]},
]


def code_part(line, sign):
    """the line without a trailing comment (quotes respected roughly)"""
    q = None
    for i, c in enumerate(line):
        if q:
            if c == q:
                q = None
        elif c in '\'"':
            q = c
        elif line.startswith(sign, i):
            return line[:i]
    return line


def mutants(lines, scope):
    """(line number, column, original text, mutated line)"""
    block_end = None
    for n, line in enumerate(lines):
        if block_end:
            if re.search(block_end, line):
                block_end = None
            continue
        for start, end in scope.get('skip_blocks', []):
            if re.search(start, line):
                block_end = end
                break
        if block_end or not line.strip() or any(re.search(p, line) for p in scope['skip']):
            continue
        code = code_part(line, scope['comment'])
        kept = [m.span() for when, what in scope.get('protect', []) if re.search(when, code) for m in re.finditer(what, code)]
        for pat, rep in scope['ops']:
            for m in re.finditer(pat, code):
                if any(a <= m.start() and m.end() <= b for a, b in kept):
                    continue
                new = rep(m) if callable(rep) else rep
                yield n, m.start(), m.group(0).strip(), line[:m.start()] + new + line[m.end():]


def vcvars_env():
    """the environment of a Visual Studio x86 command prompt (asked for once, not per mutant)"""
    out = subprocess.run('cmd /c ""%s" >nul 2>&1 && set"' % VCVARS, capture_output=True, text=True, errors='replace').stdout
    env = dict(line.split('=', 1) for line in out.splitlines() if '=' in line)
    return env if 'VSCMD_VER' in env else dict(os.environ)


def run_tests(scope, path, base_env):
    cmd, extra = scope['run'](path)
    p = subprocess.Popen(cmd, cwd=ROOT, env=dict(base_env, **extra), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        return p.wait(scope.get('timeout', 120))
    except subprocess.TimeoutExpired:  # a mutant that never ends: the whole tree goes (the test program is a grandchild)
        subprocess.run(['taskkill', '/T', '/F', '/PID', str(p.pid)], capture_output=True)
        p.wait()
        return -1


def run_scope(scope):
    src = os.path.join(ROOT, scope['file'])
    with open(src, encoding='utf-8') as f:
        lines = f.read().split('\n')
    env = vcvars_env() if scope.get('vcvars') else dict(os.environ)
    if run_tests(scope, None, env) != 0:
        print('%s: the tests fail on the unchanged file - nothing mutated' % scope['name'])
        return None
    killed, survivors = 0, []
    t0 = time.time()
    with tempfile.TemporaryDirectory() as d:
        def one(job):  # each mutant in a folder of its own, several at a time
            k, (n, col, what, new) = job
            os.mkdir(os.path.join(d, str(k)))
            path = os.path.join(d, str(k), os.path.basename(scope['file']))
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                f.write('\n'.join(lines[:n] + [new] + lines[n + 1:]))
            return run_tests(scope, path, env) != 0, (n + 1, what, new.strip())
        with ThreadPoolExecutor(os.cpu_count() or 4) as pool:
            for dead, which in pool.map(one, enumerate(mutants(lines, scope))):
                if dead:
                    killed += 1
                else:
                    survivors.append(which)
    total = killed + len(survivors)
    score = 100.0 * killed / total if total else 100.0
    print('%s: %d of %d mutants killed, %.1f %% (%.0f s)' % (scope['name'], killed, total, score, time.time() - t0))
    for n, what, new in survivors:
        print('   survived  %s:%d  %-6s -> %s' % (scope['file'], n, what, new[:110]))
    return score


def main():
    if os.name == 'nt':  # a crashing mutant ends quietly, without Windows' error box
        import ctypes
        ctypes.windll.kernel32.SetErrorMode(0x8003)
    names = [a for a in sys.argv[1:] if not a.startswith('-')]
    floor_file = os.path.join(HERE, 'mutation_floor.json')
    floors = {}
    if os.path.exists(floor_file):
        with open(floor_file) as f:
            floors = json.load(f)
    ok = True
    for scope in SCOPES:
        if names and scope['name'] not in names:
            continue
        score = run_scope(scope)
        floor = floors.get(scope['name'], 0)
        if score is None or score + 1e-9 < floor:
            print('   BELOW THE FLOOR of %s %%' % floor)
            ok = False
        elif score >= floor + 1:
            print('   above its floor of %s %%: raise it in tests/mutation_floor.json' % floor)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
