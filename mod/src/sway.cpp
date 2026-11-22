// ---------------------------------------------------------------------------------------------
// cockpit sway: the game's cockpit moves against the camera with the mech's steps; our cockpit, the
// near HUD panels and the parts on the glass follow it. It is measured in every frame (mech.cpp,
// swvOnPresent): from the cockpit's place as the game hands it to its renderer (below), or, without
// that, from the vertices of the game's own cockpit. Here is what the users of the sway read.
// (Until v11.57 the cockpit's element was looked for in the game's scene graph; for some mechs that
// search found only elements at the camera, none that moves.)

#include "mod.h"

volatile bool g_swayOn; // [vr] hud_sway=1
float g_swayGain = 1.0f;
// the sway (view space: x right, y up, z forward; game metres): p' = R p + T
float g_swayR[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, g_swayT[3];
volatile bool g_swayValid;
float g_swayShapeW[12]; // the game's cockpit -> world, as the game handed it on last (cam.cpp, the glance)
unsigned g_swayShapeWFrame = ~0u;

void swayOrtho(float *R) { // Gram-Schmidt on the rows
    float *a = R, *b = R + 3, *c = R + 6;
    float l = len3(a[0], a[1], a[2]);
    if (l < 1e-6f) return;
    for (int i = 0; i < 3; ++i) a[i] /= l;
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    for (int i = 0; i < 3; ++i) b[i] -= d * a[i];
    l = len3(b[0], b[1], b[2]);
    if (l < 1e-6f) return;
    for (int i = 0; i < 3; ++i) b[i] /= l;
    c[0] = a[1] * b[2] - a[2] * b[1];
    c[1] = a[2] * b[0] - a[0] * b[2];
    c[2] = a[0] * b[1] - a[1] * b[0];
}

// ---------------------------------------------------------------------------------------------
// The cockpit's place, from the game itself. mw4hook draws the world on its own; the shapes it leaves to the
// game's MLRClipper::DrawShape - in a battle seen from the seat that is the game's cockpit, once for each pass of
// the main camera - it hands on through one pointer, which is led through here. With a shape comes its matrix
// into the world (the DrawShapeInformation of the MLR library: GM_SHAPE_TO_WORLD). Against the camera's own
// matrix that is the cockpit's pose in the view, whatever of the cockpit is in the picture - and the step from
// one frame's pose to the next is the sway's (mech.cpp, swvOnPresent, adds the steps up).
// (The vertices the game draws of its cockpit gave the sway alone until v11.76: with a small cockpit, or the
// head turned away from it, too few of them are in the picture, and the sway stood still.)
static void *g_swayShapeOrig;
static float g_swayPose[2][12];                      // the cockpit against the camera: this frame's, the one before
static unsigned g_swayPoseFrame[2] = {~0u, ~0u};     // the frames they are of
static volatile LONG g_swayShapes, g_swayShapesNear; // shapes handed on, and those at the camera (for the log)

static void __stdcall swayShape(const uint8_t *info) {
    if (!g_camInMain || g_camPassEye > 0 || !g_camCal || !g_camEyeBaseOk[0] || g_swayPoseFrame[0] == g_frame) return;
    InterlockedIncrement(&g_swayShapes);
    float m[12];
    __try {
        const float *p = *(const float *const *)(info + GM_SHAPE_TO_WORLD);
        if (!p) return;
        memcpy(m, p, sizeof m);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    // the cockpit sits at the camera; another shape left to the game (a sky's part) does not
    const float *b = g_camEyeBase[0];
    float dx = m[3] - b[3], dy = m[7] - b[7], dz = m[11] - b[11];
    if (!(dx * dx + dy * dy + dz * dz < GM_COCKPIT_REACH * GM_COCKPIT_REACH)) return;
    InterlockedIncrement(&g_swayShapesNear);
    memcpy(g_swayShapeW, m, sizeof m);
    g_swayShapeWFrame = g_frame;
    memcpy(g_swayPose[1], g_swayPose[0], sizeof g_swayPose[0]);
    g_swayPoseFrame[1] = g_swayPoseFrame[0];
    mcShapeInView(b, g_camA, m, g_swayPose[0]);
    g_swayPoseFrame[0] = g_frame;
}
// mech.cpp, at Present: the cockpit's step against the camera since the frame before (view space: a point that
// goes with it moves by d + w x p). 0: the game handed no cockpit on in this frame; 1: it did, and this is the
// step; 2: it did, but there is none to tell (the first frame, or a cut to another view)
int swayStep(float *w, float *d) {
    if (g_swayPoseFrame[0] != g_frame) return 0;
    if (g_swayPoseFrame[1] != g_frame - 1) return 2;
    return mcShapeStep(g_swayPose[1], g_swayPose[0], w, d) ? 1 : 2;
}
void swayShapeCounts(long *all, long *nearCam) {
    *all = InterlockedExchange(&g_swayShapes, 0);
    *nearCam = InterlockedExchange(&g_swayShapesNear, 0);
}
// (the game's DrawShape is a thiscall with one argument on the stack; both are passed on as they came)
static __declspec(naked) void swayShapeThunk() {
    __asm {
        push ecx
        push edx
        push dword ptr [esp + 12]
        call swayShape
        pop edx
        pop ecx
        jmp dword ptr [g_swayShapeOrig]
    }
}
void swayHookInstall() {
    static bool done;
    if (done) return;
    uint8_t *base = hkBase();
    if (!base) return;
    __try {
        void **slot = (void **)(base + HK_PTR_DRAWSHAPE);
        const uint8_t *at = base + HK_AT_DRAWSHAPE;
        if (at[0] != 0xFF || at[1] != 0x15 || *(uint32_t *)(at + 2) != (uint32_t)(uintptr_t)slot) {
            done = true;
            logf("sway: mw4hook's call of the game's DrawShape is not where it was - the sway is taken from the "
                 "cockpit's vertices alone");
            return;
        }
        if (!*slot) return; // not set up yet: later
        g_swayShapeOrig = *slot;
        *slot = (void *)swayShapeThunk;
        done = true;
        logf("sway: the game's DrawShape, as mw4hook calls it, goes through us - the cockpit's place is read there");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        done = true;
    }
}
