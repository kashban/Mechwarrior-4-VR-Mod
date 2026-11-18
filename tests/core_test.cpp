// Tests of mod/src/core.h (the mod's arithmetic), a console program: exit code 0 when all pass.
//   tests\core_test.bat [folder holding core.h]
#include "core.h"
#include <cstdio>
#include <cstring>
#include <limits>

static int g_fail, g_checks;
static const char *g_test = "";
static void check(bool ok, const char *what, int line) {
    ++g_checks;
    if (!ok) ++g_fail, printf("FAIL %s, line %d: %s\n", g_test, line, what);
}
#define CHECK(c) check((c), #c, __LINE__)
static bool nearly(float a, float b, float eps = 1e-4f) { return fabsf(a - b) <= eps; }
#define NEAR(a, b) CHECK(nearly((a), (b)))

// ---------------------------------------------------------------- matrices
static void testMul34() {
    g_test = "mul34";
    const float a[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, b[12] = {2, 0, 1, 3, 1, 3, 0, -2, 0, 1, 2, 5};
    const float want[12] = {4, 9, 7, 18, 16, 25, 19, 46, 28, 41, 31, 74};
    float o[13];
    o[12] = -77; // nothing is written past the twelve
    mcMul34(a, b, o);
    for (int i = 0; i < 12; ++i) CHECK(o[i] == want[i]);
    CHECK(o[12] == -77);
    const float id[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    mcMul34(a, id, o);
    CHECK(memcmp(o, a, 48) == 0);
    mcMul34(id, b, o);
    CHECK(memcmp(o, b, 48) == 0);
}

static void testViewAxis() {
    g_test = "viewAxis";
    const float m[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, A[9] = {1, 2, 3, 4, 5, 6, 7, 8, 10};
    float v[3];
    mcViewAxis(m, A, 1, v);
    CHECK(v[0] == 36 && v[1] == 96 && v[2] == 156);
    const float x[3] = {1, 2, 3}, y[3] = {4, -5, 6};
    CHECK(mcDot3(x, y) == 12);
}

// ---------------------------------------------------------------- frustum
// the view's axes in the element's own coordinates: right = x, up = z, forward = y
static const float kA[9] = {1, 0, 0, 0, 0, 1, 0, 1, 0};

// the game's six planes for an element at pb looking along fb, the sides left as zeros
static void gamePlanes(float *pl, const float *fb, const float *pb, float farD, float nearD) {
    memset(pl, 0, 24 * sizeof(float));
    for (int i = 0; i < 3; ++i) pl[i] = fb[i], pl[4 + i] = -fb[i];
    pl[3] = mcDot3(fb, pb) + farD;
    pl[7] = -mcDot3(fb, pb) - nearD;
}
static const float kY[3] = {0, 1, 0};
static bool inside(const float *pl, const float *x) { return mcDot3(pl, x) <= pl[3]; }
static void at(const float *p, const float *f, float df, const float *s, float ds, float *x) {
    for (int i = 0; i < 3; ++i) x[i] = p[i] + f[i] * df + s[i] * ds;
}

static void testFrustum() {
    g_test = "frustum";
    // the mech: its view (the element's y) along 1 2 2, at 10 20 30
    const float t3 = 1.0f / 3, base[12] = {2 * t3, t3, 2 * t3, 10, t3, 2 * t3, -2 * t3, 20, -2 * t3, 2 * t3, t3, 30};
    const float pb[3] = {10, 20, 30}, fb[3] = {t3, 2 * t3, 2 * t3};
    // the head: turned so that forward is world x, right is world -y, up stays z; at 11 22 33
    const float out[12] = {0, 1, 0, 11, -1, 0, 0, 22, 0, 0, 1, 33};
    const float p[3] = {11, 22, 33}, f[3] = {1, 0, 0}, r[3] = {0, -1, 0}, u[3] = {0, 0, 1};
    float pl[24];
    gamePlanes(pl, fb, pb, 1000, 0.2f);
    McFrustum fr;
    memset(&fr, 0xee, sizeof fr);
    CHECK(mcFrustumPlanes(pl, base, out, kA, 1.2063f, &fr));
    NEAR(fr.d0, 1), NEAR(fr.d1, -1), CHECK(nearly(fr.farD, 1000, 1e-3f)), CHECK(nearly(fr.nearD, 0.2f, 1e-3f));
    CHECK(fr.t == 1.6f); // the game's narrower tangent is widened
    const float *n = fr.planes;
    for (int k = 0; k < 6; ++k) NEAR(mcDot3(n + 4 * k, n + 4 * k), 1); // unit normals
    for (int k = 2; k < 6; ++k) NEAR(mcDot3(n + 4 * k, p), n[4 * k + 3]); // the sides pass through the eye
    float x[3];
    const float zero[3] = {0, 0, 0};
    // far and near: along the head's view from the eye, the near plane at half the game's distance
    at(p, f, 999.5f, zero, 0, x), CHECK(inside(n, x));
    at(p, f, 1000.5f, zero, 0, x), CHECK(!inside(n, x));
    at(p, f, 0.11f, zero, 0, x), CHECK(inside(n + 4, x));
    at(p, f, 0.09f, zero, 0, x), CHECK(!inside(n + 4, x));
    // the sides in the order right, left, bottom, top: 10 ahead the frustum is 16 wide to each side
    const float *side[4] = {r, r, u, u};
    const float sg[4] = {1, -1, -1, 1};
    for (int k = 0; k < 4; ++k) {
        at(p, f, 10, side[k], sg[k] * 15.9f, x);
        for (int j = 0; j < 6; ++j) CHECK(inside(n + 4 * j, x));
        at(p, f, 10, side[k], sg[k] * 16.1f, x);
        for (int j = 0; j < 6; ++j) CHECK(inside(n + 4 * j, x) == (j != 2 + k));
    }
}

static void testFrustumSameView() {
    g_test = "frustum same view";
    // the head looks where the mech looks: far and near keep their normals
    const float t3 = 1.0f / 3, base[12] = {2 * t3, t3, 2 * t3, 10, t3, 2 * t3, -2 * t3, 20, -2 * t3, 2 * t3, t3, 30};
    const float pb[3] = {10, 20, 30}, fb[3] = {t3, 2 * t3, 2 * t3};
    float pl[24];
    gamePlanes(pl, fb, pb, 800, 0.04f);
    McFrustum fr;
    CHECK(mcFrustumPlanes(pl, base, base, kA, 2, &fr));
    for (int i = 0; i < 3; ++i) CHECK(fr.planes[i] == fb[i] && fr.planes[4 + i] == -fb[i]);
    CHECK(nearly(fr.planes[3], pl[3], 1e-3f) && nearly(fr.planes[7], pl[7], 1e-3f));
}

static void testFrustumTangent() {
    g_test = "frustum tangent";
    const float id[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}, origin[3] = {0, 0, 0};
    float pl[24];
    gamePlanes(pl, kY, origin, 500, 0.2f);
    McFrustum fr;
    const float give[] = {3.0f, -3.0f, 1.6f, 0.5f, 19.5f, 20.0f, 25.0f, std::numeric_limits<float>::quiet_NaN()};
    const float want[] = {3.0f, 3.0f, 1.6f, 1.6f, 19.5f, 1.6f, 1.6f, 1.6f};
    for (int i = 0; i < 8; ++i) {
        CHECK(mcFrustumPlanes(pl, id, id, kA, give[i], &fr));
        CHECK(fr.t == want[i]);
    }
    // a tangent of 3: at 10 ahead, 30 to the right
    mcFrustumPlanes(pl, id, id, kA, 3.0f, &fr);
    const float in[3] = {29.9f, 10, 0}, outp[3] = {30.1f, 10, 0};
    CHECK(inside(fr.planes + 8, in) && !inside(fr.planes + 8, outp));
}

static void testFrustumNear() {
    g_test = "frustum near";
    const float id[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}, origin[3] = {0, 0, 0};
    float pl[24];
    McFrustum fr;
    // a near plane closer than 5 cm stays; at 5 cm exactly too
    const float give[] = {0.2f, 0.04f, 0.05f, 1.0f}, want[] = {-0.1f, -0.04f, -0.05f, -0.5f};
    for (int i = 0; i < 4; ++i) {
        gamePlanes(pl, kY, origin, 500, give[i]);
        CHECK(mcFrustumPlanes(pl, id, id, kA, 2, &fr));
        CHECK(fr.planes[7] == want[i]);
        CHECK(fr.planes[3] == 500 && fr.nearD == give[i] && fr.farD == 500);
        CHECK(fr.planes[0] == 0 && fr.planes[1] == 1 && fr.planes[2] == 0 && fr.planes[4] == 0 && fr.planes[5] == -1 && fr.planes[6] == 0);
    }
}

static void testFrustumLayout() {
    g_test = "frustum layout";
    const float id[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}, origin[3] = {0, 0, 0};
    float pl[24], sw[24];
    McFrustum fr;
    gamePlanes(pl, kY, origin, 500, 0.2f);
    memcpy(sw, pl, sizeof sw); // far and near the other way round: not the layout known
    memcpy(sw, pl + 4, 16), memcpy(sw + 4, pl, 16);
    CHECK(!mcFrustumPlanes(sw, id, id, kA, 2, &fr));
    NEAR(fr.d0, -1), NEAR(fr.d1, 1);
    // each plane has to lie within about 11 degrees of the view
    memcpy(sw, pl, sizeof sw), sw[1] = 0.98f;
    CHECK(!mcFrustumPlanes(sw, id, id, kA, 2, &fr));
    sw[1] = 0.99f;
    CHECK(mcFrustumPlanes(sw, id, id, kA, 2, &fr));
    memcpy(sw, pl, sizeof sw), sw[5] = -0.98f;
    CHECK(!mcFrustumPlanes(sw, id, id, kA, 2, &fr));
    sw[5] = -0.99f;
    CHECK(mcFrustumPlanes(sw, id, id, kA, 2, &fr));
    memcpy(sw, pl, sizeof sw), sw[5] = 1; // both along the view
    CHECK(!mcFrustumPlanes(sw, id, id, kA, 2, &fr));
    memcpy(sw, pl, sizeof sw), sw[1] = -1; // both against it
    CHECK(!mcFrustumPlanes(sw, id, id, kA, 2, &fr));
}

// ---------------------------------------------------------------- wide HUD
static void testWide() {
    g_test = "wide";
    CHECK(mcWideFactor(1.0f) == 1.0f && mcWideFactor(1.02f) == 1.02f && mcWideFactor(1.3f) == 1.3f);
    CHECK(mcWideFactor(1.31f) == 1.0f && mcWideFactor(0.99f) == 1.0f && mcWideFactor(-2.0f) == 1.0f && mcWideFactor(5.0f) == 1.0f);
    CHECK(mcWideFactor(std::numeric_limits<float>::quiet_NaN()) == 1.0f);
    CHECK(mcWideWidth(2000, 1.0f) == 2000 && mcWideWidth(2001, 1.0005f) == 2001 && mcWideWidth(2000, 1.001f) == 2000);
    CHECK(mcWideWidth(2000, 1.02f) == 2040); // already a multiple of 8
    CHECK(mcWideWidth(1001, 1.08f) == 1088); // 1081 up to the next
    CHECK(mcWideWidth(100, 1.044f) == 104);  // 104.4 rounds down first
    CHECK(mcWideWidth(100, 1.046f) == 112);  // 104.6 rounds up first
    CHECK(mcWideWidth(1000, 1.002f) == 1008);
}

// ---------------------------------------------------------------- rectangles
static bool same(const McRects &q, const float (*want)[5], int n) {
    if (q.n != n) {
        printf("   %d rectangles, wanted %d\n", q.n, n);
        for (int i = 0; i < q.n && i < MC_RECTS; ++i) printf("   got %.4f %.4f %.4f %.4f kind %g\n", q.rc[i][0], q.rc[i][1], q.rc[i][2], q.rc[i][3], q.rc[i][4]);
        return false;
    }
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < 5; ++k)
            if (!nearly(q.rc[i][k], want[i][k], 1e-5f)) {
                printf("   rectangle %d: %.4f %.4f %.4f %.4f kind %g, wanted %.4f %.4f %.4f %.4f kind %g\n", i, q.rc[i][0], q.rc[i][1], q.rc[i][2],
                       q.rc[i][3], q.rc[i][4], want[i][0], want[i][1], want[i][2], want[i][3], want[i][4]);
                return false;
            }
    return true;
}

static void testAdd() {
    g_test = "add";
    McRects q = {};
    mcAdd(q, 0.1f, 0.2f, 0.3f, 0.4f, 3);
    mcAdd(q, 0, 0, 0.004f, 1, 1);  // too narrow
    mcAdd(q, 0, 0, 1, 0.004f, 1);  // too low
    mcAdd(q, 0, 0, 0.005f, 1, 1);  // at the limit: still out
    mcAdd(q, 0, 0, 1, 0.005f, 1);
    mcAdd(q, 0, 0, 0.006f, 0.006f, 2);
    mcAdd(q, 0.5f, 0.5f, 0.4f, 0.9f, 1); // inside out
    const float want[][5] = {{0.1f, 0.2f, 0.3f, 0.4f, 3}, {0, 0, 0.006f, 0.006f, 2}};
    CHECK(same(q, want, 2));
    struct {
        McRects q;
        float guard[5];
    } s = {};
    for (int i = 0; i < MC_RECTS + 3; ++i) mcAdd(s.q, 0, 0, 1, 1, (float)i);
    CHECK(s.q.n == MC_RECTS && s.q.rc[MC_RECTS - 1][4] == MC_RECTS - 1);
    for (int k = 0; k < 5; ++k) CHECK(s.guard[k] == 0); // nothing written past the list
}

static void testMeet() {
    g_test = "meet";
    const float c[4] = {0.2f, 0.3f, 0.6f, 0.7f};
    CHECK(mcMeet(c, 0.5f, 0.6f, 0.9f, 0.9f) && mcMeet(c, 0, 0, 0.3f, 0.4f) && mcMeet(c, 0.3f, 0.4f, 0.4f, 0.5f) && mcMeet(c, 0, 0, 1, 1));
    CHECK(!mcMeet(c, 0.6f, 0.3f, 0.9f, 0.7f)); // touching on each side is not meeting
    CHECK(!mcMeet(c, 0, 0.3f, 0.2f, 0.7f));
    CHECK(!mcMeet(c, 0.2f, 0.7f, 0.6f, 0.9f));
    CHECK(!mcMeet(c, 0.2f, 0, 0.6f, 0.3f));
    CHECK(mcMeet(c, 0.59f, 0.3f, 0.9f, 0.7f) && mcMeet(c, 0, 0.3f, 0.21f, 0.7f) && mcMeet(c, 0.2f, 0.69f, 0.6f, 0.9f) && mcMeet(c, 0.2f, 0, 0.6f, 0.31f));
}

static void testAddCut() {
    g_test = "addCut";
    McRects q = {};
    const float c[4] = {0.4f, 0.3f, 0.6f, 0.5f};
    mcAddCut(q, nullptr, 0, 0, 1, 1, 1);
    mcAddCut(q, c, 0.7f, 0, 1, 1, 0); // apart: whole
    const float whole[][5] = {{0, 0, 1, 1, 1}, {0.7f, 0, 1, 1, 0}};
    CHECK(same(q, whole, 2));
    q.n = 0;
    mcAddCut(q, c, 0.1f, 0.1f, 0.9f, 0.8f, 2); // the cut inside: above, below, left, right
    const float ring[][5] = {{0.1f, 0.1f, 0.9f, 0.3f, 2}, {0.1f, 0.5f, 0.9f, 0.8f, 2}, {0.1f, 0.3f, 0.4f, 0.5f, 2}, {0.6f, 0.3f, 0.9f, 0.5f, 2}};
    CHECK(same(q, ring, 4));
    q.n = 0;
    mcAddCut(q, c, 0.5f, 0.4f, 0.9f, 0.8f, 1); // the cut over the top left corner: below and right only
    const float corner[][5] = {{0.5f, 0.5f, 0.9f, 0.8f, 1}, {0.6f, 0.4f, 0.9f, 0.5f, 1}};
    CHECK(same(q, corner, 2));
    q.n = 0;
    mcAddCut(q, c, 0.1f, 0.1f, 0.5f, 0.4f, 1); // over the bottom right corner: above and left only
    const float corner2[][5] = {{0.1f, 0.1f, 0.5f, 0.3f, 1}, {0.1f, 0.3f, 0.4f, 0.4f, 1}};
    CHECK(same(q, corner2, 2));
    q.n = 0;
    mcAddCut(q, c, 0.45f, 0.35f, 0.55f, 0.45f, 1); // all of it covered
    CHECK(q.n == 0);
}

static void testMoveOut() {
    g_test = "moveOut";
    McRects q = {};
    mcAdd(q, 0.2f, 0.1f, 0.8f, 0.9f, MC_FAR);
    mcAdd(q, 0, 0, 1, 0.1f, MC_NEAR);          // not far: stays, though it meets
    mcAdd(q, 0.85f, 0.1f, 1, 0.9f, MC_FAR);    // far, apart: stays
    mcAdd(q, 0.3f, 0.2f, 0.9f, 0.45f, MC_FAR); // far, met at its bottom left
    const float c[4] = {0.4f, 0.05f, 0.6f, 0.5f};
    mcMoveOut(q, c, MC_FAR_UP);
    const float want[][5] = {{0.2f, 0.1f, 0.8f, 0.1f, MC_FAR}, // nothing left above (a sliver, kept in place)
                             {0, 0, 1, 0.1f, MC_NEAR},
                             {0.85f, 0.1f, 1, 0.9f, MC_FAR},
                             {0.3f, 0.2f, 0.9f, 0.2f, MC_FAR},
                             {0.2f, 0.5f, 0.8f, 0.9f, MC_FAR},
                             {0.2f, 0.1f, 0.4f, 0.5f, MC_FAR},
                             {0.6f, 0.1f, 0.8f, 0.5f, MC_FAR},
                             {0.3f, 0.2f, 0.4f, 0.45f, MC_FAR},
                             {0.6f, 0.2f, 0.9f, 0.45f, MC_FAR},
                             {0.4f, 0.05f, 0.6f, 0.5f, MC_FAR_UP}};
    CHECK(same(q, want, 10));
    // what lies behind the list's end is not part of it
    q.n = 0;
    mcAdd(q, 0, 0, 1, 0.1f, MC_NEAR);
    const float stale[5] = {0.2f, 0.1f, 0.8f, 0.9f, MC_FAR};
    memcpy(q.rc[1], stale, sizeof stale);
    mcMoveOut(q, c, MC_FAR_UP);
    const float two[][5] = {{0, 0, 1, 0.1f, MC_NEAR}, {0.4f, 0.05f, 0.6f, 0.5f, MC_FAR_UP}};
    CHECK(same(q, two, 2));
    q.n = 0;
    mcAdd(q, 0.2f, 0.1f, 0.8f, 0.9f, MC_FAR);
    const float in[4] = {0.4f, 0.3f, 0.6f, 0.5f};
    mcMoveOut(q, in, MC_FAR_UP);
    const float ring[][5] = {{0.2f, 0.1f, 0.8f, 0.3f, MC_FAR}, {0.2f, 0.5f, 0.8f, 0.9f, MC_FAR}, {0.2f, 0.3f, 0.4f, 0.5f, MC_FAR},
                             {0.6f, 0.3f, 0.8f, 0.5f, MC_FAR}, {0.4f, 0.3f, 0.6f, 0.5f, MC_FAR_UP}};
    CHECK(same(q, ring, 5));
}

static void testSpans() {
    g_test = "spans";
    float iv[MC_SPANS + 1][2] = {};
    int n = 0;
    const float T = 0.12f;
    const float a[4] = {0.7f, 0, 1.0f, 0.1f}, b[4] = {0.1f, 0.05f, 0.3f, 0.5f}, c[4] = {0.4f, 0, 0.6f, 0.2f};
    const float below[4] = {0, 0.12f, 0.2f, 0.5f}, justIn[4] = {0.4f, 0.119f, 0.5f, 0.5f}, flat[4] = {0.2f, 0, 0.2f, 0.1f}, low[4] = {0.2f, 0.1f, 0.3f, 0.1f};
    const float unset[4] = {0, 0, 0, 0}, sameStart[4] = {0.4f, 0, 0.45f, 0.1f};
    mcTopSpan(iv, n, a, T), mcTopSpan(iv, n, below, T), mcTopSpan(iv, n, b, T), mcTopSpan(iv, n, flat, T), mcTopSpan(iv, n, low, T);
    mcTopSpan(iv, n, unset, T), mcTopSpan(iv, n, c, T);
    CHECK(n == 3);
    CHECK(iv[0][0] == 0.1f && iv[0][1] == 0.3f && iv[1][0] == 0.4f && iv[1][1] == 0.6f && iv[2][0] == 0.7f && iv[2][1] == 1.0f);
    mcTopSpan(iv, n, sameStart, T); // the same start: behind the one there
    CHECK(n == 4 && iv[1][1] == 0.6f && iv[2][0] == 0.4f && iv[2][1] == 0.45f && iv[3][0] == 0.7f && iv[3][1] == 1.0f);
    mcTopSpan(iv, n, justIn, T);
    CHECK(n == 5 && iv[3][0] == 0.4f && iv[3][1] == 0.5f);
    const float first[4] = {0.05f, 0, 0.08f, 0.1f};
    mcTopSpan(iv, n, first, T); // before all
    CHECK(n == 6 && iv[0][0] == 0.05f && iv[0][1] == 0.08f && iv[1][0] == 0.1f && iv[5][1] == 1.0f);
    for (int i = 0; i < 5; ++i) mcTopSpan(iv, n, a, T);
    CHECK(n == MC_SPANS && iv[MC_SPANS][0] == 0 && iv[MC_SPANS][1] == 0); // full: no more, nothing written past it
}

static void testLeftAlt() {
    g_test = "leftAlt";
    // (a fifth number behind each: not part of the rectangle)
    const float a[5] = {0, 0, 0.24f, 0.43f, 9}, right[5] = {0.25f, 0, 0.5f, 0.6f, 9}, edge[5] = {0.249f, 0, 0.5f, 0.6f, 9};
    const float unset[5] = {0, 0, 0, 0, 9}, flat[5] = {0, 0.5f, 0.2f, 0.5f, 9}, thin[5] = {0.1f, 0, 0.1f, 0.9f, 9};
    CHECK(mcLeftAlt(a, 0.25f, 0) == 0.43f);
    CHECK(mcLeftAlt(a, 0.25f, 0.5f) == 0.5f);   // another reached further already
    CHECK(mcLeftAlt(a, 0.25f, 0.43f) == 0.43f);
    CHECK(mcLeftAlt(right, 0.25f, 0.1f) == 0.1f); // begins at the column's edge: not in it
    CHECK(mcLeftAlt(edge, 0.25f, 0.1f) == 0.6f);
    CHECK(mcLeftAlt(unset, 0.25f, 0.1f) == 0.1f && mcLeftAlt(flat, 0.25f, 0.1f) == 0.1f && mcLeftAlt(thin, 0.25f, 0.1f) == 0.1f);
}

// ---------------------------------------------------------------- the HUD's parts
static McHud hud() {
    McHud h = {};
    h.T = 0.12f, h.B = 0.78f, h.L = 0.25f, h.R = 0.72f, h.UR = 1.02f;
    h.margin = 0.02f;
    h.twist[0] = 0.42f, h.twist[1] = 0.585f, h.twist[2] = 0.58f, h.twist[3] = 0.665f;
    h.twistDv = -0.045f;
    // what a frame before left behind the lists' ends: a window about the middle, a span of the top strip
    const float stale[4] = {0.4f, 0.3f, 0.6f, 0.5f};
    for (int i = 0; i < MC_CAMS; ++i) memcpy(h.cam[i], stale, sizeof stale);
    for (int i = 0; i < MC_SPANS; ++i) h.span[i][0] = 0.4f, h.span[i][1] = 0.6f;
    for (int i = 0; i < MC_GLASSES; ++i) memcpy(h.glass[i], stale, sizeof stale);
    return h;
}
static void setCam(McHud &h, float u0, float v0, float u1, float v1) {
    float *c = h.cam[h.nCam++];
    c[0] = u0, c[1] = v0, c[2] = u1, c[3] = v1;
}

static void testHudPlain() {
    g_test = "hud plain";
    McHud h = hud();
    McRects q;
    q.n = 7; // whatever was there is gone
    mcHudRects(h, q);
    const float want[][5] = {{0.25f, 0.12f, 0.72f, 0.78f, MC_FAR}, {0, 0, 1.02f, 0.12f, MC_NEAR}, {0, 0.78f, 1.02f, 1, MC_NEAR},
                             {0, 0.12f, 0.25f, 0.78f, MC_NEAR}, {0.72f, 0.12f, 1.02f, 0.78f, MC_NEAR}};
    CHECK(same(q, want, 5));
    const float stale[4] = {0.80f, 0.60f, 0.98f, 0.76f}; // a window at the side behind the list's end
    memcpy(h.cam[0], stale, sizeof stale);
    mcHudRects(h, q);
    CHECK(same(q, want, 5));
}

static bool isRect(const float *r, float u0, float v0, float u1, float v1, float kind) {
    return nearly(r[0], u0) && nearly(r[1], v0) && nearly(r[2], u1) && nearly(r[3], v1) && r[4] == kind;
}

static void testHudCross() {
    g_test = "HUD cross-hair";
    CHECK(MC_FAR_SMALL == 6 && MC_SHOWN == 15);
    McHud h = {};
    McRects q;
    h.T = 0.12f, h.B = 0.77f, h.L = 0.25f, h.R = 0.72f, h.UR = 1;
    const float cr[4] = {0.39f, 0.415f, 0.61f, 0.585f};
    memcpy(h.cross, cr, sizeof cr);
    mcHudRects(h, q); // the middle around it (above, below, left, right), the near frame, then the cross-hair
    CHECK(q.n == 9);
    CHECK(isRect(q.rc[0], 0.25f, 0.12f, 0.72f, 0.415f, MC_FAR));
    for (int i = 1; i <= 4; ++i) CHECK(q.rc[i][4] == MC_NEAR);
    CHECK(isRect(q.rc[5], 0.25f, 0.585f, 0.72f, 0.77f, MC_FAR));
    CHECK(isRect(q.rc[6], 0.25f, 0.415f, 0.39f, 0.585f, MC_FAR));
    CHECK(isRect(q.rc[7], 0.61f, 0.415f, 0.72f, 0.585f, MC_FAR));
    CHECK(isRect(q.rc[8], 0.39f, 0.415f, 0.61f, 0.585f, MC_FAR_SMALL));
    McRects w;
    mcHudRectsWith(h, w, false); // not wanted
    CHECK(w.n == 5);
    for (int i = 0; i < w.n; ++i) CHECK(w.rc[i][4] != MC_FAR_SMALL);
    h.cross[2] = h.cross[0]; // no width, no height: none
    mcHudRects(h, q);
    CHECK(q.n == 5);
    h.cross[0] = 0.6f, h.cross[2] = 0.5f; // its right edge left of its left one
    mcHudRects(h, q);
    CHECK(q.n == 5);
    memcpy(h.cross, cr, sizeof cr);
    h.cross[3] = h.cross[1];
    mcHudRects(h, q);
    CHECK(q.n == 5);
    memcpy(h.cross, cr, sizeof cr);
    h.nCam = 1; // a zoom window open: the cross-hair is in it, as it is
    h.cam[0][0] = 0.4f, h.cam[0][1] = 0.4f, h.cam[0][2] = 0.6f, h.cam[0][3] = 0.6f;
    mcHudRects(h, q);
    for (int i = 0; i < q.n; ++i) CHECK(q.rc[i][4] != MC_FAR_SMALL);
    h.nCam = 0;
    // with monitors: cut out of the far parts, after the torso mark (that one keeps its place)
    h.screens = true;
    h.twist[0] = 0.42f, h.twist[1] = 0.585f, h.twist[2] = 0.58f, h.twist[3] = 0.665f, h.twistDv = -0.045f;
    mcHudRects(h, q);
    int small = 0, up = 0;
    float area = 0;
    for (int i = 0; i < q.n; ++i) {
        const float *r = q.rc[i];
        small += r[4] == MC_FAR_SMALL, up += r[4] == MC_FAR_UP;
        area += (r[2] - r[0]) * (r[3] - r[1]);
        if (r[4] == MC_FAR) CHECK(!mcMeet(cr, r[0], r[1], r[2], r[3]) && !mcMeet(h.twist, r[0], r[1], r[2], r[3]));
    }
    CHECK(small == 1 && up == 1 && q.rc[q.n - 1][4] == MC_FAR_SMALL);
    CHECK(nearly(area, 0.77f, 1e-3f)); // all of the picture above the bottom strip, each piece once
    // more parts than the runtime shows: the cross-hair stays in the middle
    h.nGlass = 8;
    for (int g = 0; g < 8; ++g) h.glass[g][0] = 0.1f * g, h.glass[g][1] = 0.8f, h.glass[g][2] = 0.1f * g + 0.08f, h.glass[g][3] = 0.9f;
    mcHudRects(h, q);
    CHECK(q.n == 14);
    for (int i = 0; i < q.n; ++i) CHECK(q.rc[i][4] != MC_FAR_SMALL);
    mcHudRectsWith(h, w, true);
    CHECK(w.n == 17);
    mcHudRects(h, q, 17); // a runtime that shows that many: the cross-hair is its own part
    small = 0;
    for (int i = 0; i < q.n; ++i) small += q.rc[i][4] == MC_FAR_SMALL;
    CHECK(q.n == 17 && small == 1);
    mcHudRects(h, q, 16);
    CHECK(q.n == 14);
    h.nGlass = 7; // one too many
    mcHudRects(h, q);
    CHECK(q.n == 13);
    h.nGlass = 6; // just as many as it shows: small
    mcHudRects(h, q);
    CHECK(q.n == 15 && q.rc[8][4] == MC_FAR_SMALL);
}

static void testHudZoom() {
    g_test = "hud zoom";
    McHud h = hud();
    setCam(h, 0.80f, 0.60f, 0.98f, 0.76f); // the target's view, at the right
    setCam(h, 0.35f, 0.05f, 0.65f, 0.50f); // the zoom window
    McRects q;
    mcHudRects(h, q);
    const float want[][5] = {{0.25f, 0.52f, 0.72f, 0.78f, MC_FAR}, // the far middle: below, left, right of the window and its frame
                             {0.25f, 0.12f, 0.33f, 0.52f, MC_FAR},
                             {0.67f, 0.12f, 0.72f, 0.52f, MC_FAR},
                             {0, 0, 0.33f, 0.12f, MC_NEAR}, // the top strip left and right of it
                             {0.67f, 0, 1.02f, 0.12f, MC_NEAR},
                             {0, 0.78f, 1.02f, 1, MC_NEAR},
                             {0, 0.12f, 0.25f, 0.78f, MC_NEAR},
                             {0.72f, 0.12f, 1.02f, 0.78f, MC_NEAR},
                             {0.35f, 0.05f, 0.65f, 0.50f, MC_FAR_OPAQUE},
                             {0.80f, 0.60f, 0.98f, 0.76f, MC_NEAR_OPAQUE}};
    CHECK(same(q, want, 10));
    float zr[4];
    CHECK(mcZoomWindow(h, zr) && zr[0] == 0.35f && zr[1] == 0.05f && zr[2] == 0.65f && zr[3] == 0.50f);
    setCam(h, 0.45f, 0.30f, 0.55f, 0.40f); // of two about the middle the later one counts
    CHECK(mcZoomWindow(h, zr) && zr[0] == 0.45f && zr[3] == 0.40f);
}

static void testHudZoomEdges() {
    g_test = "hud zoom edges";
    // a window is the zoom window when its centre lies strictly between 0.3 and 0.7 of the width
    const float lo = 0.3f + 0.3f, hi = 0.7f + 0.7f; // centres exactly 0.3 and 0.7
    const float give[][2] = {{0, lo}, {0, hi}, {0.01f, lo}, {-0.01f, hi}, {0, 0.5f}, {0.8f, 1.0f}};
    const bool want[] = {false, false, true, true, false, false};
    for (int i = 0; i < 6; ++i) {
        McHud h = hud();
        h.UR = 1.5f;
        setCam(h, give[i][0], 0.3f, give[i][1], 0.5f);
        float zr[4];
        CHECK(mcZoomWindow(h, zr) == want[i]);
        McRects q;
        mcHudRects(h, q);
        const float *last = q.rc[q.n - 1];
        CHECK(last[4] == (want[i] ? (float)MC_FAR_OPAQUE : (float)MC_NEAR_OPAQUE)); // shown either as the one or as the other
        CHECK(last[0] == give[i][0] && last[1] == 0.3f && last[2] == give[i][1] && last[3] == 0.5f);
        int shown = 0;
        for (int k = 0; k < q.n; ++k) shown += q.rc[k][4] >= MC_NEAR_OPAQUE;
        CHECK(shown == 1);
    }
}

static void testHudScreens() {
    g_test = "hud screens";
    McHud h = hud();
    h.screens = true;
    h.span[0][0] = 0, h.span[0][1] = 0.243f;       // the left monitor's part of the top strip
    h.span[1][0] = 0.2f, h.span[1][1] = 0.3f;      // one that overlaps it
    h.span[2][0] = 0.716f, h.span[2][1] = 1.0f;    // the right monitor's
    h.nSpan = 3;
    h.leftAlt = 0.435f;
    setCam(h, 0.80f, 0.60f, 0.98f, 0.76f); // the target's view: on a monitor, no quad
    McRects q;
    mcHudRects(h, q);
    const float want[][5] = {{0.25f, 0.12f, 1.02f, 0.435f, MC_FAR}, // the middle and the right column beside the lance menu
                             {0, 0.435f, 1.02f, 0.585f, MC_FAR},    // the whole width below it, down to the torso mark
                             {0.3f, 0, 0.716f, 0.12f, MC_FAR},      // the top strip between the monitors' parts
                             {1.0f, 0, 1.02f, 0.12f, MC_FAR},       // and right of them
                             {0, 0.665f, 1.02f, 0.78f, MC_FAR},     // below, left and right of the mark
                             {0, 0.585f, 0.42f, 0.665f, MC_FAR},
                             {0.58f, 0.585f, 1.02f, 0.665f, MC_FAR},
                             {0.42f, 0.585f, 0.58f, 0.665f, MC_FAR_UP}};
    CHECK(same(q, want, 8));

    h.twistDv = 0; // the mark not moved: nothing cut out
    mcHudRects(h, q);
    CHECK(q.n == 4 && q.rc[1][3] == 0.78f && q.rc[3][4] == MC_FAR);
    h.twistDv = 0.01f;
    mcHudRects(h, q);
    CHECK(q.n == 8);
    h.twist[2] = h.twist[0]; // no width
    mcHudRects(h, q);
    CHECK(q.n == 4);
    h.twist[2] = 0.58f, h.twist[3] = h.twist[1]; // no height
    mcHudRects(h, q);
    CHECK(q.n == 4);

    // the columns' tops (the mark not moved, nothing of the top strip shown elsewhere: that strip comes last)
    h = hud(), h.screens = true, h.twistDv = 0;
    mcHudRects(h, q); // both from the top strip down: one rectangle
    const float all[][5] = {{0, 0.12f, 1.02f, 0.78f, MC_FAR}, {0, 0, 1.02f, 0.12f, MC_FAR}};
    CHECK(same(q, all, 2));
    h.leftAlt = 0.05f, h.rightAlt = 0.1f; // above the strip's lower edge: as before
    mcHudRects(h, q);
    CHECK(same(q, all, 2));
    h.leftAlt = 0, h.rightAlt = 0.435f; // the right column begins lower
    mcHudRects(h, q);
    const float right[][5] = {{0, 0.12f, 0.72f, 0.435f, MC_FAR}, {0, 0.435f, 1.02f, 0.78f, MC_FAR}, {0, 0, 1.02f, 0.12f, MC_FAR}};
    CHECK(same(q, right, 3));
    h.leftAlt = 0.3f, h.rightAlt = 0.5f; // both lower, the left one first
    mcHudRects(h, q);
    const float both[][5] = {{0.25f, 0.12f, 0.72f, 0.3f, MC_FAR}, {0, 0.3f, 0.72f, 0.5f, MC_FAR}, {0, 0.5f, 1.02f, 0.78f, MC_FAR}, {0, 0, 1.02f, 0.12f, MC_FAR}};
    CHECK(same(q, both, 4));
    h.leftAlt = 0.5f, h.rightAlt = 0.3f; // the right one first
    mcHudRects(h, q);
    const float other[][5] = {{0.25f, 0.12f, 0.72f, 0.3f, MC_FAR}, {0.25f, 0.3f, 1.02f, 0.5f, MC_FAR}, {0, 0.5f, 1.02f, 0.78f, MC_FAR}, {0, 0, 1.02f, 0.12f, MC_FAR}};
    CHECK(same(q, other, 4));
    h.leftAlt = h.rightAlt = 0.4f; // at the same height
    mcHudRects(h, q);
    const float level[][5] = {{0.25f, 0.12f, 0.72f, 0.4f, MC_FAR}, {0, 0.4f, 1.02f, 0.78f, MC_FAR}, {0, 0, 1.02f, 0.12f, MC_FAR}};
    CHECK(same(q, level, 3));
    h.leftAlt = 0.9f, h.rightAlt = 0; // a column all taken: none of it
    mcHudRects(h, q);
    const float noLeft[][5] = {{0.25f, 0.12f, 1.02f, 0.78f, MC_FAR}, {0, 0, 1.02f, 0.12f, MC_FAR}};
    CHECK(same(q, noLeft, 2));
    h.leftAlt = 0, h.rightAlt = 0.9f;
    mcHudRects(h, q);
    const float noRight[][5] = {{0, 0.12f, 0.72f, 0.78f, MC_FAR}, {0, 0, 1.02f, 0.12f, MC_FAR}};
    CHECK(same(q, noRight, 2));
    h.leftAlt = 0.9f, h.rightAlt = 0.95f; // both: the middle alone
    mcHudRects(h, q);
    const float mid[][5] = {{0.25f, 0.12f, 0.72f, 0.78f, MC_FAR}, {0, 0, 1.02f, 0.12f, MC_FAR}};
    CHECK(same(q, mid, 2));

    h = hud(), h.screens = true, h.twistDv = 0;
    h.span[0][0] = 0, h.span[0][1] = 1.02f, h.nSpan = 1; // all of the top strip on monitors
    mcHudRects(h, q);
    CHECK(same(q, all, 1));
}

static void testRightAlt() {
    g_test = "rightAlt";
    // (a fifth number behind each: not part of the rectangle)
    const float a[5] = {0.7f, 0, 1.0f, 0.43f, 9}, left[5] = {0.5f, 0, 0.72f, 0.6f, 9}, edge[5] = {0.5f, 0, 0.721f, 0.6f, 9};
    const float unset[5] = {0, 0, 0, 0, 9}, flat[5] = {0.8f, 0.5f, 0.9f, 0.5f, 9}, thin[5] = {0.9f, 0, 0.9f, 0.9f, 9};
    CHECK(mcRightAlt(a, 0.72f, 0) == 0.43f);
    CHECK(mcRightAlt(a, 0.72f, 0.5f) == 0.5f);   // another reached further already
    CHECK(mcRightAlt(a, 0.72f, 0.43f) == 0.43f);
    CHECK(mcRightAlt(left, 0.72f, 0.1f) == 0.1f); // ends at the column's edge: not in it
    CHECK(mcRightAlt(edge, 0.72f, 0.1f) == 0.6f);
    CHECK(mcRightAlt(unset, 0.72f, 0.1f) == 0.1f && mcRightAlt(flat, 0.72f, 0.1f) == 0.1f && mcRightAlt(thin, 0.72f, 0.1f) == 0.1f);
    CHECK(mcRightAlt(unset, -1, 0.1f) == 0.1f);
}

static void testHudScreensZoom() {
    g_test = "hud screens zoom";
    McHud h = hud();
    h.screens = true;
    h.span[0][0] = 0, h.span[0][1] = 0.243f, h.nSpan = 1;
    setCam(h, 0.35f, 0.05f, 0.65f, 0.50f);
    McRects q;
    mcHudRects(h, q);
    const float want[][5] = {{0, 0.52f, 1.02f, 0.78f, MC_FAR}, // the middle and the columns below, left and right of the window and its frame
                             {0, 0.12f, 0.33f, 0.52f, MC_FAR},
                             {0.67f, 0.12f, 1.02f, 0.52f, MC_FAR},
                             {0.243f, 0, 1.02f, 0.03f, MC_FAR}, // the top strip around it: above, left, right
                             {0.243f, 0.03f, 0.33f, 0.12f, MC_FAR},
                             {0.67f, 0.03f, 1.02f, 0.12f, MC_FAR},
                             {0.35f, 0.05f, 0.65f, 0.50f, MC_FAR_OPAQUE}}; // the torso mark stays where it is while zooming
    CHECK(same(q, want, 7));
}

// ---------------------------------------------------------------- the parts on the glass
static void testHolds() {
    g_test = "holds";
    const float r[4] = {0.2f, 0.3f, 0.6f, 0.9f};
    CHECK(mcHolds(r, 0.21f, 0.31f) && mcHolds(r, 0.59f, 0.89f));
    CHECK(!mcHolds(r, 0.2f, 0.5f) && !mcHolds(r, 0.6f, 0.5f) && !mcHolds(r, 0.4f, 0.3f) && !mcHolds(r, 0.4f, 0.9f)); // on an edge
    CHECK(!mcHolds(r, 0.1f, 0.5f) && !mcHolds(r, 0.7f, 0.5f) && !mcHolds(r, 0.4f, 0.2f) && !mcHolds(r, 0.4f, 0.95f));
}

static void testTurnNorm() {
    g_test = "turn norm";
    CHECK(mcTurnNorm(0) == 0 && mcTurnNorm(5) == 5 && mcTurnNorm(-5) == -5);
    CHECK(mcTurnNorm(180) == 180 && mcTurnNorm(-180) == 180 && mcTurnNorm(185) == -175 && mcTurnNorm(-185) == 175);
    CHECK(mcTurnNorm(360) == 0 && mcTurnNorm(725) == 5 && mcTurnNorm(-725) == -5 && mcTurnNorm(540) == 180);
    CHECK(mcTurnNorm(179) == 179 && mcTurnNorm(-179) == -179 && mcTurnNorm(180.5f) == -179.5f && mcTurnNorm(-180.5f) == 179.5f);
    CHECK(mcTurnNorm(999990) == mcTurnNorm(999990 - 360 * 2777) && mcTurnNorm(-999990) == -mcTurnNorm(999990));
    CHECK(mcTurnNorm(1e6f) == 0 && mcTurnNorm(-1e6f) == 0 && mcTurnNorm(2e6f) == 0 && mcTurnNorm(-2e6f) == 0);
    float nan = 0;
    nan = (nan - nan) / nan;
    CHECK(mcTurnNorm(nan) == 0);
}

static void testEyeInMesh() {
    g_test = "eye in mesh";
    float eye[3];
    const float id[12] = {1, 0, 0, -4, 0, 1, 0, 5, 0, 0, 1, -6}; // only shifted: the eye is where the shift takes to 0
    mcEyeInMesh(id, 1, eye);
    CHECK(eye[0] == 4 && eye[1] == -5 && eye[2] == 6);
    const float tw[12] = {2, 0, 0, -4, 0, 2, 0, 6, 0, 0, 2, 8}; // and twice the size
    mcEyeInMesh(tw, 2, eye);
    CHECK(eye[0] == 2 && eye[1] == -3 && eye[2] == -4);
    // turned about the upright by 30 degrees, then about the right by 20, 1.7 times the size, the eye at 0.3 -0.2 0.5
    const float s = 1.7f, cy = cosf(0.5236f), sy = sinf(0.5236f), cx = cosf(0.349f), sx = sinf(0.349f);
    const float r[3][3] = {{cy, 0, sy}, {sx * sy, cx, -sx * cy}, {-cx * sy, sx, cx * cy}};
    const float want[3] = {0.3f, -0.2f, 0.5f};
    float m[12];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) m[i * 4 + j] = s * r[i][j];
        m[i * 4 + 3] = -s * (r[i][0] * want[0] + r[i][1] * want[1] + r[i][2] * want[2]);
    }
    mcEyeInMesh(m, s, eye);
    CHECK(nearly(eye[0], want[0], 1e-5f) && nearly(eye[1], want[1], 1e-5f) && nearly(eye[2], want[2], 1e-5f));
}

