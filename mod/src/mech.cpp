// ---------------------------------------------------------------------------------------------
// A profile for every cockpit. The cockpit is drawn on the game's CPU path (pre-transformed
// vertices, VS ad8cee09): in a measured frame the triangles of those draws nearer than the
// cockpit's range are read back (left eye's pass), taken to the game camera's own space (the head's
// movement undone) and entered into a coarse grid of directions (4 degrees a cell) as the distance
// of the nearest surface. The median of some such frames is the cockpit's signature: it tells the
// cockpits apart (the mech is recognised by it at the start of a battle) and gives three
// measures - how far the console below the window is, how high it reaches, and where the window's
// middle lies. Profiles are kept in mw4vr_mechs.ini: the cockpit's scale, the seat and the near HUD
// panels' place. A cockpit set by hand (a hot key, or Ctrl+F10 "this one fits") is "tuned"; one
// that is not gets its values from the tuned ones by its measures (the console at the same
// apparent distance, the panels as high above it and as far from the window's middle).
// The seat is kept twice: for the game's cockpit, and for a cockpit of our own (mw5.cpp) once it was set in one -
// the two meshes are not alike, a seat that fits the one does not fit the other.
// vr.cpp applies and stores the values.

#include "mod.h"

volatile bool g_mechOn;          // [vr] mech_profiles=1
volatile bool g_mechAuto = true; // [vr] mech_auto_fit=1
static const float MG_AZ0 = -40.0f, MG_EL0 = -48.0f, MG_STEP = 4.0f;
static float g_mgFrame[MG_N]; // this frame's grid (metres, 0 = nothing)
static int g_mgTris, g_mgBad, g_mgDraws;
static float g_mgSamples[MG_SAMPLES][MG_N];
static int g_mgSampleN;
bool g_mechSearching;            // samples are being collected
McSeat g_mechSeat;               // seen from outside (third person)? (core.h)
static int g_mechCkTris;         // triangles of the game's cockpit in this frame's left-eye pass
bool g_mechMw5;                  // a cockpit of our own is in use (mw5.cpp tells): its seat counts
bool g_mechSeated;               // the game draws its cockpit: the camera sits in the mech (not a mission's intro)
unsigned g_mechBattleFrame;      // the battle's first frame
static unsigned g_mechNextFrame; // the next frame to measure
static unsigned g_mechLastCamFrame = ~0u;

MechProfile g_mechs[MECH_MAX];
int g_mechN, g_mechCur = -1;
static bool g_mechLoaded;

std::wstring mechIni() { return g_dir + L"mw4vr_mechs.ini"; }

static float mechGetF(const wchar_t *sec, const wchar_t *key, float def) {
    wchar_t b[64];
    GetPrivateProfileStringW(sec, key, L"", b, 64, mechIni().c_str());
    return b[0] ? (float)_wtof(b) : def;
}
static void mechPutF(const wchar_t *sec, const wchar_t *key, float v) {
    wchar_t b[64];
    swprintf(b, 64, L"%.3f", v);
    WritePrivateProfileStringW(sec, key, b, mechIni().c_str());
}

// the seat in a cockpit of our own (mw5_seat_forward_m, mw5_seat_up_m); without both, the game's cockpit's seat
static void mechSeat5(const wchar_t *sec, MechProfile &p) {
    wchar_t f[64], u[64];
    GetPrivateProfileStringW(sec, L"mw5_seat_forward_m", L"", f, 64, mechIni().c_str());
    GetPrivateProfileStringW(sec, L"mw5_seat_up_m", L"", u, 64, mechIni().c_str());
    p.seat5 = f[0] && u[0];
    p.seat5Fwd = p.seat5 ? (float)_wtof(f) : 0, p.seat5Up = p.seat5 ? (float)_wtof(u) : 0;
}

static void mechMeasures(MechProfile &p) {
    // the console: the median distance below the window, straight ahead
    float v[MG_N];
    int n = 0;
    for (int r = 0; r < MG_EL; ++r) {
        float el = MG_EL0 + (r + 0.5f) * MG_STEP;
        if (el < -40 || el > -16) continue;
        for (int c = 0; c < MG_AZ; ++c) {
            float az = MG_AZ0 + (c + 0.5f) * MG_STEP;
            if (az < -16 || az > 16) continue;
            if (p.sig[r * MG_AZ + c] > 0) v[n++] = p.sig[r * MG_AZ + c];
        }
    }
    p.dash = 0, p.top = 0, p.centre = 0;
    if (n < 6) return;
    std::sort(v, v + n);
    p.dash = v[n / 2];
    // its top: in the columns straight ahead, the highest cell below the horizon that belongs to it
    float tops[MG_AZ];
    int nt = 0;
    for (int c = 0; c < MG_AZ; ++c) {
        float az = MG_AZ0 + (c + 0.5f) * MG_STEP;
        if (az < -12 || az > 12) continue;
        for (int r = MG_EL - 1; r >= 0; --r) {
            float el = MG_EL0 + (r + 0.5f) * MG_STEP;
            if (el > 6) continue;
            float d = p.sig[r * MG_AZ + c];
            if (d > 0 && d < 1.6f * p.dash) {
                tops[nt++] = el + 0.5f * MG_STEP;
                break;
            }
        }
    }
    if (nt >= 3) {
        std::sort(tops, tops + nt);
        p.top = tops[nt / 2];
    }
    // the window's middle: the open cells around the horizon
    float sum = 0;
    int open = 0;
    for (int r = 0; r < MG_EL; ++r) {
        float el = MG_EL0 + (r + 0.5f) * MG_STEP;
        if (el < -4 || el > 12) continue;
        for (int c = 0; c < MG_AZ; ++c)
            if (p.sig[r * MG_AZ + c] <= 0) sum += MG_AZ0 + (c + 0.5f) * MG_STEP, ++open;
    }
    if (open >= 4) p.centre = sum / open;
}

