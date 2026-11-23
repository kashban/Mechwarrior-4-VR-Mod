# MW4VR - MechWarrior 4: Mercenaries in VR

Plays MechWarrior 4: Mercenaries (MekTek MekPak 3.1c) in a VR headset: stereo with head
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
- [MW4Engine DX11](https://www.moddb.com/mods/mw4-mercenaries-dx11-renderer-overhaul) (`mw4hook.dll`) **build 20261107_ea16ef2** (v1.0.7). The mod reaches into this build's memory; with any
  other build those parts switch themselves off (see `mw4vr.log`, lines starting `abi:`).
- A 32-bit OpenXR runtime. Tested with a Quest 3 over Virtual Desktop (VDXR) and over SteamVR (the Steam Link app).
  Meta Quest Link's own runtime does not work: its 32-bit library dies in its own code at the start (the game's
  window stops responding; `mw4vr.log` ends with `vr: runtime ...Oculus... loaded`). Make SteamVR the OpenXR
  runtime instead.
- For the MechWarrior 5 cockpits: your own copy of MechWarrior 5: Mercenaries
  ([Epic Games Store](https://store.epicgames.com/p/mechwarrior-5), [Steam](https://store.steampowered.com/app/784080)).

## Install

From a release package (`MW4VR-v<version>.zip`): start `MW4VR_Setup.cmd`. Its window asks for the game's folder
and does the steps below, installs the trees and, if you own MechWarrior 5: Mercenaries, exports and converts its
cockpits. A later package updates the same way (the button reads "Update"): program files are replaced, your
settings stay and only get what is new. By hand:

1. In `mw4hook.ini` (MW4Engine DX11): `antihack=0`, `fov=130`, `vsync=0`, `taa=0`, `mode=windowed` and a square
   `resolution` (for example `1300x1300`; the mod renders at `render_height` whatever the window's size).
2. Copy everything in the package's `game` folder into the game's folder: `d3d11.dll`, `dinput.dll`, `mw4vr.ini`,
   `mw4vr_cockpit_map.ini`, `mw4vr_start.cmd`, `mw4vr_start.ps1`, `mw4vr_settings.cmd`, `mw4vr_settings.ps1`.
3. Start the game with `mw4vr_start.cmd`, the headset connected. `Ctrl+Home` recentres the view.

What the files are:

- `d3d11.dll` - the mod. `mw4vr.ini` - its settings, a comment at each key.
- `dinput.dll` - on some PCs the game often dies or hangs right at start-up: the system's old DirectInput damages
  the heap while it lists "other HID devices" (light controllers, receivers). This one lists only mice, keyboards
  and joysticks. It writes `mw4vr_dinput.log`. Leave it out if you never had that trouble.
- `mw4vr_settings.cmd` - a window for `mw4vr.ini`: every setting with its explanation, tick boxes and fields
  (`tools/setup/mw4vr_settings.ps1`; it is made from the file itself, so a new key needs no change there).
- `mw4vr_start.cmd` - starts the game without the administrator prompt its manifest asks for (the game does not
  need the rights while its folder is writable).
- `mw4vr_cockpit_map.ini` - which MechWarrior 5 cockpit each mech gets (only used with converted cockpits, below).
- `trees` (if the package has it) - more kinds of trees and shrubs for MW4Engine's terrain clutter, made from
  Poly Haven's plant models (CC0). Copy its `Content` folder over the game's and add the text of
  `clutter_trees.txt` to the end of `Content\Clutter\clutter.txt`.

To remove the mod, delete `d3d11.dll` and `dinput.dll` from the game's folder.

Without MechWarrior 5 the game's own cockpits are used, and the HUD's parts - radar, target, damage, lance list,
weapons, the top strip - float as panes of their own before you, below the view ahead (the compass above it). Move them with the
Ctrl+Alt keys below.

## Keys (in a battle)

| Key | |
|---|---|
| Ctrl+F1 | the keys as a page in the headset; the game waits until it is closed (Ctrl+F1 again or Esc) |
| Ctrl+Home | recentre |
| Ctrl+F11 | own cockpit mesh on / off |
| Ctrl+Alt+F11 (with Shift: back) | the next cockpit mesh; remembered for the mech you sit in |
| Ctrl+Alt+1 … 6 | radar / target / damage / lance list / weapons / top strip: onto the monitor you look at or, looking elsewhere, to where you look (0: the monitor goes dark); remembered per cockpit |
| Ctrl+Alt+7 | all floating parts back to their automatic places (in the cockpit's windows; without a cockpit of our own: their row) |
| Ctrl+Alt+9 | this cockpit's monitors off / on (off: all of the HUD on the glass) |
| Ctrl+Alt+Shift+9 | the chosen part leaves its monitor and goes onto the glass |
| Ctrl+Alt+Shift+8 | all parts together (the game's cockpits, the view from outside): then Ctrl+Alt+Shift + arrows, PgUp / PgDn, Ins / Del change them all at once |
| Ctrl+Alt+Shift+1 … 6 | choose a part (it gets a yellow frame for some seconds); then Ctrl+Alt+Shift + arrows move it, PgUp / PgDn size it, Ins / Del put it farther / nearer, 0 hides it. A part on a monitor: arrows move what the monitor shows of the HUD, PgUp / PgDn nearer / more, Ins / Del longer / shorter, Home / End turn the picture left / right |
| Ctrl+Alt+arrows, PgUp / PgDn | eye position in the cockpit mesh, its size (remembered per cockpit) |
| Ctrl+Ins / Del | the cockpit's size (with Shift: the world's) |
| Ctrl+arrows | the seat: up / down = forward / back, right / left = higher / lower (kept apart for the game's cockpit and a MechWarrior 5 one) |
| Ctrl+PgUp / PgDn / End | the far HUD (cross-hair, target marks) farther / nearer / far |
| Ctrl+F10 | this cockpit fits: remember it for this mech |
| Ctrl+F12 | a picture of both eyes and the HUD into `mw4vr_shots\` |
| Ctrl+F5 | a video of both eyes with the game's sound into `mw4vr_videos\`: begin / end (see [Videos](#videos)) |
| Ctrl+F9 | the renderer's labels of one frame into `mw4vr_labels.txt` |
| Ctrl+F6 / F7 / F8 | diagnosis, only with `[debug] level=1`: the camera's matrix kept / the eyes' mode / shaders hidden one by one |

`[debug] level=1` in `mw4vr.ini` writes the diagnosis files (shaders, frame captures with Ctrl+Shift+F11) and the full log.

Everything else is in `mw4vr.ini`, with a comment at each key.

## Videos

Ctrl+F5 begins a video of what the headset shows, Ctrl+F5 again ends it: both eyes side by side (left | right) at the
size they are rendered at - 5200 x 2600 with `render_height=2600` -, the HUD drawn in as the headset shows it, with the
game's sound, as `mw4vr_videos\mw4vr_<date>_<time>.mp4` (HEVC, AAC). A high beep and a red light up and to the left of
the cross-hair (in the headset only, not in the video) say that it runs; a lower beep and a line in the headset that
it is saved.

- **End the video before you leave the game.** The file is complete only then; a game that is closed while a video
  runs leaves a file that does not play.
- The pictures are encoded by the graphics card and never pass through the processor's memory. It needs a card
  with an HEVC encoder that Windows' Media Foundation can use (tested with NVIDIA; `mw4vr.log` says why a video
  did not begin). A frame of the game takes about 0.3 ms longer while a video runs
  ([measured](docs/PERFORMANCE.md#a-videos-cost)).
- The video has 60 pictures a second by the clock on the wall (`rec_fps`), whatever the headset's rate is. A picture
  the encoder has no room for is left out, never waited for; the log counts them.
- Beside the video lies a `.json` with what a tool needs to cut or reproject the picture: sizes, pictures a
  second, and each eye's field of view as tangents. Each eye's picture is flat (rectilinear) and, with the game's own
  camera, symmetric: about 100 degrees wide and high. A menu or a film - one flat screen in the headset, some
  metres away - fills the video's picture instead, the same in both eyes.
- For YouTube's 3D: `python tools/video/youtube3d.py <the video>` writes an `.mkv` beside it that YouTube shows
  as 3D - one 16:9 picture, the left eye squeezed into its left half and the right eye into its right half, marked
  `stereo_mode=left_right`. An eye's picture is square: `--fit whole` (the default) keeps all of it, with black to
  its left and right; `--fit fill` shows a 16:9 part of it over the whole width (`--down 1`: the lower part, with the
  dashboard). `--height 4320` for a video of 7680 x 4320, `--from` / `--to` for a part. It needs ffmpeg.
- **Sharper:** `render_height=3840` (read at the start) renders each eye at 3840 x 3840 and makes a video of
  7680 x 3840. That is 2.2 times the pixels for every picture of the game, not only for the video.
- Settings in `mw4vr.ini`: `rec_fps`, `rec_bitrate_mbps`, `rec_hud` (0: the eyes' pictures without the HUD's
  layers), `rec_audio`, `rec_light`.

## MechWarrior 5 cockpits

No MechWarrior 5 content is part of this repository or of any release, and none may be passed on: the
cockpits belong to Piranha Games. You convert your own:

1. Put UE Viewer (umodel) into `tools\umodel\`, then export all cockpits (glTF and PNG, about 2 GB):
   `powershell -File tools\export_cockpits.ps1 -Paks <...\MW5Mercs\Content\Paks>`
2. `python tools/convert_all.py mw5export <the game's folder>\mw4vr_cockpits` (about 4 GB; one cockpit:
   `python tools/cockpit_convert.py <mesh.gltf> <export root> <name>.mwc`)
3. In `mw4vr.ini`: `mw5_on=1`, and `mw5_cockpit=<name>` for mechs nothing else is chosen for.
4. `mw4vr_cockpit_map.ini` in the game's folder gives every MW4 mech a cockpit to start with (the same mech
   where MW5 has it, otherwise one with a similar face). The mod reads the mech's name from the game at the
   start of a battle and shows it together with the cockpit's. Ctrl+Alt+F11 steps through the cockpits and
   remembers the choice for the mech you sit in (`mw4vr_mechs.ini`).

A cockpit of a mod often has no pictures, only one paint a part: the converter takes that paint (and a paint mask's
second and third), shades it with the mesh's vertex colours and takes the mod's hologram materials as monitors.
Such parts are MechWarrior 5's painted metal, whose look its shader makes; the converter marks them (`PAINT:` before
the group's name) and writes `mw4vr_paint.mwd` from two of the game's own pictures, and the mod draws them with
patches, worn paint, a fine grain and gloss (`[vr] mw5_paint`, `mw5_paint_wear`). An approximation: the game's
shader itself cannot be exported.

**The mech's own arms** (`[vr] arms`, on by default; `arms=0`: off). From the seat the game draws nothing of your mech, so
beside a cockpit with side windows the arms' weapons fire out of thin air. The mod reads the mech's
parts once, the first time you look at it from outside (`V`; stand still, torso ahead), keeps them in
`mw4vr_arms\<mech>.mwa` in the game's folder and draws from then on what lies beside or above the cockpit: arms,
weapon pods, a gun on the roof. The arms go forth and back with the mech's steps (`arms_swing`, metres; 0: not).
Mechs whose arms have no joints - guns fixed to the body, as the Rifleman's or the Urban Mech's - are named in
`arms_rigid`; theirs stay put.
They do not follow the game's glance keys, and they do not recoil. A release has the files of all MekPak 3.1c mechs
as `MW4VR-arms-<version>.zip` beside the package: put the zip beside the unpacked package (or unpack it there as
`arms`) and the setup installs what you do not have yet - or copy the `.mwa` files into `mw4vr_arms\` yourself. Which parts are taken: `arms_side`, `arms_low`, `arms_deep`, `arms_wide`, `arms_above`, `arms_near`
(see `mw4vr.ini.example`); `arms_all=1` draws every part, to see what there is.

The converter turns the larger faces of the material `Panels*` into monitors and writes a `<name>.ini` beside
each `.mwc`: which part of the HUD each monitor shows (`screen_<n>`; `screen_<n>_turn`: its picture turned by so
many degrees), the eye's place, the size. Change it as
you like; it is not written again. The clan cockpits (about 35) have such monitors. What no monitor shows - in
the other cockpits everything - is shown on the glass, fixed to the cockpit: the mod looks for the cockpit's windows
as seen from the seat and puts each part into an open place near the cross-hair. A part you place yourself stays
there (`glass_<part>` in the same file: degrees right, degrees up, width in degrees, metres away).

## Performance

Every picture is drawn twice, so a battle with many trees and enemies is heavy; how heavy depends on the PC.
Each player sets this for their own machine:

- the streaming quality of the headset's software (Virtual Desktop: "VR Graphics Quality"; tested on Medium),
- `clutter_density` in `mw4hook.ini` (1.0 = all grass and trees, 0 = none; read again when the file is saved),
- fewer enemies and smaller maps.

Short freezes over WiFi are mostly the network; a cable to the PC cured them in our test.

## Share your settings, report faults

Every mech wants its seat, sizes and HUD places set once, and there are more than a hundred. What ships as the
defaults is in `defaults/`: `mw4vr_mechs.ini` (each mech, with the game's own cockpit or a MechWarrior 5 one) and
`mw4vr_cockpits/<cockpit>.ini` (each MechWarrior 5 cockpit's eye place, size, monitors and the parts on its
glass). If you have set mechs or cockpits up well, please share your two kinds of `.ini` files - as an
[issue](https://github.com/kashban/Mechwarrior-4-VR-Mod/issues) with the files attached or as a pull request
against `defaults/`. Never the `.mwc` cockpit files: they are MechWarrior 5 content.

Faults and feature requests go into the [issues](https://github.com/kashban/Mechwarrior-4-VR-Mod/issues) too;
for a fault attach `mw4vr.log` and name the mech, the map and the headset.

## Build

Visual Studio 2022 Build Tools (x86), then `mod\build.bat`. The results are `mod\build\d3d11.dll` and
`dinput.dll`. `powershell -File tools\package.ps1` builds, tests and makes the release archive in `release\`.

## Tests

`tests/run_tests.bat` runs the automatic tests; no game files are needed.

- `test_convert.py` - the cockpit converter, on a small made-up cockpit
- `core_test.cpp` - the mod's arithmetic (`mod/src/core.h`: matrices, the camera's frustum planes, which parts
  of the HUD go where), built as a console program
- `test_shaders.py` - the mod's shaders compile (Windows SDK's `fxc`)
- `test_abi.py` - `mw4hook_abi.h` against the programs themselves. Set `MW4_DIR` to the game's folder and it
  checks the builds of `mw4hook.dll` and `MW4Mercs.exe` and every instruction the mod relies on; a new build of
  mw4hook fails here first.

`tests/run_tests.bat mutation` also plants small errors into the converter and into `core.h`, one at a time,
and checks that a test fails for each (`tests/mutate.py`); the share found must not fall below the floor in
`tests/mutation_floor.json` (both at 100 %). What needs the game, a headset or Direct3D - the hooks, OpenXR,
drawing - is not covered by tests.

## Layout

How the code works: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Where the frames' time goes: [docs/PERFORMANCE.md](docs/PERFORMANCE.md). Working on it: [CONTRIBUTING.md](CONTRIBUTING.md).

- `mod/src/*.cpp` - the mod, a unit for each part, each with its header (`mod.h` brings them all); `main.cpp`
  has the entry points, `base.cpp` the proxy and the log, `hooks.cpp` and `draws.cpp` the hooks
- `mod/src/mw4hook_abi.h` - everything known about the insides of `mw4hook.dll` and `MW4Mercs.exe`
  (addresses, offsets, shader hashes, label names). A new build of either means changes here only.
- `mod/src/core.h` - the arithmetic, free of Windows and the game (what the tests test)
- `mod/src/cam.cpp` - the camera hook; `vr.cpp` - OpenXR; `hud.cpp` - the HUD layer; `mw5.cpp` - the cockpit mesh
- `tools/` - cockpit conversion, the start script, the release package; `tools/trees` - the trees (card models
  from Poly Haven's plants); `tools/faces` - comparing MW4 mechs with MW5 cockpits

## Credits

MW4Engine DX11 (`mw4hook.dll`) is the work of its author; this mod only builds on it. MechWarrior is a
trademark of Microsoft; the game and MekPak belong to their owners. OpenXR headers: The Khronos Group, Apache 2.0.
The trees are made from plant models of [Poly Haven](https://polyhaven.com) (CC0).

## License

Copyright (c) 2026 Kashban. [PolyForm Noncommercial License 1.0.0](LICENSE): use it, change it and pass it on
for any noncommercial purpose; keep the licence and the copyright notice with it. Commercial use needs the
author's permission. The OpenXR headers in `mod/include/openxr` stay under Apache 2.0.
