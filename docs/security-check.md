# Security check of the mod's code

The findings below are of v11.59 (`cc3f5b0`). **All of them are dealt with in v11.70** - what was done is in the
table; the text of each finding stays as it was found.

| | Finding | Done in v11.70 |
|---|---|---|
| S1 | a monitor's number below zero reads outside a table | the number is parsed in `core.h` (`mcScreen`): 0 .. monitors - 1 only; tests |
| S2 | a cockpit's name goes into paths as it is | `mcNameOk` refuses separators, a colon, two dots and what a file's name cannot hold, wherever a name comes in; tests |
| S3 | a cockpit file can exhaust the process | lower limits for a part and 640 MB for a file (`mcGroupBytes`), `bad_alloc` caught in the loader, a failed file's parts freed; tests |
| S4 | no Control Flow Guard, no `/sdl` | both on (`mod/build.bat`); the 23 uses of the C library's unchecked functions are replaced. BinSkim names no error |
| T1 | `powershell` by its name only | the full path in the two `.cmd` and the two scripts |
| T2 | Python packages without a version | `numpy>=1.24,<3`, `pillow>=9,<14` |
| T3 | arguments quoted by hand | a path's last backslashes are doubled before the closing quote (`Quote-Arg`) |
| T4 | AI review: file names from any line of the diff | the names come from `git diff --name-only -z`; `insideByName` and a check of the real path before a file is read; tests |
| T5 | AI review: comments count whoever wrote them | the last review only from `github-actions[bot]`, verdicts only from OWNER / MEMBER / COLLABORATOR; tests |
| T6 | the server's error text in the comment | the status code alone |
| T6 | links and @mentions in the model's answer | a link loses its address, a bare address becomes code, an @name notifies nobody; tests |
| T6 | the write token in every step of the release | only on the two steps that call `gh` |
| T6 | the package's hash compared only if the notes hold one | Publish fails without one |
| T6 | `trees.zip` without a hash | its SHA-256 is in `release.yml` |
| T6 | Poly Haven: redirect, size only | a redirect only to https, the site's checksum compared |
| T6 | `convert_all.py`: the mech's name into a path | names that are not a file's name are refused |

Not changed, as said below: the runner on the developer's PC (fine while the owner is the only one who can push),
`LoadLibraryW` from `DllMain`, the game's path in the log.

The checks run in the pipeline now (`docs/PIPELINE.md`): the compiler's analyser as a report, BinSkim as a gate of
CI and of the release, bandit and PSScriptAnalyzer as gates (medium or high, errors), cppcheck as a gate (errors).

## What was run

| Check | How | Result |
|---|---|---|
| Compiler's analyser | `tools/security/analyze.bat` (`cl /analyze`, log in `mod/build/analyze/analyze.log`) | 41 messages, none a memory error that holds (see below) |
| clang-tidy | `tools/security/tidy.bat` (`bugprone-*`, `cert-*`, `clang-analyzer-*`; log in `mod/build/analyze/tidy.log`) | 27 messages, all style or false alarms |
| Hardening of the built DLLs | `dumpbin /headers /loadconfig /imports` on `d3d11.dll`, `dinput.dll` | ASLR, DEP, stack cookie, SafeSEH present; Control Flow Guard missing |
| By hand | load paths, hooks, reading of files and of the game's memory, string functions, paths made from settings | 3 findings in the cockpit loader, 1 in the build |

| BinSkim 4.4.9.14 | `tools/security/binskim.bat` (a build with a PDB in `mod/build/analyze/bin`) | per DLL: 1 error (no Control Flow Guard), 4 warnings (Spectre switches, `/sdl`, SourceLink, LTCG) |
| cppcheck 2.22.0 | `tools/security/cppcheck.bat` | 106 messages, none of severity "error" |
| Installer, start script, tools | by hand (PowerShell, cmd), a second reader for Python, Node and the workflows | see "The installer and the tools" below |

| bandit 1.9.4 | `tools/security/scripts.ps1` (Python in `tools`, `tests`) | 16 messages: 1 medium (known, see T6), 15 low, none a finding |
| PSScriptAnalyzer 1.25.0 | `tools/security/scripts.ps1` (PowerShell in `tools`) | 26 warnings, no error, none about security |

Not run: fuzzing (the loader cannot be called without the game yet). Semgrep with `p/python`, `p/security-audit`,
`p/github-actions` runs in the pipeline and passes.

