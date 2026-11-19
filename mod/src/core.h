// MW4VR's arithmetic without Windows, Direct3D or the game: plain functions of numbers, so that
// tests/core_test.cpp can test them outside the game (tests/mutate.py plants errors into this file).
#pragma once
#include <cmath>
#include <cstring>

// ---- 3x4 matrices (rows: axis x, y, z; the fourth column the translation) ----
static const float MC_STEP_MAX = 0.03125f; // a step of the cockpit against the camera: under 3 cm, 0.03 rad a frame

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

// ---- the cockpit's sway from its place in the world (sway.cpp) ----
// A rigid 3x4 (rows: axes, the fourth column the translation) the other way round: out must not be m
static inline void mcInv34(const float *m, float *out) {
    for (int i = 0; i < 3; ++i) {
        out[i * 4 + 3] = -(m[i] * m[3] + m[4 + i] * m[7] + m[8 + i] * m[11]);
        for (int j = 0; j < 3; ++j) out[i * 4 + j] = m[j * 4 + i];
    }
}
// The cockpit's pose against the camera, in view space (x right, y up, z forward): base is the camera's matrix
// (camera -> world), shape the cockpit's (cockpit -> world), A the view's axes in the camera's own coordinates
// (3x3, an axis per column). rel: 3x4, view <- cockpit.
static inline void mcShapeInView(const float *base, const float *A, const float *shape, float *rel) {
    float inv[12], loc[12];
    mcInv34(base, inv);
    mcMul34(inv, shape, loc);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j) rel[i * 4 + j] = A[i] * loc[j] + A[3 + i] * loc[4 + j] + A[6 + i] * loc[8 + j];
}
// The small movement from one such pose to the next: a point p of the camera's space that goes with the
// cockpit moves to p + d + w x p (about the camera). false: no small movement - a cut to another view.
static inline bool mcShapeStep(const float *prev, const float *cur, float *w, float *d) {
    float inv[12], m[12];
    mcInv34(prev, inv);
    mcMul34(cur, inv, m);
    w[0] = 0.5f * (m[9] - m[6]), w[1] = 0.5f * (m[2] - m[8]), w[2] = 0.5f * (m[4] - m[1]);
    d[0] = m[3], d[1] = m[7], d[2] = m[11];
    return mcDot3(w, w) < MC_STEP_MAX * MC_STEP_MAX && mcDot3(d, d) < MC_STEP_MAX * MC_STEP_MAX;
}

