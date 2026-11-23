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
- Since v11.73 the mod can do the next best thing itself: let the game draw the left eye alone and repeat each
  of its draws for the right eye (`[vr] replay_right=1`, off by default). Where the processor is the limit that
  is half as fast again; where the graphics card is, it is slower - and with `replay_right=2` (v11.74) the mod
  takes whichever is faster where the battle is
  ([the right eye from the left eye's draws](#the-right-eye-from-the-left-eyes-draws)).

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

## The right eye from the left eye's draws

`[vr] replay_right=1` (v11.73, `mod/src/replay.cpp`). mw4hook's work for a pass - which things, in which
order, with which states - is the same for the eye 6 cm to the side. So the game draws the main camera once, for
the left eye, and the mod makes every draw of that pass a second time at once: into twins of the pass's targets,
with the constants that hold the eye's view worked out for the right eye. What is made once a frame anyway (the
sun's shadows, the terrain's pages) is not repeated; culling and levels of detail are the left eye's for both.

Measured on 2026-11-19, as above (a Mad Cat walking, 2600 x 2600 an eye, one run each; two runs of a map differ
by about 1 ms a frame):

| Map | The game draws both eyes | `replay_right=1` | A frame (ms) | What limits it then |
|---|---|---|---|---|
| 33 Tharkad - Resupply | 32 pictures a second | 50 | 20.2 | the processor (the left eye's pass 17.7 ms) |
| 2 Eaton - Bandit Hideaway | 38 | 48 | 20.8 | the processor |
| 13 New Exford - Storm Patrol | 49 | 56 | 17.9 | the processor, the graphics card close behind |
| 1 Eaton - Governor's Mansion | 53 | 80 | 12.5 | the processor |
| 16 Styk - Reinforcement | 61 | 79 | 12.7 | the processor |
| 24 Hesperus II - 'Mech Works | 75 | 89 | 11.2 | the headset |
| 48 Jungle - Heavy | 86 | **71** | 14.0 | the graphics card (7 ms of the frame waiting for it) |
| 0 Eaton - Merc Alley, 10 Halloran V | 90 | 90 | 11.1 | the headset |

The pass that is left costs a little more than the left eye's did (16.0 -> 17.7 ms on Tharkad): the second draws
and their constants are the mod's work now, about 1.5 ms.

**Why it can be slower.** The graphics card gets the same draws as before, but in another order: left, right,
left, right, each pair with a change of all render targets between them, instead of one eye after the other. On
a map that already filled the card's time (Jungle - Heavy: dense plants, drawn over each other many times) that
order costs it about 2.5 ms a frame. Changing the targets alone, with nothing drawn for the right eye, costs
nothing.

What the card loses is the change of targets itself, once something is drawn between two of them (found on
2026-11-20 with mw4hook's graphics-card clock, `[d3d11] gpu_profile=1`, on Jungle - Championship):

| | The graphics card's time for a frame | Pictures a second |
|---|---|---|
| the game draws both eyes | 11.9 ms | 82 |
| `replay_right=1` | 17.1 ms | 57 |
| `replay_right=1`, the right eye's draws made into the left eye's targets (a wrong picture, to measure) | 9.4 ms | 89.5 |

The 144 draws of the opaque models take the card 0.05 ms for an eye when the game draws the eyes one after the
other, and 1.5 ms for both with replay: about 5 microseconds for each change of the targets. The water's
mirrored view goes from 0.7 to 2.8 ms. To be rid of it, the right eye's draws would have to be collected and
made in one go for each set of targets (a deferred context), with a copy of every buffer the game writes anew
between two draws. That is not done: `replay_right=2` and the list below choose the way instead.

**Nothing is drawn beyond the fog** (v11.84; `[vr] fog_cull=1`, `mcFogCut` in `mod/src/core.h`). mw4hook draws
four kilometres far (`far_scale=4.0`); a map's fog is whole at about one. What lies beyond has the fog's colour
alone, and drawing it took most of the processor's time for a frame. The far plane the game culls with is pulled
in to a sixteenth beyond the fog's end (the distances are read from mw4hook); the projection stays as it is.
Measured, looking ahead from the start: Tharkad 53 -> 89 pictures a second, Eaton - Bandit Hideaway at night
37 -> 63, Styk 72 -> 79; the pictures compared on seven maps show no difference.

**`replay_right=2`: whichever is faster** (v11.74; `mcAutoStep` in `mod/src/core.h`). Which of the two a frame
waits for is told by the system's `Present`: it waits long when the graphics card is behind, hardly when the
processor is. A battle's frames are watched 45 at a time. When they are slower than the headset's rate and the
limit is the one the other way would ease - the processor while the game draws both eyes, the graphics card
while the draws are repeated - the other way is tried for 70 frames and kept if it is faster by a sixteenth; if
not, the next trial waits 900 frames, then twice as long each time. Since v11.78 a battle begins with the draws
repeated (on most maps the faster way: its first seconds are not spent finding that out; until then it began
with the game drawing both eyes), and the game's own way is tried only where it can win: the processor's part of
a frame, with three quarters of the left eye's pass again for the second one, must still come out a sixteenth
under the frame as it is. Measured with v11.74: Tharkad 51 pictures a second (the draws repeated from the second second on), Jungle - Heavy 87
(never left the game's own way), Storm Patrol 55, Merc Alley 90. `mw4vr.log` tells each trial and its end.
A trial that brings nothing is a second of the slower way.

v11.75 mends what a first test in the headset found in v11.73 and v11.74: on a map where the game frees a
texture that Direct3D had unbound by itself, the mod went on with the dead pointer (wrong pictures, then a
crash); the plants were drawn for the right eye where the left eye sees them (their matrix lies in a buffer of
its own); and a draw of the mod's own cockpit could set off a second "right eye's draw" inside the first.

`replay_right=2` is the default since v11.85.

**The way a battle begins with, by its mission** (v11.86; `MC_GAME_DRAWS` in `mod/src/core.h`). All 54
instant-action missions were measured both ways on 2026-11-20 (v11.85 with the fog cull, a Mad Cat walking
16 seconds from the start, 2600 x 2600 an eye, one run each): on 28 both ways reach the headset's 90 pictures
a second, on 3 replay is faster by far (Eaton - Bandit Hideaway 66 against 49, Halloran V - Escort 87 against
66, Tharkad - Overlord 60 against 44), on 23 the game drawing both eyes is faster by more than a thirtieth -
the jungle arenas, the factory and coliseum fights, New Exford, most of Styk (Jungle - Championship 83 against
57, Factory - Championship 89 against 61). On every one of them replay needs less of the processor (a quarter
to a third); where it loses, the graphics card is the limit.

A battle on one of those 23 missions begins with the game drawing both eyes, every other one with replay as
before; from there on the mod watches the frames and changes the way as described above. The mission's name
is read from mw4hook's log (`mw4hook.log` in the game's folder: the line that names the mission's files,
`...|missions\<name>\...`). That line is written about a second into the battle's first frames - while the
picture is still dark (`load_hold`) -, so the log is asked every 45 frames until it is there; when it does not
come, the battle stays with replay. `mw4vr.log` tells which mission was found and how the battle began. The numbers hold for the machine they were measured
on; another graphics card or picture size moves the line, and the watching evens that out.

What was checked: the right eye's constants against what mw4hook itself writes for that eye (`replay_right=2`
writes the comparison - the matrices agree to 2e-4, the camera's place exactly); pictures of both ways on a
still scene (the right eyes differ no more than the left eyes do, by clouds and wind); the end-to-end test,
26 of 26 with the switch on.

What is not the same as when the game draws both eyes: culling, the levels of detail and the order of the
see-through things are the left eye's; fog and the terrain's detail are worked out from the left eye's place
(3 cm off). Not checked yet: a battle with many mechs, the zoom window, a headset on a head.

## What mw4hook could do

For the author of MW4Engine DX11 - the numbers above are what such a request rests on:

1. **One walk for both eyes.** The scene walked and culled once (with a frustum that holds both eyes'), its
   draws made for two views - instanced, or as two lists from the one walk. The walk and `DrawShape` are three
   quarters of a slow frame, and nearly all of it is the same for the eye 6 cm to the side.
2. **Per frame, not per camera,** whatever does not depend on the eye: light gathering, the choice of levels of
   detail, the clutter's records. (The sun's shadow maps are made once a frame already.)
3. A way for a caller to say "this is the second eye of the same frame" would be enough of an interface for both.

## A video's cost

Measured on 2026-11-22 (RTX 5080, Virtual Desktop, 2600 x 2600 pixels an eye, Tharkad - Resupply, the mech
standing; the headset not worn, so the frames are not paced by it). One run, a video (Ctrl+F5, 5200 x 2600 at 60
pictures a second, HEVC at 122 Mbit/s, with sound) switched on and off every twelve seconds:

| | A frame (ms) | The mod's work at `Present` (ms) |
|---|---|---|
| video on | 9.39 | 0.72 |
| off | 9.08 | 0.61 |
| on | 9.58 | 0.69 |
| off | 9.16 | 0.63 |

A video makes a frame 0.3 to 0.4 ms longer (about 4 %), 0.1 ms of it the mod's own work on the render thread; the
rest is the graphics card's (the picture drawn once more, the encoder). No picture was left out in any run.
Not measured: a video at `render_height=3840`, and one on a graphics card that is the limit already.

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
- `[vr] replay_right=2` or `3` in `mw4vr.ini` (the game draws both eyes itself then): once, some seconds into a
  battle, `mw4vr_replay.txt` - with 2 the right eye's constants as mw4hook wrote them against what `replay.cpp`
  makes of the left eye's; with 3 what each eye's pass called and wrote.
- `-Map <number>`: an instant action map; mw4hook's log names it. Maps of the multiplayer list (Concourse Pro
  among them) cannot be started this way.
- mw4hook's own clocks: `[observer] report_seconds=5`, `timing=1` and `[d3d11] gpu_profile=1` in `mw4hook.ini`.
- The headset must be awake, connected and lying still. Two runs of the same map differ by about 1 ms a frame.
