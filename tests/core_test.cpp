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
    const float want[][5] = {{0.25f, 0.12f, 0.72f, 0.585f, MC_FAR}, // the far middle above the torso mark
                             {0.3f, 0, 0.716f, 0.12f, MC_FAR},      // the top strip between the monitors' parts
                             {1.0f, 0, 1.02f, 0.12f, MC_FAR},       // and right of them
                             {0, 0.435f, 0.25f, 0.78f, MC_FAR},     // the left column below the lance menu
                             {0.72f, 0.12f, 1.02f, 0.78f, MC_FAR},
                             {0.25f, 0.665f, 0.72f, 0.78f, MC_FAR}, // the middle below, left and right of the mark
                             {0.25f, 0.585f, 0.42f, 0.665f, MC_FAR},
                             {0.58f, 0.585f, 0.72f, 0.665f, MC_FAR},
                             {0.42f, 0.585f, 0.58f, 0.665f, MC_FAR_UP}};
    CHECK(same(q, want, 9));

    h.twistDv = 0; // the mark not moved: the middle whole
    mcHudRects(h, q);
    CHECK(q.n == 5 && q.rc[0][3] == 0.78f && q.rc[4][4] == MC_FAR);
    h.twistDv = 0.01f;
    mcHudRects(h, q);
    CHECK(q.n == 9);
    h.twist[2] = h.twist[0]; // no width
    mcHudRects(h, q);
    CHECK(q.n == 5);
    h.twist[2] = 0.58f, h.twist[3] = h.twist[1]; // no height
    mcHudRects(h, q);
    CHECK(q.n == 5);

    h = hud(), h.screens = true; // no monitor shows the top strip or the left column
    mcHudRects(h, q);
    CHECK(q.n == 8 && q.rc[1][0] == 0 && q.rc[1][1] == 0 && q.rc[1][2] == 1.02f && q.rc[1][3] == 0.12f && q.rc[2][1] == 0.12f);
    h.leftAlt = 0.05f; // above the strip's lower edge: the column from the strip down
    mcHudRects(h, q);
    CHECK(q.rc[2][1] == 0.12f);
    h.span[0][0] = 0, h.span[0][1] = 1.02f, h.nSpan = 1; // all of the top strip on monitors
    mcHudRects(h, q);
    CHECK(q.n == 7 && q.rc[1][0] == 0 && q.rc[1][3] == 0.78f);
}

static void testHudScreensZoom() {
    g_test = "hud screens zoom";
    McHud h = hud();
    h.screens = true;
    h.span[0][0] = 0, h.span[0][1] = 0.243f, h.nSpan = 1;
    setCam(h, 0.35f, 0.05f, 0.65f, 0.50f);
    McRects q;
    mcHudRects(h, q);
    const float want[][5] = {{0.25f, 0.52f, 0.72f, 0.78f, MC_FAR},
                             {0.25f, 0.12f, 0.33f, 0.52f, MC_FAR},
                             {0.67f, 0.12f, 0.72f, 0.52f, MC_FAR},
                             {0.243f, 0, 1.02f, 0.03f, MC_FAR}, // the top strip around the window: above, left, right
                             {0.243f, 0.03f, 0.33f, 0.12f, MC_FAR},
                             {0.67f, 0.03f, 1.02f, 0.12f, MC_FAR},
                             {0, 0.12f, 0.25f, 0.78f, MC_FAR},
                             {0.72f, 0.12f, 1.02f, 0.78f, MC_FAR},
                             {0.35f, 0.05f, 0.65f, 0.50f, MC_FAR_OPAQUE}}; // the torso mark stays where it is while zooming
    CHECK(same(q, want, 9));
}

// ---------------------------------------------------------------- the parts on the glass
static void testHolds() {
    g_test = "holds";
    const float r[4] = {0.2f, 0.3f, 0.6f, 0.9f};
    CHECK(mcHolds(r, 0.21f, 0.31f) && mcHolds(r, 0.59f, 0.89f));
    CHECK(!mcHolds(r, 0.2f, 0.5f) && !mcHolds(r, 0.6f, 0.5f) && !mcHolds(r, 0.4f, 0.3f) && !mcHolds(r, 0.4f, 0.9f)); // on an edge
    CHECK(!mcHolds(r, 0.1f, 0.5f) && !mcHolds(r, 0.7f, 0.5f) && !mcHolds(r, 0.4f, 0.2f) && !mcHolds(r, 0.4f, 0.95f));
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
    CHECK(q.n == 11); // the eight without glass, then:
    const float want[][5] = {{0, 0.772f, 0.18f, 1, MC_GLASS}, {0.01f, 0.80f, 0.07f, 0.98f, MC_GLASS_OPAQUE}, {0.125f, 0.25f, 0.75f, 0.625f, MC_GLASS + 1}};
    for (int i = 0; i < 3 && q.n == 11; ++i)
        for (int k = 0; k < 5; ++k) CHECK(nearly(q.rc[8 + i][k], want[i][k], 1e-6f));
    h.nCam = 0;
    setCam(h, 0.20f, 0.27f, 0.28f, 0.59f); // a window in the second part, off its middle both ways
    h.cam[0][0] = 0.14f; // (left of the zoom window's range)
    h.nGlass = 2;
    mcHudRects(h, q);
    CHECK(q.n == 11 && q.rc[10][4] == MC_GLASS_OPAQUE + 1 && q.rc[10][0] == 0.14f && q.rc[10][3] == 0.59f);
    h.cam[0][0] = 0, h.cam[0][2] = 0.16f; // its middle left of the part
    mcHudRects(h, q);
    CHECK(q.n == 10);
    h.cam[0][0] = 0.14f, h.cam[0][2] = 0.28f, h.cam[0][1] = 0.01f, h.cam[0][3] = 0.35f; // above it
    mcHudRects(h, q);
    CHECK(q.n == 10);
    h.nGlass = 0;
    mcHudRects(h, q);
    CHECK(q.n == 8);
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

int main() {
    testHolds(), testHudGlass(), testGlassPose(), testGlassScale(), testGlassAim();
    testMul34(), testViewAxis();
    testFrustum(), testFrustumSameView(), testFrustumTangent(), testFrustumNear(), testFrustumLayout();
    testWide();
    testAdd(), testMeet(), testAddCut(), testMoveOut(), testSpans(), testLeftAlt();
    testHudPlain(), testHudZoom(), testHudZoomEdges(), testHudScreens(), testHudScreensZoom();
    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