static void testTurnFit() {
    g_test = "turn fit";
    float m[4];
    const float e = 1e-5f;
    mcTurnFit(2, 4, 0, m); // a part wider than the monitor: its width fills, bars above and below
    CHECK(nearly(m[0], 1, e) && nearly(m[1], 0, e) && nearly(m[2], 0, e) && nearly(m[3], 2, e));
    mcTurnFit(2, 0.5f, 0, m); // narrower: its height fills
    CHECK(nearly(m[0], 4, e) && nearly(m[1], 0, e) && nearly(m[2], 0, e) && nearly(m[3], 1, e));
    mcTurnFit(2, 0.5f, 90, m); // a tall part on a wide monitor, a quarter turn clockwise: it fills the monitor;
    CHECK(nearly(m[0], 0, e) && nearly(m[1], 1, e) && nearly(m[2], -1, e) && nearly(m[3], 0, e)); // the monitor's top is the part's right
    mcTurnFit(2, 0.5f, -90, m);
    CHECK(nearly(m[0], 0, e) && nearly(m[1], -1, e) && nearly(m[2], 1, e) && nearly(m[3], 0, e));
    mcTurnFit(2, 4, 180, m); // upside down
    CHECK(nearly(m[0], -1, e) && nearly(m[1], 0, e) && nearly(m[2], 0, e) && nearly(m[3], -2, e));
    mcTurnFit(3, 0.5f, 90, m); // a quarter turn, the monitor wider than the part is tall: the monitor's height limits
    CHECK(nearly(m[0], 0, e) && nearly(m[1], 1, e) && nearly(m[2], -1.5f, e) && nearly(m[3], 0, e));
    mcTurnFit(1, 0.5f, 90, m); // and narrower: its width does
    CHECK(nearly(m[0], 0, e) && nearly(m[1], 2, e) && nearly(m[2], -1, e) && nearly(m[3], 0, e));
    // turned 30 degrees: the part's corners lie within the monitor, one pair of them on its edge
    for (int t = 0; t < 2; ++t) {
        const float mon = t ? 0.8f : 2.0f, part = 1.5f, deg = t ? -30.0f : 30.0f;
        mcTurnFit(mon, part, deg, m);
        float det = m[0] * m[3] - m[1] * m[2], far = 0;
        for (int k = 0; k < 4; ++k) {
            float qx = k & 1 ? 0.5f : -0.5f, qy = k & 2 ? 0.5f : -0.5f; // a corner of the part -> the monitor's uv
            float u = (m[3] * qx - m[1] * qy) / det, v = (-m[2] * qx + m[0] * qy) / det;
            CHECK(fabsf(u) <= 0.5f + e && fabsf(v) <= 0.5f + e);
            far = fmaxf(far, fmaxf(fabsf(u), fabsf(v)));
        }
        CHECK(nearly(far, 0.5f, e));
        // undistorted: the monitor's right and down (in metres: mon, 1) are as long in the part (in metres: part, 1) and square
        float rx = m[0] / mon * part, ry = m[2] / mon, dx = m[1] * part, dy = m[3];
        CHECK(nearly(rx * rx + ry * ry, dx * dx + dy * dy, 1e-4f) && nearly(rx * dx + ry * dy, 0, 1e-4f));
        CHECK(t ? (m[1] < 0 && m[2] > 0) : (m[1] > 0 && m[2] < 0)); // clockwise for positive degrees
    }
}

