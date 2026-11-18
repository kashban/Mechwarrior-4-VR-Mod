// ---------------------------------------------------------------------------------------------
// head tracking: the camera of each frame is the game's camera moved by Hv, a rigid transform in
// the game's view space (x right, y up, z forward; row-major 4x4, column vectors). With the game's
// projection P (reversed infinite depth: row 2 = (0 0 0 n), row 3 = (0 0 1 0)) every world/object
// -> clip matrix M becomes K M with K = P Hv P^-1, every clip -> world matrix X becomes X K^-1.
// Without head poses (desktop test, or tracking off) Hv is the old sideways eye offset.
// Included into main.cpp after the eye globals (g_eyeMode, g_P00, g_P11, g_eyeOffsetM, g_frame).

static float g_near;            // n of the projection, from g_vWorldToClip (row 2, column 3)
static float g_up[3], g_fwd[3]; // the game camera's up and forward axes (world)
static float g_K[16];           // this frame's clip-space transform for the CPU path (the cockpit)
static volatile bool g_kOn;     // this frame's transform is in use
static unsigned g_kFrame;       // the frame it was built for

// VR: per eye the rotation (3x3 row-major, columns = the new camera's axes in game view coordinates)
// and position (game view coordinates, metres) for the frame being rendered next
static float g_vrR[2][9], g_vrT[2][3];
static float g_vrRaw[2][3];         // the eye's position against the reference, view space, real metres
static float g_cockpitDelta;        // cockpit scale - world scale: the near CPU-path geometry's extra eye movement
static float g_cockpitRange = 8.0f; // that geometry: nearer than this (game metres)
static volatile bool g_kTlOnly;
static int g_kEyeOverride = -1; // buildK for this eye (both eyes drawn in one frame)
static unsigned g_tlEpoch; // changes with every K: the CPU-path shader's buffer is written again            // K is for
                           // the CPU-path shader alone (camera mode)
static float g_vrFov[2][4]; // per eye tangents left (<0), right, up, down (<0) to render with
static volatile bool g_vrFovOk;
static volatile bool g_vrPoseOk[2];
static volatile bool g_vrTracking; // [vr] tracking=1 and poses arriving
static volatile int g_kEye = -1;   // eye of this frame's transform: 0 left, 1 right

volatile bool g_srcCamActive; // head pose applied at the game's camera, calibrated and running

static void m4mul(const float *a, const float *b, float *out) {
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] + a[i * 4 + 1] * b[1 * 4 + j] + a[i * 4 + 2] * b[2 * 4 + j] +
                           a[i * 4 + 3] * b[3 * 4 + j];
    memcpy(out, r, sizeof r);
}

static inline int eyeSign() {
    int mode = g_eyeMode;
    return mode == 1 ? 1 : mode == 2 ? -1 : ((g_frame & 1) ? -1 : 1); // +1 right eye
}

// builds this frame's transform; called once per frame before anything is drawn
static void buildK() {
    g_kFrame = g_frame;
    ++g_tlEpoch;
    int mode = g_eyeMode;
    extern volatile bool g_srcCamActive;
    g_kTlOnly = false;
    if (g_srcCamActive) {
        // the camera itself is moved (cam.inc): no matrices to patch. The cockpit alone (CPU path, our
        // shader) gets the eye moved further - scaled about the eye it looks smaller without the
        // world shrinking: Hv = [I | -dt], dt = R^T raw (cockpit scale - world scale) in the moved camera's frame
        g_kOn = false;
        g_kEye = -1;
        int e2 = g_kEyeOverride >= 0 ? g_kEyeOverride : (eyeSign() > 0 ? 1 : 0);
        if (!mode || g_cockpitDelta == 0 || !g_vrPoseOk[e2] || g_P00 <= 0 || g_P11 <= 0 || g_near <= 0) return;
        const float *R = g_vrR[e2], *raw = g_vrRaw[e2];
        float dt[3];
        for (int i = 0; i < 3; ++i)
            dt[i] = (R[0 * 3 + i] * raw[0] + R[1 * 3 + i] * raw[1] + R[2 * 3 + i] * raw[2]) * g_cockpitDelta;
        float H2[16] = {1, 0, 0, -dt[0], 0, 1, 0, -dt[1], 0, 0, 1, -dt[2], 0, 0, 0, 1};
        const float n2 = g_near;
        const float P[16] = {g_P00, 0, 0, 0, 0, g_P11, 0, 0, 0, 0, 0, n2, 0, 0, 1, 0};
        const float Pi[16] = {1 / g_P00, 0, 0, 0, 0, 1 / g_P11, 0, 0, 0, 0, 0, 1, 0, 0, 1 / n2, 0};
        float tmp2[16];
        m4mul(P, H2, tmp2);
        m4mul(tmp2, Pi, g_K);
        g_kTlOnly = true;
        g_kOn = true;
        return;
    }
    // no camera hook: the picture is not moved
    g_kOn = false;
    g_kEye = -1;
}

static inline bool kReady() { return g_kOn && g_kFrame == g_frame; }
