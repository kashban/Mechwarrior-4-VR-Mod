// What sway.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_SWAY_H
#define MW4VR_SWAY_H

extern volatile bool g_swayOn; // [vr] hud_sway=1
extern float g_swayGain;
extern float g_swayR[9], g_swayT[3];
extern volatile bool g_swayValid;
void swayOrtho(float *R);
void swayHookInstall();
// the cockpit's step against the camera since the frame before, from its place in the world (sway.cpp)
int swayStep(float *w, float *d);
void swayShapeCounts(long *all, long *nearCam);

#endif // MW4VR_SWAY_H