static void testHudGlass() {
    g_test = "hud glass";
    McHud h = hud();
    h.screens = true;
    const float target[4] = {0, 0.772f, 0.18f, 1}, wide[4] = {0.125f, 0.25f, 0.75f, 0.625f}; // (wide holds the stale window's middle)
    memcpy(h.glass[0], target, sizeof target), memcpy(h.glass[1], wide, sizeof wide);
    h.nGlass = 2;
    setCam(h, 0.01f, 0.80f, 0.07f, 0.98f); // the target's view, in the target part
    setCam(h, 0.80f, 0.60f, 0.98f, 0.76f); // one in no part
    setCam(h, 0, 0.3f, 0.25f, 0.5f);         // one whose middle lies on the second part's edge
    McRects q;
    mcHudRects(h, q);
    CHECK(q.n == 9); // the six without glass, then:
    const float want[][5] = {{0, 0.772f, 0.18f, 1, MC_GLASS}, {0.01f, 0.80f, 0.07f, 0.98f, MC_GLASS_OPAQUE}, {0.125f, 0.25f, 0.75f, 0.625f, MC_GLASS + 1}};
    for (int i = 0; i < 3 && q.n == 9; ++i)
        for (int k = 0; k < 5; ++k) CHECK(nearly(q.rc[6 + i][k], want[i][k], 1e-6f));
    h.nCam = 0;
    setCam(h, 0.20f, 0.27f, 0.28f, 0.59f); // a window in the second part, off its middle both ways
    h.cam[0][0] = 0.14f; // (left of the zoom window's range)
    h.nGlass = 2;
    mcHudRects(h, q);
    CHECK(q.n == 9 && q.rc[8][4] == MC_GLASS_OPAQUE + 1 && q.rc[8][0] == 0.14f && q.rc[8][3] == 0.59f);
    h.cam[0][0] = 0, h.cam[0][2] = 0.16f; // its middle left of the part
    mcHudRects(h, q);
    CHECK(q.n == 8);
    h.cam[0][0] = 0.14f, h.cam[0][2] = 0.28f, h.cam[0][1] = 0.01f, h.cam[0][3] = 0.35f; // above it
    mcHudRects(h, q);
    CHECK(q.n == 8);
    h.nGlass = 0;
    mcHudRects(h, q);
    CHECK(q.n == 6);
}