// ---- the right eye's view from the left eye's (replay.cpp) ----
// 4x4 matrices of doubles, row-major, on column vectors: the world's coordinates are large against an eye's 6 cm.
struct McM4 {
    double m[16];
};
static inline McM4 mcM4Mul(const McM4 &a, const McM4 &b) {
    McM4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i * 4 + j] = a.m[i * 4] * b.m[j] + a.m[i * 4 + 1] * b.m[4 + j] + a.m[i * 4 + 2] * b.m[8 + j] +
                             a.m[i * 4 + 3] * b.m[12 + j];
    return r;
}
// a 3x4 (rows: axes, the fourth column the translation) as a 4x4
static inline McM4 mcM4From34(const float *c) {
    McM4 r = {{c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10], c[11], 0, 0, 0, 1}};
    return r;
}
// (limits as powers of two)
static const double MC_DET_MIN = 1.0 / 1099511627776.0; // a determinant under 2^-40: no inverse
static const double MC_PROJ_UNIT = 1.0 / 64;            // how far the w row may be from a unit vector
static const double MC_PROJ_MIN = 1.0 / 16;             // the smallest scale of a projection
static const double MC_PROJ_NEAR = 1.0 / 8192;          // its smallest near term
// the inverse of a matrix whose last row is 0 0 0 1; false: it has none
static inline bool mcM4InvAffine(const McM4 &a, McM4 *out) {
    const double *m = a.m;
    double c0 = m[5] * m[10] - m[6] * m[9], c1 = m[6] * m[8] - m[4] * m[10], c2 = m[4] * m[9] - m[5] * m[8];
    double det = m[0] * c0 + m[1] * c1 + m[2] * c2;
    if (fabs(det) < MC_DET_MIN) return false;
    const double r[] = {c0 / det, (m[2] * m[9] - m[1] * m[10]) / det, (m[1] * m[6] - m[2] * m[5]) / det,
                        c1 / det, (m[0] * m[10] - m[2] * m[8]) / det, (m[2] * m[4] - m[0] * m[6]) / det,
                        c2 / det, (m[1] * m[8] - m[0] * m[9]) / det,  (m[0] * m[5] - m[1] * m[4]) / det};
    McM4 o = {{r[0], r[1], r[2], -(r[0] * m[3] + r[1] * m[7] + r[2] * m[11]), r[3], r[4], r[5],
               -(r[3] * m[3] + r[4] * m[7] + r[5] * m[11]), r[6], r[7], r[8],
               -(r[6] * m[3] + r[7] * m[7] + r[8] * m[11]), 0, 0, 0, 1}};
    *out = o;
    return true;
}
// The projection, read from a world -> clip matrix W = P V (rows of W: x, y, depth, w; the w row is the view's
// unit forward axis):  P = [p00 0 j0 0 | 0 p11 j1 0 | 0 0 a b | 0 0 1 0]  on a view-space point (x right, y up,
// z forward, 1). ok: W is such a matrix.
struct McProj {
    double p00, p11, j0, j1, a, b;
    bool ok;
};
static inline double mcDot3d(const float *a, const float *b) {
    return (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2];
}
static inline McProj mcProjFrom(const float *w) {
    McProj p = {};
    const float *f = w + 12;
    if (fabs(sqrt(mcDot3d(f, f)) - 1) > MC_PROJ_UNIT) return p;
    p.j0 = mcDot3d(w, f);
    p.j1 = mcDot3d(w + 4, f);
    p.p00 = sqrt(mcDot3d(w, w) - p.j0 * p.j0);
    p.p11 = sqrt(mcDot3d(w + 4, w + 4) - p.j1 * p.j1);
    p.a = mcDot3d(w + 8, f);
    p.b = w[11] - p.a * w[15];
    p.ok = p.p00 > MC_PROJ_MIN && p.p11 > MC_PROJ_MIN && fabs(p.b) > MC_PROJ_NEAR;
    return p;
}
static inline McM4 mcProjM(const McProj &p) {
    McM4 r = {{p.p00, 0, p.j0, 0, 0, p.p11, p.j1, 0, 0, 0, p.a, p.b, 0, 0, 1, 0}};
    return r;
}
static inline McM4 mcProjInv(const McProj &p) {
    McM4 r = {{1 / p.p00, 0, 0, -p.j0 / p.p00, 0, 1 / p.p11, 0, -p.j1 / p.p11, 0, 0, 0, 1, 0, 0, 1 / p.b, -p.a / p.b}};
    return r;
}
// The step from the left eye's view space to the right eye's. An eye's pose: R (3x3, columns = the eye's axes)
// and t, both in the game camera's view space - a point x of the eye's own space is R x + t there. So a point x
// of the left eye's space is R1^T (R0 x + t0 - t1) in the right eye's.
static inline McM4 mcEyeStep(const float *R0, const float *t0, const float *R1, const float *t1) {
    McM4 h = {};
    for (int i = 0; i < 3; ++i) {
        h.m[i * 4 + 3] =
            R1[i] * ((double)t0[0] - t1[0]) + R1[3 + i] * ((double)t0[1] - t1[1]) + R1[6 + i] * ((double)t0[2] - t1[2]);
        for (int j = 0; j < 3; ++j)
            h.m[i * 4 + j] = (double)R1[i] * R0[j] + (double)R1[3 + i] * R0[3 + j] + (double)R1[6 + i] * R0[6 + j];
    }
    h.m[15] = 1;
    return h;
}
// The right eye's matrices from the left eye's. With the projection P (the eyes share it) and the step Hv:
// a matrix M into the left eye's clip space is kc M for the right eye, one out of it M kcInv; for a thing that
// goes with the eye (the sky) kcTurn M - the eyes' turn against each other without the step between them.
// With the eyes' camera matrices c0, c1 (3x4, camera -> world): a place p in the world that is fixed to the
// camera is kwInv p for the right eye; kw is its inverse (a matrix W from the world into something fixed to the
// camera becomes W kw - and kc W = W kw for the world -> clip matrix: the check of all this).
struct McStereo {
    McM4 kc, kcInv, kcTurn, kw, kwInv;
};
static inline bool mcStereo(const McProj &p, const McM4 &hv, const float *c0, const float *c1, McStereo *s) {
    McM4 hvInv, c0i, c1i, C0 = mcM4From34(c0), C1 = mcM4From34(c1);
    if (!p.ok || !mcM4InvAffine(hv, &hvInv) || !mcM4InvAffine(C0, &c0i) || !mcM4InvAffine(C1, &c1i)) return false;
    McM4 P = mcProjM(p), Pi = mcProjInv(p), turn = hv;
    turn.m[3] = turn.m[7] = turn.m[11] = 0;
    s->kc = mcM4Mul(mcM4Mul(P, hv), Pi);
    s->kcInv = mcM4Mul(mcM4Mul(P, hvInv), Pi);
    s->kcTurn = mcM4Mul(mcM4Mul(P, turn), Pi);
    s->kw = mcM4Mul(C0, c1i);
    s->kwInv = mcM4Mul(C1, c0i);
    return true;
}
// the largest difference of an element
static inline double mcM4Apart(const McM4 &a, const McM4 &b) {
    double e = 0;
    for (int i = 0; i < 16; ++i) e = fmax(e, fabs(a.m[i] - b.m[i]));
    return e;
}
// a 4x4 of floats (a shader's constant) times a 4x4 of doubles, either way round, back into the floats
static inline McM4 mcM4OfFloats(const float *f) {
    McM4 m = {{f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15]}};
    return m;
}
static inline void mcM4ToFloats(const McM4 &m, float *f) {
    for (int i = 0; i < 16; ++i) f[i] = (float)m.m[i];
}
static inline void mcM4Left(const McM4 &k, float *f) { mcM4ToFloats(mcM4Mul(k, mcM4OfFloats(f)), f); }
static inline void mcM4Right(const McM4 &k, float *f) { mcM4ToFloats(mcM4Mul(mcM4OfFloats(f), k), f); }
// the same in floats alone (many matrices a frame: the instances): out = k f; out must not be k or f
static inline void mcM4MulF(const float *k, const float *f, float *out) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            out[i * 4 + j] =
                k[i * 4] * f[j] + k[i * 4 + 1] * f[4 + j] + k[i * 4 + 2] * f[8 + j] + k[i * 4 + 3] * f[12 + j];
}
// a place (3 floats) through an affine matrix
static inline void mcM4Point(const McM4 &k, float *f) {
    double x = f[0], y = f[1], z = f[2];
    for (int i = 0; i < 3; ++i)
        f[i] = (float)(k.m[i * 4] * x + k.m[i * 4 + 1] * y + k.m[i * 4 + 2] * z + k.m[i * 4 + 3]);
}

