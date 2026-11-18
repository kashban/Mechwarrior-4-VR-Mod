// What cam.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_CAM_H
#define MW4VR_CAM_H

extern volatile bool g_camHooked, g_srcCam; // hook installed; [vr] source_camera=1
extern float g_camA[9];                     // view-space basis vectors in camera-local coordinates (columns)
extern volatile bool g_camCal;              // g_camA known
extern volatile int g_camEye;               // the eye the main camera was moved for, this frame
extern unsigned g_camEyeFrame;
extern float g_camEyeBase[2][12]; // the game's matrix under each eye's latest image
extern bool g_camEyeBaseOk[2];
// called from eyeLearnLayout with a fresh, unmoved g_vWorldToClip: which camera-local axis is
// right, up and forward
void camCalibrate();
extern volatile bool g_camInMain;    // the main camera is being drawn (its projection is the one to learn)
extern volatile bool g_doubleDraw;   // [vr] double_draw: the main camera drawn once per eye in every frame
extern volatile LONG g_tonemapCount; // "tonemap into the frame" labels seen (the camera's picture is in the frame)
extern unsigned g_camBothFrame;      // the frame both eyes were drawn in
extern volatile bool g_mechMeasure;  // mech.cpp: this frame's left-eye pass is measured
extern volatile int g_camPassEye;    // the eye whose pass of the main camera is being drawn
extern volatile bool g_lblDump;
extern volatile int g_lblEye;
extern std::string g_lblBuf[2];
// Diagnosis ([vr] cam_dump=1): the game's code is packed on disk. Once in a battle its image as it runs is written to
// mw4vr_dump_exe.bin (offset = address - image base), and three times (some seconds apart) the main camera's
// element and the elements above it to mw4vr_dump_cam_<n>.txt - for finding where the camera's matrix is
// made and what else looks through it.
bool camRead(const void *p, void *out, size_t n);
void camKeepKey();
// at Present (the HUD is drawn): mode 2 puts the head's matrix back
void camAtPresent();
void camInstall();

#endif // MW4VR_CAM_H
