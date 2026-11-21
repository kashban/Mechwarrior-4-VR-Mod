// ---------------------------------------------------------------------------------------------
// head tracking at the source: the game's CameraElement::Draw (0x4b6ec0 in MW4Mercs.exe, hooked by
// mw4hook with a jmp to its own camera path) is redirected through camThunk. Before the camera is
// drawn, its camera -> world matrix (3x4 at +0x80: rows of (axis x, axis y, axis z, translation))
// is moved by the head pose of the eye this frame is for; mw4hook then culls, lights and draws the
// world from there, so everything follows (terrain, shadows, effects, the cockpit, the target marks).
// The axes of the game's camera space are matched to the view space (x right, y up, z forward) once
// by comparing the matrix with g_vWorldToClip of the same frame.

#include "mod.h"

static const uintptr_t CAM_DRAW = GM_CAMERA_DRAW; // CameraElement::Draw (mw4hook_abi.h)
static void *g_camOrig;                           // where its jmp went (mw4hook's detour)
volatile bool g_camHooked, g_srcCam;              // hook installed; [vr] source_camera=1
float g_camA[9];                                  // view-space basis vectors in camera-local coordinates (columns)
volatile bool g_camCal;                           // g_camA known
static float g_camLast[12];                       // the main camera's matrix as the game left it (this frame)
static unsigned g_camLastFrame = ~0u;
static void *g_camMain;                       // the main camera's object
static float g_camWritten[12], g_camBase[12]; // what we wrote last time, and the game's matrix under it
static float g_camSeat[12]; // the game's matrix as the eyes are moved from: its own, a glance's turn taken out
volatile int g_camEye = -1; // the eye the main camera was moved for, this frame
unsigned g_camEyeFrame = ~0u;
static volatile LONG g_statCamMoves, g_statCamCalls;
float g_camEyeBase[2][12]; // the game's matrix under each eye's latest image
bool g_camEyeBaseOk[2];
enum { CAM_ANC = 5 };
static float g_camAnc[CAM_ANC][12]; // the camera element's ancestors' local -> world matrices, this frame
static uint8_t *g_camAncPtr[CAM_ANC];
static int g_camAncN;
static unsigned g_camAncFrame = ~0u;

static bool camIsMain(uint8_t *cam) {
    // the viewport (normalised x0 y0 x1 y1 at +0x2c8): the whole screen
    __try {
        const float *vp = (const float *)(cam + CAM_VIEWPORT);
        // seen in the game: 1 1 0 0 (far corner first); either order, the full extent on both axes
        return fabsf(fabsf(vp[0] - vp[2]) - 1) < 0.01f && fabsf(fabsf(vp[1] - vp[3]) - 1) < 0.01f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// called from eyeLearnLayout with a fresh, unmoved g_vWorldToClip: which camera-local axis is
// right, up and forward
void camCalibrate() {
    if (g_camCal) return;
    if (g_camLastFrame != g_frame || !g_haveRight || g_P11 <= 0) {
        static unsigned logs;
        if (logs < 8 && g_frame % 300 == 0) {
            ++logs;
            logf("cam: not calibrated yet - main camera last seen in frame %d (now %u), right axis %d, P11 %.3f, "
                 "camera calls %ld",
                 (int)g_camLastFrame, g_frame, (int)g_haveRight, g_P11, g_statCamCalls);
        }
        return;
    }
    const float *m = g_camLast;
    float col[3][3]; // camera-local axis j in world: (m[0][j], m[1][j], m[2][j])
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) col[j][i] = m[i * 4 + j];
    const float *want[3] = {g_right, g_up, g_fwd};
    float A[9] = {};
    for (int k = 0; k < 3; ++k) {
        int best = -1;
        float bd = 0;
        for (int j = 0; j < 3; ++j) {
            float l = len3(col[j][0], col[j][1], col[j][2]);
            if (l < 1e-6f) continue;
            float d = (col[j][0] * want[k][0] + col[j][1] * want[k][1] + col[j][2] * want[k][2]) / l;
            if (fabsf(d) > fabsf(bd)) bd = d, best = j;
        }
        if (best < 0 || fabsf(bd) < 0.98f) {
            static int fails;
            if (fails++ < 5)
                logf("cam: calibration failed for axis %d (best dot %.3f) - matrix [%.3f %.3f %.3f %.1f | %.3f %.3f "
                     "%.3f %.1f | %.3f %.3f %.3f %.1f], "
                     "right %.3f %.3f %.3f up %.3f %.3f %.3f fwd %.3f %.3f %.3f",
                     k, bd, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], g_right[0],
                     g_right[1], g_right[2], g_up[0], g_up[1], g_up[2], g_fwd[0], g_fwd[1], g_fwd[2]);
            return;
        }
        A[best * 3 + k] = bd > 0 ? 1.0f : -1.0f; // view axis k = sign * local axis best
    }
    memcpy(g_camA, A, sizeof A);
    g_camCal = true;
    char ax[3][4];
    for (int k = 0; k < 3; ++k)
        for (int j = 0; j < 3; ++j)
            if (A[j * 3 + k]) snprintf(ax[k], 4, "%c%c", A[j * 3 + k] > 0 ? '+' : '-', 'x' + j);
    logf("cam: camera axes matched - view right/up/forward = local %s %s %s; camera at %.1f %.1f %.1f", ax[0], ax[1],
         ax[2], m[3], m[7], m[11]);
}

volatile bool g_camInMain;         // the main camera is being drawn (its projection is the one to learn)
volatile bool g_doubleDraw = true; // [vr] double_draw: the main camera drawn once per eye in every frame
volatile LONG g_tonemapCount;      // "tonemap into the frame" labels seen (the camera's picture is in the frame)
unsigned g_camBothFrame = ~0u;     // the frame both eyes were drawn in