static void mechLoad() {
    g_mechLoaded = true;
    g_mechN = 0;
    int n = GetPrivateProfileIntW(L"mechs", L"count", 0, mechIni().c_str());
    if (n > MECH_MAX) n = MECH_MAX;
    static wchar_t buf[8192];
    for (int i = 0; i < n; ++i) {
        wchar_t sec[32];
        swprintf(sec, 32, L"mech_%d", i + 1);
        MechProfile &p = g_mechs[g_mechN];
        memset(&p, 0, sizeof p);
        GetPrivateProfileStringW(sec, L"name", sec, p.name, 48, mechIni().c_str());
        GetPrivateProfileStringW(sec, L"model", L"", p.model, 32, mechIni().c_str());
        GetPrivateProfileStringW(sec, L"signature", L"", buf, 8192, mechIni().c_str());
        int k = 0;
        for (wchar_t *q = buf; *q && k < MG_N;) {
            p.sig[k++] = (float)wcstol(q, &q, 10) * 0.01f;
            while (*q == L',' || *q == L' ') ++q;
        }
        if (k != MG_N) {
            logf("mech: profile %d in mw4vr_mechs.ini has no usable signature (%d of %d values) - left out", i + 1, k,
                 (int)MG_N);
            // keeps its number: an empty signature matches nothing
            for (int j = 0; j < MG_N; ++j) p.sig[j] = 0;
        }
        p.tuned = GetPrivateProfileIntW(sec, L"tuned", 0, mechIni().c_str()) != 0;
        p.cockpitScale = mechGetF(sec, L"cockpit_scale", 1);
        p.seatFwd = mechGetF(sec, L"seat_forward_m", 0);
        p.seatUp = mechGetF(sec, L"seat_up_m", 0);
        mechSeat5(sec, p);
        p.panelX = mechGetF(sec, L"hud_panel_x_m", 0);
        p.panelY = mechGetF(sec, L"hud_panel_y_m", 0);
        p.panelDist = mechGetF(sec, L"hud_panel_distance_m", 1.5f);
        p.panelScale = mechGetF(sec, L"hud_panel_scale", 0.6f);
        mechMeasures(p);
        ++g_mechN;
    }
    logf("mech: %d cockpit profiles in mw4vr_mechs.ini", g_mechN);
}

// the values of a profile set by hand are read again (they can be edited while playing)
void mechReload(int i) {
    if (i < 0 || i >= g_mechN) return;
    wchar_t sec[32];
    swprintf(sec, 32, L"mech_%d", i + 1);
    MechProfile &p = g_mechs[i];
    p.tuned = GetPrivateProfileIntW(sec, L"tuned", 0, mechIni().c_str()) != 0;
    p.cockpitScale = mechGetF(sec, L"cockpit_scale", p.cockpitScale);
    p.seatFwd = mechGetF(sec, L"seat_forward_m", p.seatFwd);
    p.seatUp = mechGetF(sec, L"seat_up_m", p.seatUp);
    mechSeat5(sec, p);
    p.panelX = mechGetF(sec, L"hud_panel_x_m", p.panelX);
    p.panelY = mechGetF(sec, L"hud_panel_y_m", p.panelY);
    p.panelDist = mechGetF(sec, L"hud_panel_distance_m", p.panelDist);
    p.panelScale = mechGetF(sec, L"hud_panel_scale", p.panelScale);
}

static void mechWriteNew(int i) {
    wchar_t sec[32];
    swprintf(sec, 32, L"mech_%d", i + 1);
    const MechProfile &p = g_mechs[i];
    std::wstring s;
    for (int k = 0; k < MG_N; ++k) {
        wchar_t b[16];
        swprintf(b, 16, k ? L",%d" : L"%d", (int)(p.sig[k] * 100 + 0.5f));
        s += b;
    }
    wchar_t b[16];
    swprintf(b, 16, L"%d", g_mechN);
    WritePrivateProfileStringW(L"mechs", L"count", b, mechIni().c_str());
    WritePrivateProfileStringW(sec, L"name", p.name, mechIni().c_str());
    if (p.model[0]) WritePrivateProfileStringW(sec, L"model", p.model, mechIni().c_str());
    WritePrivateProfileStringW(sec, L"tuned", L"0", mechIni().c_str());
    mechPutF(sec, L"console_m", p.dash);
    mechPutF(sec, L"console_top_deg", p.top);
    mechPutF(sec, L"window_middle_deg", p.centre);
    WritePrivateProfileStringW(sec, L"signature", s.c_str(), mechIni().c_str());
}

// how alike two signatures are: the mean difference of the distances' logarithms over the cells
// both have; 1e9 when they share too little
static float mechDiff(const float *a, const float *b) {
    int na = 0, nb = 0, both = 0;
    double sum = 0;
    for (int k = 0; k < MG_N; ++k) {
        if (a[k] > 0) ++na;
        if (b[k] > 0) ++nb;
        if (a[k] > 0 && b[k] > 0) sum += fabs(log((double)a[k] / b[k])), ++both;
    }
    if (both < 40 || both * 10 < na * 6 || both * 10 < nb * 6) return 1e9f;
    return (float)(sum / both);
}

