// ---------------------------------------------------------------------------------------------
// cockpit sway: the cockpit moves against the camera with the mech's steps; the near HUD panels
// follow it. The camera sits rigidly on the mech's root element, the cockpit on another branch of
// the scene graph (the torso's joints). So the graph below the camera's ancestors is searched
// (elements: parent pointer at +0x10, local -> world matrix at +0x80; children are found as
// pointers inside an element, or inside an array it points to, whose own parent pointer leads
// back). Elements near the camera are watched; the nearest one that moves against the camera by
// a little (a sway, not a limb) is followed. Its pose in the camera's view space, against its own
// running average, is the sway. Included into main.cpp after cam.inc.

static volatile bool g_swayOn;                 // [vr] hud_sway=1
static float g_swayGain = 1.0f;
struct SwStat {
    bool have = false;
    float avgR[9], avgT[3], var = 0, lastT[3];
    unsigned n = 0;
};
enum { SW_NODES = 96 };
struct SwNode {
    uint8_t *e;
    float m[12];
    bool read;
    SwStat st;
};
static SwNode g_swNodes[SW_NODES];
static int g_swNodeN;
static unsigned g_swFoundAt, g_swReadFrame = ~0u;
static uint8_t *g_swFollow;                    // the element followed
static uint8_t *g_swRoot;                      // the root the graph was searched from
// the sway (view space: x right, y up, z forward; game metres)
static float g_swayR[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, g_swayT[3];
static volatile bool g_swayValid;

// readable memory, without exceptions: a small cache of committed regions
static bool swReadable(const void *p, size_t len) {
    struct Reg {
        uintptr_t a, b;
    };
    static Reg cache[32];
    static int next;
    uintptr_t x = (uintptr_t)p;
    if (x < 0x10000 || (x & 3)) return false;
    for (auto &r : cache)
        if (x >= r.a && x + len <= r.b) return true;
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT) return false;
    if (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    if (!(mi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) return false;
    uintptr_t a = (uintptr_t)mi.BaseAddress, b = a + mi.RegionSize;
    cache[next++ & 31] = {a, b};
    return x + len <= b;
}
static inline bool swIsChild(uint8_t *q, uint8_t *parent) {
    return swReadable(q, 0xb0) && *(uint8_t **)(q + EL_PARENT) == parent && q != parent;
}

// the graph below 'root', breadth first; elements within 'range' of the camera are kept
static void swayDiscover(uint8_t *root, const float *camPos, float range) {
    static std::vector<uint8_t *> all;
    all.clear();
    all.push_back(root);
    g_swNodeN = 0;
    for (size_t i = 0; i < all.size() && all.size() < 1500; ++i) {
        uint8_t *e = all[i];
        if (!swReadable(e, 0x200)) continue;
        auto take = [&](uint8_t *q) {
            for (uint8_t *o : all)
                if (o == q) return;
            all.push_back(q);
        };
        for (int off = 0x14; off < 0x200; off += 4) {
            if (off >= 0x80 && off < 0xb0) continue; // the matrix
            uint8_t *p = *(uint8_t **)(e + off);
            if (swIsChild(p, e)) {
                take(p);
            } else if (swReadable(p, 4 * 48)) { // an array of children?
                uint8_t **arr = (uint8_t **)p;
                for (int k = 0; k < 48; ++k) {
                    if (!swIsChild(arr[k], e)) {
                        if (k == 0) break; // not such an array
                        continue;
                    }
                    take(arr[k]);
                }
            }
        }
    }
    for (uint8_t *e : all) {
        if (!swReadable(e, 0xb0) || g_swNodeN >= SW_NODES) continue;
        const float *m = (const float *)(e + EL_WORLD);
        float dx = m[3] - camPos[3], dy = m[7] - camPos[7], dz = m[11] - camPos[11];
        if (!(dx * dx + dy * dy + dz * dz < range * range)) continue;
        SwNode &n = g_swNodes[g_swNodeN++];
        n.e = e;
        n.read = false;
        n.st = SwStat();
    }
    static int logs;
    if (g_dbg && logs++ < 8) logf("sway: scene graph below %p - %u elements, %d within %.0f m of the camera", root, (unsigned)all.size(), g_swNodeN, range);
}

// from camPre (the main camera's base matrix just read): find the elements now and then, read their matrices
static void swayOnCamera() {
    if (!g_swayOn || !g_srcCamActive || !g_camAncN) return;
    uint8_t *root = g_camAncPtr[g_camAncN > 1 ? 1 : 0]; // the mech (the camera's grandparent), else its parent
    static uint8_t *lastRoot;
    if (root != lastRoot || (!g_swFollow && g_frame - g_swFoundAt > 300)) {
        lastRoot = root;
        g_swRoot = root;
        g_swFoundAt = g_frame;
        g_swFollow = nullptr;
        swayDiscover(root, g_camLast, 8.0f);
    }
    for (int i = 0; i < g_swNodeN; ++i) {
        SwNode &n = g_swNodes[i];
        n.read = swReadable(n.e, 0xb0);
        if (n.read) memcpy(n.m, n.e + EL_WORLD, 48);
    }
    g_swReadFrame = g_frame;
}

static void swayOrtho(float *R) { // Gram-Schmidt on the rows
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

// S (3x4 local -> world) seen from the camera B: rotation (columns = S's axes, unit) and position in view space
static bool swayPose(const float *S, float *Rv, float *tv) {
    const float *B = g_camLast, *A = g_camA;
    float Rl[9], tl[3], dw[3] = {S[3] - B[3], S[7] - B[7], S[11] - B[11]};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) Rl[i * 3 + j] = B[0 * 4 + i] * S[0 * 4 + j] + B[1 * 4 + i] * S[1 * 4 + j] + B[2 * 4 + i] * S[2 * 4 + j];
        tl[i] = B[0 * 4 + i] * dw[0] + B[1 * 4 + i] * dw[1] + B[2 * 4 + i] * dw[2];
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) Rv[i * 3 + j] = A[0 * 3 + i] * Rl[0 * 3 + j] + A[1 * 3 + i] * Rl[1 * 3 + j] + A[2 * 3 + i] * Rl[2 * 3 + j];
        tv[i] = A[0 * 3 + i] * tl[0] + A[1 * 3 + i] * tl[1] + A[2 * 3 + i] * tl[2];
    }
    for (int j = 0; j < 3; ++j) { // the element's scale taken out
        float l = len3(Rv[j], Rv[3 + j], Rv[6 + j]);
        if (l < 1e-6f) return false;
        for (int i = 0; i < 3; ++i) Rv[i * 3 + j] /= l;
    }
    return true;
}

