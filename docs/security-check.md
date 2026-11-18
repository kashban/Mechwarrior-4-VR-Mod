# Security check of the mod's code

State: v11.59 (`cc3f5b0`), branch `security-check`. Findings only - nothing in `mod/` was changed.

## What was run

| Check | How | Result |
|---|---|---|
| Compiler's analyser | `tools/security/analyze.bat` (`cl /analyze`, log in `mod/build/analyze/analyze.log`) | 41 messages, none a memory error that holds (see below) |
| clang-tidy | `tools/security/tidy.bat` (`bugprone-*`, `cert-*`, `clang-analyzer-*`; log in `mod/build/analyze/tidy.log`) | 27 messages, all style or false alarms |
| Hardening of the built DLLs | `dumpbin /headers /loadconfig /imports` on `d3d11.dll`, `dinput.dll` | ASLR, DEP, stack cookie, SafeSEH present; Control Flow Guard missing |
| By hand | load paths, hooks, reading of files and of the game's memory, string functions, paths made from settings | 3 findings in the cockpit loader, 1 in the build |

Not run: BinSkim and cppcheck (downloads, not approved yet), fuzzing (the loader cannot be called without the game
yet), the Python tools and the workflows (not part of this check).

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

## Notes (no action needed, or a matter of taste)

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
- clang-tidy's messages: `memcmp` on floats (11, meant: "has anything changed"), `(int)(x + 0.5)` (4), comparisons
  without `!= 0` (9), `va_list` "not initialised" after `va_start` (3, false), one unused store (`vr.inc:1098`).

## Next

1. Fix S1 - S3 with a test each (the loader's checks moved to `core.h`, where the tests reach them).
2. S4: switch on, measure the frame time.
3. BinSkim and cppcheck once the downloads are approved; the two scripts here as a job of the pipeline.
4. Fuzzing of the cockpit loader after the refactoring's step 3.