// v turned by the quaternion q (x y z w)
static void turn(const float *q, const float *v, float *o) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float cx = y * v[2] - z * v[1], cy = z * v[0] - x * v[2], cz = x * v[1] - y * v[0];
    float dx = y * cz - z * cy, dy = z * cx - x * cz, dz = x * cy - y * cx;
    o[0] = v[0] + 2 * (w * cx + dx), o[1] = v[1] + 2 * (w * cy + dy), o[2] = v[2] + 2 * (w * cz + dz);
}

static void testGlassPose() {
    g_test = "glass pose";
    float p[3], q[4];
    mcGlassPose(0, 0, 1.2f, p, q);
    CHECK(nearly(p[0], 0) && nearly(p[1], 0) && nearly(p[2], -1.2f));
    CHECK(nearly(q[0], 0) && nearly(q[1], 0) && nearly(q[2], 0) && nearly(q[3], 1));
    mcGlassPose(90, 0, 2, p, q);
    CHECK(nearly(p[0], 2) && nearly(p[1], 0) && nearly(p[2], 0));
    mcGlassPose(0, 90, 2, p, q);
    CHECK(nearly(p[0], 0) && nearly(p[1], 2) && nearly(p[2], 0));
    const float give[][2] = {{30, 20}, {-30, 20}, {30, -20}, {75, 50}};
    const float want[][3] = {{0.70477f, 0.51303f, -1.22070f}, {-0.70477f, 0.51303f, -1.22070f}, {0.70477f, -0.51303f, -1.22070f}, {0.93133f, 1.14907f, -0.24955f}};
    for (int i = 0; i < 4; ++i) {
        mcGlassPose(give[i][0], give[i][1], 1.5f, p, q);
        CHECK(nearly(p[0], want[i][0]) && nearly(p[1], want[i][1]) && nearly(p[2], want[i][2]));
        CHECK(nearly(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3], 1));
        const float ahead[3] = {0, 0, -1.5f}, side[3] = {1, 0, 0}, top[3] = {0, 1, 0};
        float o[3];
        turn(q, ahead, o); // the part's own -z points from the seat to its middle: it faces the seat
        CHECK(nearly(o[0], p[0]) && nearly(o[1], p[1]) && nearly(o[2], p[2]));
        turn(q, side, o); // its lower edge stays level, its right side to the right as seen from the seat
        CHECK(nearly(o[1], 0) && o[0] * -p[2] + o[2] * p[0] > 0);
        turn(q, top, o);
        CHECK(o[1] > 0);
    }
}