// ---- which way the right eye is made (replay.cpp, [vr] replay_right=2) ----
// Two ways: the game draws both eyes, or it draws the left one and the mod repeats its draws for the right one
// ("replay"). Replay saves the processor a pass and costs the graphics card a little; which is faster depends
// on which of the two the frame waits for. That is told by how long the system's Present waits: long - for the
// graphics card; hardly - the processor is the limit (or neither: the headset's rate is reached).
// The frames of a battle are watched a window at a time. When they are slower than the headset's and the limit
// is the one the other way would ease, the other way is tried for some frames, and kept if it is faster by a
// sixteenth; if not, it is not tried again for a while, each time twice as long.
// A battle begins with replay: on most maps it is the faster way, and a battle's first seconds are not spent
// finding that out. And the game's own way is tried only where it can win at all: it costs the processor a
// second pass of the camera, about MC_AUTO_SECOND of the first - with that added to the processor's part of a
// frame (the frame less the wait for the graphics card) it must still come out faster.
struct McAuto {
    bool replay;    // the way in use
    bool trying;    // the other way is being tried
    unsigned n;     // frames of the window or the trial so far
    double sum;     // their lengths (ms)
    double waits;   // their waits in Present (ms)
    double passes;  // their passes of the main camera (ms)
    double before;  // the mean length before the trial
    double after;   // the mean length in the last trial
    unsigned rest;  // frames until a trial may begin again
    unsigned pause; // the rest after the last trial that brought nothing
};
// Counted in frames: the rest when a battle begins (it loads still); the frames watched at a time; the frames of
// a trial that do not count (new textures, histories) and all its frames; the rest after a trial that was kept,
// after a first one that brought nothing, and the longest.
enum { MC_AUTO_START = 120, MC_AUTO_WINDOW = 45, MC_AUTO_SETTLE = 25, MC_AUTO_TRIAL = 70 };
enum { MC_AUTO_KEPT_REST = 225, MC_AUTO_PAUSE = 900, MC_AUTO_PAUSE_MAX = 7200 };
enum { MC_AUTO_NONE, MC_AUTO_TRY, MC_AUTO_KEEP, MC_AUTO_BACK };
static const double MC_AUTO_SLOW = 11.625; // a mean frame longer than this (ms) is slower than 90 a second
static const double MC_AUTO_GPU = 2.0;     // a mean wait in Present above this: the graphics card is the limit
static const double MC_AUTO_CPU = 0.75;    // one under this: it is not
static const double MC_AUTO_GAIN = 0.9375; // a trial is kept when its frames are this much of the ones before
static const double MC_AUTO_SECOND = 0.75; // the game's pass for the second eye, against the first one's
// a battle begins: with replay
static inline McAuto mcAutoStart() {
    McAuto a = {};
    a.replay = true;
    a.rest = MC_AUTO_START;
    return a;
}
// one frame of a battle: its length, the time the system's Present waited in it and the time the main camera's
// pass took (ms). What happened: nothing, a trial began (a.replay is the other way now), a trial was kept, or
// it was not (a.replay is back)
static inline int mcAutoStep(McAuto &a, double frame, double wait, double pass) {
    if (!a.trying) {
        if (a.rest) {
            --a.rest;
            return MC_AUTO_NONE;
        }
        a.sum += frame, a.waits += wait, a.passes += pass;
        if (++a.n < MC_AUTO_WINDOW) return MC_AUTO_NONE;
        double mean = a.sum / MC_AUTO_WINDOW, w = a.waits / MC_AUTO_WINDOW, p = a.passes / MC_AUTO_WINDOW;
        a.n = 0, a.sum = 0, a.waits = 0, a.passes = 0;
        bool other =
            a.replay ? w > MC_AUTO_GPU && mean - w + MC_AUTO_SECOND * p < mean * MC_AUTO_GAIN : w < MC_AUTO_CPU;
        if (!(mean > MC_AUTO_SLOW && other)) return MC_AUTO_NONE;
        a.before = mean, a.replay = !a.replay, a.trying = true;
        return MC_AUTO_TRY;
    }
    if (++a.n > MC_AUTO_SETTLE) a.sum += frame;
    if (a.n < MC_AUTO_TRIAL) return MC_AUTO_NONE;
    double mean = a.sum / (MC_AUTO_TRIAL - MC_AUTO_SETTLE);
    a.n = 0, a.sum = 0, a.trying = false, a.after = mean;
    if (mean < a.before * MC_AUTO_GAIN) {
        a.pause = 0, a.rest = MC_AUTO_KEPT_REST;
        return MC_AUTO_KEEP;
    }
    a.replay = !a.replay;
    a.pause = a.pause ? (unsigned)fmin(a.pause * 2.0, (double)MC_AUTO_PAUSE_MAX) : MC_AUTO_PAUSE;
    a.rest = a.pause;
    return MC_AUTO_BACK;
}

