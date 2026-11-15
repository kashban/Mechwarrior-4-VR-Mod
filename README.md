# MW4VR - MechWarrior 4: Mercenaries in VR

Work in progress. Plays MechWarrior 4: Mercenaries (MekTek MekPak 3.1c) in a VR headset: stereo with head
tracking inside the cockpit, the HUD on the cockpit's monitors, and - if you own MechWarrior 5: Mercenaries -
its cockpits in place of the old ones.

**Single player only.** The mod needs MekTek's AntiHack switched off (`antihack=0` in `mw4hook.ini`). Do not
use it online.

## What it is

A 32-bit `d3d11.dll` that the game loads in place of the system's (it passes everything on to the real one).
It sits behind the *MW4Engine DX11* renderer mod (`mw4hook.dll`, by another author) and

- opens an OpenXR session on the game's own D3D11 device,
- moves the game's camera to each eye's pose and has every frame drawn twice, once per eye,
- takes the HUD out of the picture and shows it as layers fixed to the mech, or on the monitors of the cockpit,
- draws a cockpit mesh of its own (`.mwc`) in place of the game's.

## Requirements

- MechWarrior 4: Mercenaries, MekTek MekPak 3.1c
- MW4Engine DX11 (`mw4hook.dll`) **build 20261107_ea16ef2**. The mod reaches into this build's memory; with any
  other build those parts switch themselves off (see `mw4vr.log`, lines starting `abi:`).
- A 32-bit OpenXR runtime. Tested with Virtual Desktop (VDXR) and a Quest 3 only.
- For the MechWarrior 5 cockpits: your own copy of MechWarrior 5: Mercenaries.

## Install

1. In `mw4hook.ini`: `antihack=0`, `fov=130`, `vsync=0`.
2. Copy `d3d11.dll` and `mw4vr.ini` (from `docs/mw4vr.ini.example`) into the game's folder.
3. Start the game with the headset connected. `Ctrl+Home` recentres the view.

To remove it, delete `d3d11.dll` from the game's folder.

## Keys (in a battle)

| Key | |
|---|---|
| Ctrl+Home | recentre |
| Ctrl+F11 | own cockpit mesh on / off |
| Ctrl+Alt+F11 (with Shift: back) | the next cockpit mesh; remembered for the mech you sit in |
| Ctrl+Alt+1 … 6 | radar / target / damage / lance list / weapons / top strip: onto the monitor you look at or, looking elsewhere, onto the glass there (0: the monitor goes dark); remembered per cockpit |
| Ctrl+Alt+7 | all parts on the glass back to their automatic places (in the cockpit's windows) |
| Ctrl+Alt+Shift+1 … 6 | choose a part on the glass; then Ctrl+Alt+Shift + arrows move it, PgUp / PgDn size it, Ins / Del put it farther / nearer, 0 takes it off the glass |
| Ctrl+Alt+arrows, PgUp / PgDn | eye position in the cockpit mesh, its size (remembered per cockpit) |
| Ctrl+Ins / Del | the cockpit's size |
| Ctrl+F10 | this cockpit fits: remember it for this mech |
| Ctrl+F12 | a picture of both eyes and the HUD into `mw4vr_shots\` |
| Ctrl+F9 | the renderer's labels of one frame into `mw4vr_labels.txt` |

`[debug] level=1` in `mw4vr.ini` writes the diagnosis files (shaders, frame captures with Ctrl+Shift+F11) and the full log.

Everything else is in `mw4vr.ini`, with a comment at each key.

## MechWarrior 5 cockpits

No MechWarrior 5 content is part of this repository or of any release, and none may be passed on: the
cockpits belong to Piranha Games. You convert your own:

1. Put UE Viewer (umodel) into `tools\umodel\`, then export all cockpits (glTF and PNG, about 2 GB):
   `powershell -File tools\export_cockpits.ps1 -Paks <...\MW5Mercs\Content\Paks>`
2. `python tools/convert_all.py mw5export <the game's folder>\mw4vr_cockpits` (about 4 GB; one cockpit:
   `python tools/cockpit_convert.py <mesh.gltf> <export root> <name>.mwc`)
3. `mw5_cockpit=<name>` in `mw4vr.ini` is the cockpit for every mech; in a battle Ctrl+Alt+F11 steps through
   the files and remembers the choice for the mech you sit in (`mw4vr_mechs.ini`).

The converter turns the larger faces of the material `Panels*` into monitors and writes a `<name>.ini` beside
each `.mwc`: which part of the HUD each monitor shows (`screen_<n>`), the eye's place, the size. Change it as
you like; it is not written again. The clan cockpits (about 35) have such monitors. What no monitor shows - in
the other cockpits everything - is shown on the glass, fixed to the cockpit: the mod looks for the cockpit's windows
as seen from the seat and puts each part into an open place near the cross-hair. A part you place yourself stays
there (`glass_<part>` in the same file: degrees right, degrees up, width in degrees, metres away).

## Build

Visual Studio 2022 Build Tools (x86), then `mod\build.bat`. The result is `mod\build\d3d11.dll`.

## Tests

`tests/run_tests.bat` runs the automatic tests; no game files are needed.

- `test_convert.py` - the cockpit converter, on a small made-up cockpit
- `core_test.cpp` - the mod's arithmetic (`mod/src/core.h`: matrices, the camera's frustum planes, which parts
  of the HUD go where), built as a console program
- `test_shaders.py` - the mod's shaders compile (Windows SDK's `fxc`)
- `test_abi.py` - `mw4hook_abi.inc` against the programs themselves. Set `MW4_DIR` to the game's folder and it
  checks the builds of `mw4hook.dll` and `MW4Mercs.exe` and every instruction the mod relies on; a new build of
  mw4hook fails here first.

`tests/run_tests.bat mutation` also plants small errors into the converter and into `core.h`, one at a time,
and checks that a test fails for each (`tests/mutate.py`); the share found must not fall below the floor in
`tests/mutation_floor.json` (both at 100 %). What needs the game, a headset or Direct3D - the hooks, OpenXR,
drawing - is not covered by tests.

## Layout

- `mod/src/main.cpp` - the proxy, hooks, shader tracking
- `mod/src/mw4hook_abi.inc` - everything known about the insides of `mw4hook.dll` and `MW4Mercs.exe`
  (addresses, offsets, shader hashes, label names). A new build of either means changes here only.
- `mod/src/core.h` - the arithmetic, free of Windows and the game (what the tests test)
- `mod/src/cam.inc` - the camera hook; `vr.inc` - OpenXR; `hud.inc` - the HUD layer; `mw5.inc` - the cockpit mesh
- `tools/` - cockpit conversion

## Credits

MW4Engine DX11 (`mw4hook.dll`) is the work of its author; this mod only builds on it. MechWarrior is a
trademark of Microsoft; the game and MekPak belong to their owners. OpenXR headers: The Khronos Group, Apache 2.0.

## License

Copyright (c) 2026 Kashban. [PolyForm Noncommercial License 1.0.0](LICENSE): use it, change it and pass it on
for any noncommercial purpose; keep the licence and the copyright notice with it. Commercial use needs the
author's permission. The OpenXR headers in `mod/include/openxr` stay under Apache 2.0.