// diagnosis: the annotation labels inside the main camera's Draw, for each eye's pass of one frame,
// written to mw4vr_labels.txt (Ctrl+F9, and once some seconds after both eyes are first drawn)
volatile bool g_mechMeasure;    // mech.cpp: this frame's left-eye pass is measured
volatile int g_camPassEye = -1; // the eye whose pass of the main camera is being drawn
volatile bool g_lblDump;
volatile int g_lblEye;
std::string g_lblBuf[2];
static void lblWrite() {
    std::string a, b;
    {
        std::lock_guard<std::mutex> lk(g_labelMx);
        a.swap(g_lblBuf[0]);
        b.swap(g_lblBuf[1]);
    }
    FILE *f = fileOpen((g_dir + L"mw4vr_labels.txt").c_str(), L"ab");
    if (!f) return;
    fprintf(f, "==== frame %u, left eye's pass ====\n%s==== frame %u, right eye's pass ====\n%s\n", g_frame, a.c_str(),
            g_frame, b.c_str());
    fclose(f);
    logf("cam: labels of both eyes' passes written to mw4vr_labels.txt (%u / %u bytes)", (unsigned)a.size(),
         (unsigned)b.size());
}

// Diagnosis ([vr] cam_dump=1): the game's code is packed on disk. Once in a battle its image as it runs is written to
// mw4vr_dump_exe.bin (offset = address - image base), and three times (some seconds apart) the main camera's
// element and the elements above it to mw4vr_dump_cam_<n>.txt - for finding where the camera's matrix is
// made and what else looks through it.
bool camRead(const void *p, void *out, size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), p, out, n, &got) && got == n;
}
static void camDump(uint8_t *cam) {
    static int done;
    static unsigned first = ~0u;
    static int want = -1;
    if (want < 0) want = GetPrivateProfileIntW(L"vr", L"cam_dump", 0, (g_dir + L"mw4vr.ini").c_str());
    if (!want || done >= 3 || g_menuMode) return;
    if (first == ~0u) first = g_frame;
    static const unsigned when[3] = {300, 1500, 3000};
    if (g_frame - first < when[done]) return;
    if (done == 0) {
        uint8_t *base = (uint8_t *)GetModuleHandleW(nullptr);
        IMAGE_DOS_HEADER dh;
        IMAGE_NT_HEADERS32 nh;
        if (camRead(base, &dh, sizeof dh) && camRead(base + dh.e_lfanew, &nh, sizeof nh)) {
            DWORD size = nh.OptionalHeader.SizeOfImage;
            FILE *f = fileOpen((g_dir + L"mw4vr_dump_exe.bin").c_str(), L"wb");
            if (f) {
                std::vector<uint8_t> page(0x1000);
                DWORD ok = 0;
                for (DWORD o = 0; o < size; o += 0x1000) {
                    if (camRead(base + o, page.data(), 0x1000))
                        ++ok;
                    else
                        memset(page.data(), 0, 0x1000);
                    fwrite(page.data(), 1, 0x1000, f);
                }
                fclose(f);
                logf("cam: the game's image written to mw4vr_dump_exe.bin (base %p, %lu bytes, %lu of %lu pages "
                     "readable)",
                     base, size, ok, size / 0x1000);
            }
        }
    }
    wchar_t name[64];
    swprintf(name, 64, L"mw4vr_dump_cam_%d.txt", done);
    FILE *f = fileOpen((g_dir + name).c_str(), L"wb");
    if (f) {
        uint8_t *e = cam;
        uint8_t buf[0x400]; // (outside the loop: an older cppcheck takes the next element's address, read from it, for
                            // a pointer into it)
        for (int lvl = 0; e && lvl < 8; ++lvl) {
            if (!camRead(e, buf, sizeof buf)) {
                fprintf(f, "element %d at %p: not readable\n", lvl, e);
                break;
            }
            uint32_t vt = *(uint32_t *)buf;
            fprintf(f, "element %d at %p vtable %08x frame %u\n", lvl, e, vt, g_frame);
            uint32_t slots[96];
            if (camRead((void *)(uintptr_t)vt, slots, sizeof slots)) {
                fprintf(f, " vtable:");
                for (int i = 0; i < 96; ++i) fprintf(f, " %08x", slots[i]);
                fprintf(f, "\n");
            }
            for (int o = 0; o < 0x400; o += 16) {
                const uint32_t *u = (const uint32_t *)(buf + o);
                const float *v = (const float *)(buf + o);
                fprintf(f, " +%03x: %08x %08x %08x %08x | %12.5g %12.5g %12.5g %12.5g\n", o, u[0], u[1], u[2], u[3],
                        v[0], v[1], v[2], v[3]);
            }
            e = *(uint8_t **)(buf + EL_PARENT);
        }
        fclose(f);
        logf("cam: camera elements written to mw4vr_dump_cam_%d.txt (frame %u)", done, g_frame);
    }
    ++done;
}