// ---- measuring -------------------------------------------------------------------------------

struct MgVtx {
    float az, el, d;
};

static void mgTriangle(const MgVtx &a, const MgVtx &b, const MgVtx &c) {
    float az0 = a.az < b.az ? a.az : b.az, az1 = a.az > b.az ? a.az : b.az;
    float el0 = a.el < b.el ? a.el : b.el, el1 = a.el > b.el ? a.el : b.el;
    if (c.az < az0) az0 = c.az;
    if (c.az > az1) az1 = c.az;
    if (c.el < el0) el0 = c.el;
    if (c.el > el1) el1 = c.el;
    if (az1 - az0 > 120) return; // around the back: not a cell's worth
    int c0 = (int)floorf((az0 - MG_AZ0) / MG_STEP), c1 = (int)floorf((az1 - MG_AZ0) / MG_STEP);
    int r0 = (int)floorf((el0 - MG_EL0) / MG_STEP), r1 = (int)floorf((el1 - MG_EL0) / MG_STEP);
    if (c1 < 0 || r1 < 0 || c0 >= MG_AZ || r0 >= MG_EL) return;
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 >= MG_AZ) c1 = MG_AZ - 1;
    if (r1 >= MG_EL) r1 = MG_EL - 1;
    float den = (b.el - c.el) * (a.az - c.az) + (c.az - b.az) * (a.el - c.el);
    bool any = false;
    if (fabsf(den) > 1e-6f)
        for (int r = r0; r <= r1; ++r)
            for (int cc = c0; cc <= c1; ++cc) {
                float pa = MG_AZ0 + (cc + 0.5f) * MG_STEP, pe = MG_EL0 + (r + 0.5f) * MG_STEP;
                float l0 = ((b.el - c.el) * (pa - c.az) + (c.az - b.az) * (pe - c.el)) / den;
                float l1 = ((c.el - a.el) * (pa - c.az) + (a.az - c.az) * (pe - c.el)) / den;
                float l2 = 1 - l0 - l1;
                if (l0 < 0 || l1 < 0 || l2 < 0) continue;
                float d = l0 * a.d + l1 * b.d + l2 * c.d;
                float &g = g_mgFrame[r * MG_AZ + cc];
                if (g <= 0 || d < g) g = d;
                any = true;
            }
    if (!any) { // smaller than a cell: where its middle is
        float pa = (a.az + b.az + c.az) / 3, pe = (a.el + b.el + c.el) / 3, d = (a.d + b.d + c.d) / 3;
        int cc = (int)floorf((pa - MG_AZ0) / MG_STEP), r = (int)floorf((pe - MG_EL0) / MG_STEP);
        if (cc >= 0 && cc < MG_AZ && r >= 0 && r < MG_EL) {
            float &g = g_mgFrame[r * MG_AZ + cc];
            if (g <= 0 || d < g) g = d;
        }
    }
}

// ---- the sway, from the game's cockpit itself --------------------------------------------------
// The game moves its cockpit against the camera with the mech's steps. Its vertices are read in every frame (the
// left eye's pass), in the camera's space as the game left it (the head's pose taken out) - without those the
// game's clipping makes at the view's edges, which move with the head. Each vertex is found again in the frame
// before (the nearest one); the small rigid movement between the two frames (a shift d and a turn w:
// q = p + d + w x (p - pc), least squares) is added up, and the sum leaks away in about a second and a half - so
// it is the movement against the cockpit's average pose.
enum { SWV_MAX = 1500 };
static float g_swvCur[SWV_MAX][3], g_swvPrev[SWV_MAX][3];
static float g_swvPairP[SWV_MAX][3], g_swvPairQ[SWV_MAX][3];
static int g_swvCurN, g_swvPrevN, g_swvAllN; // AllN: with the vertices at the view's edges (is there a cockpit at all?)
static float g_swvTh[3], g_swvTau[3];        // the sum: p' = p + tau + th x p

