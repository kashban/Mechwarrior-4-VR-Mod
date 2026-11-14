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
| Ctrl+Alt+arrows, PgUp / PgDn | eye position in the cockpit mesh, its size |
| Ctrl+Ins / Del | the cockpit's size |
| Ctrl+F10 | this cockpit fits: remember it for this mech |
| Ctrl+F12 | a picture of both eyes and the HUD into `mw4vr_shots\` |
| Ctrl+F9 | the renderer's labels of one frame into `mw4vr_labels.txt` |

`[debug] level=1` in `mw4vr.ini` writes the diagnosis files (shaders, frame captures with Ctrl+Shift+F11) and the full log.

Everything else is in `mw4vr.ini`, with a comment at each key.

## MechWarrior 5 cockpits

No MechWarrior 5 content is part of this repository or of any release, and none may be passed on: the
cockpits belong to Piranha Games. You convert your own:

1. Export a cockpit's static mesh with UE Viewer (umodel) as glTF, with its textures as PNG.
2. `python tools/cockpit_convert.py <mesh.gltf> <export root> <name>.mwc`
3. Put the `.mwc` into `mw4vr_cockpits\` in the game's folder and set `mw5_cockpit=<name>` in `mw4vr.ini`.

The converter turns the faces of the material `Panels*` into monitors; `mw5_screen_<n>` in `mw4vr.ini` says
which part of the HUD each one shows.

## Build

Visual Studio 2022 Build Tools (x86), then `mod\build.bat`. The result is `mod\build\d3d11.dll`.

## Tests

`testsun_tests.bat` runs the automatic tests (Python, no game files needed). `testsun_tests.bat mutation` also
plants small errors into the tested code, one at a time, and checks that a test fails for each
(`tests/mutate.py`); the share found must not fall below the floor in `tests/mutation_floor.json`.
So far the cockpit converter is covered; the mod itself (C++) is not yet.

## Layout

- `mod/src/main.cpp` - the proxy, hooks, shader tracking
- `mod/src/mw4hook_abi.inc` - everything known about the insides of `mw4hook.dll` and `MW4Mercs.exe`
  (addresses, offsets, shader hashes, label names). A new build of either means changes here only.
- `mod/src/cam.inc` - the camera hook; `vr.inc` - OpenXR; `hud.inc` - the HUD layer; `mw5.inc` - the cockpit mesh
- `tools/` - cockpit conversion

## Credits

MW4Engine DX11 (`mw4hook.dll`) is the work of its author; this mod only builds on it. MechWarrior is a
trademark of Microsoft; the game and MekPak belong to their owners. OpenXR headers: The Khronos Group, Apache 2.0.

## License

Copyright (c) 2026 Kashban. [PolyForm Noncommercial License 1.0.0](LICENSE): use it, change it and pass it on
for any noncommercial purpose; keep the licence and the copyright notice with it. Commercial use needs the
author's permission. The OpenXR headers in `mod/include/openxr` stay under Apache 2.0.
