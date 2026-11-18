# AI review guide

Project context for the AI PR review (`tools/ci/ai-review.mjs` sends this file to the model with every review).
Keep it short and factual: every line costs prompt tokens and review time. Add a line when the review keeps
getting something wrong about this project.

## Architecture

- `mod/src`: one 32-bit `d3d11.dll` (C++, MSVC x86) that the game
  MechWarrior 4: Mercenaries loads in place of the system's and that passes every call on to the real one. It sits
  behind another author's renderer mod (`mw4hook.dll`, MW4Engine DX11) and hooks that mod's Direct3D 11 calls.
  Every part is a unit of its own (`x.cpp`) with a header (`x.h`) of what the other units use; `mod.h` includes
  all headers in the order in which they build on each other, and every unit includes `mod.h`.
- `mod/src/core.h`: the arithmetic (matrices, frustum planes, HUD rectangles) as plain functions without Windows
  headers, so `tests/core_test.cpp` can test them alone.
- OpenXR session on the game's own device; every frame is drawn twice, once per eye.
- `tools/cockpit_convert.py`, `tools/convert_all.py`: turn a glTF export of a cockpit into the mod's `.mwc` mesh
  format (numpy, Pillow). `tools/ci/guard.py`: the repository guard.
- Tests: `tests/core_test.cpp` (own check macro, no framework), `tests/test_*.py` (unittest),
  `tests/mutate.py` (mutation tests, floor 100 % in `tests/mutation_floor.json`).
- CI: GitHub Actions, `.github/workflows`, described in `docs/PIPELINE.md`.

## Facts the review must not get wrong

- The game is a 32-bit process: pointers are 4 bytes, `size_t` is 32 bits. Offsets into the game's or the hook's
  memory are for exactly one build of `mw4hook.dll`; `mw4hook_abi.cpp` checks that build (the facts are in `mw4hook_abi.h`) and switches those
  parts off for any other. Hard-coded offsets there are intended.
- `core.h`'s matrices are 3 rows of 4 floats (the rows: the axes x, y, z; the fourth column: the translation).
  HLSL constant buffers are filled by hand as `float` arrays, so a buffer's size and each index in it must match
  the shader's `cbuffer`.
- HLSL lives in C++ string literals and is compiled at run time; `tests/test_shaders.py` compiles the same
  strings with `fxc`.
- The mod draws inside the game's frame: it saves the device context's state it changes (render targets, shaders,
  buffers, shader resources, blend / depth / rasteriser state) and restores it. COM getters (`OMGetRenderTargets`,
  `PSGetShaderResources`, ...) add a reference that must be released.
- Global state without locks is intended: all drawing happens on the game's one render thread.
- `static` locals as per-frame memory are the project's style, not a bug by themselves.
- Windows headers define macros named `small`, `near`, `far`, `min`, `max`: such identifiers in `core.h` compile
  in the tests (no Windows headers) and fail in the mod.
- Third-party actions are pinned to commit SHAs on purpose; Python libraries are pinned in `requirements*.txt`.
- Settings are read from `.ini` files beside the game with defaults in the code; a missing key is normal.

## Project rules (flag violations)

- **Nothing of MechWarrior 5:** no cockpit, texture or export file (`.mwc`, `.mwd`, `.gltf`, `.uasset`, `.pak`,
  ...) and nothing derived from one may be added to the repository or the package; only the loader, the converter
  and numbers of our own (`.ini`). The same for reverse-engineering dumps of the game.
- **No private data** in tracked files: no user-folder paths, no private network addresses, no mail addresses,
  no names of persons. Examples use made-up paths.
- **Online play:** the mod is for single player only; nothing that hides it from the game's anti-cheat or helps
  using it online.
- **Tests:** a change to `core.h` or `cockpit_convert.py` needs tests that kill every mutant of the changed lines
  (the mutation floors only rise). Code that cannot be tested without the game does not belong in `core.h`.
- **Reading files** (`.mwc`, `.mwd`, `.ini`, glTF): sizes and counts from a file are checked before they are
  used for an allocation, a copy or an index.
- **Releases** are made by the Release workflow from a tag; the tag equals the version in `mod/src/main.cpp`.
  Tags are never moved or deleted.
- **Downloads** in tools and workflows: https only, pinned versions, checksums where the source offers them.

## Accepted risks (don't report)

- The mod needs the hook's anti-hack check switched off (`antihack=0`) - offline use only, stated in the readme.
- The mod reads and patches another module's memory at fixed offsets (guarded by the build check above).
- Diagnosis code (`[debug] level=1`) writes files into the game's folder without size limits.