static void testGlassScale() {
    g_test = "glass scale";
    CHECK(nearly(mcGlassScale(90, 1, 0.5f), 4));
    CHECK(nearly(mcGlassScale(60, 2, 0.25f), 9.23760f));
    CHECK(nearly(mcGlassScale(20, 1.2f, 0.37f), 1.14371f));
}

static void testGlassAim() {
    g_test = "glass aim";
    float r, u;
    const float seat[3] = {0, 0, 0}, ahead[3] = {0, 0, -1};
    mcGlassAim(seat, ahead, 1.2f, &r, &u);
    CHECK(nearly(r, 0) && nearly(u, 0));
    const float f1[3] = {0.5f, 0, -0.8660254f}, f2[3] = {0, 0.3420201f, -0.9396926f}, f3[3] = {0.5f, 0.5f, -0.7071068f}, f4[3] = {-0.5f, -0.5f, -0.7071068f};
    mcGlassAim(seat, f1, 1.2f, &r, &u);
    CHECK(nearly(r, 30, 1e-3f) && nearly(u, 0, 1e-3f));
    mcGlassAim(seat, f2, 1.2f, &r, &u);
    CHECK(nearly(r, 0, 1e-3f) && nearly(u, 20, 1e-3f));
    mcGlassAim(seat, f3, 1.2f, &r, &u);
    CHECK(nearly(r, 35.2644f, 1e-3f) && nearly(u, 30, 1e-3f));
    mcGlassAim(seat, f4, 1.2f, &r, &u);
    CHECK(nearly(r, -35.2644f, 1e-3f) && nearly(u, -30, 1e-3f));
    // a head off the seat: the point named lies on the glass and on the line of sight, ahead of the head
    const float heads[][3] = {{0.2f, 0, 0}, {0, 0.1f, 0.3f}, {0.3f, -0.2f, -0.4f}, {-0.25f, 0.15f, 0.2f}};
    const float *looks[] = {ahead, f1, f3, f4};
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 4; ++k) {
            const float D = 1.1f, *p = heads[i], *f = looks[k];
            mcGlassAim(p, f, D, &r, &u);
            float x[3], q[4];
            mcGlassPose(r, u, D, x, q);
            float d[3] = {x[0] - p[0], x[1] - p[1], x[2] - p[2]};
            float t = mcDot3(d, f);
            CHECK(t > 0.5f && nearly(d[0], t * f[0], 1e-3f) && nearly(d[1], t * f[1], 1e-3f) && nearly(d[2], t * f[2], 1e-3f));
        }
    const float out[3] = {0, 0, -3}, right[3] = {1, 0, 0}; // a head outside the glass, looking past it: where the head is
    mcGlassAim(out, right, 1, &r, &u);
    CHECK(nearly(r, 0) && nearly(u, 0));
    const float out2[3] = {-2, 0, -3}; // (the nearest point of its line of sight: 2 m along it)
    mcGlassAim(out2, right, 1, &r, &u);
    CHECK(nearly(r, 0, 1e-3f) && nearly(u, 0));
}