bandit's medium one is `urlopen` in `fetch_polyhaven.py:13` (the address is checked for https before; what is left
is the redirect of T6). Its low ones: `subprocess` with argument lists in the tests and `guard.py`, `assert` in
tools. PSScriptAnalyzer: empty `catch` blocks (9, meant - "this may fail"), names and verbs of functions (8),
functions without `ShouldProcess` (4), `Write-Host` (2), two parameters "not used" (one is used inside a function,
one - `-Out` of `export_mechs.ps1` - is used by `Say`). None of its security rules (`Invoke-Expression`, passwords
in plain text, ...) fired. `scripts.ps1` takes the tools' places from `BANDIT` and `PSSA`.

Both scripts take the tool's place from a variable (`BINSKIM`, `CPPCHECK`): BinSkim is the NuGet package
`Microsoft.CodeAnalysis.BinSkim` (signed by Microsoft), cppcheck the project's release on GitHub (its installer is
not signed; it was unpacked with `msiexec /a`, not installed).

## What an attacker could reach

The mod opens no network connection, starts no process and writes nothing to the registry. It imports only
`kernel32`, `user32`, `advapi32`, `gdi32`. What comes from outside:

- cockpit files `mw4vr_cockpits\*.mwc` / `mw4vr_paint.mwd` - the only input that may come from a stranger
  (somebody passes a converted cockpit on);
- `mw4vr.ini`, `mw4vr_mechs.ini`, `mw4vr_cockpit_map.ini`, a cockpit's own `.ini` - written by the user or the mod;
- the game's own memory and its Direct3D calls - not an attacker.

## Findings

### S1 - cockpit file: a monitor's number below zero reads outside a table (medium)

`mod/src/mw5.inc:402`. A group named `SCREEN:<n>` gets `screen = atoi(n) + 1`; only numbers that are too large are
refused (`> MW5_SCREENS`). `SCREEN:-5` gives `screen = -4`, and `g_m5Rect[screen - 1]` (lines 493, 501, 510, 541,
744) is then read before the table's start. A prepared `.mwc` is needed. Read only, so a wrong picture or a crash,
not code of the attacker's.

Fix: accept `1..MW5_SCREENS` only.

### S2 - a cockpit's name from the settings goes into paths as it is (low)

`mod/src/mw5.inc:85, 362, 840, 872`. The name (`mw5_cockpit`, the lists of `mw4vr_cockpit_map.ini`) is put between
`mw4vr_cockpits\` and `.mwc` / `.ini` unchecked; `..\` leaves the folder. The file must still be a valid cockpit
file to be loaded, and settings are only written to an `.ini` that exists already. Whoever can write the settings
can do more than this anyway.

Fix: refuse names with `\`, `/`, `:` or `..`.

### S3 - cockpit file: sizes large enough to exhaust the 32-bit process (low)

`mod/src/mw5.inc:375, 329`. One group may ask for 128 MB of vertices, 48 MB of indices and two textures of 89 MB,
and there may be 40 groups. Memory running out throws inside `mw5Load`'s `__try`; the `__except` catches it without
the objects being cleaned up (the file stays open, the buffers stay allocated). Result: no cockpit, the game may
run out of memory later.

Fix: lower limits (the largest real cockpit decides), a sum over the file, `catch (std::bad_alloc)` in
`mw5LoadFile`.

### S4 - the DLLs are built without Control Flow Guard and `/sdl` (hardening)

`mod/build.bat`. Present: `/DYNAMICBASE`, `/NXCOMPAT`, `/GS`, SafeSEH; no section is writable and executable.
Missing: `/guard:cf` (compiler and linker), `/sdl`. The game itself has none of these, so the gain is small - but
it costs nothing to switch on, apart from a check that the many calls through saved function pointers (every draw)
do not get slower.

BinSkim's list for both DLLs: BA2008 no Control Flow Guard (error); BA2024 no `/Qspectre` (needs the Spectre
libraries of the Build Tools; of little worth in a game); BA2026 no `/sdl`; BA2027 no SourceLink in the PDB and
BA6006 no `/LTCG` (not about security). Everything else it checks passes.

## The installer and the tools

### T1 - the start and setup scripts call `powershell` by its name only (low)

`tools/setup/MW4VR_Setup.cmd:3`, `tools/start/mw4vr_start.cmd:3`, `tools/setup/mw4vr_setup.ps1:614`,
`tools/setup/mw4vr_setup_cockpits.ps1:21`. `cmd` looks in the current folder first: a `powershell.exe`, `.cmd` or
`.bat` lying in the game's folder (the shortcut's working folder) or in the unpacked package would be started
instead of Windows' own. Whoever can put a file there can as well replace `d3d11.dll`, so this opens no new door -
but the full path costs nothing.

Fix: `"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"` in the two `.cmd`, the same from
`$env:SystemRoot` in the scripts.

### T2 - the setup installs Python packages without a version or a hash (low)

`tools/setup/mw4vr_setup_cockpits.ps1:18` (`pip install --user numpy pillow`), and Python itself by `winget`
(`mw4vr_setup.ps1:510`). The newest packages of the day are taken, into the user's own Python. Usual for such a
tool, and only on the player's click - but a bad release of one of them would be installed unseen.

Fix (a matter of taste): versions known to work as lower and upper bounds, and a line in the readme saying what is
installed.

### T3 - arguments for the background step are quoted by hand (low, a bug in function)

`tools/setup/mw4vr_setup.ps1:607`. Each path is put between `"`. A path typed with a `\` at its end (`...\Paks\`)
ends in `\"`, which the next program reads as a quote inside the argument: the arguments run together and the
export fails with a confusing message. No way in for somebody else - the paths are the player's own.