// The camera element's own (local) matrix: 3x4 at +0x20, a copy at +0x50; the game makes the element's
// world matrix (+0x80) from its parent's and this one, every frame. It is the identity in the game. With
// [vr] cam_local=1 the head's pose (the middle between the eyes) is written there: whatever the game works
// out from the camera before it is drawn - which trees and tiles exist - then looks where the head looks,
// not where the mech's torso points. The mech's own view is then the parent's world matrix.
static volatile int g_camLocal = -1;
static uint8_t *g_camLocalCam;   // the element whose own matrix holds the head's pose
static float g_camLocalOrig[12]; // its own matrix as the game had it
static float g_camLocalLast[12]; // what we wrote
// the game's own matrix back (the camera left, tracking off, a menu)
static void camLocalRestore() {
    if (!g_camLocalCam) return;
    __try {
        if (memcmp(g_camLocalCam + EL_LOCAL, g_camLocalLast, 48) == 0) {
            memcpy(g_camLocalCam + EL_LOCAL, g_camLocalOrig, 48);
            memcpy(g_camLocalCam + EL_LOCAL_COPY, g_camLocalOrig, 48);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    logf("cam: the camera element's own matrix is the game's again, frame %u", g_frame);
    g_camLocalCam = nullptr;
}
// the mech's own view of this camera (its world matrix without the head's pose); false: the game's world
// matrix is that already
static bool camLocalBase(uint8_t *cam, float *base) {
    if (cam != g_camLocalCam) return false;
    if (memcmp(cam + EL_LOCAL, g_camLocalLast, 48) != 0) { // the game wrote its own matrix anew (a moving camera)
        memcpy(g_camLocalOrig, cam + EL_LOCAL, 48);
        return false;
    }
    uint8_t *par = *(uint8_t **)(cam + EL_PARENT);
    if (!par) return false;
    mcMul34((const float *)(par + EL_WORLD), g_camLocalOrig, base);
    return true;
}
static void camLocalWrite(uint8_t *cam, const float *Rc, const float *tc) {
    if (cam != g_camLocalCam) {
        camLocalRestore();
        memcpy(g_camLocalOrig, cam + EL_LOCAL, 48);
        g_camLocalCam = cam;
        const float *o = g_camLocalOrig;
        logf("cam: the head's pose goes into the camera element's own matrix (cam_local=1), frame %u; the game's: "
             "[%.3f %.3f %.3f %.2f | %.3f %.3f %.3f %.2f | %.3f %.3f %.3f %.2f]",
             g_frame, o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7], o[8], o[9], o[10], o[11]);
    }
    float h[12], l[12];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) h[i * 4 + j] = Rc[i * 3 + j];
        h[i * 4 + 3] = tc[i];
    }
    mcMul34(g_camLocalOrig, h, l);
    memcpy(cam + EL_LOCAL, l, sizeof l);
    memcpy(cam + EL_LOCAL_COPY, l, sizeof l);
    memcpy(g_camLocalLast, l, sizeof l);
}

static volatile int g_camPlanes = -1;
static void camLocalIni() {
    g_camLocal = GetPrivateProfileIntW(L"vr", L"cam_local", 0, (g_dir + L"mw4vr.ini").c_str());
    g_camPlanes = GetPrivateProfileIntW(L"vr", L"cam_planes", 1, (g_dir + L"mw4vr.ini").c_str());
}

// The camera's frustum in the world: six planes (normal, d: inside is n.p <= d) at +0x1ac - far, near, right,
// left, bottom, top - which the game makes from the camera's world matrix when it updates the scene, i.e. from
// the mech's own view; what is walked and culled with them while the camera is drawn (trees, tiles) then
// vanished where the head looked past the torso's view. Before each eye's Draw they are made anew from that
// eye's matrix, a little wider than the headset sees; after it the game's are back.
// The far plane pulled in to where the map's fog is whole (core.h: mcFogCut; [vr] fog_cull=0: not). The fog's
// distances are mw4hook's: its deferred view keeps what it last told of them.
static float camFogCut(float farD) {
    static int on = -1;
    static const float *fog;
    static float told = -1;
    if (on < 0) {
        on = GetPrivateProfileIntW(L"vr", L"fog_cull", 1, (g_dir + L"mw4vr.ini").c_str()) != 0;
        uint8_t *base = hkBase();
        uint8_t at[7] = {};
        if (on && base && camRead(base + HK_AT_FOG, at, sizeof at) && at[0] == 0x0F && at[1] == 0x11 && at[2] == 0x05 &&
            *(uint32_t *)(at + 3) == (uint32_t)(uintptr_t)(base + HK_FOG))
            fog = (const float *)(base + HK_FOG);
        else if (on)
            logf("cam: mw4hook's fog distances are not where they were - the far plane stays where it is");
    }
    float v[5];
    if (!fog || !camRead(fog, v, sizeof v)) return 0;
    const float cut = mcFogCut(v[3], v[4], farD);
    if (cut != told) {
        told = cut;
        if (cut > 0)
            logf("cam: the fog is whole at %.0f m - nothing is drawn beyond %.0f m (the far plane: %.0f m)", v[4], cut,
                 farD);
        else
            logf("cam: no fog that ends before the far plane (%.0f..%.0f m, far %.0f m) - drawn as far as that", v[3],
                 v[4], farD);
    }
    return cut;
}
static float g_camPlanesSaved[24];
static bool g_camPlanesIn;
static void camPlanesWrite(uint8_t *cam, const float *base, const float *out) {
    float *pl = (float *)(cam + CAM_PLANES);
    static int told;
    McFrustum fr;
    if (!mcFrustumPlanes(pl, base, out, g_camA, *(float *)(cam + CAM_TANGENTS), &fr)) {
        // the layout as it is known: plane 0 looks along the mech's view, plane 1 against it
        if (told++ < 3)
            logf("cam: the frustum planes at +0x1ac are not as expected (%.3f %.3f) - left alone", fr.d0, fr.d1);
        return;
    }
    if (float cut = camFogCut(fr.farD)) fr.planes[3] -= fr.farD - cut;
    if (!g_camPlanesIn) memcpy(g_camPlanesSaved, pl, sizeof g_camPlanesSaved);
    g_camPlanesIn = true;
    memcpy(pl, fr.planes, sizeof fr.planes);
    if (told == 0) {
        told = 100;
        logf("cam: frustum planes made from the head's view (cam_planes=1): far %.0f, near %.2f, tangent %.3f", fr.farD,
             fr.nearD, fr.t);
    }
}
static void camPlanesRestore(uint8_t *cam) {
    if (!g_camPlanesIn) return;
    g_camPlanesIn = false;
    memcpy(cam + CAM_PLANES, g_camPlanesSaved, sizeof g_camPlanesSaved);
}