// false: the game drew no cockpit in this frame
static bool swvOnPresent() {
    int n = g_swvCurN;
    bool seated = g_swvAllN >= 90;
    g_swvAllN = 0;
    static unsigned frames, matched, placed;
    static float most;
    static int pairsLast;
    // the cockpit's place as the game hands it to its renderer (sway.cpp): the step is told outright
    float sw[3], sd[3];
    int place = swayStep(sw, sd);
    if (place == 1) {
        for (int a = 0; a < 3; ++a) g_swvTh[a] += sw[a], g_swvTau[a] += sd[a];
        ++placed;
    }
    if (seated || place) {
        ++frames;
        // Which vertex is which? Not by their order: the game clips the cockpit's triangles at the view's edges, so
        // with every turn of the head others are there, in another order (compared by order, the cockpit moved
        // with the head). Each vertex is the nearest one of the frame before, if within 3 cm - a step moves the
        // cockpit some millimetres a frame, and its vertices lie further apart.
        // (Looked for among the vertices of the frame before that lie within those 3 cm in x - they are sorted by x
        // for it - and not among all 1500: the same vertex is found, the first of the nearest, in a fortieth of
        // the comparisons.)
        static float(*const P)[3] = g_swvPairP, (*const Q)[3] = g_swvPairQ;
        static std::pair<float, int> byX[SWV_MAX];
        const int prevN = g_swvPrevN;
        for (int j = 0; j < prevN; ++j) byX[j] = {g_swvPrev[j][0], j};
        std::sort(byX, byX + prevN);
        int m = 0;
        for (int i = 0; i < n; ++i) {
            const float *q = g_swvCur[i];
            float best = 9e-4f;
            int bj = -1;
            const std::pair<float, int> *it =
                std::lower_bound(byX, byX + prevN, std::pair<float, int>(q[0] - 0.0301f, -1));
            for (; it != byX + prevN && it->first <= q[0] + 0.0301f; ++it) {
                const int j = it->second;
                const float *p = g_swvPrev[j];
                float dx = q[0] - p[0], dy = q[1] - p[1], dz = q[2] - p[2], d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < best || (d2 == best && bj >= 0 && j < bj)) best = d2, bj = j;
            }
            if (bj >= 0) memcpy(P[m], g_swvPrev[bj], sizeof P[0]), memcpy(Q[m], q, sizeof Q[0]), ++m;
        }
        pairsLast = m;
        if (m >= 60 && !place) { // (without the cockpit's place: its vertices' movement)
            n = m;
            double pc[3] = {0, 0, 0}, dm[3] = {0, 0, 0};
            for (int i = 0; i < n; ++i)
                for (int a = 0; a < 3; ++a) pc[a] += P[i][a], dm[a] += Q[i][a] - P[i][a];
            for (int a = 0; a < 3; ++a) pc[a] /= n, dm[a] /= n;
            double A[9] = {0}, b[3] = {0, 0, 0};
            for (int i = 0; i < n; ++i) {
                double r[3], d[3];
                for (int a = 0; a < 3; ++a) r[a] = P[i][a] - pc[a], d[a] = Q[i][a] - P[i][a] - dm[a];
                double rr = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
                for (int a = 0; a < 3; ++a)
                    for (int c = 0; c < 3; ++c) A[a * 3 + c] += (a == c ? rr : 0) - r[a] * r[c];
                b[0] += r[1] * d[2] - r[2] * d[1], b[1] += r[2] * d[0] - r[0] * d[2], b[2] += r[0] * d[1] - r[1] * d[0];
            }
            double det = A[0] * (A[4] * A[8] - A[5] * A[7]) - A[1] * (A[3] * A[8] - A[5] * A[6]) +
                         A[2] * (A[3] * A[7] - A[4] * A[6]);
            double w[3] = {0, 0, 0};
            if (fabs(det) > 1e-9) {
                w[0] = (b[0] * (A[4] * A[8] - A[5] * A[7]) - A[1] * (b[1] * A[8] - A[5] * b[2]) +
                        A[2] * (b[1] * A[7] - A[4] * b[2])) /
                       det;
                w[1] = (A[0] * (b[1] * A[8] - A[5] * b[2]) - b[0] * (A[3] * A[8] - A[5] * A[6]) +
                        A[2] * (A[3] * b[2] - b[1] * A[6])) /
                       det;
                w[2] = (A[0] * (A[4] * b[2] - b[1] * A[7]) - A[1] * (A[3] * b[2] - b[1] * A[6]) +
                        b[0] * (A[3] * A[7] - A[4] * A[6])) /
                       det;
            }
            double dl = sqrt(dm[0] * dm[0] + dm[1] * dm[1] + dm[2] * dm[2]),
                   wl = sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
            if (dl < 0.03 && wl < 0.03) { // a step, not a cut to another view
                // about the origin: d + w x (p - pc) = (d - w x pc) + w x p
                g_swvTh[0] += (float)w[0], g_swvTh[1] += (float)w[1], g_swvTh[2] += (float)w[2];
                g_swvTau[0] += (float)(dm[0] - (w[1] * pc[2] - w[2] * pc[1]));
                g_swvTau[1] += (float)(dm[1] - (w[2] * pc[0] - w[0] * pc[2]));
                g_swvTau[2] += (float)(dm[2] - (w[0] * pc[1] - w[1] * pc[0]));
                ++matched;
            }
        }
        for (int a = 0; a < 3; ++a) g_swvTh[a] *= 0.99f, g_swvTau[a] *= 0.99f;
        { // never more than a cockpit sways: 15 cm, 6 degrees
            float tl = len3(g_swvTau[0], g_swvTau[1], g_swvTau[2]), al = len3(g_swvTh[0], g_swvTh[1], g_swvTh[2]);
            if (tl > 0.15f)
                for (int a = 0; a < 3; ++a) g_swvTau[a] *= 0.15f / tl;
            if (al > 0.1f)
                for (int a = 0; a < 3; ++a) g_swvTh[a] *= 0.1f / al;
        }
        most = fmaxf(most, len3(g_swvTau[0], g_swvTau[1], g_swvTau[2]));
        static int told;
        if (frames % 900 == 0 && told < 6) {
            ++told;
            long shapes, nearCam;
            swayShapeCounts(&shapes, &nearCam);
            logf("sway: of the last 900 frames %u from the cockpit's place (the game handed %ld shapes on, %ld at the "
                 "camera), %u from its vertices (%d inside the view now, %d found again); largest shift %.1f mm",
                 placed, shapes, nearCam, matched, g_swvCurN, pairsLast, most * 1000);
            matched = 0, most = 0, placed = 0;
        }
    }
    // no cockpit of the game's (an outside view), or only every other frame (the eyes drawn in turns): the sway
    // stays for three frames, then it is none
    static unsigned last = ~0u;
    if (seated || place) last = g_frame;
    bool lately = last != ~0u && g_frame - last <= 3;
    if (!lately) {
        memset(g_swvTh, 0, sizeof g_swvTh);
        memset(g_swvTau, 0, sizeof g_swvTau);
    }
    memcpy(g_swvPrev, g_swvCur, (size_t)g_swvCurN * sizeof g_swvCur[0]);
    g_swvPrevN = g_swvCurN;
    g_swvCurN = 0;
    g_swayValid = lately && g_swayOn;
    if (!(seated || place) || !g_swayOn) return seated;
    const float *th = g_swvTh;
    float R[9] = {1, -th[2], th[1], th[2], 1, -th[0], -th[1], th[0], 1};
    swayOrtho(R);
    memcpy(g_swayR, R, sizeof R);
    memcpy(g_swayT, g_swvTau, sizeof g_swvTau);
    return true;
}

