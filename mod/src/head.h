// What head.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_HEAD_H
#define MW4VR_HEAD_H

extern float g_near;            // n of the projection, from g_vWorldToClip (view.cpp)
extern float g_up[3], g_fwd[3]; // the game camera's up and forward axes (world)
extern float g_K[16];           // this frame's clip-space transform for the CPU path (the cockpit)
extern volatile bool g_kOn;     // this frame's transform is in use
extern unsigned g_kFrame;       // the frame it was built for
extern float g_vrR[2][9], g_vrT[2][3];
extern float g_vrRaw[2][3];  // the eye's position against the reference, view space, real metres
extern float g_cockpitDelta; // cockpit scale - world scale: the near CPU-path geometry's extra eye movement
extern float g_cockpitRange; // that geometry: nearer than this (game metres)
extern volatile bool g_kTlOnly;
extern int g_kEyeOverride; // buildK for this eye (both eyes drawn in one frame)
extern unsigned g_tlEpoch; // changes with every K: the CPU-path shader's buffer is written again            // K is for
extern float g_vrFov[2][4]; // per eye tangents left (<0), right, up, down (<0) to render with
extern volatile bool g_vrFovOk;
extern volatile bool g_vrPoseOk[2];
extern volatile bool g_vrTracking;   // [vr] tracking=1 and poses arriving
extern volatile int g_kEye;          // eye of this frame's transform: 0 left, 1 right
extern volatile bool g_srcCamActive; // head pose applied at the game's camera, calibrated and running
static inline int eyeSign() {
    int mode = g_eyeMode;
    return mode == 1 ? 1 : mode == 2 ? -1 : ((g_frame & 1) ? -1 : 1); // +1 right eye
}
// builds this frame's transform; called once per frame before anything is drawn
void buildK();
static inline bool kReady() { return g_kOn && g_kFrame == g_frame; }

#endif // MW4VR_HEAD_H