// the camera matrix of an eye: the game's (base) moved by that eye's pose; Rc, tc: the pose in the camera's
// own coordinates
static void camEyeMatrix(const float *base, int eye, float *out, float *Rc, float *tc) {
    // head pose in view space (R columns = new axes, t) -> camera-local: Rc = A R A^T, tc = A t
    const float *A = g_camA, *R = g_vrR[eye], *t = g_vrT[eye];
    float AR[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            AR[i * 3 + j] = A[i * 3 + 0] * R[0 * 3 + j] + A[i * 3 + 1] * R[1 * 3 + j] + A[i * 3 + 2] * R[2 * 3 + j];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            Rc[i * 3 + j] = AR[i * 3 + 0] * A[j * 3 + 0] + AR[i * 3 + 1] * A[j * 3 + 1] + AR[i * 3 + 2] * A[j * 3 + 2];
    for (int i = 0; i < 3; ++i) tc[i] = A[i * 3 + 0] * t[0] + A[i * 3 + 1] * t[1] + A[i * 3 + 2] * t[2];
    // camera -> world: M' = M [Rc tc]; rows of m: (M00 M01 M02 Tx)
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            out[i * 4 + j] =
                base[i * 4 + 0] * Rc[0 * 3 + j] + base[i * 4 + 1] * Rc[1 * 3 + j] + base[i * 4 + 2] * Rc[2 * 3 + j];
        out[i * 4 + 3] = base[i * 4 + 3] + base[i * 4 + 0] * tc[0] + base[i * 4 + 1] * tc[1] + base[i * 4 + 2] * tc[2];
    }
}

// (A trial, [vr] arms_glance=1.) While the pilot glances to a side (the game's own look-left / look-right keys)
// the game draws the player's mech - the arms too, moving as they do - and turns its camera about its upright
// axis, smoothly, up to a quarter. That turn is taken out again here: the view stays the seat's, and the mech's
// parts stay in the picture.
// How far the camera is turned is read from the game's own cockpit (sway.cpp has that shape's matrix): its
// heading in the camera's space against what it was when the pilot first sat. Only that one angle is taken, and it is
// the full quarter exactly once it is near it: the cockpit also sways with the mech's steps, and a view tied to it
// frame by frame trembled.
// [vr] arms_glance_hold=1 (default): the mod holds the glance key itself while the pilot sits in the mech, so the
// parts are always there; arms_glance_key=LEFT or RIGHT.
static bool g_glanceKeyDown;
static int g_glanceVk = VK_LEFT;
static void camGlanceKey(bool want) {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    const bool front = pid == GetCurrentProcessId();
    if (!front) {
        g_glanceKeyDown = false; // (with another window in front the game let the key go itself)
        return;
    }
    // Held, it is pressed again a few times a second: the pilot's own press and release of the same key ends the
    // game's glance, and nothing tells the mod (a key held down repeats by itself anyway).
    if (want == g_glanceKeyDown && !(want && g_frame % 8 == 0)) return;
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = (WORD)MapVirtualKeyW((UINT)g_glanceVk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY | (want ? 0 : KEYEVENTF_KEYUP);
    if (SendInput(1, &in, sizeof in)) g_glanceKeyDown = want;
}
static void camGlance(float *base) {
    static int want = -1, hold = 1;
    if (want < 0) {
        const std::wstring ini = g_dir + L"mw4vr.ini";
        want = GetPrivateProfileIntW(L"vr", L"arms_glance", 0, ini.c_str());
        hold = GetPrivateProfileIntW(L"vr", L"arms_glance_hold", 1, ini.c_str());
        wchar_t k[16];
        GetPrivateProfileStringW(L"vr", L"arms_glance_key", L"LEFT", k, 16, ini.c_str());
        g_glanceVk = !_wcsicmp(k, L"RIGHT") ? VK_RIGHT : VK_LEFT;
    }
    if (!want) return;
    if (want == 2) { // (diagnosis: the camera's and the cockpit's headings, nothing changed)
        static unsigned at;
        if (g_frame != at && g_frame % 6 == 0) {
            at = g_frame;
            const float *w = g_swayShapeW;
            logf("cam: DIAG2 frame %u cam yaw %.1f pos %.2f %.2f %.2f | cockpit (frame %d) yaw %.1f pos %.2f %.2f %.2f",
                 g_frame, atan2f(base[8], base[0]) * 57.2958f, base[3], base[7], base[11], (int)g_swayShapeWFrame,
                 atan2f(w[8], w[0]) * 57.2958f, w[3], w[7], w[11]);
        }
        return;
    }
    static float ref[12], angle; // angle: the glance's turn (radians) that is taken out now
    static unsigned realFrame = ~0u, seenFrame = ~0u;
    static bool haveRef;
    static int seen; // frames in a row with the cockpit's matrix
    const float *A = g_camA;
    if (realFrame != g_frame) {
        const float was = angle;
        // This frame's camera against the cockpit's matrix of the frame before (this frame's comes later, with its
        // draws): the mech turns little in a frame, the glance's camera much - and at times it jumps, when the key
        // is pressed again while the camera swings back. With the camera of the frame before as well, the view
        // was a frame behind the swing and shook with it.
        if (g_swayShapeWFrame == g_frame - 1 && g_camCal) {
            float inv[12], rel[12], invRef[12], d[12];
            mcInv34(base, inv);
            mcMul34(inv, g_swayShapeW, rel);
            if (!haveRef) memcpy(ref, rel, sizeof ref), haveRef = true;
            seenFrame = g_frame;
            ++seen;
            mcInv34(ref, invRef);
            mcMul34(rel, invRef, d);
            // d turns the view's forward axis (the camera's own coordinates) towards its right axis by the angle
            float fr = 0, ff = 0;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    fr += A[i * 3 + 0] * d[i * 4 + j] * A[j * 3 + 2], ff += A[i * 3 + 2] * d[i * 4 + j] * A[j * 3 + 2];
            float a = atan2f(fr, ff);
            const float q = 1.5707963f, snap = 0.05f; // (3 degrees)
            if (fabsf(a) < snap) a = 0;
            if (fabsf(a) > q || fabsf(fabsf(a) - q) < snap) a = a > 0 ? q : -q;
            angle = a;
        } else if (seenFrame == ~0u || g_frame - seenFrame > 45) { // no cockpit for a while: another view
            angle = 0, haveRef = false, seen = 0;
        }
        if ((angle != 0) != (was != 0))
            logf("cam: frame %u - %s", g_frame,
                 angle != 0 ? "a glance to a side: its turn is taken out, the mech's parts stay in the picture"
                            : "the glance is over");
        realFrame = g_frame;
        if (hold)
            camGlanceKey(haveRef && seen > 45 && g_frame - seenFrame < 10 && g_mechSeated && !g_mechSeat.outside &&
                         !g_menuMode);
    }
    if (angle != 0) { // base * Y: Y turns about the view's upright axis, in the camera's own coordinates
        const float c = cosf(angle), s = sinf(angle);
        float Y[12] = {}, o[12];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                const float r_i = A[i * 3 + 0], u_i = A[i * 3 + 1], f_i = A[i * 3 + 2];
                const float r_j = A[j * 3 + 0], u_j = A[j * 3 + 1], f_j = A[j * 3 + 2];
                Y[i * 4 + j] = c * (r_i * r_j + f_i * f_j) + u_i * u_j + s * (r_i * f_j - f_i * r_j);
            }
        mcMul34(base, Y, o);
        memcpy(base, o, sizeof o);
    }
}