// the vertices are read from the buffers' own memory; nothing here has a destructor (SEH)
// collect: the left eye's pass (the sway, the cockpit's shape). Returns how many of the cockpit's triangles it read.
static int mgRead(const uint8_t *vbase, UINT vbytes, UINT stride, UINT voff, INT baseV, const uint8_t *ibase,
                  UINT ibytes, UINT ioff, bool idx32, UINT start, UINT count, float W, float H, bool collect) {
    const float *R = g_vrR[0], *T = g_vrT[0];
    int found = 0;
    __try {
        for (UINT t = 0; t + 2 < count; t += 3) {
            MgVtx v[3];
            float pos[3][3];
            bool edge[3] = {};
            bool ok = true;
            for (int k = 0; k < 3 && ok; ++k) {
                long long vi;
                if (ibase) {
                    UINT io = ioff + (start + t + k) * (idx32 ? 4u : 2u);
                    if (io + 4 > ibytes) {
                        ok = false;
                        break;
                    }
                    vi = idx32 ? *(const uint32_t *)(ibase + io) : *(const uint16_t *)(ibase + io);
                    vi += baseV;
                } else {
                    vi = (long long)start + t + k;
                }
                long long vo = (long long)voff + vi * stride;
                if (vi < 0 || vo + 16 > (long long)vbytes) {
                    ok = false;
                    break;
                }
                const float *p = (const float *)(vbase + vo);
                float rhw = p[3];
                if (!(rhw > 1e-4f) || !(rhw < 100.0f) || !(fabsf(p[0]) < 1e6f) || !(fabsf(p[1]) < 1e6f)) {
                    ok = false;
                    break;
                }
                float w = 1.0f / rhw;
                if (w >= g_cockpitRange) { // not the cockpit
                    ok = false;
                    break;
                }
                float nx = (p[0] + 0.5f) * 2.0f / W - 1.0f, ny = 1.0f - (p[1] + 0.5f) * 2.0f / H;
                if (fabsf(nx) > 3 || fabsf(ny) > 3) {
                    ++g_mgBad;
                    ok = false;
                    break;
                }
                edge[k] = fabsf(nx) > 0.97f || fabsf(ny) > 0.97f || w < g_near * 1.05f;
                float e[3] = {nx * w / g_P00, ny * w / g_P11, w}; // the eye's view space
                float x = R[0] * e[0] + R[1] * e[1] + R[2] * e[2] + T[0];
                float y = R[3] * e[0] + R[4] * e[1] + R[5] * e[2] + T[1];
                float z = R[6] * e[0] + R[7] * e[1] + R[8] * e[2] + T[2];
                pos[k][0] = x, pos[k][1] = y, pos[k][2] = z;
                float d2 = x * x + y * y + z * z;
                if (!(d2 > 0.02f * 0.02f)) ok = false; // nearer than 2 cm: not a vertex of the cockpit
                // its direction and distance: only the grid of a measured frame asks for them (two atan2 a vertex,
                // in every frame and for both eyes, were a millisecond of a frame)
                if (collect && g_mechMeasure) {
                    float h = sqrtf(x * x + z * z);
                    v[k].az = atan2f(x, z) * 57.29578f;
                    v[k].el = atan2f(y, h) * 57.29578f;
                    v[k].d = sqrtf(d2);
                }
            }
            if (!ok) continue;
            ++found;
            if (!collect) continue;
            g_swvAllN += 3;
            for (int k = 0; k < 3;
                 ++k) // for the sway (above): not what the clipping made at the view's edges and its near plane
                if (!edge[k] && g_swvCurN < SWV_MAX) memcpy(g_swvCur[g_swvCurN++], pos[k], sizeof pos[k]);
            if (!g_mechMeasure) continue;
            mgTriangle(v[0], v[1], v[2]);
            ++g_mgTris;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ++g_mgBad;
    }
    return found;
}

// a CPU-path draw of an eye's pass of the main camera. Returns how many triangles of the game's cockpit it draws.
int mechDraw(ID3D11DeviceContext *c, bool indexed, UINT count, UINT start, INT baseV, bool collect) {
    if (count < 3 || count > 200000 || !(g_P00 > 0) || !(g_P11 > 0)) return 0;
    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    c->IAGetPrimitiveTopology(&topo);
    if (topo != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) return 0;
    D3D11_VIEWPORT vp = {};
    UINT nvp = 1;
    c->RSGetViewports(&nvp, &vp);
    if (!nvp || vp.Width < 16 || vp.Height < 16) return 0;
    int found = 0;
    ID3D11Buffer *vb = nullptr, *ib = nullptr;
    UINT stride = 0, voff = 0, ioff = 0;
    DXGI_FORMAT ifmt = DXGI_FORMAT_UNKNOWN;
    c->IAGetVertexBuffers(0, 1, &vb, &stride, &voff);
    if (indexed) c->IAGetIndexBuffer(&ib, &ifmt, &ioff);
    D3D11_BUFFER_DESC vd = {}, id = {};
    if (vb) vb->GetDesc(&vd);
    if (ib) ib->GetDesc(&id);
    bool ok = vb && stride >= 16 && vd.Usage == D3D11_USAGE_DYNAMIC && (vd.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) &&
              (!indexed || (ib && id.Usage == D3D11_USAGE_DYNAMIC && (id.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) &&
                            (ifmt == DXGI_FORMAT_R16_UINT || ifmt == DXGI_FORMAT_R32_UINT)));
    if (ok) {
        // the buffers' memory as it is (nothing is written)
        D3D11_MAPPED_SUBRESOURCE mv = {}, mi = {};
        bool mvOk =
            SUCCEEDED(ORIG(MechPFN_Map, c, VT_Ctx_Map)(c, vb, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mv)) && mv.pData;
        bool miOk =
            !indexed ||
            (SUCCEEDED(ORIG(MechPFN_Map, c, VT_Ctx_Map)(c, ib, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mi)) && mi.pData);
        if (mvOk && miOk) {
            found = mgRead((const uint8_t *)mv.pData, vd.ByteWidth, stride, voff, baseV,
                           indexed ? (const uint8_t *)mi.pData : nullptr, id.ByteWidth, ioff,
                           ifmt == DXGI_FORMAT_R32_UINT, start, count, vp.Width, vp.Height, collect);
            ++g_mgDraws;
        }
        if (indexed && miOk && mi.pData) ORIG(MechPFN_Unmap, c, VT_Ctx_Unmap)(c, ib, 0);
        if (mvOk) ORIG(MechPFN_Unmap, c, VT_Ctx_Unmap)(c, vb, 0);
    }
    if (vb) vb->Release();
    if (ib) ib->Release();
    if (g_camPassEye == 0) g_mechCkTris += found;
    return found;
}

// ---- once per frame (from vrOnPresent) -------------------------------------------------------

static void mechIdentify() {
    // the median over the samples, cell by cell
    static float sig[MG_N];
    int valid = 0;
    for (int k = 0; k < MG_N; ++k) {
        float v[MG_SAMPLES];
        int n = 0;
        for (int s = 0; s < g_mgSampleN; ++s)
            if (g_mgSamples[s][k] > 0) v[n++] = g_mgSamples[s][k];
        if (n * 2 > g_mgSampleN) {
            std::sort(v, v + n);
            sig[k] = v[n / 2];
            ++valid;
        } else {
            sig[k] = 0;
        }
    }
    if (valid < 50) {
        logf("mech: the cockpit covers only %d cells - not recognised (outside view?), trying again", valid);
        g_mgSampleN = 0;
        return;
    }
    // the mech's name, when the game tells it, decides; the signature only among profiles without a name
    char mech[32];
    wchar_t model[32] = L"";
    if (gmPlayerMech(mech, 32)) swprintf(model, 32, L"%hs", mech);
    int best = -1;
    float bd = 1e9f;
    bool byName = false;
    for (int i = 0; i < g_mechN && !byName; ++i) {
        const MechProfile &q = g_mechs[i];
        if (model[0] && q.model[0]) {
            if (!_wcsicmp(model, q.model)) best = i, bd = mechDiff(sig, q.sig), byName = true;
            continue; // another mech's profile
        }
        float d = mechDiff(sig, q.sig);
        if (d < bd) bd = d, best = i;
    }
    g_mechSearching = false;
    if (best >= 0 && (byName || bd < 0.06f)) {
        g_mechCur = best;
        mechReload(best);
        MechProfile &p = g_mechs[best];
        if (model[0] && !p.model[0]) { // a profile from before the names: it is this mech's now
            wchar_t sec[32];
            swprintf(sec, 32, L"mech_%d", best + 1);
            wcscpy_s(p.model, model);
            WritePrivateProfileStringW(sec, L"model", model, mechIni().c_str());
            if (!wcsncmp(p.name, L"cockpit ", 8)) {
                wcscpy_s(p.name, model);
                WritePrivateProfileStringW(sec, L"name", model, mechIni().c_str());
            }
        }
        logf("mech: cockpit recognised - profile %d '%s' (%s, difference %.3f), %s; console %.2f m, top %.0f deg, "
             "window middle %.0f deg",
             best + 1, narrow(p.name).c_str(), byName ? "by the mech's name" : "by its shape", bd < 1e8f ? bd : -1.0f,
             p.tuned ? "set by hand" : "fitted from the others", p.dash, p.top, p.centre);
        return;
    }
    if (g_mechN >= MECH_MAX) {
        logf("mech: a cockpit not known, and no room for another profile (%d)", (int)MECH_MAX);
        g_mechCur = -1;
        return;
    }
    MechProfile &p = g_mechs[g_mechN];
    memset(&p, 0, sizeof p);
    memcpy(p.sig, sig, sizeof sig);
    if (model[0])
        wcscpy_s(p.name, model), wcscpy_s(p.model, model);
    else
        swprintf(p.name, 48, L"cockpit %d", g_mechN + 1);
    p.cockpitScale = 1, p.panelDist = 1.5f, p.panelScale = 0.6f;
    mechMeasures(p);
    g_mechCur = g_mechN++;
    mechWriteNew(g_mechCur);
    logf("mech: a new cockpit - profile %d (%d cells; nearest known one differs by %.3f); console %.2f m, top %.0f "
         "deg, window middle %.0f deg",
         g_mechCur + 1, valid, bd < 1e8f ? bd : -1.0f, p.dash, p.top, p.centre);
    Beep(500, 40);
}

// inBattle: the main camera is drawn with head tracking; zoomed: its projection is not the wide one
void mechOnPresent(bool inBattle, bool zoomed) {
    bool measured = g_mechMeasure;
    g_mechMeasure = false;
    bool cockpitNow = swvOnPresent();
    // the third-person view: the game draws no cockpit although the head looks ahead (not judged in the zoomed view)
    int ckTris = g_mechCkTris;
    g_mechCkTris = 0;
    // (only a frame the camera's passes were drawn in: one without them has no cockpit, and says nothing)
    static unsigned skipped;
    if (inBattle && !zoomed && g_doubleDraw && g_camBothFrame != g_frame) ++skipped;
    if (inBattle && !zoomed && g_doubleDraw && g_camBothFrame == g_frame) {
        bool was = g_mechSeat.outside,
             ahead = g_vrPoseOk[0] && g_vrR[0][8] > 0.94f; // (within 20 degrees of straight ahead)
        // A little of the cockpit says nothing by its triangles - but the game handed its cockpit's shape, at the
        // camera, to its renderer in this frame (sway.cpp): that is the seat. (Without it the Marauder II's small
        // cockpit, the head a little down after a look from outside, never came to thirty triangles: the game's
        // cockpit stayed in the picture, ours away.)
        const bool shape = g_swayShapeWFrame == g_frame && ckTris >= MC_NO_COCKPIT_TRIS;
        mcSeatStep(g_mechSeat, shape && ckTris < MC_COCKPIT_TRIS ? (int)MC_COCKPIT_TRIS : ckTris, ahead);
        // what the judgement rests on, for the log: how much of the game's cockpit there is while the head looks ahead
        static int least = 1 << 30, most = -1, told, toldSum;
        static unsigned judged, little, sumFrame;
        if (ahead) {
            ++judged;
            if (ckTris < least) least = ckTris;
            if (ckTris > most) most = ckTris;
            if (ckTris >= MC_NO_COCKPIT_TRIS && ckTris < MC_COCKPIT_TRIS) ++little;
        }
        if (was != g_mechSeat.outside && told++ < 300)
            logf(g_mechSeat.outside
                     ? "mech: frame %u - the game draws no cockpit although the head looks ahead (%d triangles now): "
                       "seen from outside, ours is not drawn"
                     : "mech: frame %u - the game draws its cockpit again (%d triangles): back in the mech",
                 g_frame, ckTris);
        if (g_frame - sumFrame >= 2700 && judged) { // every half minute
            if (toldSum++ < 60)
                logf("mech: the game's cockpit, head ahead, %u frames (%s now): %d to %d triangles, %u frames with a "
                     "little of it (%d to %d), %u frames without the camera's passes",
                     judged, g_mechSeat.outside ? "outside" : "inside", least, most, little, (int)MC_NO_COCKPIT_TRIS,
                     MC_COCKPIT_TRIS - 1, skipped);
            sumFrame = g_frame, judged = little = skipped = 0, least = 1 << 30, most = -1;
        }
    }
    if (!g_mechOn) {
        g_mechCur = -1;
        return;
    }
    if (!g_mechLoaded) mechLoad();
    // a battle begins (the camera was not drawn for a while): which cockpit is it?
    if (inBattle && (g_mechLastCamFrame == ~0u || g_frame - g_mechLastCamFrame > 30)) {
        g_mechSearching = true;
        g_mechSeated = false;
        g_mechSeat = {};
        g_mechBattleFrame = g_frame;
        g_mgSampleN = 0;
        g_mechNextFrame = g_frame + 60; // (the cockpit known so far stays in use until this one is recognised)
    }
    if (inBattle) g_mechLastCamFrame = g_frame;
    // the game tells the mech's name and there is a profile of it: no need to look at the cockpit's shape,
    // so its values and its MechWarrior 5 cockpit are there from the battle's first picture
    if (g_mechSearching && inBattle) {
        char mech[32];
        wchar_t model[32];
        if (gmPlayerMech(mech, 32)) {
            swprintf(model, 32, L"%hs", mech);
            for (int i = 0; i < g_mechN; ++i) {
                if (_wcsicmp(model, g_mechs[i].model) != 0) continue;
                g_mechSearching = false;
                g_mgSampleN = 0;
                g_mechCur = i;
                g_mechNextFrame = g_frame + 2; // (is the camera in the cockpit? - looked at now)
                mechReload(i);
                logf("mech: profile %d '%s' by the mech's name, at once (%s)", i + 1, narrow(g_mechs[i].name).c_str(),
                     g_mechs[i].tuned ? "set by hand" : "fitted from the others");
                break;
            }
        }
    }
    if (((measured && g_mgTris >= 30) || (cockpitNow && inBattle)) && !g_mechSeated) {
        g_mechSeated = true;
        logf("mech: frame %u - the game draws its cockpit: the camera sits in the mech (%u frames into the battle)",
             g_frame, g_frame - g_mechBattleFrame);
    }
    if (inBattle && !g_mechSeated && !g_mechSearching &&
        g_frame - g_mechBattleFrame > 2700) { // never seen for half a minute: not held back for ever
        g_mechSeated = true;
        logf("mech: frame %u - no cockpit of the game's seen in this battle; ours is shown all the same", g_frame);
    }
    if (measured && g_mechSearching) {
        static int logs;
        if (logs < 6) {
            ++logs;
            int cells = 0;
            for (int k = 0; k < MG_N; ++k)
                if (g_mgFrame[k] > 0) ++cells;
            logf("mech: measured frame %u - %d draws, %d cockpit triangles, %d cells, %d unreadable", g_frame,
                 g_mgDraws, g_mgTris, cells, g_mgBad);
        }
        if (g_mgTris >= 30 && g_mgSampleN < MG_SAMPLES) memcpy(g_mgSamples[g_mgSampleN++], g_mgFrame, sizeof g_mgFrame);
        if (g_mgSampleN >= MG_SAMPLES) mechIdentify();
    }
    if ((g_mechSearching || !g_mechSeated) && inBattle && !zoomed && g_doubleDraw && g_frame >= g_mechNextFrame) {
        memset(g_mgFrame, 0, sizeof g_mgFrame);
        g_mgTris = g_mgBad = g_mgDraws = 0;
        g_mechMeasure = true; // the coming frame
        // outside the cockpit nothing is found: then only now and then
        g_mechNextFrame = g_frame + (!g_mechSearching ? 10 : g_mgSampleN ? 12 : 45);
    }
}

// ---- the values in use -----------------------------------------------------------------------

static inline float mechClamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// g: the [vr] values; out: what this cockpit gets. false: the [vr] values stay
bool mechValues(const MechValues &g, MechValues *out, const char **how) {
    if (!g_mechOn || g_mechCur < 0 || g_mechCur >= g_mechN) return false;
    const MechProfile &p = g_mechs[g_mechCur];
    if (p.tuned) {
        bool own = g_mechMw5 && p.seat5;
        *out = {
            p.cockpitScale, own ? p.seat5Fwd : p.seatFwd, own ? p.seat5Up : p.seatUp, p.panelX, p.panelY, p.panelDist,
            p.panelScale};
        *how = "set by hand";
        return true;
    }
    if (!g_mechAuto || !(p.dash > 0.05f)) return false;
    // from the tuned cockpits: the one most alike in its console's distance counts most
    double wsum = 0, k = 0, sf = 0, su = 0, px = 0, py = 0, pd = 0, ps = 0;
    for (int i = 0; i < g_mechN; ++i) {
        const MechProfile &r = g_mechs[i];
        if (!r.tuned || !(r.dash > 0.05f)) continue;
        double w = 1.0 / (0.1 + fabs(log((double)p.dash / r.dash)));
        double ratio = (double)p.dash / r.dash;
        float D = r.panelDist;
        k += w * r.cockpitScale * ratio; // the console as far away as there
        sf += w * r.seatFwd * ratio;     // the seat moved in proportion
        su += w * r.seatUp * ratio;
        px += w * (r.panelX + (tanf(p.centre * 0.0174533f) - tanf(r.centre * 0.0174533f)) * D);
        py += w * (r.panelY + (tanf(p.top * 0.0174533f) - tanf(r.top * 0.0174533f)) * D);
        pd += w * D;
        ps += w * r.panelScale;
        wsum += w;
    }
    if (wsum <= 0) return false;
    out->cockpitScale = mechClamp((float)(k / wsum), 0.5f, 8.0f);
    out->seatFwd = mechClamp((float)(sf / wsum), -3.0f, 3.0f);
    out->seatUp = mechClamp((float)(su / wsum), -3.0f, 3.0f);
    out->panelX = mechClamp((float)(px / wsum), g.panelX - 0.6f, g.panelX + 0.6f);
    out->panelY = mechClamp((float)(py / wsum), g.panelY - 0.6f, g.panelY + 0.6f);
    out->panelDist = (float)(pd / wsum);
    out->panelScale = (float)(ps / wsum);
    *how = "fitted from the cockpits set by hand";
    return true;
}

// the values in use become this cockpit's own ("set by hand"); false: no cockpit is known now
bool mechStore(const MechValues &v) {
    if (!g_mechOn || g_mechCur < 0 || g_mechCur >= g_mechN) return false;
    wchar_t sec[32];
    swprintf(sec, 32, L"mech_%d", g_mechCur + 1);
    MechProfile &p = g_mechs[g_mechCur];
    // In a cockpit of our own the seat is that cockpit's; the game's cockpit keeps the one it has. (A profile not
    // set by hand before has none yet: it gets the seat in use, as it always did.)
    bool gameSeat = !g_mechMw5 || !p.tuned;
    p.tuned = true;
    p.cockpitScale = v.cockpitScale;
    p.panelX = v.panelX, p.panelY = v.panelY, p.panelDist = v.panelDist, p.panelScale = v.panelScale;
    WritePrivateProfileStringW(sec, L"tuned", L"1", mechIni().c_str());
    mechPutF(sec, L"cockpit_scale", v.cockpitScale);
    if (gameSeat) {
        p.seatFwd = v.seatFwd, p.seatUp = v.seatUp;
        mechPutF(sec, L"seat_forward_m", v.seatFwd);
        mechPutF(sec, L"seat_up_m", v.seatUp);
    }
    if (g_mechMw5) {
        p.seat5 = true, p.seat5Fwd = v.seatFwd, p.seat5Up = v.seatUp;
        mechPutF(sec, L"mw5_seat_forward_m", v.seatFwd);
        mechPutF(sec, L"mw5_seat_up_m", v.seatUp);
    }
    mechPutF(sec, L"hud_panel_x_m", v.panelX);
    mechPutF(sec, L"hud_panel_y_m", v.panelY);
    mechPutF(sec, L"hud_panel_distance_m", v.panelDist);
    mechPutF(sec, L"hud_panel_scale", v.panelScale);
    return true;
}
