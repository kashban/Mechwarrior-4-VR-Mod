// MW4VR's arithmetic without Windows, Direct3D or the game: plain functions of numbers, so that
// tests/core_test.cpp can test them outside the game (tests/mutate.py plants errors into this file).
#pragma once
#include <cmath>
#include <cstring>

// ---- 3x4 matrices (rows: axis x, y, z; the fourth column the translation) ----

// out = a o b; out must not be a or b
static inline void mcMul34(const float *a, const float *b, float *out) {
    for (int i = 0; i < 3; ++i) {
        const float *r = a + i * 4;
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = r[0] * b[j] + r[1] * b[4 + j] + r[2] * b[8 + j];
        out[i * 4 + 3] += r[3];
    }
}

// view axis k (0 right, 1 up, 2 forward) of the element matrix m, in the world; A: the view's axes in the
// element's own coordinates (3x3, an axis per column)
static inline void mcViewAxis(const float *m, const float *A, int k, float *v) {
    for (int i = 0; i < 3; ++i) v[i] = m[i * 4] * A[k] + m[i * 4 + 1] * A[3 + k] + m[i * 4 + 2] * A[6 + k];
}

static inline float mcDot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// ---- the camera's frustum planes (cam.inc) ----
// Six planes (normal, d: inside is n.p <= d): far, near, right, left, bottom, top.
struct McFrustum {
    float planes[24];
    float d0, d1;      // how the game's planes 0 and 1 lie to the mech's view (1 and -1 when the layout is as known)
    float farD, nearD; // the game's far and near distance
    float t;           // the side planes' tangent used
};

// The game's planes pl (made from the mech's own view, matrix base) made anew for the view out, the side
// planes at the tangent given but never narrower than 1.6 (58 degrees: the headset's widest side is 55).
// The near plane comes half as near (the head moves in the cockpit). false: pl is not laid out as known.
static inline bool mcFrustumPlanes(const float *pl, const float *base, const float *out, const float *A, float tangent, McFrustum *res) {
    float fb[3], pb[3] = {base[3], base[7], base[11]};
    mcViewAxis(base, A, 2, fb);
    res->d0 = mcDot3(pl, fb);
    res->d1 = mcDot3(pl + 4, fb);
    if (!(res->d0 > 0.98f && res->d1 < -0.98f)) return false;
    float farD = pl[3] - mcDot3(fb, pb), nearD = -pl[7] - mcDot3(fb, pb);
    float r[3], u[3], f[3], p[3] = {out[3], out[7], out[11]};
    mcViewAxis(out, A, 0, r), mcViewAxis(out, A, 1, u), mcViewAxis(out, A, 2, f);
    float t = fabsf(tangent);
    t = t < 20.0f ? fmaxf(t, 1.6f) : 1.6f;
    float fp = mcDot3(f, p);
    float *n = res->planes;
    n[0] = f[0], n[1] = f[1], n[2] = f[2];
    n[4] = -f[0], n[5] = -f[1], n[6] = -f[2];
    n[3] = fp + farD, n[7] = -fp - (nearD > 0.05f ? nearD * 0.5f : nearD);
    const float *side[4] = {r, r, u, u};
    const float sg[4] = {1, -1, -1, 1};
    for (int k = 0; k < 4; ++k) {
        float v[3], l = 0;
        for (int i = 0; i < 3; ++i) v[i] = sg[k] * side[k][i] - t * f[i], l += v[i] * v[i];
        l = sqrtf(l);
        float *o = n + 8 + 4 * k;
        o[0] = v[0] / l, o[1] = v[1] / l, o[2] = v[2] / l;
        o[3] = mcDot3(o, p);
    }
    res->farD = farD, res->nearD = nearD, res->t = t;
    return true;
}

// ---- the wide HUD (hud.inc) ----

// [vr] hud_extra_width as read: 1 (not wider) for anything outside 1..1.3
static inline float mcWideFactor(float k) { return k <= 1.3f ? fmaxf(k, 1.0f) : 1.0f; }
// the layer texture's width for a picture w wide: a multiple of 8
static inline unsigned mcWideWidth(unsigned w, float k) { return k > 1.001f ? ((unsigned)(w * k + 0.5f) + 7) & ~7u : w; }

// ---- the parts of the HUD image shown as quads (vr.inc) ----
// u, v: 0..1 over the game's picture (u beyond 1 with the wide HUD), v down.

enum { MC_RECTS = 32, MC_SPANS = 8, MC_CAMS = 4 };
enum { MC_NEAR = 0, MC_FAR = 1, MC_NEAR_OPAQUE = 2, MC_FAR_OPAQUE = 3, MC_FAR_UP = 4, MC_FADE = 5 };
// a part on the cockpit's glass: MC_GLASS + its number; a camera's window in it, opaque: MC_GLASS_OPAQUE + its number
enum { MC_GLASSES = 8, MC_GLASS = 8, MC_GLASS_OPAQUE = 16 };
struct McRects {
    float rc[MC_RECTS][5]; // u0 v0 u1 v1 kind
    int n;
};

