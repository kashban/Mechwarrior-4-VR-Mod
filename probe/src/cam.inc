// ---------------------------------------------------------------------------------------------
// head tracking at the source: the game's CameraElement::Draw (0x4b6ec0 in MW4Mercs.exe, hooked by
// mw4hook with a jmp to its own camera path) is redirected through camThunk. Before the camera is
// drawn, its camera -> world matrix (3x4 at +0x80: rows of (axis x, axis y, axis z, translation))
// is moved by the head pose of the eye this frame is for; mw4hook then culls, lights and draws the
// world from there, so everything follows (terrain, shadows, effects, the cockpit, the target marks).
// The axes of the game's camera space are matched to the view space (x right, y up, z forward) once
// by comparing the matrix with g_vWorldToClip of the same frame. Included into main.cpp after head.inc.

static const uintptr_t CAM_DRAW = 0x4b6ec0; // CameraElement::Draw
static void *g_camOrig;                       // where its jmp went (mw4hook's detour)
static volatile bool g_camHooked, g_srcCam;   // hook installed; [vr] source_camera=1
static float g_camA[9];                       // view-space basis vectors in camera-local coordinates (columns)
static volatile bool g_camCal;                // g_camA known
static float g_camLast[12];                   // the main camera's matrix as the game left it (this frame)
static unsigned g_camLastFrame = ~0u;
static void *g_camMain;                       // the main camera's object
static float g_camWritten[12], g_camBase[12]; // what we wrote last time, and the game's matrix under it
static volatile int g_camEye = -1;            // the eye the main camera was moved for, this frame
static unsigned g_camEyeFrame = ~0u;
static volatile LONG g_statCamMoves, g_statCamCalls;
static float g_camEyeBase[2][12];             // the game's matrix under each eye's latest image
static bool g_camEyeBaseOk[2];
enum { CAM_ANC = 5 };
static float g_camAnc[CAM_ANC][12];           // the camera element's ancestors' local -> world matrices, this frame
static uint8_t *g_camAncPtr[CAM_ANC];
static int g_camAncN;
static void swayOnCamera();
static unsigned g_camAncFrame = ~0u;

