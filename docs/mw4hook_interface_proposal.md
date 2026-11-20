# MW4Engine and the VR mod – what an interface could look like

This follows up on `report_for_mw4engine_author.md`. Since that report the VR mod has become a complete thing:
both eyes at 90 Hz through OpenXR, head tracking, the HUD as its own layer, a cockpit of its own. It still does
all of it **from the outside**, as a `d3d11.dll` proxy that watches MW4Engine and patches around it. This note
lists what the mod takes from `mw4hook.dll` today, why each of those is fragile, and what a small interface in
MW4Engine would replace it with.

Everything here is a proposal. Names, shapes and the order of work are yours to change; a subset already helps.
The section "Smallest useful set" at the end says which three pieces remove most of the fragile code.

Reference build: mw4hook `20261107_ea16ef2`, MekTek MekPak 3.1c.

## What the mod takes from mw4hook today

| What | How it is found today | Breaks when |
| --- | --- | --- |
| Is this the known build | PE time stamp and image size | any new build: the mod switches its memory access off |
| Frame phases (world, tonemap done, HUD/2D, water pass, small cameras) | beginnings of your annotation labels (`3D`, `after 3D`, `tonemap into the frame`, `water (RTIN`, `SSAA 2x2`, `camera:`) | a label is reworded |
| "The map is complete" after loading | first frame with the label `terrain: one RTIN draw` | the terrain pass is renamed or split |
| The right eye's picture | the left eye's draws are recorded and repeated with 17 shader constants rewritten by name (`g_mlrToClip`, `g_vWorldToClip`, `g_vClipToWorld`, `g_vCam`, …), plus the instance records (288 bytes, last frame's matrix at 224) and the clutter's view buffer | a constant is renamed, moved, or a new one holds the view |
| Things made once a frame, not once an eye | labels `RTIN tree of sun shadow`, `sun shadows:`, `terrain VT:` | those labels change |
| CPU-path meshes (cockpit, some props) | your screen-space vertex shader is recognised by the hash of its bytecode and replaced | the shader is recompiled |
| The backdrop of a small camera | pixel shader hash | the shader is recompiled |
| Fog start and end | 5 floats at a fixed offset, checked against the instruction that writes them | the code moves |
| Picture size, output window and rectangle, cursor mapping | fixed offsets, each checked against the instruction that uses it | the code moves |
| Water reflection wanted in this pass | one byte at a fixed offset | the code moves |
| Culling | the game's frustum planes are overwritten in the camera element before your pass reads them | you cull from something else |

On the Direct3D side the mod has to hot-patch the immediate context's draw slots, because the runtime rewrites
them. That, too, would go away with an interface.

## Wishes, in the order of their use

### 1. An interface with a version

One exported function that hands out a table, so nothing has to be found by address, hash or text:

```c
// mw4hook.dll exports this one function; NULL = this version is not offered
const MW4HookApi *MW4Hook_GetApi(uint32_t version);
```

A client asks for the version it was built for. A build that no longer offers it returns `NULL`, and the VR mod
shows the game flat instead of crashing.

### 2. Two views a frame

The largest gain in speed and in correctness. Today the right eye costs either a second full pass of the game or a
replay of recorded draws; on heavy maps most of the frame's time is spent inside mw4hook on the CPU.

- The client gives, per eye, a view matrix relative to the game's camera and an asymmetric projection.
- MW4Engine renders both eyes into textures the client provides (or into its own, handed back).
- **Once a frame:** game update, sun shadow cascades, clutter build, water simulation, virtual texture page
  requests (from the union of both eyes), particle simulation.
- **Once an eye:** G-buffer, lighting, SSAO/SSGI, TAA with a history per eye (or off), bloom, tonemap.
- CPU-path meshes need the eye's offset as well: a constant added to `clip.x` in the screen-space vertex shader
  gives exactly the right parallax.
- If it fits your renderer: both eyes in one pass (instanced, render target array). Not required.

### 3. Culling from the client's frustum

The head looks elsewhere than the mech's torso, and both eyes together see more than one camera.

- A frustum for culling given by the client (planes, or the two eye views and you build the union).
- Optional and useful without VR as well: pull the far plane in to where the fog is complete. With
  `far_scale=4.0` the pass draws 4 km while the fog is whole at about 1 km; cutting there took Tharkad from 53 to
  89 pictures a second in the VR mod, with pictures that compare equal.
- Fog start, fog end and the far distance readable.

### 4. Events and state instead of labels

Callbacks, or a state block the client reads once a frame:

- Frame phases: frame begins, world done (before tonemap), tonemap done, HUD/2D begins, before Present.
- The map is loaded and everything of yours is in the picture (trees, grass, terrain) – the VR mod holds a black
  picture until then.
- What is on screen: menu, loading, battle, pause menu, cut scene; whether a zoom window or another small camera
  is drawn, and its rectangle.
- The map's name or id, and day or night.

### 5. HUD and 2D apart from the world

- HUD and 2D drawn into a texture of their own with alpha, when the client provides one. Today the VR mod cuts
  the HUD out of the frame and has special cases for the pause menu's sheet and for wide hint boxes.
- The cockpit's draws marked as such (today: CPU-path shapes nearer than a few metres).
- Small cameras (zoom, rear view) into targets of their own.

### 6. Sizes and targets

- The size the battle is rendered at, set by the client, whatever the window's size.
- Each eye's depth buffer and whether depth is reversed, so it can be given to OpenXR (better reprojection).
- Motion vectors, if you have them.

### 7. Smaller things

- The water's planar reflection per eye without a second full pass, or a switch for it.
- Screen-space effects that look wrong in a headset switchable: vignette, lens dirt, lens flares.
- A word that a `d3d11.dll` proxy in front of mw4hook stays allowed, or a way for the VR mod to be loaded by you
  instead (a named DLL you load when it is present).

## A header to start from

```c
#define MW4HOOK_API_VERSION 1

typedef struct MW4Eye {
    float fromCamera[4][4];                  // eye pose relative to the game's camera, row-major
    float tanLeft, tanRight, tanUp, tanDown; // asymmetric field of view
    ID3D11Texture2D *colour;                 // the tonemapped picture goes here
    ID3D11Texture2D *depth;                  // optional: the eye's depth
} MW4Eye;

typedef enum MW4Screen { MW4_MENU, MW4_LOADING, MW4_BATTLE, MW4_PAUSED, MW4_CUTSCENE } MW4Screen;

typedef struct MW4FrameInfo {
    uint32_t size;          // of this struct: fields may be added at its end
    MW4Screen screen;
    int worldComplete;      // 1 once everything of the map is drawn
    int smallCamera;        // 1 while a zoom window or the like is on screen
    float smallRect[4];     // its rectangle, 0..1 of the picture
    float fogStart, fogEnd; // metres; both 0 = no fog
    float farDistance;      // metres
    int reversedDepth;
    const char *map;        // the map's name or file
    int night;
} MW4FrameInfo;

typedef enum MW4Phase {
    MW4_FRAME_BEGIN, // before the game's update: the client sets the eyes here
    MW4_WORLD_DONE,  // lit world, before tonemap
    MW4_EYE_DONE,    // one eye's picture is in its target (arg: 0 left, 1 right)
    MW4_HUD_BEGIN,
    MW4_HUD_DONE,
    MW4_BEFORE_PRESENT
} MW4Phase;

typedef void (*MW4PhaseCallback)(MW4Phase phase, int arg, const MW4FrameInfo *info, void *user);

typedef struct MW4HookApi {
    uint32_t version;
    const char *build; // "20261107_ea16ef2"
    void (*SetPhaseCallback)(MW4PhaseCallback cb, void *user);
    void (*SetEyes)(const MW4Eye *eyes, int count); // 2 = stereo, 0 = as without a client
    void (*SetCullFrustum)(const float planes[6][4]); // NULL = from the eyes
    void (*SetHudTarget)(ID3D11Texture2D *hud);       // NULL = into the frame as now
    void (*SetRenderSize)(int width, int height);     // 0, 0 = from the window
    void (*SetFogCull)(int on);                       // far plane pulled in to the fog's end
    const MW4FrameInfo *(*GetFrameInfo)(void);
} MW4HookApi;

__declspec(dllexport) const MW4HookApi *MW4Hook_GetApi(uint32_t version);
```

All calls are made on the render thread, inside a phase callback or before the first frame. A function pointer
that is `NULL` means "not offered in this build"; the VR mod keeps its own way for that part.

## Smallest useful set

If time is short, these three remove nearly all of the fragile code:

1. `MW4Hook_GetApi` with a version.
2. `SetEyes`: two views a frame into given targets, shadows and the like once.
3. `SetPhaseCallback` with `MW4FrameInfo` (screen, world complete, fog).

The VR mod can take each piece as it comes: every part of it that an API call replaces is switched over, the rest
keeps working as now. I can test builds on a Quest 3 (OpenXR through Virtual Desktop) and send frame timings,
logs and pictures.

## What the VR mod can give back

- The fog cull and its numbers (above), usable without VR.
- Timings of your passes per map from the mod's own profiler, on request.
- The source of the VR mod is public; the file `mod/src/mw4hook_abi.h` holds everything it knows of mw4hook in
  one place.
