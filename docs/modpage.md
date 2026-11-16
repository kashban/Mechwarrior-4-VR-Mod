# Text for the mod pages (Nexus Mods, ModDB)

**Title:** MW4VR - MechWarrior 4: Mercenaries in VR

**Short description (one line):**
Play MechWarrior 4: Mercenaries (MekTek MekPak 3.1c with MW4Engine DX11) in a VR headset: real stereo, head
tracking inside the cockpit, a HUD with depth.

## Description

MW4VR puts you into the cockpit of MechWarrior 4: Mercenaries. It is an add-on to the *MW4Engine DX11*
renderer mod and needs it.

- **Real stereo, both eyes every frame.** The game's camera is moved to each eye; nothing is reprojected or faked.
- **Head tracking in the cockpit.** Look around freely; the mech, the world and the cockpit keep their place and size.
- **A HUD with depth.** Cross-hair and target marks lie far out on their targets. Radar, target and damage
  displays, the lance and weapon lists float before you as panes of their own - move, size and set each nearer or
  farther with a few keys.
- **Every mech remembered.** The mod reads which mech you sit in and keeps seat, scale and HUD places for each.
- **MechWarrior 5 cockpits (optional).** If you own MechWarrior 5: Mercenaries you can convert its cockpits
  with the tools in the package and sit in them in MW4, the HUD on their monitors or on their glass. All
  MW4 mechs have a cockpit assigned to start with. No MechWarrior 5 files are part of this mod.
- **More trees (optional).** Firs, pines, broad-leaved trees and shrubs for MW4Engine's terrain clutter, made
  from Poly Haven's plant models (CC0).
- **Start-up fix.** A small `dinput.dll` cures the game dying or hanging at start-up on PCs with certain USB
  devices, and the start script starts the game without the administrator prompt.

## Requirements

- MechWarrior 4: Mercenaries, MekTek MekPak 3.1c
- MW4Engine DX11 (https://www.moddb.com/mods/mw4-mercenaries-dx11-renderer-overhaul), **build 20261107_ea16ef2**, that is v1.0.7 (the mod checks the build and switches parts of itself off with
  any other)
- A VR headset with a 32-bit OpenXR runtime. Tested with a Quest 3 over Virtual Desktop (VDXR) only.
- A fast graphics card: every frame is drawn twice at a high resolution.

## Limits

- **Single player only.** MekTek's AntiHack must be off (`antihack=0`); do not use the mod online.
- Tested on one PC and one headset. Other runtimes (SteamVR, Meta Link) are untried.
- No motion controllers: mouse, keyboard and joystick as in the flat game.
- Menus and films are shown on a screen in front of you.
- The MechWarrior 5 cockpits need your own copy of that game (Epic Games Store: https://store.epicgames.com/p/mechwarrior-5, Steam: https://store.steampowered.com/app/784080), UE Viewer and about 6 GB of disk space.

## Install

See README.md in the package: three settings in `mw4hook.ini`, copy the `game` folder's files into the
game's folder, start with `mw4vr_start.cmd`.

## Source, licence

Source: https://github.com/kashban/Mechwarrior-4-VR-Mod. PolyForm Noncommercial 1.0.0: free for any noncommercial use, with the
licence and copyright notice kept. MW4Engine DX11 is the work of its author; this mod only builds on it.