// ---------------------------------------------------------------- the grid of directions
// (a row of memory before and after it: writing or reading past its ends shows)
static struct {
    float before[MC_GW];
    McGrid g;
    float after[MC_GW];
} g_box;
#define g_grid g_box.g
static void gridClear() { memset(&g_box, 0, sizeof g_box); }
static float cell(int c, int r) { return g_grid.d[r * MC_GW + c]; }
static int marked() {
    int n = 0;
    for (int i = 0; i < MC_GW * MC_GH; ++i) n += g_grid.d[i] > 0;
    return n;
}
static bool around() { // nothing written beside the grid
    for (int i = 0; i < MC_GW; ++i)
        if (g_box.before[i] != 0 || g_box.after[i] != 0) return false;
    return true;
}
// a vertex d metres away at the grid's place x, y (cells); after its three coordinates comes something else
static void at(float x, float y, float d, float *p) {
    float q[4];
    mcGlassPose(MC_GAZ0 + x * MC_GSTEP, MC_GEL0 + y * MC_GSTEP, d, p, q);
    p[2] = -p[2]; // ahead
    p[3] = -1;
}

static void testGridMark() {
    g_test = "grid mark";
    gridClear();
    mcGridMark(g_grid, 0, 0, 2), mcGridMark(g_grid, MC_GW - 1, MC_GH - 1, 3), mcGridMark(g_grid, 5, 7, 4);
    CHECK(cell(0, 0) == 2 && cell(MC_GW - 1, MC_GH - 1) == 3 && cell(5, 7) == 4 && marked() == 3);
    mcGridMark(g_grid, -1, 0, 1), mcGridMark(g_grid, MC_GW, 0, 1), mcGridMark(g_grid, 0, -1, 1), mcGridMark(g_grid, 0, MC_GH, 1);
    mcGridMark(g_grid, 3, -1, 1), mcGridMark(g_grid, 3, MC_GH, 1), mcGridMark(g_grid, -1, 9, 1), mcGridMark(g_grid, MC_GW, 9, 1);
    CHECK(marked() == 3 && around() && cell(0, 0) == 2 && cell(MC_GW - 1, 0) == 0 && cell(0, MC_GH - 1) == 0 && cell(1, 0) == 0);
    mcGridMark(g_grid, 5, 7, 6); // the nearest counts
    CHECK(cell(5, 7) == 4);
    mcGridMark(g_grid, 5, 7, 1.5f);
    CHECK(cell(5, 7) == 1.5f);
    mcGridMark(g_grid, 5, 7, 0.5f), mcGridMark(g_grid, 5, 7, 0.75f);
    CHECK(cell(5, 7) == 0.5f);
}

static void testGridPlace() {
    g_test = "grid place";
    const float ahead[4] = {0, 0, 1, -1}, p[4] = {1, 0.5f, 1, -1}, q[4] = {-0.5f, -1, 2, -1};
    CHECK(mcGridX(ahead) == 40 && mcGridY(ahead) == 25);
    CHECK(nearly(mcGridX(p), 62.5f, 1e-3f) && nearly(mcGridY(p), 34.7356f, 1e-3f));
    CHECK(nearly(mcGridX(q), 32.9819f, 1e-3f) && nearly(mcGridY(q), 12.0615f, 1e-3f));
    CHECK(mcGridSide(1, 1, 3, 2, 2, 1.5f) == 0 && mcGridSide(1, 1, 3, 2, 2, 2) == 1 && mcGridSide(1, 1, 3, 2, 2, 1) == -1);
    CHECK(mcGridSide(1, 1, 3, 2, 5, 3.5f) == 1 && mcGridSide(3, 2, 1, 1, 5, 3.5f) == -1);
}

