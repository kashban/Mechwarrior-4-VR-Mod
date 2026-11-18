# Where the frames' time goes

Measured on 2026-11-18 with v11.71 and v11.72 (Quest 3 over Virtual Desktop at 90 Hz, 2600 x 2600 pixels an eye,
a Mad Cat walking for half a minute, no fighting). How to measure: [below](#measuring).

## In short

- On light maps the mod reaches the headset's 90 pictures a second with time to spare.
- Where it does not, the time is spent by the processor inside `mw4hook.dll` - the renderer, another author's
  work - while it draws the main camera, and the mod has that camera drawn twice, once for each eye. The graphics
  card waits.
- The mod's own share of a frame was 1.7 ms and is 0.9 ms since v11.72.
- What would help most is not in this mod: mw4hook walking its scene once for both eyes
  ([what to ask its author for](#what-mw4hook-could-do)).

## The maps

Instant action, map by number (`-Map`), v11.72. "Eyes": the two passes of the main camera, left + right.

| Map | Pictures a second | A frame (ms) | Eyes (ms) | Waiting for the headset (ms) |
|---|---|---|---|---|
| 33 Tharkad - Resupply | 32 | 31.1 | 15.6 + 12.2 | 0 |
| 2 Eaton - Bandit Hideaway | 38 | 26.3 | 14.0 + 9.8 | 0 |
| 13 New Exford - Storm Patrol | 49 | 20.6 | 10.7 + 7.7 | 0 |
| 1 Eaton - Governor's Mansion | 53 | 18.9 | 9.4 + 7.6 | 0 |
| 16 Styk - Reinforcement | 61 | 16.3 | 7.7 + 6.1 | 0 |
| 24 Hesperus II - 'Mech Works | 75 | 13.4 | 5.9 + 4.6 | 0.6 |
| 48 Jungle - Heavy | 86 | 11.6 | 3.9 + 2.5 | 0.5 |
| 0 Eaton - Merc Alley | 90 | 11.1 | 4.0 + 3.0 | 1.4 |
| 3, 5, 7, 10, 20, 27, 30, 36, 42 | 90 | 11.1 | 2 to 8 together | 1.5 to 5.9 |

A frame at 90 Hz has 11.1 ms. Where no time is left to wait for the headset, the frame is longer than that - and
on every such map the two passes are nearly all of it.

## The slowest map, taken apart

Tharkad - Resupply, v11.71, the mean of about a thousand frames:

| Part of the frame | ms |
|---|---|
| The left eye's pass of the main camera | 16.0 |
| The right eye's pass | 12.5 |
| The game beside the passes (its logic, the HUD, the small cameras) | 2.7 |
| The mod's work at `Present` | 0.7 |
| The system's `Present`, waiting for the headset | 0.1 |
| **The frame** | **32.0** |

Inside the passes (the render thread's instruction pointer, read about every millisecond):

| Module | Share of a pass |
|---|---|
| `mw4hook.dll` | 82 to 85 % |
| `MW4Mercs.exe` | 9 to 11 % |
| this mod | 4 to 5 % (2.5 % since v11.72) |
| Direct3D, the graphics driver, Windows | 3 % |

Direct3D's own draw calls take 0.04 ms of a frame (750 calls): nothing waits for the graphics card there.

mw4hook's own clock (`[observer] timing=1`) says the same from the inside - per frame, both passes together:
its walk over the static objects 21.9 ms, its `DrawShape` 9.4 ms for 14 600 calls, `RenderNow` 9.2 ms. With one
camera (`[vr] double_draw=0`) the same scene takes mw4hook 17 to 20 ms: the second eye costs about two thirds of
the first again.

Its graphics-card clock (`[d3d11] gpu_profile=1`) shows 22 ms a frame with both eyes, 10 ms of them in "lighting
resolve + SSGI" of the first eye alone - and 0.9 ms for the same pass with one camera, 1.2 ms for the second eye.
Those 10 ms are the card waiting between two batches of commands while the processor prepares the second eye,
not work: the card is busy for some 12 ms of the 32.

Tried, without effect: mw4hook's statistics off (`[observer] enabled=0`); the mod's own cockpit off.

## What was done in this mod (v11.72)

| | Before | After |
|---|---|---|
| The mod's hooks of the draw calls, a frame | 1.0 ms | 0.57 ms |
| The mod's work at `Present` | 0.68 ms | 0.45 ms |
| Tharkad - Resupply | 31.2 pictures a second | 32.2 |

- The game's cockpit is read in every frame to measure its sway. Each of its vertices had its direction worked
  out (two `atan2`), for both eyes, although only the grid of a measured frame at a battle's start needs it.
- Each vertex found itself again in the frame before among all 1500; now among those near it in x. The same
  vertex is found.
- Which vertex shaders draw was noted for every draw under a lock, for a diagnosis key alone; now only with
  `[debug] level=1`.

The picture is the same: nothing of what is drawn was changed.

## What mw4hook could do

For the author of MW4Engine DX11 - the numbers above are what such a request rests on:

1. **One walk for both eyes.** The scene walked and culled once (with a frustum that holds both eyes'), its
   draws made for two views - instanced, or as two lists from the one walk. The walk and `DrawShape` are three
   quarters of a slow frame, and nearly all of it is the same for the eye 6 cm to the side.
2. **Per frame, not per camera,** whatever does not depend on the eye: light gathering, the choice of levels of
   detail, the clutter's records. (The sun's shadow maps are made once a frame already.)
3. A way for a caller to say "this is the second eye of the same frame" would be enough of an interface for both.

## Measuring

```
powershell -File tools\e2e\run.ps1 -Game <the game's folder> -Pilot <pilot> -Runtime active -Perf -Map 33 -Seconds 35 -Keys "8:W:25"
```

- `-Perf`: the mod writes `mw4vr_perf.csv`, a line for every frame with its length and its parts in microseconds
  (`mod/src/perf.cpp`); `tools/e2e/perf.py` prints the table. The eyes' pictures are not read back for their
  colours in such a run - reading them makes the graphics card wait, and a normal test run shows half the
  pictures a second for it.
- `-Samples`: also where the render thread is, about every millisecond, by module and by stretch of code
  (`mw4vr_perf_samples.txt`). For the mod's own functions build it with `set MW4VR_MAP=1` first: the map file
  `mod\build\d3d11.map` says which function lies where.
- `-Map <number>`: an instant action map; mw4hook's log names it. Maps of the multiplayer list (Concourse Pro
  among them) cannot be started this way.
- mw4hook's own clocks: `[observer] report_seconds=5`, `timing=1` and `[d3d11] gpu_profile=1` in `mw4hook.ini`.
- The headset must be awake, connected and lying still. Two runs of the same map differ by about 1 ms a frame.