static bool camIsMain(uint8_t *cam) {
    // the viewport (normalised x0 y0 x1 y1 at +0x2c8): the whole screen
    __try {
        const float *vp = (const float *)(cam + 0x2c8);
        // seen in the game: 1 1 0 0 (far corner first); either order, the full extent on both axes
        return fabsf(fabsf(vp[0] - vp[2]) - 1) < 0.01f && fabsf(fabsf(vp[1] - vp[3]) - 1) < 0.01f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// called from eyeLearnLayout with a fresh, unmoved g_vWorldToClip: which camera-local axis is
// right, up and forward
static void camCalibrate() {
    if (g_camCal) return;
    if (g_camLastFrame != g_frame || !g_haveRight || g_P11 <= 0) {
        static unsigned logs;
        if (logs < 8 && g_frame % 300 == 0) {
            ++logs;
            logf("cam: not calibrated yet - main camera last seen in frame %d (now %u), right axis %d, P11 %.3f, camera calls %ld",
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
                logf("cam: calibration failed for axis %d (best dot %.3f) - matrix [%.3f %.3f %.3f %.1f | %.3f %.3f %.3f %.1f | %.3f %.3f %.3f %.1f], "
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

static volatile bool g_camInMain;              // the main camera is being drawn (its projection is the one to learn)
static volatile bool g_doubleDraw = true;     // [vr] double_draw: the main camera drawn once per eye in every frame
static volatile LONG g_tonemapCount;          // "tonemap into the frame" labels seen (the camera's picture is in the frame)
static unsigned g_camBothFrame = ~0u;         // the frame both eyes were drawn in
static bool eyeGrab(int e);                   // hud.inc: the frame as it is -> this eye's texture

// diagnosis: the annotation labels inside the main camera's Draw, for each eye's pass of one frame,
// written to mw4vr_labels.txt (Ctrl+F9, and once some seconds after both eyes are first drawn)
static volatile bool g_mechMeasure;           // mech.inc: this frame's left-eye pass is measured
static volatile int g_camPassEye = -1;        // the eye whose pass of the main camera is being drawn
static volatile bool g_lblDump;
static volatile int g_lblEye;
static std::string g_lblBuf[2];
static void lblWrite() {
    std::string a, b;
    {
        std::lock_guard<std::mutex> lk(g_labelMx);
        a.swap(g_lblBuf[0]);
        b.swap(g_lblBuf[1]);
    }
    FILE *f = _wfopen((g_dir + L"mw4vr_labels.txt").c_str(), L"ab");
    if (!f) return;
    fprintf(f, "==== frame %u, left eye's pass ====\n%s==== frame %u, right eye's pass ====\n%s\n", g_frame, a.c_str(), g_frame, b.c_str());
    fclose(f);
    logf("cam: labels of both eyes' passes written to mw4vr_labels.txt (%u / %u bytes)", (unsigned)a.size(), (unsigned)b.size());
}

// Diagnosis: the game's code is packed on disk. Once in a battle its image as it runs is written to
// mw4vr_dump_exe.bin (offset = address - image base), and three times (some seconds apart) the main camera's
// element and the elements above it to mw4vr_dump_cam_<n>.txt - for finding where the camera's matrix is
// made and what else looks through it.
static bool camRead(const void *p, void *out, size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), p, out, n, &got) && got == n;
}
static void camDump(uint8_t *cam) {
    static int done;
    static unsigned first = ~0u;
    if (done >= 3 || g_menuMode) return;
    if (first == ~0u) first = g_frame;
    static const unsigned when[3] = {300, 1500, 3000};
    if (g_frame - first < when[done]) return;
    if (done == 0) {
        uint8_t *base = (uint8_t *)GetModuleHandleW(nullptr);
        IMAGE_DOS_HEADER dh;
        IMAGE_NT_HEADERS32 nh;
        if (camRead(base, &dh, sizeof dh) && camRead(base + dh.e_lfanew, &nh, sizeof nh)) {
            DWORD size = nh.OptionalHeader.SizeOfImage;
            FILE *f = _wfopen((g_dir + L"mw4vr_dump_exe.bin").c_str(), L"wb");
            if (f) {
                std::vector<uint8_t> page(0x1000);
                DWORD ok = 0;
                for (DWORD o = 0; o < size; o += 0x1000) {
                    if (camRead(base + o, page.data(), 0x1000)) ++ok;
                    else memset(page.data(), 0, 0x1000);
                    fwrite(page.data(), 1, 0x1000, f);
                }
                fclose(f);
                logf("cam: the game's image written to mw4vr_dump_exe.bin (base %p, %lu bytes, %lu of %lu pages readable)", base, size, ok, size / 0x1000);
            }
        }
    }
    wchar_t name[64];
    swprintf(name, 64, L"mw4vr_dump_cam_%d.txt", done);
    FILE *f = _wfopen((g_dir + name).c_str(), L"wb");
    if (f) {
        uint8_t *e = cam;
        for (int lvl = 0; e && lvl < 8; ++lvl) {
            uint8_t buf[0x400];
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
                fprintf(f, " +%03x: %08x %08x %08x %08x | %12.5g %12.5g %12.5g %12.5g\n", o, u[0], u[1], u[2], u[3], v[0], v[1], v[2], v[3]);
            }
            e = *(uint8_t **)(buf + 0x10);
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
static void camMul34(const float *a, const float *b, float *out) { // out = a o b (rows: axis x, y, z, translation)
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) out[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] + a[i * 4 + 1] * b[1 * 4 + j] + a[i * 4 + 2] * b[2 * 4 + j];
        out[i * 4 + 3] = a[i * 4 + 3] + a[i * 4 + 0] * b[3] + a[i * 4 + 1] * b[7] + a[i * 4 + 2] * b[11];
    }
}
// the game's own matrix back (the camera left, tracking off, a menu)
static void camLocalRestore() {
    if (!g_camLocalCam) return;
    __try {
        if (memcmp(g_camLocalCam + 0x20, g_camLocalLast, 48) == 0) {
            memcpy(g_camLocalCam + 0x20, g_camLocalOrig, 48);
            memcpy(g_camLocalCam + 0x50, g_camLocalOrig, 48);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    logf("cam: the camera element's own matrix is the game's again, frame %u", g_frame);
    g_camLocalCam = nullptr;
}
// the mech's own view of this camera (its world matrix without the head's pose); false: the game's world
// matrix is that already
static bool camLocalBase(uint8_t *cam, float *base) {
    if (cam != g_camLocalCam) return false;
    if (memcmp(cam + 0x20, g_camLocalLast, 48) != 0) { // the game wrote its own matrix anew (a moving camera)
        memcpy(g_camLocalOrig, cam + 0x20, 48);
        return false;
    }
    uint8_t *par = *(uint8_t **)(cam + 0x10);
    if (!par) return false;
    camMul34((const float *)(par + 0x80), g_camLocalOrig, base);
    return true;
}
static void camLocalWrite(uint8_t *cam, const float *Rc, const float *tc) {
    if (cam != g_camLocalCam) {
        camLocalRestore();
        memcpy(g_camLocalOrig, cam + 0x20, 48);
        g_camLocalCam = cam;
        const float *o = g_camLocalOrig;
        logf("cam: the head's pose goes into the camera element's own matrix (cam_local=1), frame %u; the game's: [%.3f %.3f %.3f %.2f | %.3f %.3f %.3f %.2f | %.3f %.3f %.3f %.2f]",
             g_frame, o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7], o[8], o[9], o[10], o[11]);
    }
    float h[12], l[12];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) h[i * 4 + j] = Rc[i * 3 + j];
        h[i * 4 + 3] = tc[i];
    }
    camMul34(g_camLocalOrig, h, l);
    memcpy(cam + 0x20, l, sizeof l);
    memcpy(cam + 0x50, l, sizeof l);
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
static float g_camPlanesSaved[24];
static bool g_camPlanesIn;
static void camPlanesWrite(uint8_t *cam, const float *base, const float *out) {
    float *pl = (float *)(cam + 0x1ac);
    const float *A = g_camA;
    auto axis = [&](const float *m, int k, float *v) { // view axis k (right, up, forward) in the world
        for (int i = 0; i < 3; ++i) v[i] = m[i * 4 + 0] * A[0 * 3 + k] + m[i * 4 + 1] * A[1 * 3 + k] + m[i * 4 + 2] * A[2 * 3 + k];
    };
    float fb[3], pb[3] = {base[3], base[7], base[11]};
    axis(base, 2, fb);
    // the layout as it is known: plane 0 looks along the mech's view, plane 1 against it
    float d0 = pl[0] * fb[0] + pl[1] * fb[1] + pl[2] * fb[2], d1 = pl[4] * fb[0] + pl[5] * fb[1] + pl[6] * fb[2];
    static int told;
    if (!(d0 > 0.98f && d1 < -0.98f)) {
        if (told++ < 3) logf("cam: the frustum planes at +0x1ac are not as expected (%.3f %.3f) - left alone", d0, d1);
        return;
    }
    float farD = pl[3] - (fb[0] * pb[0] + fb[1] * pb[1] + fb[2] * pb[2]);
    float nearD = -pl[7] - (fb[0] * pb[0] + fb[1] * pb[1] + fb[2] * pb[2]);
    if (!g_camPlanesIn) memcpy(g_camPlanesSaved, pl, sizeof g_camPlanesSaved);
    g_camPlanesIn = true;
    float r[3], u[3], f[3], p[3] = {out[3], out[7], out[11]};
    axis(out, 0, r), axis(out, 1, u), axis(out, 2, f);
    float t = fabsf(*(float *)(cam + 0x20c));
    if (!(t > 1.6f && t < 20)) t = 1.6f; // +-58 degrees and more: the headset's widest side is 55
    float fp = f[0] * p[0] + f[1] * p[1] + f[2] * p[2];
    float n[24];
    for (int i = 0; i < 3; ++i) n[i] = f[i], n[4 + i] = -f[i];
    n[3] = fp + farD, n[7] = -fp - (nearD > 0.05f ? nearD * 0.5f : nearD);
    const float *side[4] = {r, r, u, u};
    const float sg[4] = {1, -1, -1, 1};
    for (int k = 0; k < 4; ++k) {
        float v[3], l = 0;
        for (int i = 0; i < 3; ++i) v[i] = sg[k] * side[k][i] - t * f[i], l += v[i] * v[i];
        l = sqrtf(l);
        float *o = n + 8 + 4 * k;
        for (int i = 0; i < 3; ++i) o[i] = v[i] / l;
        o[3] = o[0] * p[0] + o[1] * p[1] + o[2] * p[2];
    }
    memcpy(pl, n, sizeof n);
    if (told == 0) {
        told = 100;
        logf("cam: frustum planes made from the head's view (cam_planes=1): far %.0f, near %.2f, tangent %.3f", farD, nearD, t);
    }
}
static void camPlanesRestore(uint8_t *cam) {
    if (!g_camPlanesIn) return;
    g_camPlanesIn = false;
    memcpy(cam + 0x1ac, g_camPlanesSaved, sizeof g_camPlanesSaved);
}

static void camPreEye(uint8_t *cam, int eyeOv) {
    InterlockedIncrement(&g_statCamCalls);
    {
        static int logs;
        if (cam && logs < 24 && g_frame > 200) {
            ++logs;
            __try {
                const float *vp = (const float *)(cam + 0x2c8), *mm = (const float *)(cam + 0x80);
                logf("cam: Draw %p frame %u viewport %.3f %.3f %.3f %.3f | +0x80 [%.3f %.3f %.3f %.1f | %.3f %.3f %.3f %.1f | %.3f %.3f %.3f %.1f]",
                     cam, g_frame, vp[0], vp[1], vp[2], vp[3], mm[0], mm[1], mm[2], mm[3], mm[4], mm[5], mm[6], mm[7],
                     mm[8], mm[9], mm[10], mm[11]);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }
    if (!cam || !camIsMain(cam)) return;
    if (eyeOv <= 0) camDump(cam);
    float *m = (float *)(cam + 0x80);
    // the game's own matrix: what is there, unless it is still what we wrote last time
    float base[12];
    memcpy(base, m, sizeof base);
    if (cam == g_camMain && memcmp(m, g_camWritten, sizeof base) == 0) memcpy(base, g_camBase, sizeof base);
    if (g_camLocal < 0) camLocalIni();
    __try {
        camLocalBase(cam, base);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_camMain = cam;
    memcpy(g_camLast, base, sizeof base);
    g_camLastFrame = g_frame;
    __try { // the scene graph above the camera: parent at +0x10, matrix at +0x80
        int n = 0;
        uint8_t *e = *(uint8_t **)(cam + 0x10);
        while (e && n < CAM_ANC) {
            g_camAncPtr[n] = e;
            memcpy(g_camAnc[n++], e + 0x80, 48);
            e = *(uint8_t **)(e + 0x10);
        }
        g_camAncN = n;
        g_camAncFrame = g_frame;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_camAncN = 0;
    }
    __try {
        swayOnCamera();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (memcmp(m, base, sizeof base)) memcpy(m, base, sizeof base); // our last move undone first
    bool act = g_srcCam && g_camCal && g_vrTracking && g_eyeMode && !g_menuMode;
    int eye = eyeOv >= 0 ? eyeOv : (eyeSign() > 0 ? 1 : 0);
    if (!act || !g_vrPoseOk[eye]) {
        camLocalRestore();
        return;
    }
    // head pose in view space (R columns = new axes, t) -> camera-local: Rc = A R A^T, tc = A t
    const float *A = g_camA, *R = g_vrR[eye], *t = g_vrT[eye];
    float AR[9], Rc[9], tc[3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) AR[i * 3 + j] = A[i * 3 + 0] * R[0 * 3 + j] + A[i * 3 + 1] * R[1 * 3 + j] + A[i * 3 + 2] * R[2 * 3 + j];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) Rc[i * 3 + j] = AR[i * 3 + 0] * A[j * 3 + 0] + AR[i * 3 + 1] * A[j * 3 + 1] + AR[i * 3 + 2] * A[j * 3 + 2];
    for (int i = 0; i < 3; ++i) tc[i] = A[i * 3 + 0] * t[0] + A[i * 3 + 1] * t[1] + A[i * 3 + 2] * t[2];
    if (g_camLocal > 0 && eyeOv <= 0) { // once per frame: the middle between the eyes, for the game's next update
        float tm[3], tcm[3];
        bool two = g_vrPoseOk[0] && g_vrPoseOk[1];
        for (int i = 0; i < 3; ++i) tm[i] = two ? 0.5f * (g_vrT[0][i] + g_vrT[1][i]) : t[i];
        for (int i = 0; i < 3; ++i) tcm[i] = A[i * 3 + 0] * tm[0] + A[i * 3 + 1] * tm[1] + A[i * 3 + 2] * tm[2];
        camLocalWrite(cam, Rc, tcm);
    } else if (g_camLocal <= 0) {
        camLocalRestore();
    }
    // camera -> world: M' = M [Rc tc]; rows of m: (M00 M01 M02 Tx)
    float out[12];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            out[i * 4 + j] = base[i * 4 + 0] * Rc[0 * 3 + j] + base[i * 4 + 1] * Rc[1 * 3 + j] + base[i * 4 + 2] * Rc[2 * 3 + j];
        out[i * 4 + 3] = base[i * 4 + 3] + base[i * 4 + 0] * tc[0] + base[i * 4 + 1] * tc[1] + base[i * 4 + 2] * tc[2];
    }
    memcpy(m, out, sizeof out);
    if (g_camPlanes > 0) camPlanesWrite(cam, base, out);
    memcpy(g_camWritten, out, sizeof out);
    memcpy(g_camBase, base, sizeof base);
    g_camEye = eye;
    g_camEyeFrame = g_frame;
    memcpy(g_camEyeBase[eye], base, sizeof base);
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
static void camKeepKey() {
    if (g_camKeep < 0) g_camKeep = GetPrivateProfileIntW(L"vr", L"cam_keep", 0, (g_dir + L"mw4vr.ini").c_str()) % 3;
    static bool was;
    bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_F6) & 0x8000);
    if (down && !was) {
        g_camKeep = (g_camKeep + 1) % 3;
        logf("cam: keep mode %d (%s), frame %u", (int)g_camKeep,
             g_camKeep == 0 ? "the game's matrix back after the Draw" : g_camKeep == 1 ? "the head's matrix stays" : "the head's matrix from Present to the next Draw", g_frame);
        for (int i = 0; i <= g_camKeep; ++i) Beep(900 + 300 * g_camKeep, 70), Sleep(60);
    }
    was = down;
}
// at Present (the HUD is drawn): mode 2 puts the head's matrix back
static void camAtPresent() {
    if (g_camKeep != 2 || !g_camMain || g_camEyeFrame != g_frame) return;
    __try {
        float *m = (float *)((uint8_t *)g_camMain + 0x80);
        if (memcmp(m, g_camBase, sizeof g_camBase) == 0) memcpy(m, g_camWritten, sizeof g_camWritten);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void __stdcall camPost(uint8_t *cam) {
    if (cam && cam == g_camMain) camPlanesRestore(cam);
    if (g_camKeep == 1) return;
    if (cam && cam == g_camMain && g_camEyeFrame == g_frame) {
        float *m = (float *)(cam + 0x80);
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
        int r = camCallOrig(cam, edxv, arg);
        g_camInMain = false;
        camPost(cam);
        return r;
    }
    // the left eye: drawn, its picture taken out of the frame; then the right eye over it
    int r = 0;
    for (int e = 0; e < 2; ++e) {
        camPreEye(cam, e);
        g_lblEye = e;
        g_camPassEye = e;
        g_kEyeOverride = e;
        buildK(); // the cockpit's extra movement for this eye
        g_kEyeOverride = -1;
        LONG before = g_tonemapCount;
        g_camInMain = true;
        r = camCallOrig(cam, edxv, arg);
        g_camInMain = false;
        g_camPassEye = -1;
        camPost(cam);
        bool ok = g_tonemapCount != before && eyeGrab(e);
        if (!ok) {
            static int fails;
            if (e == 0) {
                // the picture is not in the frame when the camera returns (or the frame is not known yet):
                // this frame goes on with one eye; after some tries the old way for good
                if (++fails == 300) {
                    g_doubleDraw = false;
                    logf("cam: both eyes per frame not possible (the camera's picture is not in the frame when it returns, tonemap %d) - alternating",
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
            } else if (firstBoth != ~0u && g_frame - firstBoth == 900) {
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

static void camInstall() {
    if (g_camHooked) return;
    uint8_t *p = (uint8_t *)CAM_DRAW;
    __try {
        if (p[0] != 0xE9) {
            logf("cam: CameraElement::Draw at %p does not start with mw4hook's jmp (%02x %02x %02x %02x %02x) - not hooked",
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