// the cells a triangle with the corners (cells) x, y must mark: those of its corners and those whose direction it covers
static bool covered(const float *x, const float *y) {
    for (int r = 0; r < MC_GH; ++r)
        for (int c = 0; c < MC_GW; ++c) {
            bool want = false;
            for (int k = 0; k < 3; ++k) want = want || (c == (int)floorf(x[k]) && r == (int)floorf(y[k]));
            int pos = 0, neg = 0;
            for (int k = 0; k < 3; ++k) {
                int n = (k + 1) % 3;
                double e = ((double)x[n] - x[k]) * (r - (double)y[k]) - ((double)y[n] - y[k]) * (c - (double)x[k]);
                pos += e > 1e-4, neg += e < -1e-4;
            }
            want = want || !(pos && neg);
            if (want != (cell(c, r) > 0)) {
                printf("   cell %d %d: %s\n", c, r, want ? "not marked" : "marked");
                return false;
            }
        }
    return true;
}

static void testGridTri() {
    g_test = "grid triangle";
    float v[3][4];
    // corners at three different places across and up, given in every order
    const float shapes[][6] = {{30.3f, 41.7f, 36.4f, 20.2f, 23.8f, 27.6f}, {50.5f, 55.3f, 60.7f, 10.6f, 14.2f, 12.4f}, {20.4f, 20.7f, 21.3f, 30.2f, 36.8f, 33.1f}};
    const int order[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (int s = 0; s < 3; ++s)
        for (int o = 0; o < 6; ++o) {
            float x[3], y[3];
            for (int k = 0; k < 3; ++k) x[k] = shapes[s][order[o][k]], y[k] = shapes[s][3 + order[o][k]], at(x[k], y[k], 1 + k, v[k]);
            gridClear();
            mcGridTri(g_grid, v[0], v[1], v[2]);
            CHECK(covered(x, y) && marked() > (s == 2 ? 3 : 8));
            CHECK(nearly(cell((int)x[0], (int)y[0]), 2)); // the mean distance of its corners
        }
    // a small one within a cell: that cell alone
    gridClear();
    at(45.2f, 27.2f, 1, v[0]), at(45.6f, 27.2f, 2, v[1]), at(45.2f, 27.7f, 6, v[2]);
    mcGridTri(g_grid, v[0], v[1], v[2]);
    CHECK(marked() == 1 && nearly(cell(45, 27), 3.0f));
    // corners in three cells, no cell's direction inside
    gridClear();
    at(29.75f, 19.75f, 2, v[0]), at(30.2f, 19.75f, 2, v[1]), at(29.75f, 20.2f, 2, v[2]);
    mcGridTri(g_grid, v[0], v[1], v[2]);
    CHECK(marked() == 3 && cell(29, 19) > 0 && cell(30, 19) > 0 && cell(29, 20) > 0);
    // its edges count: one straight up at 0 degrees, one corner straight ahead
    gridClear();
    const float t0[4] = {0, -0.2f, 1, -1}, t1[4] = {0, 0.2f, 1, -1}, t2[4] = {0.2f, 0, 1, -1};
    mcGridTri(g_grid, t0, t1, t2);
    CHECK(cell(40, 25) > 0 && cell(40, 20) > 0 && cell(40, 30) > 0 && cell(45, 25) > 0 && cell(42, 22) > 0 && cell(42, 27) > 0);
    CHECK(cell(39, 25) == 0 && cell(40, 31) == 0 && cell(40, 18) == 0 && cell(46, 25) == 0 && cell(44, 22) == 0 && cell(44, 28) == 0);
    // one with a corner beside or behind the seat: left out, whichever corner it is
    const float s0[4] = {0.3f, 0, 1, -1}, s1[4] = {0.3f, 0.3f, 1, -1}, s2[4] = {1, 0, 0.05f, -1}, s3[4] = {1, 0, -1, -1}, s4[4] = {1, 0, 0.06f, -1};
    const float s5[4] = {0.3f, 0, 0.06f, 1}, s6[4] = {0.3f, 0.02f, 0.06f, 1}, s7[4] = {0.31f, 0, 0.06f, 1};
    gridClear();
    mcGridTri(g_grid, s0, s1, s2), mcGridTri(g_grid, s0, s3, s1), mcGridTri(g_grid, s3, s0, s1), mcGridTri(g_grid, s2, s0, s1), mcGridTri(g_grid, s0, s2, s1);
    CHECK(marked() == 0);
    mcGridTri(g_grid, s0, s1, s4);
    CHECK(marked() > 3);
    gridClear();
    mcGridTri(g_grid, s5, s6, s7); // all three just ahead
    CHECK(marked() > 0);
    // one reaching out of the grid: what lies inside it is marked, nothing beside it
    gridClear();
    at(75.3f, 45.3f, 1, v[0]), at(82.5f, 45.3f, 1, v[1]), at(75.3f, 54.0f, 1, v[2]);
    mcGridTri(g_grid, v[0], v[1], v[2]);
    CHECK(cell(76, 46) > 0 && cell(79, 46) > 0 && cell(76, 49) > 0 && marked() > 10 && around());
    gridClear();
    at(-3.3f, -2.6f, 3, v[0]), at(4.4f, 1.3f, 1, v[1]), at(1.2f, 4.5f, 1, v[2]);
    mcGridTri(g_grid, v[0], v[1], v[2]);
    CHECK(cell(0, 0) > 0 && cell(2, 1) > 0 && marked() > 5 && around());
}

static void testGridCells() {
    g_test = "grid cells";
    int q[4];
    mcGridCells(0, 0, 4, 2, q); // -2..2 right, -1..1 up
    CHECK(q[0] == 39 && q[1] == 24 && q[2] == 41 && q[3] == 25);
    mcGridCells(-21, 11, 6, 10, q); // -24..-18, 6..16
    CHECK(q[0] == 28 && q[1] == 28 && q[2] == 31 && q[3] == 33);
    mcGridCells(31, -19, 3, 5, q); // 29.5..32.5, -21.5..-16.5
    CHECK(q[0] == 54 && q[1] == 14 && q[2] == 56 && q[3] == 16);
}

static void testGridFree() {
    g_test = "grid free";
    gridClear();
    CHECK(mcGridFree(g_grid, 0, 0, 20, 10));
    CHECK(mcGridFree(g_grid, -69, -44, 20, 10) && mcGridFree(g_grid, 69, 44, 19, 9)); // up to the grid's edges
    CHECK(!mcGridFree(g_grid, -71, 0, 20, 10) && !mcGridFree(g_grid, 71, 0, 20, 10) && !mcGridFree(g_grid, 0, -46, 20, 10) && !mcGridFree(g_grid, 0, 46, 20, 10));
    const int in[][2] = {{35, 22}, {45, 22}, {35, 27}, {45, 27}, {40, 25}}; // the corner cells of -10..10 x -5..5, and one inside
    for (int i = 0; i < 5; ++i) {
        gridClear();
        mcGridMark(g_grid, in[i][0], in[i][1], 1);
        CHECK(!mcGridFree(g_grid, 0, 0, 20, 10));
    }
    const int out[][2] = {{34, 25}, {46, 25}, {40, 21}, {40, 28}}; // the cells just outside
    gridClear();
    for (int i = 0; i < 4; ++i) mcGridMark(g_grid, out[i][0], out[i][1], 1);
    CHECK(mcGridFree(g_grid, 0, 0, 20, 10));
}

static void testGridRim() {
    g_test = "grid rim";
    gridClear();
    CHECK(mcGridRim(g_grid, 0, 0, 20, 10) == 0);
    mcGridMark(g_grid, 32, 19, 2), mcGridMark(g_grid, 48, 30, 4); // three cells out from the corners 35 22 and 45 27
    CHECK(nearly(mcGridRim(g_grid, 0, 0, 20, 10), 3));
    mcGridMark(g_grid, 31, 25, 9), mcGridMark(g_grid, 49, 25, 9), mcGridMark(g_grid, 40, 18, 9), mcGridMark(g_grid, 40, 31, 9); // four out
    CHECK(nearly(mcGridRim(g_grid, 0, 0, 20, 10), 3));
    mcGridMark(g_grid, 40, 25, 6); // in it
    CHECK(nearly(mcGridRim(g_grid, 0, 0, 20, 10), 4));
    // at the grid's corners: what there is of the rim, nothing from beside the grid or from the row above
    gridClear();
    for (int i = 0; i < MC_GW; ++i) g_box.before[i] = g_box.after[i] = 100;
    mcGridMark(g_grid, 0, 0, 5), mcGridMark(g_grid, MC_GW - 1, MC_GH - 1, 7), mcGridMark(g_grid, MC_GW - 1, 0, 1);
    mcGridMark(g_grid, 0, 3, 50), mcGridMark(g_grid, 0, MC_GH - 3, 60);
    CHECK(nearly(mcGridRim(g_grid, 75, 45, 4, 4), 7));
    CHECK(nearly(mcGridRim(g_grid, 75, -46, 4, 4), 1));
    CHECK(nearly(mcGridRim(g_grid, -76, 45, 4, 4), 60));
    CHECK(nearly(mcGridRim(g_grid, -76, -46, 4, 4), 27.5f)); // (5 and 50)
}

static void testGridFit() {
    g_test = "grid fit";
    gridClear();
    float r = 99, u = 99;
    CHECK(mcGridFit(g_grid, 20, 10, 7.1f, -2.9f, 1000, nullptr, 0, &r, &u) && r == 8 && u == -2); // open everywhere: the nearest direction of the grid
    CHECK(mcGridFit(g_grid, 20, 10, 6.9f, -3.1f, 1000, nullptr, 0, &r, &u) && r == 6 && u == -4);
    CHECK(mcGridFit(g_grid, 20, 10, 7, -3, 1000, nullptr, 0, &r, &u) && r == 6 && u == -4); // between four: the first met
    CHECK(mcGridFit(g_grid, 20, 10, -200, 0, 1000, nullptr, 0, &r, &u) && r == -70 && u == 0); // wanted outside: the edge
    CHECK(mcGridFit(g_grid, 20, 10, 0, 200, 1000, nullptr, 0, &r, &u) && r == 0 && u == 44);
    CHECK(mcGridFit(g_grid, 2, 2, -200, -200, 1000, nullptr, 0, &r, &u) && r == -78 && u == -48); // the grid's second row and column
    CHECK(mcGridFit(g_grid, 2, 2, 200, 200, 1000, nullptr, 0, &r, &u) && r == 78 && u == 48);     // and its last
    // places taken beside it: clear of them, touching them
    const float left[][4] = {{-30, -60, 0, 60}}, right[][4] = {{0, -60, 30, 60}};
    CHECK(mcGridFit(g_grid, 20, 10, -10, 0, 1000, left, 1, &r, &u) && r == 10 && u == 0);
    CHECK(mcGridFit(g_grid, 20, 10, 10, 0, 1000, right, 1, &r, &u) && r == -10 && u == 0);
    // a post right of where it is wanted: left of it
    for (int row = 0; row < MC_GH; ++row) mcGridMark(g_grid, 44, row, 1.5f);
    CHECK(mcGridFit(g_grid, 20, 10, 8, 0, 1000, nullptr, 0, &r, &u) && r == -4 && u == 0);
    CHECK(mcGridFit(g_grid, 20, 10, 14, 0, 1000, nullptr, 0, &r, &u) && r == 20 && u == 0); // nearer to its other side
    // places taken above and below
    const float taken[][4] = {{-30, -10, 0, 10}, {-100, 12, 100, 14}, {-100, -22, 100, -12}};
    CHECK(mcGridFit(g_grid, 20, 10, -10, 1, 1000, taken, 1, &r, &u) && r == -10 && u == 16);
    CHECK(mcGridFit(g_grid, 20, 10, -10, -1, 1000, taken, 1, &r, &u) && r == -10 && u == -16);
    CHECK(mcGridFit(g_grid, 20, 10, -10, 1, 1000, taken, 2, &r, &u) && r == -10 && u == -16); // above it the second
    CHECK(mcGridFit(g_grid, 20, 10, -10, 1, 1000, taken, 3, &r, &u) && r == -10 && u == 20);  // below it the third: above the second
    CHECK(mcGridFit(g_grid, 20, 4, -10, 12, 1000, taken, 1, &r, &u) && r == -10 && u == 12);  // touching is clear
    // no farther than its reach
    const float mid[][4] = {{-11, -6, 11, 6}};
    gridClear();
    CHECK(mcGridFit(g_grid, 20, 10, 0, 0, 12.1f, mid, 1, &r, &u) && r == 0 && u == -12);
    r = u = 99;
    CHECK(!mcGridFit(g_grid, 20, 10, 0, 0, 12, mid, 1, &r, &u) && r == 99 && u == 99);
    for (int row = 0; row < MC_GH; ++row) mcGridMark(g_grid, 44, row, 1.5f);
    // nowhere: too large for the grid, or mesh in every fourth row
    r = u = 99;
    CHECK(!mcGridFit(g_grid, 20, 102, 0, 0, 1000, nullptr, 0, &r, &u) && r == 99 && u == 99);
    for (int row = 0; row < MC_GH; row += 4)
        for (int col = 0; col < MC_GW; ++col) mcGridMark(g_grid, col, row, 1);
    CHECK(!mcGridFit(g_grid, 20, 10, 0, 0, 1000, nullptr, 0, &r, &u));
    CHECK(mcGridFit(g_grid, 4, 4, 0, 0, 1000, nullptr, 0, &r, &u));
}

static void testSeat() {
    McSeat s = {};
    // the game's cockpit in every frame: inside, whatever the head does
    for (int i = 0; i < 20; ++i) mcSeatStep(s, 200, i & 1);
    CHECK(!s.outside && s.misses == 0);
    // looking away, no cockpit of the game's: nothing is concluded
    for (int i = 0; i < 20; ++i) mcSeatStep(s, 0, false);
    CHECK(!s.outside && s.misses == 0);
    // looking ahead without it: outside with the fifth such frame, not before
    for (int i = 0; i < MC_OUTSIDE_FRAMES - 1; ++i) mcSeatStep(s, 0, true);
    CHECK(!s.outside && s.misses == MC_OUTSIDE_FRAMES - 1);
    mcSeatStep(s, 0, false); // (a look to the side in between does not count, and does not start anew)
    CHECK(!s.outside && s.misses == MC_OUTSIDE_FRAMES - 1);
    mcSeatStep(s, 0, true);
    CHECK(s.outside && s.misses == MC_OUTSIDE_FRAMES);
    mcSeatStep(s, 0, true), mcSeatStep(s, 0, true);
    CHECK(s.outside && s.misses == MC_OUTSIDE_FRAMES); // (the count does not run on)
    // outside it stays while the head looks away
    mcSeatStep(s, 0, false);
    CHECK(s.outside);
    // a few triangles near the camera (the mech's hull at the picture's edge) are not the cockpit
    mcSeatStep(s, MC_COCKPIT_TRIS - 1, true);
    CHECK(s.outside && s.misses == MC_OUTSIDE_FRAMES);
    // one frame with the cockpit: inside again, and the count starts anew
    mcSeatStep(s, MC_COCKPIT_TRIS, false);
    CHECK(!s.outside && s.misses == 0);
    // a little of the cockpit says nothing: no miss, and a count begun is not taken back
    mcSeatStep(s, MC_COCKPIT_TRIS - 1, true);
    CHECK(!s.outside && s.misses == 0);
    mcSeatStep(s, MC_NO_COCKPIT_TRIS, true);
    CHECK(!s.outside && s.misses == 0);
    mcSeatStep(s, MC_NO_COCKPIT_TRIS - 1, true);
    CHECK(!s.outside && s.misses == 1);
    mcSeatStep(s, MC_NO_COCKPIT_TRIS, true);
    CHECK(!s.outside && s.misses == 1);
    // ... so frames with nothing need not follow each other, as long as the cockpit is not seen in between
    for (int i = 0; i < MC_OUTSIDE_FRAMES - 1; ++i) mcSeatStep(s, 0, true), mcSeatStep(s, 10, true);
    CHECK(s.outside);
}

static void testSheet() {
    // hardly there: no sheet, whatever its colour
    CHECK(mcSheet(0, 0, 0, 0) == MC_SHEET_NONE);
    CHECK(mcSheet(MC_SHEET_ALPHA - 1, 0, 0, 0) == MC_SHEET_NONE);
    CHECK(mcSheet(MC_SHEET_ALPHA - 1, 7, 0, 0) == MC_SHEET_NONE);
    // the menu's sheet is told while it fades in, from the least alpha a point can show
    CHECK(mcSheet(MC_SHEET_ALPHA - 1, 0, 0, 7) == MC_SHEET_MENU);
    CHECK(mcSheet(MC_SHEET_MIN, 0, 0, 1) == MC_SHEET_MENU);
    CHECK(mcSheet(MC_SHEET_MIN - 1, 0, 0, 1) == MC_SHEET_NONE);
    // blue, but not alone: no sheet of ours - and no fade, though red and green are black against the alpha
    CHECK(mcSheet(255, 5, 0, 17) == MC_SHEET_NONE);
    CHECK(mcSheet(32, 1, 0, 2) == MC_SHEET_FADE); // (blue at a sixteenth of the alpha is still black)
    // black: a fade, from the least alpha that counts
    CHECK(mcSheet(MC_SHEET_ALPHA, 0, 0, 0) == MC_SHEET_FADE);
    CHECK(mcSheet(255, 0, 0, 0) == MC_SHEET_FADE);
    // a colour up to a sixteenth of the alpha is still black - each of the three
    CHECK(mcSheet(16, 1, 0, 0) == MC_SHEET_FADE);
    CHECK(mcSheet(16, 0, 1, 0) == MC_SHEET_FADE);
    CHECK(mcSheet(16, 0, 0, 1) == MC_SHEET_FADE);
    CHECK(mcSheet(255, 15, 15, 15) == MC_SHEET_FADE);
    // more than that is a colour: red or green alone are no sheet of ours, blue alone is the menu's
    CHECK(mcSheet(16, 2, 0, 0) == MC_SHEET_NONE);
    CHECK(mcSheet(16, 0, 2, 0) == MC_SHEET_NONE);
    CHECK(mcSheet(16, 0, 0, 2) == MC_SHEET_MENU);
    CHECK(mcSheet(128, 0, 0, 59) == MC_SHEET_MENU); // (as measured: the paused battle's sheet)
    CHECK(mcSheet(255, 0, 60, 0) == MC_SHEET_NONE); // (light amplification)
    // blue alone: red and green each at most a quarter of it
    CHECK(mcSheet(255, 5, 0, 20) == MC_SHEET_MENU);
    CHECK(mcSheet(255, 0, 5, 20) == MC_SHEET_MENU);
    CHECK(mcSheet(255, 6, 0, 20) == MC_SHEET_NONE);
    CHECK(mcSheet(255, 0, 6, 20) == MC_SHEET_NONE);
    CHECK(mcSheet(255, 20, 0, 20) == MC_SHEET_NONE);
    CHECK(mcSheet(255, 0, 20, 20) == MC_SHEET_NONE);
}

int main() {
    testHolds(), testTurnNorm(), testTurnFit(), testEyeInMesh(), testHudCross(), testHudGlass(), testGlassPose(), testGlassScale(), testGlassAim();
    testGridMark(), testGridPlace(), testGridTri(), testGridCells(), testGridFree(), testGridRim(), testGridFit();
    testMul34(), testViewAxis();
    testFrustum(), testFrustumSameView(), testFrustumTangent(), testFrustumNear(), testFrustumLayout();
    testWide();
    testAdd(), testMeet(), testAddCut(), testMoveOut(), testSpans(), testLeftAlt(), testRightAlt();
    testHudPlain(), testHudZoom(), testHudZoomEdges(), testHudScreens(), testHudScreensZoom();
    testSeat();
    testSheet();
    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