// slivers are left out
static inline void mcAdd(McRects &q, float u0, float v0, float u1, float v1, float kind) {
    if (q.n < MC_RECTS && u1 - u0 > 0.005f && v1 - v0 > 0.005f) {
        float *r = q.rc[q.n++];
        r[0] = u0, r[1] = v0, r[2] = u1, r[3] = v1, r[4] = kind;
    }
}

static inline bool mcMeet(const float *c, float u0, float v0, float u1, float v1) { return c[0] < u1 && c[2] > u0 && c[1] < v1 && c[3] > v0; }

// a rectangle without what the rectangle c covers of it (c null: all of it): above, below, left, right
static inline void mcAddCut(McRects &q, const float *c, float u0, float v0, float u1, float v1, float kind) {
    if (!(c && mcMeet(c, u0, v0, u1, v1))) {
        mcAdd(q, u0, v0, u1, v1, kind);
        return;
    }
    float a0 = fmaxf(c[0], u0), a1 = fmaxf(c[1], v0), a2 = fminf(c[2], u1), a3 = fminf(c[3], v1); // what the two share
    mcAdd(q, u0, v0, u1, a1, kind);
    mcAdd(q, u0, a3, u1, v1, kind);
    mcAdd(q, u0, a1, a0, a3, kind);
    mcAdd(q, a2, a1, u1, a3, kind);
}

// the rectangle c cut out of every far part there is, and added by its own as kind
static inline void mcMoveOut(McRects &q, const float *c, float kind) {
    int n0 = q.n;
    for (int i = 0; i < n0; ++i) {
        float *r = q.rc[i];
        if (r[4] != MC_FAR) continue;
        float u0 = r[0], v0 = r[1], u1 = r[2], v1 = r[3];
        if (!mcMeet(c, u0, v0, u1, v1)) continue;
        float a0 = fmaxf(c[0], u0), a1 = fmaxf(c[1], v0), a2 = fminf(c[2], u1), a3 = fminf(c[3], v1); // what the two share
        r[3] = a1; // what lies above it; then below, left, right
        mcAdd(q, u0, a3, u1, v1, MC_FAR);
        mcAdd(q, u0, a1, a0, a3, MC_FAR);
        mcAdd(q, a2, a1, u1, a3, MC_FAR);
    }
    mcAdd(q, c[0], c[1], c[2], c[3], kind);
}

// a monitor's part r (u0 v0 u1 v1) of the HUD image: if it reaches into the top strip (above v = T), its span
// of u goes into iv, kept in rising order of u0
static inline void mcTopSpan(float (*iv)[2], int &n, const float *r, float T) {
    if (n >= MC_SPANS || !(r[2] > r[0] && r[3] > r[1]) || r[1] >= T) return;
    int k = n++;
    for (; k > 0 && iv[k - 1][0] > r[0]; --k) iv[k][0] = iv[k - 1][0], iv[k][1] = iv[k - 1][1];
    iv[k][0] = r[0], iv[k][1] = r[2];
}

// a monitor's second part a in the left side column (u < L): how far down it reaches, if further than v
static inline float mcLeftAlt(const float *a, float L, float v) { return a[2] > a[0] && a[3] > a[1] && a[0] < L ? fmaxf(a[3], v) : v; }

struct McHud {
    float T, B, L, R;   // the far middle: marks stay on their targets there
    float UR;           // the image's right edge (beyond 1: the wide HUD)
    int nCam;           // the small cameras' windows this frame (at most MC_CAMS); one about the middle is the zoom window
    float cam[MC_CAMS][4];
    float margin;       // cut away around the zoom window (the game's frame of it)
    bool screens;       // the cockpit's monitors show the near frame
    int nSpan;          // what they show of the top strip (mcTopSpan; at most MC_SPANS)
    float span[MC_SPANS][2];
    float leftAlt;      // and of the left column (mcLeftAlt)
    float twist[4];     // the torso mark, shown higher
    float twistDv;
    int nGlass;         // parts shown on the cockpit's glass (at most MC_GLASSES)
    float glass[MC_GLASSES][4];
};

// the point u v lies in the rectangle r (not on its edge)
static inline bool mcHolds(const float *r, float u, float v) { return r[0] < u && u < r[2] && r[1] < v && v < r[3]; }

// true and zr: the zoom window (the last window whose centre lies in the middle of the picture)
static inline bool mcZoomWindow(const McHud &h, float *zr) {
    bool zoom = false;
    for (int i = 0; i < h.nCam; ++i) {
        float cu = 0.5f * (h.cam[i][0] + h.cam[i][2]);
        if (cu > 0.3f && cu < 0.7f) {
            memcpy(zr, h.cam[i], sizeof h.cam[i]);
            zoom = true;
        }
    }
    return zoom;
}

