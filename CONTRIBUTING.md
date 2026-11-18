# Working on MW4VR

How the code is built: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). The pipeline: [docs/PIPELINE.md](docs/PIPELINE.md).

## What you need

- Windows, Visual Studio 2022 Build Tools with the x86 C++ compiler (the game is a 32-bit process).
- Python 3.10 or newer: `pip install -r requirements.txt -r requirements-ci.txt` (the converter's libraries; the
  test and style tools, pinned).
- To try a change: MechWarrior 4: Mercenaries with MekPak 3.1c and MW4Engine DX11, and a headset.

## Build and test

```
mod\build.bat                  the DLLs, into mod\build
tests\run_tests.bat            all tests (a minute)
tests\run_tests.bat mutation   with the mutation round (some minutes)
python tools\ci\guard.py       nothing of MechWarrior 5, no private data
```

Before a pull request, the checks the pipeline holds a change against:

```
ruff format --check tools && ruff check tools      Python: PEP 8 and the common pitfalls (pyproject.toml)
mypy                                               Python: types
tools\security\scripts.ps1                         PowerShell (PSScriptAnalyzerSettings.psd1) and bandit
tools\security\binskim.bat                         the DLLs' hardening
```

## Rules

- **Nothing of MechWarrior 5** - no cockpit, texture or export file and nothing made from one - in the
  repository or the package. The same for dumps of the game or of mw4hook. The guard refuses them.
- **No private data** in tracked files: no paths into a user's folder, no network addresses, no names. Examples
  use made-up paths.
- **Single player only.** Nothing that hides the mod from the game's anti-cheat or helps using it online.
- **Files from outside are not trusted.** Sizes, counts and names read from a `.mwc`, `.mwd`, `.ini` or glTF are
  checked before they size an allocation, a copy or an index, or become part of a path.
- **What can be decided without Direct3D goes into `core.h`** with tests that kill every mutant of the new lines
  (`tests\mutate.py core`). The floors in `tests/mutation_floor.json` only rise.
- **Tests are not rewritten along with the code they test.** A change that tidies code leaves its tests as they
  are, so a change of behaviour shows; new tests are welcome. A change of behaviour says so and changes the test
  that held the old one.
- **Offsets and addresses** of the game and of mw4hook live in `mw4hook_abi.inc` and nowhere else.
- **Releases** are made by the pipeline from a tag `v<version>` that equals the version in `mod/src/main.cpp`.
  Tags are never moved or deleted.

## Style

- **C++:** `.clang-format` (LLVM's style, four spaces, 120 columns) and `.clang-tidy`. The sources are in
  that format and `tools\security\tidy.bat` finds nothing; both are to stay so (the command for the format
  is at the top of `.clang-format`). C++17, no exceptions across
  the hooks, no dependencies beyond the Windows SDK and the OpenXR headers in `mod/include`. The checked forms of
  the C library's functions (`swscanf_s`, `wcscpy_s`, ...): the build has `/sdl`.
- **Python:** `ruff format` and `ruff check` with the rules in `pyproject.toml`; `%` formatting is the one way of
  writing a number's format here.
- **PowerShell:** approved verbs, singular nouns, no empty `catch`; the rules in `PSScriptAnalyzerSettings.psd1`.
- **Comments** say why, and what a number means - in whole sentences, in plain words. A comment that tells the
  story of a bug (what went wrong before the code was as it is) is worth more than one that repeats the code.
- **The log** (`mw4vr.log`) is how a player's problem is found from afar: a new feature says once what it found
  and what it decided, with the frame's number.
- **Commit messages:** what changed for the player or the reader, then why. No ticket numbers, no private data.

## Licence

By contributing you agree that your work is published under the project's licence
([PolyForm Noncommercial 1.0.0](LICENSE)).