// once per frame (at Present)
static void swayOnPresent() {
    g_swayValid = false;
    if (!g_swayOn || !g_srcCamActive || g_camLastFrame != g_frame || g_swReadFrame != g_frame) {
        static unsigned misses;
        static int told;
        if (g_swayOn && g_srcCamActive && ++misses % 600 == 300 && told < 3) {
            ++told;
            logf("sway: not measured - the camera's matrix is %u frames old, its elements were read %u frames ago (%d ancestors, %d elements watched)",
                 g_frame - g_camLastFrame, g_frame - g_swReadFrame, g_camAncN, g_swNodeN);
        }
        return;
    }
    static float Rv[SW_NODES][9], tv[SW_NODES][3];
    static bool ok[SW_NODES];
    int follow = -1;
    for (int l = 0; l < g_swNodeN; ++l) {
        SwNode &nd = g_swNodes[l];
        ok[l] = nd.read && swayPose(nd.m, Rv[l], tv[l]);
        if (!ok[l]) continue;
        SwStat &s = nd.st;
        if (s.have && len3(tv[l][0] - s.lastT[0], tv[l][1] - s.lastT[1], tv[l][2] - s.lastT[2]) > 1.0f) s.have = false;
        memcpy(s.lastT, tv[l], sizeof s.lastT);
        if (!s.have) {
            memcpy(s.avgR, Rv[l], sizeof s.avgR);
            memcpy(s.avgT, tv[l], sizeof s.avgT);
            s.var = 0;
            s.n = 0;
            s.have = true;
        } else {
            const float k = 0.01f; // about a second and a half
            for (int i = 0; i < 9; ++i) s.avgR[i] += k * (Rv[l][i] - s.avgR[i]);
            float d2 = 0;
            for (int i = 0; i < 3; ++i) {
                float d = tv[l][i] - s.avgT[i];
                d2 += d * d;
                s.avgT[i] += k * d;
            }
            s.var += 0.02f * (d2 - s.var);
            ++s.n;
        }
        if (nd.e == g_swFollow) follow = l;
    }
    // choose: watched for a while, moving 0.5 mm .. 30 cm (rms) against the camera, the nearest such
    if (follow < 0 || g_swNodes[follow].st.n < 5) {
        int best = -1;
        float bd = 1e30f;
        bool ripe = false;
        for (int l = 0; l < g_swNodeN; ++l) {
            if (!ok[l] || g_swNodes[l].st.n < 150) continue;
            ripe = true;
            float v = g_swNodes[l].st.var, d = len3(tv[l][0], tv[l][1], tv[l][2]);
            // not the mech's root (the camera turns against it with the torso), nothing outside the cockpit
            if (g_swNodes[l].e == g_swRoot || d > 4.0f) continue;
            if (v > 2.5e-7f && v < 0.09f && d < bd) bd = d, best = l;
        }
        static int dumps;
        static unsigned none;
        if (ripe && best < 0 && ++none % 900 == 450 && dumps < 3) { // none fits for a while: the candidates, for the log
            ++dumps;
            logf("sway: %d elements watched near the camera, none moves like a cockpit (0.5 mm .. 30 cm, within 4 m):", g_swNodeN);
            for (int l = 0; l < g_swNodeN && l < 40; ++l)
                if (ok[l])
                    logf("sway:   element %p - right %.2f up %.2f forward %.2f m, moving %.1f mm (rms)%s", g_swNodes[l].e, tv[l][0],
                         tv[l][1], tv[l][2], sqrtf(g_swNodes[l].st.var) * 1000, l == best ? "  <- followed" : "");
        }
        if (best >= 0 && g_swNodes[best].e != g_swFollow) {
            g_swFollow = g_swNodes[best].e;
            logf("sway: following element %p, %.2f m from the camera, moving %.1f mm (rms)", g_swFollow, bd,
                 sqrtf(g_swNodes[best].st.var) * 1000);
        }
        follow = best;
    }
    if (follow < 0) {
        static unsigned waits;
        static int told;
        if (++waits % 900 == 600 && told < 3) {
            ++told;
            int ripeN = 0;
            for (int l = 0; l < g_swNodeN; ++l) ripeN += ok[l] && g_swNodes[l].st.n >= 150;
            logf("sway: no element followed yet - %d watched, %d of them long enough", g_swNodeN, ripeN);
        }
        return;
    }
    const SwStat &s = g_swNodes[follow].st;
    float Ra[9], T[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) T[i * 3 + j] = s.avgR[j * 3 + i];
    swayOrtho(T);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) Ra[i * 3 + j] = T[j * 3 + i];
    // sway: pose_now = D pose_avg  ->  DR = Rv Ra^T, Dt = tv - DR ta
    float DR[9], Dt[3];
    const float *Rn = Rv[follow], *tn = tv[follow];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) DR[i * 3 + j] = Rn[i * 3 + 0] * Ra[j * 3 + 0] + Rn[i * 3 + 1] * Ra[j * 3 + 1] + Rn[i * 3 + 2] * Ra[j * 3 + 2];
    for (int i = 0; i < 3; ++i) Dt[i] = tn[i] - (DR[i * 3 + 0] * s.avgT[0] + DR[i * 3 + 1] * s.avgT[1] + DR[i * 3 + 2] * s.avgT[2]);
    if (len3(Dt[0], Dt[1], Dt[2]) > 1.5f) return;
    memcpy(g_swayR, DR, sizeof DR);
    memcpy(g_swayT, Dt, sizeof Dt);
    g_swayValid = true;
}