// The list, in the order drawn: the far middle around the zoom window; with monitors what none of them shows
// (top strip, side columns) far and the torso mark moved up, without monitors the near frame; the zoom window
// far and opaque; without monitors the other windows near and opaque.
static inline void mcHudRects(const McHud &h, McRects &q) {
    q.n = 0;
    float zr[4], zc[4]; // the zoom window, and with its frame: what is cut out of the rest
    const float *cut = nullptr;
    if (mcZoomWindow(h, zr)) {
        zc[0] = zr[0] - h.margin, zc[1] = zr[1] - h.margin, zc[2] = zr[2] + h.margin, zc[3] = zr[3] + h.margin;
        cut = zc;
    }
    float T = h.T, B = h.B, L = h.L, R = h.R, UR = h.UR;
    mcAddCut(q, cut, L, T, R, B, MC_FAR);
    if (h.screens) {
        float u = 0; // the top strip between the monitors' parts (the compass, mission texts)
        for (int i = 0; i < h.nSpan; ++i) {
            mcAddCut(q, cut, u, 0, h.span[i][0], T, MC_FAR); // (nothing where the spans overlap: mcAdd leaves it out)
            u = fmaxf(u, h.span[i][1]);
        }
        mcAddCut(q, cut, u, 0, UR, T, MC_FAR);
        mcAddCut(q, cut, 0, fmaxf(h.leftAlt, T), L, B, MC_FAR); // the side columns (messages)
        mcAddCut(q, cut, R, T, UR, B, MC_FAR);
        const float *tw = h.twist;
        if (!cut && tw[2] > tw[0] && tw[3] > tw[1] && h.twistDv != 0) mcMoveOut(q, tw, MC_FAR_UP);
    } else {
        const float nearRc[4][4] = {{0, 0, UR, T}, {0, B, UR, 1}, {0, T, L, B}, {R, T, UR, B}};
        for (const auto &nr : nearRc) {
            float u0 = nr[0], v0 = nr[1], u1 = nr[2], v1 = nr[3];
            if (cut && mcMeet(zc, u0, v0, u1, v1)) { // left and right of the window only
                mcAdd(q, u0, v0, zc[0], v1, MC_NEAR);
                mcAdd(q, zc[2], v0, u1, v1, MC_NEAR);
            } else {
                mcAdd(q, u0, v0, u1, v1, MC_NEAR);
            }
        }
    }
    if (cut) mcAdd(q, zr[0], zr[1], zr[2], zr[3], MC_FAR_OPAQUE);
    for (int i = 0; i < h.nCam && !h.screens; ++i) { // the other windows (the target's view), over the frame
        float cu = 0.5f * (h.cam[i][0] + h.cam[i][2]);
        if (cu > 0.3f && cu < 0.7f) continue;
        mcAdd(q, h.cam[i][0], h.cam[i][1], h.cam[i][2], h.cam[i][3], MC_NEAR_OPAQUE);
    }
    for (int g = 0; g < h.nGlass; ++g) { // the parts on the glass, each with the windows in it (the target's view) over it
        const float *r = h.glass[g];
        mcAdd(q, r[0], r[1], r[2], r[3], (float)(MC_GLASS + g));
        for (int i = 0; i < h.nCam; ++i) {
            const float *c = h.cam[i];
            if (mcHolds(r, 0.5f * (c[0] + c[2]), 0.5f * (c[1] + c[3]))) mcAdd(q, c[0], c[1], c[2], c[3], (float)(MC_GLASS_OPAQUE + g));
        }
    }
}

// ---- where a part on the glass lies (vr.inc, mw5.inc) ----
// Seen from the seat: degrees to the right and up from straight ahead, at the distance D. The runtime's axes:
// x right, y up, z back.

static const float MC_RAD = 0.01745329252f;

// the middle of the part (pos) and its turn towards the seat (q: x y z w)
static inline void mcGlassPose(float right, float up, float D, float *pos, float *q) {
    float a = right * MC_RAD, b = up * MC_RAD;
    pos[0] = D * sinf(a) * cosf(b), pos[1] = D * sinf(b), pos[2] = -D * cosf(a) * cosf(b);
    float sy = -sinf(0.5f * a), cy = cosf(0.5f * a), sx = sinf(0.5f * b), cx = cosf(0.5f * b); // about y, then about x
    q[0] = cy * sx, q[1] = sy * cx, q[2] = -sy * sx, q[3] = cy * cx;
}

// metres for one unit of u, for a part du wide in the HUD image shown width degrees wide
static inline float mcGlassScale(float width, float D, float du) { return 2 * D * tanf(0.5f * width * MC_RAD) / du; }

// where the pilot looks at the glass: from the head p along f (a unit vector) to the sphere of radius D about the
// seat (a head outside it: the nearest point of the line of sight)
static inline void mcGlassAim(const float *p, const float *f, float D, float *right, float *up) {
    float pf = mcDot3(p, f);
    float t = sqrtf(fmaxf(pf * pf - mcDot3(p, p) + D * D, 0.0f)) - pf;
    float x = p[0] + t * f[0], y = p[1] + t * f[1], z = p[2] + t * f[2];
    *right = atan2f(x, -z) / MC_RAD;
    *up = atan2f(y, sqrtf(x * x + z * z)) / MC_RAD;
}
