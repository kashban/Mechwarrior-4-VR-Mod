# How the mod is built

For somebody who wants to read or change the code. What the mod does for a player is in the [README](../README.md).

## The situation

MechWarrior 4: Mercenaries is a 32-bit program from 2002. *MW4Engine DX11* (`mw4hook.dll`, another author's work)
replaces its renderer with a Direct3D 11 one. MW4VR is a third party in that process: a `d3d11.dll` in the game's
folder, loaded in place of the system's, that passes every call on to the real one and watches - and at some
points changes - what goes through.

```
MW4Mercs.exe ── mw4hook.dll ──► d3d11.dll (MW4VR) ──► the system's d3d11.dll
   the game       the renderer      this mod              Direct3D
                                        │
                                        └──► the OpenXR runtime (the headset)
```

Nothing of the game or of mw4hook is changed on disk. In memory the mod patches a few places of both (below).

## A frame

1. The game calls its camera (`CameraElement::Draw`). The mod has redirected that call (`cam.inc`): it runs the
   camera twice, once for each eye, each time with the camera's matrix moved by that eye's pose in the headset.
   mw4hook culls, lights and draws the world from there, so everything follows the head.
2. While an eye's pass runs, the mod sees every draw (`main.cpp`, the hooks). Draws of the game's old
   "CPU path" (vertices already transformed: the cockpit) get a replacement shader, and where the game draws its
   cockpit the mod draws a cockpit mesh of its own instead (`mw5.inc`).
3. After each pass the picture is copied to that eye's texture (`hud.inc`, `eyeGrab`).
4. The game draws its HUD. The mod has replaced the render target for that pass with a transparent texture of
   its own (`hud.inc`), so the eyes' pictures hold the world alone.
5. At `Present` (`vr.inc`, `vrOnPresent`) the two eye textures go to the headset as a projection layer, and the
   HUD's texture as flat layers fixed to the mech: parts of it on the cockpit's monitors and glass, the cross-hair
   far ahead. Then the head's pose for the next frame is asked for.

## The files

All of `mod/src` is one translation unit: `main.cpp` includes the `.inc` files in an order that matters (each
says at its top what must come before it). They share globals; all drawing happens on the game's one render
thread, so those are not locked. Splitting them into units of their own is planned work.

| File | What it holds |
|---|---|
| `main.cpp` | The proxy (`D3D11CreateDevice` and friends), the hooks of the device's and the context's methods, the book-keeping of what the game has bound (shaders, constant buffers, render targets), the log. |
| `mw4hook_abi.inc` | Everything known about the insides of `mw4hook.dll` and `MW4Mercs.exe`: addresses, offsets, shader hashes, label names - and the check that the builds are the known ones. A new build of either means changes here only. |
| `core.h` | The arithmetic and the decisions, free of Windows and the game: matrices, frustum planes, which part of the HUD goes where, what a cockpit file may hold. **The only part the tests can reach** - what can be decided without Direct3D belongs here. |
| `head.inc` | The head's pose as a change of the camera. |
| `cam.inc` | The camera hook: both eyes drawn in every frame. |
| `mech.inc` | Which mech's cockpit this is (a profile for each), the cockpit's sway with the mech's steps, whether the camera sits in the mech or looks at it from outside. |
| `hud.inc` | The HUD pass drawn into a texture of our own; the eyes' textures. |
| `vr.inc` | OpenXR: the session, the swap chains, the layers of a frame, the settings in `mw4vr.ini`, the hot keys for seat and sizes. |
| `mw5.inc` | A cockpit mesh of our own (`.mwc`): loading, drawing, its monitors, the HUD's parts on its glass, the hot keys that place them. |
| `cross.inc` | The cross-hair taken out of the HUD's picture by its colour, as a layer of its own. |
| `sway.inc`, `note.inc`, `help.inc`, `shot.inc` | The sway's values for its users; a line of text in the headset; the page of keys (Ctrl+F1); pictures for diagnosis (Ctrl+F12). |
| `e2e.inc` | The test mode for the end-to-end test: only with `MW4VR_E2E` set, plays a plan of keys and records every frame. |
| `eye_tl_vs.hlsl` / `.h` | The replacement vertex shader for the CPU path. |
| `vtidx.c` | The positions of methods in Direct3D's vtables, taken from the SDK's own C declarations. |
| `mod/dinput/dinput.cpp` | A second, small DLL: `dinput.dll` in front of the system's, which leaves out one class of devices that crashes the game on some PCs. |

## How the mod gets between the calls

- **The proxy.** The DLL exports what `d3d11.dll` exports and loads the real one from the system folder by its full
  path. The device and context it hands back are the real ones.
- **Vtable hooks.** For the device and each context, slots of the vtable are replaced by the mod's functions
  (`main.cpp`), which do their work and call the original.
- **Hot patches.** mw4hook rewrites some vtable slots itself, so for the draw calls the mod patches the first
  bytes of Direct3D's own functions instead (`HP_Draw` and its siblings).
- **Two patches in the game and mw4hook.** The camera's call (`cam.inc`) and the clocks while the help page is up
  (`help.inc`). Both check the bytes they expect before they write, and both are switched off for a build that is
  not the known one.

Every patch makes its bytes writable, writes, and puts the protection back.

## What comes from outside

Cockpit files (`mw4vr_cockpits\*.mwc`) may come from a stranger; settings (`*.ini`) are the user's. Neither is
trusted: sizes, counts and names are checked before they are used (`core.h`: `mcGroupBytes`, `mcScreen`,
`mcNameOk`). The mod opens no network connection and starts no process. Details: [security-check.md](security-check.md).

## Tools

| | |
|---|---|
| `tools/cockpit_convert.py`, `convert_all.py` | A cockpit exported from MechWarrior 5 (glTF, by UE Viewer) becomes a `.mwc`. Nothing of MechWarrior 5 is in the repository or the package - the player converts their own copy. |
| `tools/setup` | The setup's window (PowerShell): installs the mod into the game's folder, exports and converts cockpits. |
| `tools/start` | Starts the game without the administrator prompt. |
| `tools/trees` | Card trees for mw4hook's terrain clutter, made from Poly Haven's plants (CC0). |
| `tools/ci` | The repository guard (nothing of MechWarrior 5, no private data) and the AI review. |
| `tools/e2e` | The end-to-end test's runner and judge (see Tests). |
| `tools/security` | The security checks the pipeline runs. |
| `tools/package.ps1` | Puts the release package together. |

## Tests

- `tests/core_test.cpp` tests `core.h`; `tests/test_*.py` the converter, the shaders (compiled with `fxc`), the
  guard and the check of mw4hook's build.
- `tests/mutate.py` plants errors into `core.h` and the converter and expects a test to fail for each
  (`tests/mutation_floor.json`: both at 100 %, and the floors only rise).
- What needs the game, a headset or Direct3D is not reached by those. For that there is an end-to-end test
  (`tools/e2e/run.ps1`): it starts the game with `MW4VR_E2E` set to a plan, the mod (`e2e.inc`) plays the plan's
  keys and writes a record of every frame, and `tools/e2e/judge.py` judges that record. It needs the game, the
  cockpits and an awake headset, so it runs on a developer's PC and not in the pipeline. Without `MW4VR_E2E` the
  test mode does nothing.

The pipeline is described in [PIPELINE.md](PIPELINE.md).
