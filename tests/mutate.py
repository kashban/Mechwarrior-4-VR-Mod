# Do the tests find bugs? Plants one small error at a time into a copy of a source file and runs its tests:
# a mutant is killed when a test fails, a survivor went unnoticed. The score is the share killed.
#   python tests/mutate.py            all scopes; fails (exit 1) when a score is below its floor
#   python tests/mutate.py -v         and every survivor
# The floors (tests/mutation_floor.json) only rise: when new tests lift a score, raise its floor to it.
#
# A scope: the file, the environment variable that tells its tests which copy to load, the test module, and
# the lines left alone (output, the command line).
import json, os, re, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCOPES = [
    {'name': 'cockpit_convert', 'file': 'tools/cockpit_convert.py', 'env': 'MW4VR_CONVERT', 'tests': 'tests/test_convert.py',
     'skip': [r'^\s*log\(', r'^\s*#', r"^if __name__", r'^\s*"""', r'^\s*main\(sys\.argv']},
]

# (pattern, replacement): each match on a line is one mutant
OPERATORS = [
    (r' <= ', ' < '), (r' >= ', ' > '), (r' < ', ' <= '), (r' > ', ' >= '),
    (r' == ', ' != '), (r' != ', ' == '),
    (r' \+ ', ' - '), (r' - ', ' + '), (r' \* ', ' / '), (r' / ', ' * '), (r' // ', ' * '),
    (r' \+= ', ' -= '), (r' -= ', ' += '), (r' \*= ', ' /= '), (r' /= ', ' *= '), (r' //= ', ' *= '),
    (r'\bmin\(', 'max('), (r'\bmax\(', 'min('),
    (r' and ', ' or '), (r' or ', ' and '), (r'\bnot ', ''),
    (r'\bTrue\b', 'False'), (r'\bFalse\b', 'True'),
    (r'\bis None\b', 'is not None'), (r'\bis not None\b', 'is None'),
    (r'(?<![\w.])(\d+)(?![\w.])', lambda m: str(int(m.group(1)) + 1)),  # an integer, one more
    (r'(?<![\w.])(\d+\.\d+)(?![\w.])', lambda m: repr(float(m.group(1)) + 1)),
    (r'\.min\(', '.max('), (r'\.max\(', '.min('),
    (r'\bcontinue\b', 'pass'), (r'\breturn None\b', 'return 0'),
]


def code_part(line):
    """the line without a trailing comment (quotes respected roughly)"""
    q = None
    for i, c in enumerate(line):
        if q:
            if c == q:
                q = None
        elif c in '\'"':
            q = c
        elif c == '#':
            return line[:i]
    return line


def mutants(lines, skip):
    """(line number, column, original text, mutated line)"""
    in_doc = False
    for n, line in enumerate(lines):
        stripped = line.strip()
        if stripped.count('"""') == 1:
            in_doc = not in_doc
            continue
        if in_doc or not stripped or any(re.search(p, line) for p in skip):
            continue
        code = code_part(line)
        for pat, rep in OPERATORS:
            for m in re.finditer(pat, code):
                new = rep(m) if callable(rep) else rep
                yield n, m.start(), m.group(0).strip(), line[:m.start()] + new + line[m.end():]


def run_tests(scope, path):
    env = dict(os.environ)
    if path:
        env[scope['env']] = path
    try:
        r = subprocess.run([sys.executable, scope['tests']], cwd=ROOT, env=env, capture_output=True, timeout=120)
        return r.returncode
    except subprocess.TimeoutExpired:
        return -1


def run_scope(scope, verbose):
    src = os.path.join(ROOT, scope['file'])
    with open(src, encoding='utf-8') as f:
        lines = f.read().split('\n')
    if run_tests(scope, None) != 0:
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
            return run_tests(scope, path) != 0, (n + 1, what, new.strip())
        with ThreadPoolExecutor(os.cpu_count() or 4) as pool:
            for dead, which in pool.map(one, enumerate(mutants(lines, scope['skip']))):
                if dead:
                    killed += 1
                else:
                    survivors.append(which)
    total = killed + len(survivors)
    score = 100.0 * killed / total if total else 100.0
    print('%s: %d of %d mutants killed, %.1f %% (%.0f s)' % (scope['name'], killed, total, score, time.time() - t0))
    if verbose or survivors:
        for n, what, new in survivors:
            print('   survived  %s:%d  %-6s -> %s' % (scope['file'], n, what, new[:110]))
    return score


def main():
    verbose = '-v' in sys.argv
    floor_file = os.path.join(HERE, 'mutation_floor.json')
    floors = json.load(open(floor_file)) if os.path.exists(floor_file) else {}
    ok = True
    for scope in SCOPES:
        score = run_scope(scope, verbose)
        floor = floors.get(scope['name'], 0)
        if score is None or score + 1e-9 < floor:
            print('   BELOW THE FLOOR of %s %%' % floor)
            ok = False
        elif score >= floor + 1:
            print('   above its floor of %s %%: raise it in tests/mutation_floor.json' % floor)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
