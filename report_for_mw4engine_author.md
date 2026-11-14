# MW4Engine and VR – findings from an external probe

Hi,

following up on my earlier message about VR: in the meantime I looked at how far VR can get **from the outside**, without touching your files. To do that, I built a small `d3d11.dll` proxy for the game folder that observes your renderer and, as an experiment, moves the camera sideways by an eye offset. Below are the results. I hope they show that a small official hook would get the whole way, while patching from the outside keeps chasing your updates.

Tested with build 20261107_ea16ef2 and MekTek 3.1c, on an RTX 5080, Windows 11.

## What works from the outside

- Your renderer loads `d3d11.dll`/`dxgi.dll` by bare name, so a proxy in the game folder works. Your annotation labels ("3D", "sun shadows: cascade n", "deferred view: TAA", "after 3D: HUD, 2D", …) made the frame structure easy to read. Thank you for those.
- **Eye offset by matrix patch.** A camera moved by `e` along its right axis means `clip.x += -P00·e` for every point. That is one float per world/mesh → clip matrix (row-major, index 3), with P00 = 1.299 at the default FOV. It works for:
  - `Mlr` (`g_mlrToClip`, `g_mlrPrevToClip`): meshes, terrain RTIN, culturals
  - `View` (`g_vWorldToClip`, `g_vSsgiPrevClip`)
  - `Cards`
  - the clutter's `g_ctView` (via `UpdateSubresource`)
  - the per-instance matrices in the `g_mlrInst` instance buffers
- **Inverse matrices** used to rebuild positions from depth: `g_vClipToWorld`, `g_fClipToWorld`, `g_wClipToWorld`. These get the matching rank-1 correction (Sherman–Morrison: subtract column 0 × row 3 / (1 + c·m30)), so lighting and shadows follow the eye.
- **Camera positions** (`g_cultCam`, `g_ctEye`, `g_vCam`, `g_camPos`, `g_wCam`, `g_rEye`, …) are moved along the right axis.
- **Light clusters:** `g_clView`, `g_clToWorld`, `g_clWorldToMlr`.
- **The CPU path.** `MLR_I_L_TMesh` meshes that still go through the game's CPU transform, including the cockpit, are drawn by VS `ad8cee09…` with screen-space vertices (x, y px, z, rhw). I replace that shader inside the "3D" section with an equivalent HLSL version that adds the same constant to `clip.x`. That gives exactly `-P00·e/w` on screen, so the parallax is correct.

With all of this, terrain, mechs, buildings, wrecks, both kinds of grass, water, sky, the cockpit, shadows and lighting shift correctly per eye.

## What I could not get cleanly

- **Effects.** Fire and smoke (`g_fx`/`g_fxEffects`, VS `899f1624…`/`8c76461f…`) and the sprite path (`g_sprites`, screen-space like the CPU path) still need individual work.
- **Some trees, shrubs and older wrecks.** I haven't found their camera input yet.
- **Fragility.** Every input has to be found separately: four constant-buffer layouts, the inverses, the positions, instance buffers, two screen-space paths. Every update of the engine can move any of them.
- **D3D11 itself.** It rewrites the immediate context's `Draw*`/`Dispatch*`/`UpdateSubresource*` vtable slots every frame, so an outside proxy has to hot-patch `d3d11.dll`. That works, but it is not something I'd want to ship.

## What a minimal hook in MW4Engine could look like

Your renderer already has several deferred views per frame (the main camera and the zoom windows). VR is "two more views", plus one extra column in the projection.

```c
// exported from mw4hook.dll; all optional for the engine to call
typedef struct {
    float viewOffset[4][4];   // eye pose relative to the game camera (row-major), applied after the game's view
    float tanLeft, tanRight, tanUp, tanDown;  // asymmetric FOV per eye
} MW4EyeView;

int  MW4Hook_GetVersion(void);
void MW4Hook_SetEyes(const MW4EyeView eyes[2]);           // set before the frame; 0 eyes = normal mode
void MW4Hook_SetEyeTargets(ID3D11Texture2D *left, ID3D11Texture2D *right); // tonemapped results go here
void MW4Hook_SetHudTarget(ID3D11Texture2D *hud);          // HUD/2D into its own texture (optional)
typedef void (*MW4FrameCallback)(void *user);              // called once per frame after the game update
void MW4Hook_SetFrameCallback(MW4FrameCallback cb, void *user);
```

Inside the engine:

- **Per eye:** the G-buffer, lighting, SSAO/SSGI, TAA (separate history, or off in VR), bloom and tonemap.
- **Once per frame:** the game update, the shadow cascades, the clutter build, the water simulation and the VT page requests (use the union of both eyes).
- **CPU-path meshes:** need the eye offset too. A constant added to `clip.x` in your screen-space VS is enough, as described above.

I'd do the OpenXR side as a separate DLL that only loads when it is present. I can share the probe code and its findings. It is a read-only observer plus the experiment, and it changes nothing in your files.

Thanks again for MW4Engine. Even without VR it is a big upgrade.