Fix: trim a trailing `\` (as `Show-Mw5` does already for one use), or double it before the closing quote.

### T4 - AI review: file names are taken from any line of the diff that looks like a header (medium)

`tools/ci/ai-review.mjs:222, 226`. `changedPaths` matches `+++ b/<path>` anywhere in the diff, and `readText` reads
what it names without checking that it lies inside the checkout. A pull request that adds the line
`++ b/../../<file>` to a reviewed file makes the script read that file from the self-hosted runner's disk into the
prompt; text in the same diff can ask the model to quote it, and the answer is posted as a comment. Needs the right
to push a branch to the repository (forks are left out), so today only the owner - it matters once there are
collaborators. Found by reading, not tried.

Fix: the names from `git diff --name-only -z`; refuse absolute paths, `..` and links; check that the real path
stays inside the checkout.

### T5 - AI review: verdict comments count whoever wrote them (medium once the repository is public)

`tools/ci/ai-review.mjs:266, 276, 520`. A comment holding the verdicts marker goes into the prompt as the author's
word, one holding the review marker is taken for the bot's own last review (and is the one that gets edited). On a
public repository anybody may comment.

Fix: verdicts only from `author_association` OWNER / MEMBER / COLLABORATOR, the review marker only from
`github-actions[bot]`. To be done before the repository is made public.

### T6 - smaller ones in the pipeline and the tools (low)

- `tools/ci/ai-review.mjs:427, 445`: the model server's error text goes into the message as it is; if that message
  reaches the comment, host names or paths of the server could be published. Fix: status code and a fixed phrase.
- `tools/ci/ai-review.mjs:188`: `defang` leaves links and @mentions in the model's answer. Fix: neutralise both
  outside code.
- `.github/workflows/release.yml:11, 22`: the write token is in the environment of every step (build, tests, pip,
  a third-party action). Fix: `GH_TOKEN` only on the steps that call `gh`, or a job of its own for publishing.
- `.github/workflows/publish.yml:40`: the package's hash is only compared when the notes hold one. Fix: fail when
  there is none.
- `.github/workflows/release.yml:50`: `trees.zip` comes from a release asset that can be replaced, without a hash.
  Fix: its SHA-256 in the workflow.
- `tools/trees/fetch_polyhaven.py:13, 38`: only the first address is checked for https (a redirect is followed),
  and a download is checked by its size only. Fix: refuse other redirects, compare the hash the API gives.
- `tools/convert_all.py:24`: the mech's name from `cockpits.txt` goes into the output path unchecked. The list is
  written by our own export script from names matching `\w+`, so only a hand-made list could leave the folder.
  Fix: refuse names with separators or `..`.
- `ai-review.yml`: the runner on the developer's PC runs what a branch's workflow says for whoever may push.
  Fine while the owner is the only one who can.

(`tools/cockpit_convert.py` does not check indices against the number of vertices; the loader does where it reads
them itself, `mw5.inc:950`, and Direct3D defines a draw with an index outside the buffer.)

### Fine

- Python and Node: no `eval`, `exec`, `pickle`, `yaml.load`, `shell=True`, `os.system`; programs are started with
  argument lists. The glTF readers use `numpy.frombuffer`, which refuses sizes that do not fit.
- Workflows: every action pinned to a commit, no `${{ }}` inside a `run:` text, the AI review runs the base
  branch's script only, forks and Dependabot left out, no secret and no token in the prompt; pull requests of forks
  get hosted runners, a read-only token and no secrets.
- The game is started with `RunAsInvoker` (`mw4vr_start.ps1:20`): without the administrator rights its manifest
  asks for. A gain - the mod's DLL then runs without them too.
- No script downloads and runs code by itself: the store, python.org and UE Viewer buttons open pages; `winget`
  and `pip` run on the player's click; pypi.org is asked over TLS and only numbers are taken from the answer.
- What is deleted has a fixed name (`<game>\mw4vr_export`, `release\<name>`); what is overwritten in the game's
  folder is kept once as `*.before_mw4vr` (the DLLs, `mw4hook.ini`, `clutter.txt`, `mw4vr_mechs.ini`).
- `package.ps1` refuses a package holding cockpit, export or log files.
- Meant so, and worth knowing: the scripts run with `-ExecutionPolicy Bypass` (they are not signed), and the setup
  takes the "from the internet" mark off the files it installs (`Clear-WebMark`), so that the start script does not
  ask at every start. The setup sets `antihack=0` in `mw4hook.ini` (said in its window): for offline play.

## Notes on the DLLs (no action needed, or a matter of taste)

- **Load paths: fine.** The system's `d3d11.dll`, `d3dcompiler_47.dll` and `dinput.dll` are loaded with the full
  path of the system folder (`main.cpp:136, 271`, `dinput.cpp:40`), not by the search order. The OpenXR runtime is
  the one the machine's registry names (`vr.inc:127`, HKLM - only an administrator writes there), as the standard
  loader does it. A path with letters outside ASCII is widened byte by byte (`vr.inc:155`) and would not load - a
  bug in function, not in security.
- **Hooks: fine.** Each patch makes its bytes writable, writes, and puts the protection back (`main.cpp:191, 1476,
  2168`, `cam.inc:505`, `help.inc:182`). The camera hook checks the byte it expects; mw4hook's and the game's builds
  are compared by timestamp and size before fixed addresses are used (`mw4hook_abi.inc:113`).
- **Reading the game's vertices: fine.** `mgRead` (`mech.inc:353`) checks every index and offset against the
  buffers' sizes and runs under `__try`.
- **String functions: fine.** Every `swprintf` has its count; the four `wcscpy` copy between buffers of the same
  size or after a length check; the log's formats are constants.
- `LoadLibraryW` is called from `DllMain` (`main.cpp:2254` -> `loadReal`). Against the rule book (loader lock); it
  has worked since the first version because the system's d3d11.dll is loaded anyway.
- `mw4vr.log` holds the game's full path (`main.cpp:2253`). Whoever posts a log shows their folder names - worth a
  line in the readme, or only the file's name in the log.
- The analyser's messages:
  - C6320 / C6322 (21): `__except (EXCEPTION_EXECUTE_HANDLER)` catches everything. Meant so where the game's
    memory is read; it would also hide a fault of our own in those blocks.
  - C28159 (14): `GetTickCount` wraps after 49 days. No effect here.
  - C28112 (3): `g_nvts` is read without an interlocked call (`main.cpp:177`). Harmless on x86, the writer holds
    the mutex.
  - C6054 (2): `told` / `g_mw5Name` "may not end in 0" - it always does (`mw5.inc:802, 803`).
  - C6385 (1): `mw5.inc:290`, reading `g[3]` of a row of four floats - looks like a false alarm, to be confirmed
    when the findings are dealt with.
- cppcheck's messages: members without a value in a constructor (43; the structs are globals or are zeroed where
  they are made), C-style casts of pointers (53; the vtable and memory work of a hook), `memset` on structs with
  floats (4), parameters by value (5), one needless `substr`. No bounds, null or leak finding.
- clang-tidy's messages: `memcmp` on floats (11, meant: "has anything changed"), `(int)(x + 0.5)` (4), comparisons
  without `!= 0` (9), `va_list` "not initialised" after `va_start` (3, false), one unused store (`vr.inc:1098`).

## Next

1. S4's cost: the frame time with `/sdl` and Control Flow Guard against v11.69 (not measured yet; Control Flow
   Guard checks nothing inside the game, whose `.exe` is built without it).
2. Fuzzing of the cockpit loader, once it can be called without the game (the refactoring's step 3).
3. clang-tidy (`tools/security/tidy.bat`, with the checks of `.clang-tidy`) is not in the pipeline. Since the
   style work it finds nothing; what is switched off is listed in `.clang-tidy` with the reason.