// ---- the camera's frustum planes (cam.cpp) ----
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
static inline bool mcFrustumPlanes(const float *pl, const float *base, const float *out, const float *A, float tangent,
                                   McFrustum *res) {
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

// ---- the wide HUD (hud.cpp) ----

// [vr] hud_extra_width as read: 1 (not wider) for anything outside 1..1.3
static inline float mcWideFactor(float k) { return k <= 1.3f ? fmaxf(k, 1.0f) : 1.0f; }
// the layer texture's width for a picture w wide: a multiple of 8
static inline unsigned mcWideWidth(unsigned w, float k) {
    return k > 1.001f ? ((unsigned)(w * k + 0.5f) + 7) & ~7u : w;
}

// ---- the parts of the HUD image shown as quads (vr.cpp) ----
// u, v: 0..1 over the game's picture (u beyond 1 with the wide HUD), v down.

enum { MC_RECTS = 32, MC_SPANS = 8, MC_CAMS = 4 };
enum { MC_NEAR = 0, MC_FAR = 1, MC_NEAR_OPAQUE = 2, MC_FAR_OPAQUE = 3, MC_FAR_UP = 4, MC_FADE = 5, MC_FAR_SMALL = 6 };
enum {
    MC_SHOWN = 15
}; // as many parts every runtime shows beside the world's layer (a runtime may show more: mcHudRects' shown)
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

static inline bool mcMeet(const float *c, float u0, float v0, float u1, float v1) {
    return c[0] < u1 && c[2] > u0 && c[1] < v1 && c[3] > v0;
}

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
        float a0 = fmaxf(c[0], u0), a1 = fmaxf(c[1], v0), a2 = fminf(c[2], u1),
              a3 = fminf(c[3], v1); // what the two share
        r[3] = a1;                  // what lies above it; then below, left, right
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
static inline float mcLeftAlt(const float *a, float L, float v) {
    return a[2] > a[0] && a[3] > a[1] && a[0] < L ? fmaxf(a[3], v) : v;
}

// the same for a part a in the right side column (u > R)
static inline float mcRightAlt(const float *a, float R, float v) {
    return a[2] > a[0] && a[3] > a[1] && a[2] > R ? fmaxf(a[3], v) : v;
}

struct McHud {
    float T, B, L, R; // the far middle: marks stay on their targets there
    float UR;         // the image's right edge (beyond 1: the wide HUD)
    int nCam; // the small cameras' windows this frame (at most MC_CAMS); one about the middle is the zoom window
    float cam[MC_CAMS][4];
    float margin; // cut away around the zoom window (the game's frame of it)
    bool screens; // the cockpit's monitors show the near frame
    int nSpan;    // what they show of the top strip (mcTopSpan; at most MC_SPANS)
    float span[MC_SPANS][2];
    float leftAlt;  // and of the left column (mcLeftAlt)
    float rightAlt; // and of the right one (mcRightAlt)
    float twist[4]; // the torso mark, shown higher
    float twistDv;
    int nGlass; // parts shown on the cockpit's glass (at most MC_GLASSES)
    float glass[MC_GLASSES][4];
    float cross[4]; // the cross-hair with what belongs to it, shown smaller about its middle (empty: as it is)
};

// the point u v lies in the rectangle r (not on its edge)
static inline bool mcHolds(const float *r, float u, float v) { return r[0] < u && u < r[2] && r[1] < v && v < r[3]; }

// degrees as -180 < d <= 180 (a whole turn is none; what is no number: 0)
static inline float mcTurnNorm(float d) {
    if (!(d > -1e6f && d < 1e6f)) return 0;
    d = fmodf(d, 360.0f);
    if (d > 180)
        d -= 360;
    else if (d <= -180)
        d += 360;
    return d;
}

// A monitor (width / height: mon) shows a part of the HUD image (width / height: part) whole, undistorted and as
// large as it fits, turned deg clockwise. m[0..1], m[2..3]: the rows that take the monitor's uv about its middle
// (u to the right, v down) to the part's about its middle (inside: both within -0.5..0.5)
static inline void mcTurnFit(float mon, float part, float deg, float *m) {
    const float c = cosf(deg * 0.01745329252f), s = sinf(deg * 0.01745329252f);
    const float ac = fabsf(c), as = fabsf(s);
    const float kw = mon / (part * ac + as), kh = 1 / (part * as + ac);
    const float k = fminf(kw, kh); // the part's height in monitor heights
    m[0] = mon * c / (part * k), m[1] = s / (part * k);
    m[2] = -mon * s / k, m[3] = c / k;
}

// The eye's place in a mesh drawn with m (3 rows of 4, mesh -> view: a rotation times the size s, then a shift)
static inline void mcEyeInMesh(const float *m, float s, float *eye) {
    for (int j = 0; j < 3; ++j) eye[j] = -(m[j] * m[3] + m[4 + j] * m[7] + m[8 + j] * m[11]) / (s * s);
}

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
// (top strip, side columns) far and the torso mark moved up, without monitors the near frame; the cross-hair
// (small: a part of its own, while no zoom window is open); the zoom window far and opaque; without monitors the
// other windows near and opaque.
static inline void mcHudRectsWith(const McHud &h, McRects &q, bool shrink) {
    q.n = 0;
    float zr[4], zc[4]; // the zoom window, and with its frame: what is cut out of the rest
    const float *cut = nullptr;
    if (mcZoomWindow(h, zr)) {
        zc[0] = zr[0] - h.margin, zc[1] = zr[1] - h.margin, zc[2] = zr[2] + h.margin, zc[3] = zr[3] + h.margin;
        cut = zc;
    }
    float T = h.T, B = h.B, L = h.L, R = h.R, UR = h.UR;
    if (h.screens) {
        // the middle and the side columns (messages), each column from where the part above it ends, in as few
        // rectangles as there can be (the runtime shows 16 layers): the middle alone down to the higher column's
        // top, with that column down to the other's top, then the whole width
        float a = fminf(fmaxf(h.leftAlt, T), B), b = fminf(fmaxf(h.rightAlt, T), B), lo = fminf(a, b), hi = fmaxf(a, b);
        mcAddCut(q, cut, L, T, R, lo, MC_FAR);
        mcAddCut(q, cut, 0, a, R, hi, MC_FAR);
        mcAddCut(q, cut, L, b, UR, hi, MC_FAR);
        mcAddCut(q, cut, 0, hi, UR, B, MC_FAR);
        float u = 0; // the top strip between the monitors' parts (the compass, mission texts)
        for (int i = 0; i < h.nSpan; ++i) {
            mcAddCut(q, cut, u, 0, h.span[i][0], T, MC_FAR); // (nothing where the spans overlap: mcAdd leaves it out)
            u = fmaxf(u, h.span[i][1]);
        }
        mcAddCut(q, cut, u, 0, UR, T, MC_FAR);
        const float *tw = h.twist;
        if (!cut && tw[2] > tw[0] && tw[3] > tw[1] && h.twistDv != 0) mcMoveOut(q, tw, MC_FAR_UP);
    } else {
        mcAddCut(q, cut, L, T, R, B, MC_FAR);
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
    const float *cr = h.cross;
    if (shrink && !cut && cr[2] > cr[0] && cr[3] > cr[1]) mcMoveOut(q, cr, MC_FAR_SMALL);
    if (cut) mcAdd(q, zr[0], zr[1], zr[2], zr[3], MC_FAR_OPAQUE);
    for (int i = 0; i < h.nCam && !h.screens; ++i) { // the other windows (the target's view), over the frame
        float cu = 0.5f * (h.cam[i][0] + h.cam[i][2]);
        if (cu > 0.3f && cu < 0.7f) continue;
        mcAdd(q, h.cam[i][0], h.cam[i][1], h.cam[i][2], h.cam[i][3], MC_NEAR_OPAQUE);
    }
    for (int g = 0; g < h.nGlass;
         ++g) { // the parts on the glass, each with the windows in it (the target's view) over it
        const float *r = h.glass[g];
        mcAdd(q, r[0], r[1], r[2], r[3], (float)(MC_GLASS + g));
        for (int i = 0; i < h.nCam; ++i) {
            const float *c = h.cam[i];
            if (mcHolds(r, 0.5f * (c[0] + c[2]), 0.5f * (c[1] + c[3])))
                mcAdd(q, c[0], c[1], c[2], c[3], (float)(MC_GLASS_OPAQUE + g));
        }
    }
}

// (the cross-hair's own part costs up to four more: left as it is where the runtime would not show them all)
static inline void mcHudRects(const McHud &h, McRects &q, int shown = MC_SHOWN) {
    mcHudRectsWith(h, q, true);
    if (q.n > shown) mcHudRectsWith(h, q, false);
}

// ---- where a part on the glass lies (vr.cpp, mw5.cpp) ----
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

// ---- the cockpit as seen from the seat: where its windows are (mw5.cpp) ----
// A grid of directions, 2 degrees a cell, 80 degrees to each side and 50 up and down: the nearest mesh in each
// (metres), 0 where the view is open. A cell stands for the direction of its lower left corner.

enum { MC_GW = 80, MC_GH = 50 };
static const float MC_GSTEP = 2.0f, MC_GAZ0 = -80.0f, MC_GEL0 = -50.0f;
struct McGrid {
    float d[MC_GW * MC_GH];
};

static inline void mcGridMark(McGrid &g, int c, int r, float d) {
    if (c < 0 || c >= MC_GW || r < 0 || r >= MC_GH) return;
    float &o = g.d[r * MC_GW + c];
    o = o > 0 ? fminf(o, d) : d;
}

// a point (metres right, up, ahead of the seat) in cells: across, up
static inline float mcGridX(const float *v) { return (atan2f(v[0], v[2]) / MC_RAD - MC_GAZ0) / MC_GSTEP; }
static inline float mcGridY(const float *v) {
    return (atan2f(v[1], sqrtf(v[0] * v[0] + v[2] * v[2])) / MC_RAD - MC_GEL0) / MC_GSTEP;
}
// which side of the line from a to b the point p lies on (0: on it)
static inline float mcGridSide(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

// a triangle of the mesh: the cells its corners lie in and those whose direction it covers. One reaching beside or
// behind the seat is left out.
static inline void mcGridTri(McGrid &g, const float *a, const float *b, const float *c) {
    if (!(a[2] > 0.05f && b[2] > 0.05f && c[2] > 0.05f)) return;
    float xa = mcGridX(a), ya = mcGridY(a), xb = mcGridX(b), yb = mcGridY(b), xc = mcGridX(c), yc = mcGridY(c);
    float d = (sqrtf(mcDot3(a, a)) + sqrtf(mcDot3(b, b)) + sqrtf(mcDot3(c, c))) / 3;
    mcGridMark(g, (int)floorf(xa), (int)floorf(ya), d);
    mcGridMark(g, (int)floorf(xb), (int)floorf(yb), d);
    mcGridMark(g, (int)floorf(xc), (int)floorf(yc), d);
    int cEnd = (int)floorf(fmaxf(xa, fmaxf(xb, xc))), rEnd = (int)floorf(fmaxf(ya, fmaxf(yb, yc)));
    for (int r = (int)ceilf(fminf(ya, fminf(yb, yc))); r <= rEnd; ++r)
        for (int cc = (int)ceilf(fminf(xa, fminf(xb, xc))); cc <= cEnd; ++cc) {
            float e0 = mcGridSide(xa, ya, xb, yb, (float)cc, (float)r),
                  e1 = mcGridSide(xb, yb, xc, yc, (float)cc, (float)r);
            float e2 = mcGridSide(xc, yc, xa, ya, (float)cc, (float)r);
            if (fminf(e0, fminf(e1, e2)) * fmaxf(e0, fmaxf(e1, e2)) >= 0)
                mcGridMark(g, cc, r, d); // on one side of all three
        }
}

// the cells a rectangle w x h degrees about right / up reaches into: c0 r0 c1 r1
static inline void mcGridCells(float right, float up, float w, float h, int *q) {
    q[0] = (int)floorf((right - 0.5f * w - MC_GAZ0) / MC_GSTEP),
    q[1] = (int)floorf((up - 0.5f * h - MC_GEL0) / MC_GSTEP);
    q[2] = (int)floorf((right + 0.5f * w - MC_GAZ0) / MC_GSTEP),
    q[3] = (int)floorf((up + 0.5f * h - MC_GEL0) / MC_GSTEP);
}

// the view is open all over that rectangle (and the grid covers it)
static inline bool mcGridFree(const McGrid &g, float right, float up, float w, float h) {
    int q[4];
    mcGridCells(right, up, w, h, q);
    if (q[0] < 0 || q[1] < 0 || q[2] >= MC_GW || q[3] >= MC_GH) return false;
    for (int r = q[1]; r <= q[3]; ++r)
        for (int c = q[0]; c <= q[2]; ++c)
            if (g.d[r * MC_GW + c] > 0) return false;
    return true;
}

// how far the mesh around that rectangle is (its window's frame): the mean over the cells in it and up to three
// cells around it that hold mesh; 0: none
static inline float mcGridRim(const McGrid &g, float right, float up, float w, float h) {
    int q[4], n = 0;
    mcGridCells(right, up, w, h, q);
    float sum = 0;
    for (int r = q[1] - 3; r <= q[3] + 3; ++r)
        for (int c = q[0] - 3; c <= q[2] + 3; ++c)
            if (c >= 0 && c < MC_GW && r >= 0 && r < MC_GH && g.d[r * MC_GW + c] > 0) sum += g.d[r * MC_GW + c], ++n;
    return n ? sum / n : 0;
}

// The open place for a part w x h degrees nearest to wantR / wantU, less than reach degrees from there, that keeps
// clear of the rectangles taken (right0 up0 right1 up1 each). false: there is none. (No place is about the grid's
// first row or column.)
static inline bool mcGridFit(const McGrid &g, float w, float h, float wantR, float wantU, float reach,
                             const float (*taken)[4], int nTaken, float *right, float *up) {
    bool found = false;
    float best = reach * reach;
    for (int r = 1; r != MC_GH; ++r)
        for (int c = 1; c != MC_GW; ++c) {
            float az = MC_GAZ0 + c * MC_GSTEP, el = MC_GEL0 + r * MC_GSTEP;
            float cost = (az - wantR) * (az - wantR) + (el - wantU) * (el - wantU);
            if (cost >= best || !mcGridFree(g, az, el, w, h)) continue;
            bool clear = true;
            for (int i = 0; i < nTaken; ++i)
                if (mcMeet(taken[i], az - 0.5f * w, el - 0.5f * h, az + 0.5f * w, el + 0.5f * h)) clear = false;
            if (!clear) continue;
            best = cost, found = true, *right = az, *up = el;
        }
    return found;
}

// ---- is the camera in the mech? ------------------------------------------------------------------------------------
// The game draws its cockpit only while the camera sits in the mech; seen from outside (the third-person view) it
// draws none, and ours must not be drawn either. But the game draws none as well while the pilot looks far to the
// side or back (its cockpit is then outside the picture), and there ours has to stay. So a frame counts against
// "inside" only while the head looks ahead, where the game's cockpit cannot be missed: some such frames in a row
// mean outside, one frame with the game's cockpit means inside again.
// A frame with a little of the cockpit (the pilot leans far forward, the seat is set near the window: most of the
// game's cockpit lies behind the eye) says nothing either way - counted as "none", it made ours flicker.
enum { MC_COCKPIT_TRIS = 30, MC_NO_COCKPIT_TRIS = 6, MC_OUTSIDE_FRAMES = 5 };
struct McSeat {
    int misses;   // frames in a row, looking ahead, without the game's cockpit
    bool outside; // the camera is not in the mech
};
// once for every frame of a battle. tris: triangles of the game's cockpit in the frame; ahead: the head looks ahead
static inline void mcSeatStep(McSeat &s, int tris, bool ahead) {
    if (tris >= MC_COCKPIT_TRIS) {
        s.misses = 0, s.outside = false;
        return;
    }
    if (!ahead || tris >= MC_NO_COCKPIT_TRIS) return;
    if (s.misses < MC_OUTSIDE_FRAMES) ++s.misses;
    if (s.misses >= MC_OUTSIDE_FRAMES) s.outside = true;
}

// ---- a sheet over the whole HUD image ------------------------------------------------------------------------------
// The game lays a sheet over its whole picture, drawn with the HUD: a black one fades a scene in and out, a blue one
// lies behind the menu of a paused battle. Some points of the HUD image that are empty otherwise are read back; the
// one with the least alpha shows the sheet alone. Blue alone: the menu's - from its first trace, while it still
// fades in (told only once it was well there, the menu showed in the HUD's parts first and jumped). A black sheet
// hardly there yet is none: taken for a fade, it switched the HUD off for a moment. Black: every colour below a
// sixteenth of the alpha.
// The menu's sheet is blue only on some maps: it has the colour of the map's haze (grey-green in the jungle,
// orange on New Avalon - there the menu stood in the HUD's parts, taken apart, until v11.80). Any other colour
// is told as such, and is the menu's when Esc brought it (mcSheetIsMenu); without Esc it is something else - light
// amplification - and stays with the HUD as it is.
enum { MC_SHEET_NONE, MC_SHEET_FADE, MC_SHEET_MENU, MC_SHEET_COLOUR };
enum { MC_SHEET_MIN = 2, MC_SHEET_ALPHA = 16 };
// a: the point's alpha, r g b: its colour (all 0..255)
static inline int mcSheet(int a, int r, int g, int b) {
    if (a < MC_SHEET_MIN) return MC_SHEET_NONE;
    if (b * 16 > a && r * 4 <= b && g * 4 <= b) return MC_SHEET_MENU;
    bool coloured = r * 16 > a || g * 16 > a || b * 16 > a;
    if (coloured) return MC_SHEET_COLOUR;
    if (a < MC_SHEET_ALPHA) return MC_SHEET_NONE;
    return MC_SHEET_FADE;
}
// Is the sheet a paused battle's menu's? was: it was taken for one in the frame before. escOpens: at the last
// press of Esc no menu was up (it opens one; otherwise it closes one). escLately: that press is not long ago.
// A blue sheet is the menu's as ever. A coloured one: after an opening Esc, and from then on for as long as it
// lasts; after a closing Esc only while the menu's sheet fades out - what is there afterwards is not the menu's.
static inline bool mcSheetIsMenu(int kind, bool was, bool escOpens, bool escLately) {
    if (kind == MC_SHEET_MENU) return true;
    if (kind != MC_SHEET_COLOUR) return false;
    return escOpens ? was || escLately : was && escLately;
}

// ---- what a cockpit file and a cockpit's name may hold --------------------------------------------------------------
// A cockpit file may come from a stranger (somebody passes a converted cockpit on), and a cockpit's name comes from
// settings anybody can write: neither is trusted.

// The monitor a part named "SCREEN:<n>" is (digits: the text after the colon): n + 1, or 0 when it is none of the
// cockpit's - no number, a sign before it, or a number from `screens` on. (A number below zero read before the
// monitors' table.)
static inline int mcScreen(const char *digits, int screens) {
    int n = 0, len = 0;
    for (; len < 3 && digits[len] >= '0' && digits[len] <= '9'; ++len) n = n * 10 + (digits[len] - '0');
    if (len == 0) return 0;
    return n < screens ? n + 1 : 0;
}

// A cockpit's name as it goes between "mw4vr_cockpits\" and ".mwc": a file's name alone - nothing that leaves the
// folder (a separator, a drive's colon, two dots), nothing a file's name cannot hold, no control character, not
// empty and at most 63 letters.
static inline bool mcNameOk(const wchar_t *s) {
    int n = 0;
    wchar_t before = L' ';
    for (; s[n]; ++n) {
        wchar_t c = s[n];
        if (n >= 63 || c < 32 || c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' ||
            c == L'<' || c == L'>' || c == L'|')
            return false;
        if (c == L'.' && before == L'.') return false;
        before = c;
    }
    return n > 0;
}

// A part of a cockpit file by its header: the bytes it takes here (vertices of 32 bytes, indices of 4, two square
// pictures with their smaller copies - six bytes a pixel is above what those take), or 0 when it is not one this
// loader reads. The largest real cockpit has some 13,000 vertices in a part and pictures of 1024.
enum { MC_GROUP_VERTS = 500000, MC_GROUP_INDICES = 3000000, MC_GROUP_TEX = 4096 };
static inline unsigned mcGroupBytes(unsigned nv, unsigned ni, unsigned cs, unsigned es) {
    if (nv < 1 || nv > MC_GROUP_VERTS || ni < 3 || ni > MC_GROUP_INDICES || cs < 1 || cs > MC_GROUP_TEX ||
        es > MC_GROUP_TEX)
        return 0;
    return nv * 32 + ni * 4 + cs * cs * 6 + es * es * 6;
}

// ---------------------------------------------------------------------------------------------
// A battle's first pictures are held back until the world is complete (vr.cpp). mw4hook builds its own terrain
// and what it plants on it - grass, bushes, trees - while the battle already runs: for two or three seconds the
// game's old terrain was seen, bare, and then the rest came in. Until the complete world has been drawn
// MC_HOLD_SETTLE frames in a row the headset stays dark, then the picture comes up in MC_HOLD_FADE ms. A world
// that never says it is complete (a map without that terrain) is shown after MC_HOLD_MAX ms; one that is complete
// from the battle's first frame (back from the options' screen) is shown at once, without the fade.
enum { MC_HOLD_IDLE, MC_HOLD_DARK, MC_HOLD_FADING, MC_HOLD_SHOWN };
enum { MC_HOLD_SETTLE = 3 };
static const double MC_HOLD_MAX = 4000, MC_HOLD_FADE = 500;
struct McHold {
    int state;
    int n;     // the battle's frames so far (while dark)
    int ok;    // of them the last ones with the complete world, in a row
    double t0; // the state's begin (ms)
};
// one frame: is it a battle's, was the complete world drawn in it, the clock (ms). How dark the picture is to be:
// 1 wholly, 0 not at all
static inline float mcHoldStep(McHold &h, bool battle, bool complete, double now) {
    if (!battle) {
        h = {};
        return 0;
    }
    if (h.state == MC_HOLD_IDLE) h.state = MC_HOLD_DARK, h.n = 0, h.ok = 0, h.t0 = now;
    if (h.state == MC_HOLD_DARK) {
        ++h.n;
        h.ok = complete ? h.ok + 1 : 0;
        if (h.ok >= MC_HOLD_SETTLE) {
            h.state = h.n == h.ok ? MC_HOLD_SHOWN : MC_HOLD_FADING;
            h.t0 = now;
            return h.state == MC_HOLD_SHOWN ? 0.0f : 1.0f;
        }
        if (now - h.t0 >= MC_HOLD_MAX) h.state = MC_HOLD_FADING, h.t0 = now;
        return 1;
    }
    if (h.state == MC_HOLD_FADING) {
        double a = 1 - (now - h.t0) / MC_HOLD_FADE;
        if (a > 0) return (float)a;
        h.state = MC_HOLD_SHOWN;
    }
    return 0;
}
