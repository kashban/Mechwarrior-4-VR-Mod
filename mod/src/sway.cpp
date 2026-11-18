// ---------------------------------------------------------------------------------------------
// cockpit sway: the game's cockpit moves against the camera with the mech's steps; our cockpit, the
// near HUD panels and the parts on the glass follow it. It is measured from the vertices of the
// game's own cockpit, in every frame (mech.cpp, swvOnPresent); here is what the users of the sway
// read.
// (Until v11.57 the cockpit's element was looked for in the game's scene graph; for some mechs that
// search found only elements at the camera, none that moves.)

#include "mod.h"

volatile bool g_swayOn; // [vr] hud_sway=1
float g_swayGain = 1.0f;
// the sway (view space: x right, y up, z forward; game metres): p' = R p + T
float g_swayR[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, g_swayT[3];
volatile bool g_swayValid;

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