// (Diagnosis, [debug] level=1: a file mw4vr_memdump.txt in the game's folder has the player's vehicle, the camera
// and what they point to - two steps deep - written to mw4vr_mem_<n>.bin: records of address, size, depth, bytes.
// For finding, by comparing such pictures, what the game changes when it begins to draw the player's own mech.)
static void camMemDump(uint8_t *cam) {
    static unsigned at = ~0u;
    static int n;
    if (!g_dbg || g_frame == at || g_frame % 20) return;
    at = g_frame;
    const std::wstring trig = g_dir + L"mw4vr_memdump.txt";
    if (GetFileAttributesW(trig.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    DeleteFileW(trig.c_str());
    uintptr_t veh = 0;
    {
        uint32_t refp = 0, ref = 0, key = 0, hold = 0, v = 0;
        if (camRead((const void *)GM_PLAYER_REF, &refp, 4) && refp && camRead((const void *)(uintptr_t)refp, &ref, 4) &&
            camRead((const void *)GM_PLAYER_KEY, &key, 4) && (key ^ ref) &&
            camRead((const void *)(uintptr_t)((key ^ ref) + PL_HOLDER), &hold, 4) &&
            (hold ^ GM_PLAYER_XOR) >= 0x10000 &&
            camRead((const void *)(uintptr_t)((hold ^ GM_PLAYER_XOR) + PL_VEHICLE), &v, 4))
            veh = v;
    }
    wchar_t name[64];
    swprintf(name, 64, L"mw4vr_mem_%03d.bin", n++);
    FILE *f = fileOpen((g_dir + name).c_str(), L"wb");
    if (!f) return;
    struct Item {
        uintptr_t p;
        uint32_t size, depth;
    };
    std::vector<Item> q;
    std::unordered_set<uintptr_t> seen;
    if (veh >= 0x10000) q.push_back({veh, 0x4000, 0}), seen.insert(veh);
    q.push_back({(uintptr_t)cam, 0x400, 0}), seen.insert((uintptr_t)cam);
    std::vector<uint8_t> buf;
    size_t written = 0;
    const uintptr_t img = (uintptr_t)GetModuleHandleW(nullptr);
    for (size_t i = 0; i < q.size() && written < 60000; ++i) {
        const Item it = q[i];
        buf.assign(it.size, 0);
        if (!camRead((const void *)it.p, buf.data(), it.size)) {
            if (it.size <= 0x100 || !camRead((const void *)it.p, buf.data(), 0x100)) continue;
            buf.resize(0x100);
        }
        const uint32_t head[3] = {(uint32_t)it.p, (uint32_t)buf.size(), it.depth};
        fwrite(head, 4, 3, f);
        fwrite(buf.data(), 1, buf.size(), f);
        ++written;
        uint32_t vt0;
        memcpy(&vt0, buf.data(), 4);
        const bool cls = vt0 >= img && vt0 < img + GM_KNOWN.image; // an object of one of the game's classes
        if (it.depth >= 4 || (it.depth >= 2 && !cls)) continue;
        for (size_t o = 0; o + 4 <= buf.size(); o += 4) {
            uint32_t v;
            memcpy(&v, buf.data() + o, 4);
            if (v < 0x10000 || v >= 0x7fff0000 || (v & 3) || !seen.insert(v).second) continue;
            q.push_back({v, 0x400, it.depth + 1});
        }
    }
    fclose(f);
    logf("cam: DUMP %ls at frame %u - vehicle %p, camera %p, %u records", name, g_frame, (void *)veh, cam,
         (unsigned)written);
}

// (A trial, [vr] arms_own=1.) Every object of the player's mech - the vehicle and what hangs on it, some seventy
// objects of the game's own classes - has a word at +0x158 that is 2 while the pilot looks ahead from the seat and
// 0x20 while they glance to a side; and the game draws the mech only then (found by comparing the memory of both
// states). Here the word is set to 0x20 before the camera is drawn, in every frame the pilot sits in the mech: the
// mech's own parts are drawn without a glance - the arms where they are, moving as the game moves them.
enum { OWN_STATE = 0x158, OWN_SEAT = 2, OWN_SEEN = 0x20, OWN_MAX = 1200 };
// Beside them: small objects of one class (0x78c638), one for a part each, with the same word at +0x30 and the part
// they belong to at +0x34. They lie elsewhere in the heap and nothing found here points to them - so the heap is
// searched for them, once when the pilot sits down.
enum : uint32_t { OWN_LINK_CLASS = 0x78c638, OWN_LINK_STATE = 0x30, OWN_LINK_OWNER = 0x34, OWN_LINK_MAX = 8192 };
static uintptr_t g_ownLink[OWN_LINK_MAX];
static int g_ownLinkN;
// every place in a block of memory where such an object begins
static void camOwnScan(const uint8_t *base, size_t size) {
    __try {
        for (size_t o = 0; o + 0x44 <= size && g_ownLinkN < OWN_LINK_MAX; o += 4)
            if (*(const uint32_t *)(base + o) == OWN_LINK_CLASS) g_ownLink[g_ownLinkN++] = (uintptr_t)(base + o);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static uintptr_t g_ownList[OWN_MAX];
static int g_ownN;
static uintptr_t camPlayerVehicle() {
    uint32_t refp = 0, ref = 0, key = 0, hold = 0, v = 0;
    if (camRead((const void *)GM_PLAYER_REF, &refp, 4) && refp && camRead((const void *)(uintptr_t)refp, &ref, 4) &&
        camRead((const void *)GM_PLAYER_KEY, &key, 4) && (key ^ ref) &&
        camRead((const void *)(uintptr_t)((key ^ ref) + PL_HOLDER), &hold, 4) && (hold ^ GM_PLAYER_XOR) >= 0x10000 &&
        camRead((const void *)(uintptr_t)((hold ^ GM_PLAYER_XOR) + PL_VEHICLE), &v, 4))
        return v;
    return 0;
}
// an object of one of the game's classes (its first word points into the game's image) with the state word
static bool camOwnIs(uintptr_t p, uint32_t *state) {
    uint32_t vt = 0;
    const uintptr_t img = (uintptr_t)GetModuleHandleW(nullptr);
    return camRead((const void *)p, &vt, 4) && vt >= img && vt < img + GM_KNOWN.image &&
           camRead((const void *)(p + OWN_STATE), state, 4) && (*state == OWN_SEAT || *state == OWN_SEEN);
}
static void camOwnCollect() {
    g_ownN = 0;
    const uintptr_t veh = camPlayerVehicle();
    uint32_t st = 0;
    if (veh < 0x10000 || !camOwnIs(veh, &st)) return;
    std::unordered_set<uintptr_t> seen{veh}, parts;
    std::vector<std::pair<uintptr_t, int>> q{{veh, 0}};
    std::vector<uint8_t> buf;
    for (size_t i = 0; i < q.size() && q.size() < 150000; ++i) {
        const uintptr_t p = q[i].first;
        const int depth = q[i].second;
        const bool own = camOwnIs(p, &st);
        if (own && g_ownN < OWN_MAX) g_ownList[g_ownN++] = p + OWN_STATE, parts.insert(p);
        if (depth >= 3 || (depth == 2 && !own))
            continue; // (the mech's parts hang two steps below the vehicle, in lists of other objects)
        buf.assign(depth ? 0x400 : 0x1000, 0);
        if (!camRead((const void *)p, buf.data(), buf.size())) continue;
        for (size_t o = 0; o + 4 <= buf.size(); o += 4) {
            uint32_t v;
            memcpy(&v, buf.data() + o, 4);
            if (v < 0x10000 || v >= 0x7fff0000 || (v & 3) || !seen.insert(v).second) continue;
            q.push_back({v, depth + 1});
        }
    }
    const int ownParts = g_ownN;
    g_ownLinkN = 0;
    MEMORY_BASIC_INFORMATION mi;
    for (uintptr_t a = 0x10000; a < 0x7fff0000 && VirtualQuery((const void *)a, &mi, sizeof mi) == sizeof mi;
         a = (uintptr_t)mi.BaseAddress + mi.RegionSize) {
        if (mi.State == MEM_COMMIT && mi.Type == MEM_PRIVATE && mi.Protect == PAGE_READWRITE &&
            mi.RegionSize <= (256u << 20))
            camOwnScan((const uint8_t *)mi.BaseAddress, mi.RegionSize);
    }
    for (int i = 0; i < g_ownLinkN && g_ownN < OWN_MAX; ++i) {
        uint32_t owner = 0, stw = 0;
        if (camRead((const void *)(g_ownLink[i] + OWN_LINK_OWNER), &owner, 4) && parts.count(owner) &&
            camRead((const void *)(g_ownLink[i] + OWN_LINK_STATE), &stw, 4) && (stw == OWN_SEAT || stw == OWN_SEEN))
            g_ownList[g_ownN++] = g_ownLink[i] + OWN_LINK_STATE;
    }
    logf("cam: the player's mech - %d parts, %d of %d link objects are theirs", ownParts, g_ownN - ownParts,
         g_ownLinkN);
}
// (two more words change with a glance: +0x1e4 of the one object of class 0x7a339c, and +0x2e8 of the object the
// vehicle points to at +0x3b8 - tried one by one, bits 2 and 4 of arms_own)
static int camOwnSet(int want) {
    int n = 0;
    if (want & 4) {
        __try {
            const uintptr_t veh = g_ownN ? g_ownList[0] - OWN_STATE : 0;
            uint32_t *o = veh ? *(uint32_t **)(veh + 0x3b8) : nullptr;
            if (o && o[0x2e8 / 4] == 0) o[0x2e8 / 4] = 1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    for (int i = 0; i < g_ownN; ++i) {
        __try {
            uint32_t *s = (uint32_t *)g_ownList[i];
            if (*s == OWN_SEAT) *s = OWN_SEEN, ++n;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_ownList[i] = g_ownList[--g_ownN];
            --i;
        }
    }
    return n;
}
static void camOwn() {
    static int want = -1;
    if (want < 0) want = GetPrivateProfileIntW(L"vr", L"arms_own", 0, (g_dir + L"mw4vr.ini").c_str());
    static unsigned at = ~0u, listAt = ~0u;
    if (!want || at == g_frame) return;
    at = g_frame;
    if (!g_mechSeated || g_mechSeat.outside || g_menuMode) {
        g_ownN = 0, listAt = ~0u;
        return;
    }
    if (listAt == ~0u || g_frame - listAt >= 1800) {
        const int was = g_ownN;
        camOwnCollect();
        if (listAt == ~0u || was != g_ownN)
            logf("cam: frame %u - %d objects of the player's mech are told to be drawn from the seat", g_frame, g_ownN);
        listAt = g_frame;
    }
    const int n = camOwnSet(want);
    static int told;
    if (told < 6 && n)
        ++told, logf("cam: frame %u - %d of them were set back by the game since the frame before", g_frame, n);
}

static void camPreEye(uint8_t *cam, int eyeOv) {
    InterlockedIncrement(&g_statCamCalls);
    {
        static int logs;
        if (g_dbg && cam && logs < 24 && g_frame > 200) {
            ++logs;
            __try {
                const float *vp = (const float *)(cam + CAM_VIEWPORT), *mm = (const float *)(cam + EL_WORLD);
                logf("cam: Draw %p frame %u viewport %.3f %.3f %.3f %.3f | +0x80 [%.3f %.3f %.3f %.1f | %.3f %.3f %.3f "
                     "%.1f | %.3f %.3f %.3f %.1f]",
                     cam, g_frame, vp[0], vp[1], vp[2], vp[3], mm[0], mm[1], mm[2], mm[3], mm[4], mm[5], mm[6], mm[7],
                     mm[8], mm[9], mm[10], mm[11]);
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    if (!cam || !camIsMain(cam)) return;
    if (eyeOv <= 0) camDump(cam);
    if (eyeOv <= 0) camMemDump(cam);
    if (eyeOv <= 0) camOwn();
    float *m = (float *)(cam + EL_WORLD);
    // the game's own matrix: what is there, unless it is still what we wrote last time
    float base[12];
    memcpy(base, m, sizeof base);
    if (cam == g_camMain && memcmp(m, g_camWritten, sizeof base) == 0) memcpy(base, g_camBase, sizeof base);
    if (g_camLocal < 0) camLocalIni();
    __try {
        camLocalBase(cam, base);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_camMain = cam;
    // (base stays the game's own matrix - it is what is put back after the Draw, and what the game's frustum
    // planes were made from; seat is what the eyes see from)
    float seat[12];
    memcpy(seat, base, sizeof seat);
    camGlance(seat);
    memcpy(g_camSeat, seat, sizeof seat);
    memcpy(g_camLast, seat, sizeof seat);
    g_camLastFrame = g_frame;
    __try { // the scene graph above the camera: parent at +0x10, matrix at +0x80
        int n = 0;
        uint8_t *e = *(uint8_t **)(cam + EL_PARENT);
        while (e && n < CAM_ANC) {
            g_camAncPtr[n] = e;
            memcpy(g_camAnc[n++], e + EL_WORLD, 48);
            e = *(uint8_t **)(e + EL_PARENT);
        }
        g_camAncN = n;
        g_camAncFrame = g_frame;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_camAncN = 0;
    }
    if (memcmp(m, base, sizeof base) != 0) memcpy(m, base, sizeof base); // our last move undone first
    bool act = g_srcCam && g_camCal && g_vrTracking && g_eyeMode && !g_menuMode;
    int eye = eyeOv >= 0 ? eyeOv : (eyeSign() > 0 ? 1 : 0);
    if (!act || !g_vrPoseOk[eye]) {
        camLocalRestore();
        return;
    }
    const float *A = g_camA, *t = g_vrT[eye];
    float out[12], Rc[9], tc[3];
    camEyeMatrix(seat, eye, out, Rc, tc);
    if (g_camLocal > 0 && eyeOv <= 0) { // once per frame: the middle between the eyes, for the game's next update
        float tm[3], tcm[3];
        bool two = g_vrPoseOk[0] && g_vrPoseOk[1];
        for (int i = 0; i < 3; ++i) tm[i] = two ? 0.5f * (g_vrT[0][i] + g_vrT[1][i]) : t[i];
        for (int i = 0; i < 3; ++i) tcm[i] = A[i * 3 + 0] * tm[0] + A[i * 3 + 1] * tm[1] + A[i * 3 + 2] * tm[2];
        camLocalWrite(cam, Rc, tcm);
    } else if (g_camLocal <= 0) {
        camLocalRestore();
    }
    memcpy(m, out, sizeof out);
    if (g_camPlanes > 0) camPlanesWrite(cam, base, out);
    memcpy(g_camWritten, out, sizeof out);
    memcpy(g_camBase, base, sizeof base);
    g_camEye = eye;
    g_camEyeFrame = g_frame;
    memcpy(g_camEyeBase[eye], seat, sizeof seat);
    g_camEyeBaseOk[eye] = true;
    InterlockedIncrement(&g_statCamMoves);
}

// after the camera is drawn its matrix is the game's again: the HUD's target marks and the aiming
// are worked out from the mech's own view, not the head's
// Diagnosis (trees and other things vanish when the head turns away from the mech's own view): something
// outside the camera's Draw seems to choose what exists from the camera's matrix. [vr] cam_keep, Ctrl+F6:
// 0 = the game's matrix back right after the Draw (as before); 1 = the head's matrix stays (the HUD's marks
// and the aiming then follow the head - for the test); 2 = the game's for the HUD, the head's again from
// Present until the next Draw.
static volatile int g_camKeep = -1;
void camKeepKey() {
    if (g_camKeep < 0) g_camKeep = GetPrivateProfileIntW(L"vr", L"cam_keep", 0, (g_dir + L"mw4vr.ini").c_str()) % 3;
    static bool was;
    // (a key for diagnosis, [debug] level=1 only: pressed by mistake beside Ctrl+F11 it changed the picture for good)
    bool down = g_dbg && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_F6) & 0x8000);
    if (down && !was) {
        g_camKeep = (g_camKeep + 1) % 3;
        logf("cam: keep mode %d (%s), frame %u", (int)g_camKeep,
             g_camKeep == 0   ? "the game's matrix back after the Draw"
             : g_camKeep == 1 ? "the head's matrix stays"
                              : "the head's matrix from Present to the next Draw",
             g_frame);
        for (int i = 0; i <= g_camKeep; ++i) Beep(900 + 300 * g_camKeep, 70), Sleep(60);
    }
    was = down;
}
// at Present (the HUD is drawn): mode 2 puts the head's matrix back
void camAtPresent() {
    if (g_camKeep != 2 || !g_camMain || g_camEyeFrame != g_frame) return;
    __try {
        float *m = (float *)((uint8_t *)g_camMain + EL_WORLD);
        if (memcmp(m, g_camBase, sizeof g_camBase) == 0) memcpy(m, g_camWritten, sizeof g_camWritten);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void __stdcall camPost(uint8_t *cam) {
    if (cam && cam == g_camMain) camPlanesRestore(cam);
    if (g_camKeep == 1) return;
    if (cam && cam == g_camMain && g_camEyeFrame == g_frame) {
        float *m = (float *)(cam + EL_WORLD);
        if (memcmp(m, g_camWritten, sizeof g_camWritten) == 0) memcpy(m, g_camBase, sizeof g_camBase);
    }
}

// CameraElement::Draw is a thiscall (ecx = the camera) with one stack argument; mw4hook's detour
// also takes edx along. Both are passed through unchanged.
static __declspec(naked) int __cdecl camCallOrig(void *cam, void *edxv, void *arg) {
    __asm {
        push ebp
        mov ebp, esp
        mov ecx, [ebp + 8]
        mov edx, [ebp + 12]
        push dword ptr [ebp + 16]
        call dword ptr [g_camOrig]
        mov esp, ebp
        pop ebp
        ret
    }
}

static int __cdecl camDraw(uint8_t *cam, void *edxv, void *arg) {
    bool both = g_doubleDraw && g_srcCam && g_camCal && g_vrTracking && g_eyeMode && cam && camIsMain(cam) &&
                g_vrPoseOk[0] && g_vrPoseOk[1] && !g_menuMode;
    bool isMain = cam && camIsMain(cam);
    if (!both) {
        camPreEye(cam, -1);
        g_camInMain = isMain;
        int r;
        if (isMain) {
            PerfScope perf(PF_EYE0);
            r = camCallOrig(cam, edxv, arg);
        } else {
            r = camCallOrig(cam, edxv, arg);
        }
        g_camInMain = false;
        camPost(cam);
        return r;
    }
    // the left eye: drawn, its picture taken out of the frame; then the right eye over it
    int r = 0;
    bool replay = false; // the right eye's picture is made with the left eye's draws (replay.cpp)
    for (int e = 0; e < 2; ++e) {
        bool ok = false;
        LONG before = g_tonemapCount;
        if (e == 1 && replay) {
            // not drawn by the game: its picture is in the twin of the frame's texture
            memcpy(g_camEyeBase[1], g_camSeat, sizeof g_camSeat);
            g_camEyeBaseOk[1] = true;
            g_camEye = 1;
            PerfScope perf(PF_GRAB);
            ok = eyeGrab(1, true);
            if (!ok) { // the game draws it after all
                replay = false;
                --e;
                continue;
            }
        } else {
            camPreEye(cam, e);
            g_lblEye = e;
            g_camPassEye = e;
            if (e == 0 && (rpWanted() || g_rpMode == RP_COMPARE)) {
                float c1[12], Rc[9], tc[3], k1[16];
                camEyeMatrix(g_camSeat, 1, c1, Rc, tc);
                g_kEyeOverride = 1;
                buildK();
                memcpy(k1, g_K, sizeof k1);
                eyeTlRightSet(k1, g_kOn && g_kTlOnly);
                if (g_rpMode == RP_COMPARE)
                    rpCheckK(g_camWritten, c1);
                else
                    replay = rpBegin(g_camWritten, c1);
            }
            g_kEyeOverride = e;
            buildK(); // the cockpit's extra movement for this eye
            g_kEyeOverride = -1;
            g_camInMain = true;
            rpPassBegin(e);
            {
                PerfScope perf(PF_EYE0 + e);
                r = camCallOrig(cam, edxv, arg);
            }
            rpPassEnd(e);
            if (e == 0 && replay) replay = rpEnd();
            g_camInMain = false;
            g_camPassEye = -1;
            camPost(cam);
            if (g_tonemapCount != before) {
                PerfScope perf(PF_GRAB);
                ok = eyeGrab(e);
            }
        }
        if (!ok) {
            static int fails;
            if (e == 0) {
                // the picture is not in the frame when the camera returns (or the frame is not known yet):
                // this frame goes on with one eye; after some tries the old way for good
                if (++fails == 300) {
                    g_doubleDraw = false;
                    logf("cam: both eyes per frame not possible (the camera's picture is not in the frame when it "
                         "returns, tonemap %d) - alternating",
                         (int)(g_tonemapCount != before));
                }
                g_camEye = 0;
                return r;
            }
        } else if (e == 1) {
            static unsigned firstBoth = ~0u;
            if (g_camBothFrame == ~0u) {
                logf("cam: both eyes are drawn in every frame");
                firstBoth = g_frame;
            }
            g_camBothFrame = g_frame;
            if (g_lblDump) {
                g_lblDump = false;
                lblWrite();
            } else if (g_dbg && firstBoth != ~0u && g_frame - firstBoth == 900) {
                g_lblDump = true; // the next frame
            }
        }
    }
    return r;
}

static __declspec(naked) void camThunk() {
    __asm {
        push ebp
        mov ebp, esp
        push dword ptr [ebp + 8]
        push edx
        push ecx
        call camDraw
        add esp, 12
        mov esp, ebp
        pop ebp
        ret 4
    }
}

void camInstall() {
    if (g_camHooked) return;
    gmCheck();
    uint8_t *p = (uint8_t *)CAM_DRAW;
    __try {
        if (p[0] != 0xE9) {
            logf("cam: CameraElement::Draw at %p does not start with mw4hook's jmp (%02x %02x %02x %02x %02x) - not "
                 "hooked",
                 p, p[0], p[1], p[2], p[3], p[4]);
            g_camHooked = true; // do not try again
            g_srcCam = false;
            return;
        }
        g_camOrig = p + 5 + *(int32_t *)(p + 1);
        DWORD old;
        if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return;
        *(int32_t *)(p + 1) = (int32_t)((uint8_t *)&camThunk - (p + 5));
        VirtualProtect(p, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 5);
        g_camHooked = true;
        logf("cam: CameraElement::Draw redirected (was a jmp to %p)", g_camOrig);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("cam: CameraElement::Draw not readable - not hooked");
        g_camHooked = true;
        g_srcCam = false;
    }
}
