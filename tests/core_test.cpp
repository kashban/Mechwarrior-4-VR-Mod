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
    for (int k = 0; k < 6; ++k) NEAR(mcDot3(n + 4 * k, n + 4 * k), 1);    // unit normals
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
        CHECK(fr.planes[0] == 0 && fr.planes[1] == 1 && fr.planes[2] == 0 && fr.planes[4] == 0 && fr.planes[5] == -1 &&
              fr.planes[6] == 0);
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
    CHECK(mcWideFactor(1.31f) == 1.0f && mcWideFactor(0.99f) == 1.0f && mcWideFactor(-2.0f) == 1.0f &&
          mcWideFactor(5.0f) == 1.0f);
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
        for (int i = 0; i < q.n && i < MC_RECTS; ++i)
            printf("   got %.4f %.4f %.4f %.4f kind %g\n", q.rc[i][0], q.rc[i][1], q.rc[i][2], q.rc[i][3], q.rc[i][4]);
        return false;
    }
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < 5; ++k)
            if (!nearly(q.rc[i][k], want[i][k], 1e-5f)) {
                printf("   rectangle %d: %.4f %.4f %.4f %.4f kind %g, wanted %.4f %.4f %.4f %.4f kind %g\n", i,
                       q.rc[i][0], q.rc[i][1], q.rc[i][2], q.rc[i][3], q.rc[i][4], want[i][0], want[i][1], want[i][2],
                       want[i][3], want[i][4]);
                return false;
            }
    return true;
}

static void testAdd() {
    g_test = "add";
    McRects q = {};
    mcAdd(q, 0.1f, 0.2f, 0.3f, 0.4f, 3);
    mcAdd(q, 0, 0, 0.004f, 1, 1); // too narrow
    mcAdd(q, 0, 0, 1, 0.004f, 1); // too low
    mcAdd(q, 0, 0, 0.005f, 1, 1); // at the limit: still out
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
    CHECK(mcMeet(c, 0.5f, 0.6f, 0.9f, 0.9f) && mcMeet(c, 0, 0, 0.3f, 0.4f) && mcMeet(c, 0.3f, 0.4f, 0.4f, 0.5f) &&
          mcMeet(c, 0, 0, 1, 1));
    CHECK(!mcMeet(c, 0.6f, 0.3f, 0.9f, 0.7f)); // touching on each side is not meeting
    CHECK(!mcMeet(c, 0, 0.3f, 0.2f, 0.7f));
    CHECK(!mcMeet(c, 0.2f, 0.7f, 0.6f, 0.9f));
    CHECK(!mcMeet(c, 0.2f, 0, 0.6f, 0.3f));
    CHECK(mcMeet(c, 0.59f, 0.3f, 0.9f, 0.7f) && mcMeet(c, 0, 0.3f, 0.21f, 0.7f) && mcMeet(c, 0.2f, 0.69f, 0.6f, 0.9f) &&
          mcMeet(c, 0.2f, 0, 0.6f, 0.31f));
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
    const float ring[][5] = {{0.1f, 0.1f, 0.9f, 0.3f, 2},
                             {0.1f, 0.5f, 0.9f, 0.8f, 2},
                             {0.1f, 0.3f, 0.4f, 0.5f, 2},
                             {0.6f, 0.3f, 0.9f, 0.5f, 2}};
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
    const float want[][5] = {
        {0.2f, 0.1f, 0.8f, 0.1f, MC_FAR}, // nothing left above (a sliver, kept in place)
        {0, 0, 1, 0.1f, MC_NEAR},          {0.85f, 0.1f, 1, 0.9f, MC_FAR},    {0.3f, 0.2f, 0.9f, 0.2f, MC_FAR},
        {0.2f, 0.5f, 0.8f, 0.9f, MC_FAR},  {0.2f, 0.1f, 0.4f, 0.5f, MC_FAR},  {0.6f, 0.1f, 0.8f, 0.5f, MC_FAR},
        {0.3f, 0.2f, 0.4f, 0.45f, MC_FAR}, {0.6f, 0.2f, 0.9f, 0.45f, MC_FAR}, {0.4f, 0.05f, 0.6f, 0.5f, MC_FAR_UP}};
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
    const float ring[][5] = {{0.2f, 0.1f, 0.8f, 0.3f, MC_FAR},
                             {0.2f, 0.5f, 0.8f, 0.9f, MC_FAR},
                             {0.2f, 0.3f, 0.4f, 0.5f, MC_FAR},
                             {0.6f, 0.3f, 0.8f, 0.5f, MC_FAR},
                             {0.4f, 0.3f, 0.6f, 0.5f, MC_FAR_UP}};
    CHECK(same(q, ring, 5));
}

static void testSpans() {
    g_test = "spans";
    float iv[MC_SPANS + 1][2] = {};
    int n = 0;
    const float T = 0.12f;
    const float a[4] = {0.7f, 0, 1.0f, 0.1f}, b[4] = {0.1f, 0.05f, 0.3f, 0.5f}, c[4] = {0.4f, 0, 0.6f, 0.2f};
    const float below[4] = {0, 0.12f, 0.2f, 0.5f}, justIn[4] = {0.4f, 0.119f, 0.5f, 0.5f},
                flat[4] = {0.2f, 0, 0.2f, 0.1f}, low[4] = {0.2f, 0.1f, 0.3f, 0.1f};
    const float unset[4] = {0, 0, 0, 0}, sameStart[4] = {0.4f, 0, 0.45f, 0.1f};
    mcTopSpan(iv, n, a, T), mcTopSpan(iv, n, below, T), mcTopSpan(iv, n, b, T), mcTopSpan(iv, n, flat, T),
        mcTopSpan(iv, n, low, T);
    mcTopSpan(iv, n, unset, T), mcTopSpan(iv, n, c, T);
    CHECK(n == 3);
    CHECK(iv[0][0] == 0.1f && iv[0][1] == 0.3f && iv[1][0] == 0.4f && iv[1][1] == 0.6f && iv[2][0] == 0.7f &&
          iv[2][1] == 1.0f);
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
    const float a[5] = {0, 0, 0.24f, 0.43f, 9}, right[5] = {0.25f, 0, 0.5f, 0.6f, 9},
                edge[5] = {0.249f, 0, 0.5f, 0.6f, 9};
    const float unset[5] = {0, 0, 0, 0, 9}, flat[5] = {0, 0.5f, 0.2f, 0.5f, 9}, thin[5] = {0.1f, 0, 0.1f, 0.9f, 9};
    CHECK(mcLeftAlt(a, 0.25f, 0) == 0.43f);
    CHECK(mcLeftAlt(a, 0.25f, 0.5f) == 0.5f); // another reached further already
    CHECK(mcLeftAlt(a, 0.25f, 0.43f) == 0.43f);
    CHECK(mcLeftAlt(right, 0.25f, 0.1f) == 0.1f); // begins at the column's edge: not in it
    CHECK(mcLeftAlt(edge, 0.25f, 0.1f) == 0.6f);
    CHECK(mcLeftAlt(unset, 0.25f, 0.1f) == 0.1f && mcLeftAlt(flat, 0.25f, 0.1f) == 0.1f &&
          mcLeftAlt(thin, 0.25f, 0.1f) == 0.1f);
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
    const float want[][5] = {{0.25f, 0.12f, 0.72f, 0.78f, MC_FAR},
                             {0, 0, 1.02f, 0.12f, MC_NEAR},
                             {0, 0.78f, 1.02f, 1, MC_NEAR},
                             {0, 0.12f, 0.25f, 0.78f, MC_NEAR},
                             {0.72f, 0.12f, 1.02f, 0.78f, MC_NEAR}};
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
    for (int g = 0; g < 8; ++g)
        h.glass[g][0] = 0.1f * g, h.glass[g][1] = 0.8f, h.glass[g][2] = 0.1f * g + 0.08f, h.glass[g][3] = 0.9f;
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
    const float want[][5] = {
        {0.25f, 0.52f, 0.72f, 0.78f, MC_FAR}, // the far middle: below, left, right of the window and its frame
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
        CHECK(last[4] ==
              (want[i] ? (float)MC_FAR_OPAQUE : (float)MC_NEAR_OPAQUE)); // shown either as the one or as the other
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
    h.span[0][0] = 0, h.span[0][1] = 0.243f;    // the left monitor's part of the top strip
    h.span[1][0] = 0.2f, h.span[1][1] = 0.3f;   // one that overlaps it
    h.span[2][0] = 0.716f, h.span[2][1] = 1.0f; // the right monitor's
    h.nSpan = 3;
    h.leftAlt = 0.435f;
    setCam(h, 0.80f, 0.60f, 0.98f, 0.76f); // the target's view: on a monitor, no quad
    McRects q;
    mcHudRects(h, q);
    const float want[][5] = {
        {0.25f, 0.12f, 1.02f, 0.435f, MC_FAR}, // the middle and the right column beside the lance menu
        {0, 0.435f, 1.02f, 0.585f, MC_FAR},    // the whole width below it, down to the torso mark
        {0.3f, 0, 0.716f, 0.12f, MC_FAR},      // the top strip between the monitors' parts
        {1.0f, 0, 1.02f, 0.12f, MC_FAR},       // and right of them
        {0, 0.665f, 1.02f, 0.78f, MC_FAR},     // below, left and right of the mark
        {0, 0.585f, 0.42f, 0.665f, MC_FAR},       {0.58f, 0.585f, 1.02f, 0.665f, MC_FAR},
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
    const float right[][5] = {
        {0, 0.12f, 0.72f, 0.435f, MC_FAR}, {0, 0.435f, 1.02f, 0.78f, MC_FAR}, {0, 0, 1.02f, 0.12f, MC_FAR}};
    CHECK(same(q, right, 3));
    h.leftAlt = 0.3f, h.rightAlt = 0.5f; // both lower, the left one first
    mcHudRects(h, q);
    const float both[][5] = {{0.25f, 0.12f, 0.72f, 0.3f, MC_FAR},
                             {0, 0.3f, 0.72f, 0.5f, MC_FAR},
                             {0, 0.5f, 1.02f, 0.78f, MC_FAR},
                             {0, 0, 1.02f, 0.12f, MC_FAR}};
    CHECK(same(q, both, 4));
    h.leftAlt = 0.5f, h.rightAlt = 0.3f; // the right one first
    mcHudRects(h, q);
    const float other[][5] = {{0.25f, 0.12f, 0.72f, 0.3f, MC_FAR},
                              {0.25f, 0.3f, 1.02f, 0.5f, MC_FAR},
                              {0, 0.5f, 1.02f, 0.78f, MC_FAR},
                              {0, 0, 1.02f, 0.12f, MC_FAR}};
    CHECK(same(q, other, 4));
    h.leftAlt = h.rightAlt = 0.4f; // at the same height
    mcHudRects(h, q);
    const float level[][5] = {
        {0.25f, 0.12f, 0.72f, 0.4f, MC_FAR}, {0, 0.4f, 1.02f, 0.78f, MC_FAR}, {0, 0, 1.02f, 0.12f, MC_FAR}};
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
    const float a[5] = {0.7f, 0, 1.0f, 0.43f, 9}, left[5] = {0.5f, 0, 0.72f, 0.6f, 9},
                edge[5] = {0.5f, 0, 0.721f, 0.6f, 9};
    const float unset[5] = {0, 0, 0, 0, 9}, flat[5] = {0.8f, 0.5f, 0.9f, 0.5f, 9}, thin[5] = {0.9f, 0, 0.9f, 0.9f, 9};
    CHECK(mcRightAlt(a, 0.72f, 0) == 0.43f);
    CHECK(mcRightAlt(a, 0.72f, 0.5f) == 0.5f); // another reached further already
    CHECK(mcRightAlt(a, 0.72f, 0.43f) == 0.43f);
    CHECK(mcRightAlt(left, 0.72f, 0.1f) == 0.1f); // ends at the column's edge: not in it
    CHECK(mcRightAlt(edge, 0.72f, 0.1f) == 0.6f);
    CHECK(mcRightAlt(unset, 0.72f, 0.1f) == 0.1f && mcRightAlt(flat, 0.72f, 0.1f) == 0.1f &&
          mcRightAlt(thin, 0.72f, 0.1f) == 0.1f);
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
    const float want[][5] = {
        {0, 0.52f, 1.02f, 0.78f, MC_FAR}, // the middle and the columns below, left and right of the window and its
                                          // frame
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
    CHECK(!mcHolds(r, 0.2f, 0.5f) && !mcHolds(r, 0.6f, 0.5f) && !mcHolds(r, 0.4f, 0.3f) &&
          !mcHolds(r, 0.4f, 0.9f)); // on an edge
    CHECK(!mcHolds(r, 0.1f, 0.5f) && !mcHolds(r, 0.7f, 0.5f) && !mcHolds(r, 0.4f, 0.2f) && !mcHolds(r, 0.4f, 0.95f));
}

static void testTurnNorm() {
    g_test = "turn norm";
    CHECK(mcTurnNorm(0) == 0 && mcTurnNorm(5) == 5 && mcTurnNorm(-5) == -5);
    CHECK(mcTurnNorm(180) == 180 && mcTurnNorm(-180) == 180 && mcTurnNorm(185) == -175 && mcTurnNorm(-185) == 175);
    CHECK(mcTurnNorm(360) == 0 && mcTurnNorm(725) == 5 && mcTurnNorm(-725) == -5 && mcTurnNorm(540) == 180);
    CHECK(mcTurnNorm(179) == 179 && mcTurnNorm(-179) == -179 && mcTurnNorm(180.5f) == -179.5f &&
          mcTurnNorm(-180.5f) == 179.5f);
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
    CHECK(nearly(m[0], 0, e) && nearly(m[1], 1, e) && nearly(m[2], -1, e) &&
          nearly(m[3], 0, e)); // the monitor's top is the part's right
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
        // undistorted: the monitor's right and down (in metres: mon, 1) are as long in the part (in metres: part, 1)
        // and square
        float rx = m[0] / mon * part, ry = m[2] / mon, dx = m[1] * part, dy = m[3];
        CHECK(nearly(rx * rx + ry * ry, dx * dx + dy * dy, 1e-4f) && nearly(rx * dx + ry * dy, 0, 1e-4f));
        CHECK(t ? (m[1] < 0 && m[2] > 0) : (m[1] > 0 && m[2] < 0)); // clockwise for positive degrees
    }
}

static void testHudGlass() {
    g_test = "hud glass";
    McHud h = hud();
    h.screens = true;
    const float target[4] = {0, 0.772f, 0.18f, 1},
                wide[4] = {0.125f, 0.25f, 0.75f, 0.625f}; // (wide holds the stale window's middle)
    memcpy(h.glass[0], target, sizeof target), memcpy(h.glass[1], wide, sizeof wide);
    h.nGlass = 2;
    setCam(h, 0.01f, 0.80f, 0.07f, 0.98f); // the target's view, in the target part
    setCam(h, 0.80f, 0.60f, 0.98f, 0.76f); // one in no part
    setCam(h, 0, 0.3f, 0.25f, 0.5f);       // one whose middle lies on the second part's edge
    McRects q;
    mcHudRects(h, q);
    CHECK(q.n == 9); // the six without glass, then:
    const float want[][5] = {{0, 0.772f, 0.18f, 1, MC_GLASS},
                             {0.01f, 0.80f, 0.07f, 0.98f, MC_GLASS_OPAQUE},
                             {0.125f, 0.25f, 0.75f, 0.625f, MC_GLASS + 1}};
    for (int i = 0; i < 3 && q.n == 9; ++i)
        for (int k = 0; k < 5; ++k) CHECK(nearly(q.rc[6 + i][k], want[i][k], 1e-6f));
    h.nCam = 0;
    setCam(h, 0.20f, 0.27f, 0.28f, 0.59f); // a window in the second part, off its middle both ways
    h.cam[0][0] = 0.14f;                   // (left of the zoom window's range)
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
    const float want[][3] = {{0.70477f, 0.51303f, -1.22070f},
                             {-0.70477f, 0.51303f, -1.22070f},
                             {0.70477f, -0.51303f, -1.22070f},
                             {0.93133f, 1.14907f, -0.24955f}};
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
    const float f1[3] = {0.5f, 0, -0.8660254f}, f2[3] = {0, 0.3420201f, -0.9396926f}, f3[3] = {0.5f, 0.5f, -0.7071068f},
                f4[3] = {-0.5f, -0.5f, -0.7071068f};
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
            CHECK(t > 0.5f && nearly(d[0], t * f[0], 1e-3f) && nearly(d[1], t * f[1], 1e-3f) &&
                  nearly(d[2], t * f[2], 1e-3f));
        }
    const float out[3] = {0, 0, -3},
                right[3] = {1, 0, 0}; // a head outside the glass, looking past it: where the head is
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
    mcGridMark(g_grid, -1, 0, 1), mcGridMark(g_grid, MC_GW, 0, 1), mcGridMark(g_grid, 0, -1, 1),
        mcGridMark(g_grid, 0, MC_GH, 1);
    mcGridMark(g_grid, 3, -1, 1), mcGridMark(g_grid, 3, MC_GH, 1), mcGridMark(g_grid, -1, 9, 1),
        mcGridMark(g_grid, MC_GW, 9, 1);
    CHECK(marked() == 3 && around() && cell(0, 0) == 2 && cell(MC_GW - 1, 0) == 0 && cell(0, MC_GH - 1) == 0 &&
          cell(1, 0) == 0);
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
    CHECK(mcGridSide(1, 1, 3, 2, 2, 1.5f) == 0 && mcGridSide(1, 1, 3, 2, 2, 2) == 1 &&
          mcGridSide(1, 1, 3, 2, 2, 1) == -1);
    CHECK(mcGridSide(1, 1, 3, 2, 5, 3.5f) == 1 && mcGridSide(3, 2, 1, 1, 5, 3.5f) == -1);
}

// the cells a triangle with the corners (cells) x, y must mark: those of its corners and those whose direction it
// covers
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
    const float shapes[][6] = {{30.3f, 41.7f, 36.4f, 20.2f, 23.8f, 27.6f},
                               {50.5f, 55.3f, 60.7f, 10.6f, 14.2f, 12.4f},
                               {20.4f, 20.7f, 21.3f, 30.2f, 36.8f, 33.1f}};
    const int order[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (int s = 0; s < 3; ++s)
        for (int o = 0; o < 6; ++o) {
            float x[3], y[3];
            for (int k = 0; k < 3; ++k)
                x[k] = shapes[s][order[o][k]], y[k] = shapes[s][3 + order[o][k]], at(x[k], y[k], 1 + k, v[k]);
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
    CHECK(cell(40, 25) > 0 && cell(40, 20) > 0 && cell(40, 30) > 0 && cell(45, 25) > 0 && cell(42, 22) > 0 &&
          cell(42, 27) > 0);
    CHECK(cell(39, 25) == 0 && cell(40, 31) == 0 && cell(40, 18) == 0 && cell(46, 25) == 0 && cell(44, 22) == 0 &&
          cell(44, 28) == 0);
    // one with a corner beside or behind the seat: left out, whichever corner it is
    const float s0[4] = {0.3f, 0, 1, -1}, s1[4] = {0.3f, 0.3f, 1, -1}, s2[4] = {1, 0, 0.05f, -1},
                s3[4] = {1, 0, -1, -1}, s4[4] = {1, 0, 0.06f, -1};
    const float s5[4] = {0.3f, 0, 0.06f, 1}, s6[4] = {0.3f, 0.02f, 0.06f, 1}, s7[4] = {0.31f, 0, 0.06f, 1};
    gridClear();
    mcGridTri(g_grid, s0, s1, s2), mcGridTri(g_grid, s0, s3, s1), mcGridTri(g_grid, s3, s0, s1),
        mcGridTri(g_grid, s2, s0, s1), mcGridTri(g_grid, s0, s2, s1);
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
    CHECK(!mcGridFree(g_grid, -71, 0, 20, 10) && !mcGridFree(g_grid, 71, 0, 20, 10) &&
          !mcGridFree(g_grid, 0, -46, 20, 10) && !mcGridFree(g_grid, 0, 46, 20, 10));
    const int in[][2] = {
        {35, 22}, {45, 22}, {35, 27}, {45, 27}, {40, 25}}; // the corner cells of -10..10 x -5..5, and one inside
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
    mcGridMark(g_grid, 31, 25, 9), mcGridMark(g_grid, 49, 25, 9), mcGridMark(g_grid, 40, 18, 9),
        mcGridMark(g_grid, 40, 31, 9); // four out
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
    CHECK(mcGridFit(g_grid, 20, 10, 7.1f, -2.9f, 1000, nullptr, 0, &r, &u) && r == 8 &&
          u == -2); // open everywhere: the nearest direction of the grid
    CHECK(mcGridFit(g_grid, 20, 10, 6.9f, -3.1f, 1000, nullptr, 0, &r, &u) && r == 6 && u == -4);
    CHECK(mcGridFit(g_grid, 20, 10, 7, -3, 1000, nullptr, 0, &r, &u) && r == 6 &&
          u == -4); // between four: the first met
    CHECK(mcGridFit(g_grid, 20, 10, -200, 0, 1000, nullptr, 0, &r, &u) && r == -70 &&
          u == 0); // wanted outside: the edge
    CHECK(mcGridFit(g_grid, 20, 10, 0, 200, 1000, nullptr, 0, &r, &u) && r == 0 && u == 44);
    CHECK(mcGridFit(g_grid, 2, 2, -200, -200, 1000, nullptr, 0, &r, &u) && r == -78 &&
          u == -48); // the grid's second row and column
    CHECK(mcGridFit(g_grid, 2, 2, 200, 200, 1000, nullptr, 0, &r, &u) && r == 78 && u == 48); // and its last
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
    CHECK(mcGridFit(g_grid, 20, 10, -10, 1, 1000, taken, 3, &r, &u) && r == -10 &&
          u == 20); // below it the third: above the second
    CHECK(mcGridFit(g_grid, 20, 4, -10, 12, 1000, taken, 1, &r, &u) && r == -10 && u == 12); // touching is clear
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
    CHECK(mcSheet(MC_SHEET_ALPHA - 1, 7, 0, 0) == MC_SHEET_COLOUR);
    // the menu's sheet is told while it fades in, from the least alpha a point can show
    CHECK(mcSheet(MC_SHEET_ALPHA - 1, 0, 0, 7) == MC_SHEET_MENU);
    CHECK(mcSheet(MC_SHEET_MIN, 0, 0, 1) == MC_SHEET_MENU);
    CHECK(mcSheet(MC_SHEET_MIN - 1, 0, 0, 1) == MC_SHEET_NONE);
    // blue, but not alone: no sheet of ours - and no fade, though red and green are black against the alpha
    CHECK(mcSheet(255, 5, 0, 17) == MC_SHEET_COLOUR);
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
    CHECK(mcSheet(16, 2, 0, 0) == MC_SHEET_COLOUR);
    CHECK(mcSheet(16, 0, 2, 0) == MC_SHEET_COLOUR);
    CHECK(mcSheet(16, 0, 0, 2) == MC_SHEET_MENU);
    CHECK(mcSheet(128, 0, 0, 59) == MC_SHEET_MENU);   // (as measured: the paused battle's sheet)
    CHECK(mcSheet(255, 0, 60, 0) == MC_SHEET_COLOUR); // (light amplification)
    // blue alone: red and green each at most a quarter of it
    CHECK(mcSheet(255, 5, 0, 20) == MC_SHEET_MENU);
    CHECK(mcSheet(255, 0, 5, 20) == MC_SHEET_MENU);
    CHECK(mcSheet(255, 6, 0, 20) == MC_SHEET_COLOUR);
    CHECK(mcSheet(255, 0, 6, 20) == MC_SHEET_COLOUR);
    CHECK(mcSheet(255, 20, 0, 20) == MC_SHEET_COLOUR);
    CHECK(mcSheet(255, 0, 20, 20) == MC_SHEET_COLOUR);
    // a coloured sheet hardly there is one all the same (the menu's sheet fades in from nothing)
    CHECK(mcSheet(MC_SHEET_MIN, 1, 0, 0) == MC_SHEET_COLOUR && mcSheet(MC_SHEET_MIN, 0, 1, 0) == MC_SHEET_COLOUR);
    CHECK(mcSheet(MC_SHEET_MIN - 1, 1, 1, 0) == MC_SHEET_NONE);
    CHECK(mcSheet(128, 64, 73, 74) == MC_SHEET_COLOUR); // (as measured: the jungle's)
    // whose it is: blue the menu's always; coloured the menu's after an opening Esc and for as long as it lasts,
    // after a closing one only for a while; anything else never
    for (int was = 0; was < 2; ++was)
        for (int opens = 0; opens < 2; ++opens)
            for (int lately = 0; lately < 2; ++lately) {
                CHECK(mcSheetIsMenu(MC_SHEET_MENU, was, opens, lately));
                CHECK(!mcSheetIsMenu(MC_SHEET_NONE, was, opens, lately));
                CHECK(!mcSheetIsMenu(MC_SHEET_FADE, was, opens, lately));
                bool menu = opens ? was || lately : was && lately;
                CHECK(mcSheetIsMenu(MC_SHEET_COLOUR, was, opens, lately) == menu);
            }
    CHECK(mcSheetIsMenu(MC_SHEET_COLOUR, false, true, true) && !mcSheetIsMenu(MC_SHEET_COLOUR, false, true, false));
    CHECK(mcSheetIsMenu(MC_SHEET_COLOUR, true, true, false) && mcSheetIsMenu(MC_SHEET_COLOUR, true, false, true));
    CHECK(!mcSheetIsMenu(MC_SHEET_COLOUR, true, false, false) && !mcSheetIsMenu(MC_SHEET_COLOUR, false, false, true));
}

static void testCockpitFile() {
    // a monitor's number: n + 1 for 0 .. screens - 1
    CHECK(mcScreen("0", 8) == 1);
    CHECK(mcScreen("7", 8) == 8);
    CHECK(mcScreen("9", 12) == 10);
    CHECK(mcScreen("12", 20) == 13);
    CHECK(mcScreen("5", 5) == 0); // (from `screens` on: none)
    CHECK(mcScreen("8", 8) == 0);
    // no number, or a sign before it - the number below zero that read before the table
    CHECK(mcScreen("", 8) == 0);
    CHECK(mcScreen("-5", 8) == 0);
    CHECK(mcScreen("+1", 8) == 0);
    CHECK(mcScreen("x", 8) == 0);
    CHECK(mcScreen("/", 8) == 0); // (the letters beside the digits)
    CHECK(mcScreen(":", 8) == 0);
    CHECK(mcScreen(":", 20) == 0); // (the letter after '9' is no ten)
    // what follows the digits does not count; of many digits the first three (so the number cannot run over)
    CHECK(mcScreen("3 left", 8) == 4);
    CHECK(mcScreen("1234", 2000) == 124);
    CHECK(mcScreen("99999999999999999999", 8) == 0);

    // a cockpit's name
    CHECK(mcNameOk(L"a"));
    CHECK(mcNameOk(L"madcat"));
    CHECK(mcNameOk(L"mad cat"));
    CHECK(mcNameOk(L"mad.cat_2-b"));
    CHECK(!mcNameOk(L""));
    CHECK(!mcNameOk(L"/"));
    CHECK(!mcNameOk(L"..\\..\\Windows\\win"));
    CHECK(!mcNameOk(L".."));
    CHECK(!mcNameOk(L"a..b"));
    CHECK(mcNameOk(L".a.b.")); // (single dots are a name's)
    const wchar_t *bad[] = {L"a\\b",
                            L"a/b",
                            L"c:a",
                            L"a*",
                            L"a?",
                            L"a\"b",
                            L"a<b",
                            L"a>b",
                            L"a|b",
                            L"a\x1f"
                            L"b",
                            L"a\tb"};
    for (const wchar_t *b : bad) CHECK(!mcNameOk(b));
    wchar_t n63[65];
    for (int i = 0; i < 64; ++i) n63[i] = L'x';
    n63[64] = 0;
    CHECK(!mcNameOk(n63)); // 64 letters
    n63[63] = 0;
    CHECK(mcNameOk(n63)); // 63

    // a part's size
    CHECK(mcGroupBytes(10, 30, 4, 2) == 10 * 32 + 30 * 4 + 4 * 4 * 6 + 2 * 2 * 6);
    CHECK(mcGroupBytes(10, 30, 4, 0) == 320 + 120 + 96); // (no second picture)
    CHECK(mcGroupBytes(1, 3, 1, 0) == 32 + 12 + 6);      // the least
    CHECK(mcGroupBytes(MC_GROUP_VERTS, MC_GROUP_INDICES, MC_GROUP_TEX, MC_GROUP_TEX) ==
          MC_GROUP_VERTS * 32u + MC_GROUP_INDICES * 4u + 2u * MC_GROUP_TEX * MC_GROUP_TEX * 6u); // the most
    CHECK(mcGroupBytes(0, 30, 4, 2) == 0);
    CHECK(mcGroupBytes(MC_GROUP_VERTS + 1, 30, 4, 2) == 0);
    CHECK(mcGroupBytes(10, 2, 4, 2) == 0);
    CHECK(mcGroupBytes(10, MC_GROUP_INDICES + 1, 4, 2) == 0);
    CHECK(mcGroupBytes(10, 30, 0, 2) == 0);
    CHECK(mcGroupBytes(10, 30, MC_GROUP_TEX + 1, 2) == 0);
    CHECK(mcGroupBytes(10, 30, 4, MC_GROUP_TEX + 1) == 0);
}

// ---------------------------------------------------------------- the right eye's view from the left eye's
static bool nearlyD(double a, double b, double eps = 1e-12) { return fabs(a - b) <= eps; }
static bool sameM4(const McM4 &a, const double *want, double eps = 1e-12) {
    for (int i = 0; i < 16; ++i)
        if (!nearlyD(a.m[i], want[i], eps)) return false;
    return true;
}
static void testM4() {
    g_test = "m4";
    const McM4 a = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}},
               b = {{2, 0, 1, 3, 1, 3, 0, -2, 0, 1, 2, 5, 4, -1, 3, 1}};
    const double ab[16] = {20, 5, 19, 18, 48, 17, 43, 46, 76, 29, 67, 74, 104, 41, 91, 102},
                 ba[16] = {50, 56, 62, 68, -10, -8, -6, -4, 88, 96, 104, 112, 39, 46, 53, 60};
    CHECK(sameM4(mcM4Mul(a, b), ab, 0));
    CHECK(sameM4(mcM4Mul(b, a), ba, 0));
    const float c[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    const double c4[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 0, 0, 1};
    CHECK(sameM4(mcM4From34(c), c4, 0));
    const float x[3] = {1, 2, 3}, y[3] = {4, -5, 6};
    CHECK(mcDot3d(x, y) == 12);
    // the largest difference, whichever way and wherever
    McM4 d = a;
    CHECK(mcM4Apart(a, d) == 0);
    d.m[0] += 0.5, d.m[7] -= 2, d.m[15] += 1;
    CHECK(mcM4Apart(a, d) == 2 && mcM4Apart(d, a) == 2);
    d = a, d.m[15] += 3;
    CHECK(mcM4Apart(a, d) == 3);
    d = a, d.m[0] -= 4;
    CHECK(mcM4Apart(a, d) == 4);
    // floats times doubles, either way round
    float f[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
          g[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    float f17[18], g17[18]; // nothing is written before or past the sixteen
    f17[0] = g17[0] = f17[17] = g17[17] = -77;
    memcpy(f17 + 1, f, sizeof f), memcpy(g17 + 1, g, sizeof g);
    mcM4Left(b, f17 + 1), mcM4Right(b, g17 + 1);
    for (int i = 0; i < 16; ++i) CHECK(f17[i + 1] == (float)ba[i] && g17[i + 1] == (float)ab[i]);
    CHECK(f17[0] == -77 && g17[0] == -77 && f17[17] == -77 && g17[17] == -77);
    float kf[16], af[16], ff[18];
    ff[0] = ff[17] = -77;
    for (int i = 0; i < 16; ++i) kf[i] = (float)b.m[i], af[i] = (float)a.m[i];
    mcM4MulF(kf, af, ff + 1);
    for (int i = 0; i < 16; ++i) CHECK(ff[i + 1] == (float)ba[i]);
    CHECK(ff[0] == -77 && ff[17] == -77);
    float pt[4] = {2, 3, 5, -77};
    const McM4 aff = {{2, 7, 1, 10, 4, 3, 6, 20, 1, 8, 9, 30, 9, 9, 9, 9}};
    mcM4Point(aff, pt);
    CHECK(pt[0] == 40 && pt[1] == 67 && pt[2] == 101 && pt[3] == -77);
}
static void testM4Inv() {
    g_test = "m4Inv";
    const McM4 m = {{2, 1, 0, 3, 1, 1, 0, -2, 0, 0, 1, 5, 0, 0, 0, 1}},
               m2 = {{0, 1, 0, 1, 1, 0, 0, 2, 0, 0, 2, 4, 0, 0, 0, 1}},
               g = {{1, 2, 0, 1, 0, 1, 3, 2, 2, 0, 1, -3, 0, 0, 0, 1}};
    const double mi[16] = {1, -1, 0, -5, -1, 2, 0, 7, 0, 0, 1, -5, 0, 0, 0, 1},
                 m2i[16] = {0, 1, 0, -2, 1, 0, 0, -1, 0, 0, 0.5, -2, 0, 0, 0, 1},
                 gi[16] = {0.076923076923076927,
                           -0.15384615384615385,
                           0.46153846153846156,
                           1.6153846153846154,
                           0.46153846153846156,
                           0.076923076923076927,
                           -0.23076923076923078,
                           -1.3076923076923077,
                           -0.15384615384615385,
                           0.30769230769230771,
                           0.076923076923076927,
                           -0.23076923076923078,
                           0,
                           0,
                           0,
                           1};
    McM4 o;
    CHECK(mcM4InvAffine(m, &o) && sameM4(o, mi, 0));
    CHECK(mcM4InvAffine(m2, &o) && sameM4(o, m2i, 0)); // (a negative determinant)
    CHECK(mcM4InvAffine(g, &o) && sameM4(o, gi));
    const double id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    CHECK(sameM4(mcM4Mul(g, o), id) && sameM4(mcM4Mul(o, g), id));
    // none: flat; and just under the limit - at it there is one
    McM4 flat = {{1, 2, 3, 0, 2, 4, 6, 0, 0, 0, 1, 0, 0, 0, 0, 1}},
         keep = {{7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7}};
    o = keep;
    CHECK(!mcM4InvAffine(flat, &o) && sameM4(o, keep.m, 0));
    McM4 tiny = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, MC_DET_MIN, 0, 0, 0, 0, 1}};
    CHECK(mcM4InvAffine(tiny, &o) && o.m[10] == 1 / MC_DET_MIN);
    tiny.m[10] = -MC_DET_MIN;
    CHECK(mcM4InvAffine(tiny, &o) && o.m[10] == -1 / MC_DET_MIN);
    tiny.m[10] = MC_DET_MIN / 2;
    CHECK(!mcM4InvAffine(tiny, &o));
    tiny.m[10] = -MC_DET_MIN / 2;
    CHECK(!mcM4InvAffine(tiny, &o));
}
static void testProj() {
    g_test = "proj";
    float w[16] = {0.25, 1.5, 0, -8.25, -0.5, 0, 2, -12.5, 0.125, 0, 0, 0.375, 1, 0, 0, -3};
    McProj p = mcProjFrom(w);
    CHECK(p.ok && p.p00 == 1.5 && p.p11 == 2 && p.j0 == 0.25 && p.j1 == -0.5 && p.a == 0.125 && p.b == 0.75);
    const double pm[16] = {1.5, 0, 0.25, 0, 0, 2, -0.5, 0, 0, 0, 0.125, 0.75, 0, 0, 1, 0};
    const double id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    CHECK(sameM4(mcProjM(p), pm, 0));
    CHECK(sameM4(mcM4Mul(mcProjM(p), mcProjInv(p)), id) && sameM4(mcM4Mul(mcProjInv(p), mcProjM(p)), id));
    const McProj q = {2, 4, 0.5, -0.25, 0.125, 0.5, true};
    const double qi[16] = {0.5, 0, 0, -0.25, 0, 0.25, 0, 0.0625, 0, 0, 0, 1, 0, 0, 2, -0.25};
    CHECK(sameM4(mcProjInv(q), qi, 0));
    // not a world -> clip matrix: the w row is no unit vector (at the limit it still is one)
    float v[16];
    memcpy(v, w, sizeof v), v[12] = 1 + (float)MC_PROJ_UNIT;
    CHECK(mcProjFrom(v).ok);
    v[12] = 1 + 2 * (float)MC_PROJ_UNIT;
    CHECK(!mcProjFrom(v).ok && mcProjFrom(v).p00 == 0);
    v[12] = 1 - (float)MC_PROJ_UNIT;
    CHECK(mcProjFrom(v).ok);
    v[12] = 1 - 2 * (float)MC_PROJ_UNIT;
    CHECK(!mcProjFrom(v).ok);
    // scales and the near term at their limits: not enough; just above: enough
    const float lim = (float)MC_PROJ_MIN, nearLim = (float)MC_PROJ_NEAR;
    float u[16] = {0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0};
    CHECK(mcProjFrom(u).ok && mcProjFrom(u).p00 == 1 && mcProjFrom(u).p11 == 1 && mcProjFrom(u).b == 1);
    u[1] = lim;
    CHECK(!mcProjFrom(u).ok);
    u[1] = lim * 1.5f;
    CHECK(mcProjFrom(u).ok);
    u[1] = 1, u[6] = lim;
    CHECK(!mcProjFrom(u).ok);
    u[6] = lim * 1.5f;
    CHECK(mcProjFrom(u).ok);
    u[6] = 1, u[11] = nearLim;
    CHECK(!mcProjFrom(u).ok);
    u[11] = 0.0001220703125f; // (2^-13, whatever the constant says)
    CHECK(!mcProjFrom(u).ok);
    u[11] = 0.000123f;
    CHECK(mcProjFrom(u).ok);
    u[11] = -nearLim;
    CHECK(!mcProjFrom(u).ok);
    u[11] = nearLim * 1.5f;
    CHECK(mcProjFrom(u).ok);
    u[11] = -nearLim * 1.5f;
    CHECK(mcProjFrom(u).ok && mcProjFrom(u).b < 0);
}
static void testEyeStep() {
    g_test = "eyeStep";
    const float r0[9] = {0, -1, 0, 1, 0, 0, 0, 0, 1}, r1[9] = {1, 0, 0, 0, 0, -1, 0, 1, 0}, t0[3] = {1, 2, 3},
                t1[3] = {0.5, -1, 4};
    const double h[16] = {0, -1, 0, 0.5, 0, 0, 1, -1, -1, 0, 0, -3, 0, 0, 0, 1};
    CHECK(sameM4(mcEyeStep(r0, t0, r1, t1), h, 0));
    const double id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    CHECK(sameM4(mcEyeStep(r0, t0, r0, t0), id, 0)); // one eye: no step
}
static void testStereo() {
    g_test = "stereo";
    const McProj p = {1.5, 2, 0.25, -0.5, 0.125, 0.75, true};
    const float c0[12] = {0, 0, 1, 100, 1, 0, 0, 20, 0, 1, 0, -50},
                c1[12] = {-0.28, 0, 0.96, 99.984375, 0.96, 0, 0.28, 20.0625, 0, 1, 0, -49.96875};
    const McM4 hv = {{0.96000000000000008, 0, -0.28000000000000008, -0.064375000000000002, 0, 1, 0, -0.03125,
                      0.28000000000000008, 0, 0.95999999999999996, -0.0025000000000000044, 0, 0, 0, 1}};
    const double kc[16] = {1.0066666666666668,    0, -0.12958333333333333,   -0.41546875000000022,
                           -0.093333333333333351, 1, -0.081666666666666665,  0.053541666666666668,
                           0.023333333333333338,  0, 0.99958333333333327,    -0.010781249999999985,
                           0.1866666666666667,    0, -0.0033333333333333392, 0.91374999999999995};
    const double kci[16] = {0.91333333333333322,
                            0,
                            0.11979166666666666,
                            0.41669270833333344,
                            0.093333333333333338,
                            1,
                            0.09375,
                            -0.015052083333333299,
                            -0.023333333333333334,
                            0,
                            0.99739583333333326,
                            0.001158854166666665,
                            -0.18666666666666668,
                            0,
                            -0.020833333333333332,
                            1.0092708333333333};
    const double kct[16] = {
        1.0066666666666668,   0, 0, -0.43166666666666687,  -0.093333333333333351, 1, 0, 0.043333333333333335,
        0.023333333333333338, 0, 1, -0.010833333333333327, 0.1866666666666667,    0, 0, 0.91333333333333333};
    const double kw[16] = {0.95999999999999996,
                           0.28000000000000008,
                           0,
                           -1.602499999999992,
                           -0.28000000000000008,
                           0.96000000000000008,
                           0,
                           28.735624999999999,
                           0,
                           0,
                           1,
                           -0.03125,
                           0,
                           0,
                           0,
                           1};
    const double kwi[16] = {0.95999999999999996,
                            -0.28000000000000003,
                            0,
                            9.5843750000000085,
                            0.28000000000000003,
                            0.95999999999999996,
                            0,
                            -27.137500000000003,
                            0,
                            0,
                            1,
                            0.03125,
                            0,
                            0,
                            0,
                            1};
    McStereo s;
    CHECK(mcStereo(p, hv, c0, c1, &s));
    CHECK(sameM4(s.kc, kc, 1e-9) && sameM4(s.kcInv, kci, 1e-9) && sameM4(s.kcTurn, kct, 1e-9));
    CHECK(sameM4(s.kw, kw, 1e-4) && sameM4(s.kwInv, kwi, 1e-4));
    // the two ways to the right eye's world -> clip matrix meet
    const McM4 w = {{0.25, 1.5, 0, -55, -0.5, 0, 2, 150, 0.125, 0, 0, -11.75, 1, 0, 0, -100}};
    CHECK(mcM4Apart(mcM4Mul(s.kc, w), mcM4Mul(w, s.kw)) < 1e-4);
    // none without a projection, or with a matrix that has no inverse
    McProj bad = p;
    bad.ok = false;
    CHECK(!mcStereo(bad, hv, c0, c1, &s));
    McM4 flat = hv;
    for (int i = 0; i < 12; ++i) flat.m[i] = 0;
    CHECK(!mcStereo(p, flat, c0, c1, &s));
    const float zero[12] = {};
    CHECK(!mcStereo(p, hv, zero, c1, &s));
    CHECK(!mcStereo(p, hv, c0, zero, &s));
}

// ---------------------------------------------------------------- which way the right eye is made
// n frames of one length and wait; the last step's answer, and that every step before it said "nothing"
static int autoRun(McAuto &a, int n, double frame, double wait, double pass = 0) {
    int r = MC_AUTO_NONE;
    for (int i = 0; i < n; ++i) {
        if (r != MC_AUTO_NONE) return -1;
        r = mcAutoStep(a, frame, wait, pass);
    }
    return r;
}
static void testAuto() {
    g_test = "auto";
    // a battle begins with replay, and a rest
    McAuto a = mcAutoStart();
    CHECK(a.replay && !a.trying && a.rest == MC_AUTO_START && a.n == 0 && a.sum == 0 && a.waits == 0 && a.pause == 0);
    CHECK(a.passes == 0);
    CHECK(autoRun(a, MC_AUTO_START, 30, 0.1) == MC_AUTO_NONE && a.rest == 0 && a.n == 0);
    a.replay = false; // (the rest of this test: from the game's own way)
    // the game draws both eyes, slow, and the graphics card is not what it waits for: replay is tried
    CHECK(autoRun(a, MC_AUTO_WINDOW - 1, 30, 0.1) == MC_AUTO_NONE && !a.replay && !a.trying);
    CHECK(mcAutoStep(a, 30, 0.1, 9) == MC_AUTO_TRY && a.replay && a.trying && a.before == 30 && a.n == 0 && a.sum == 0);
    CHECK(a.passes == 0);
    // the first frames of the trial do not count (very slow here); the rest are faster by more than a sixteenth
    CHECK(autoRun(a, MC_AUTO_SETTLE, 500, 0.1) == MC_AUTO_NONE);
    CHECK(autoRun(a, MC_AUTO_TRIAL - MC_AUTO_SETTLE - 1, 28, 0.1) == MC_AUTO_NONE && a.trying);
    CHECK(mcAutoStep(a, 28, 0.1, 0) == MC_AUTO_KEEP && a.replay && !a.trying && a.rest == MC_AUTO_KEPT_REST &&
          a.pause == 0);
    CHECK(a.after == 28 && a.before == 30);
    CHECK(a.n == 0 && a.sum == 0);
    // rest: no trial, whatever the wait is (frames much longer than at the trial end it: testAutoChange)
    CHECK(autoRun(a, MC_AUTO_KEPT_REST, 40, 9) == MC_AUTO_NONE && a.rest == 0 && a.n == 0 && a.sum == 0 &&
          a.waits == 0);
    // replay, slow, the graphics card is the limit: the game's own way is tried; it is no faster - back
    CHECK(autoRun(a, MC_AUTO_WINDOW, 20, 5) == MC_AUTO_TRY && !a.replay && a.before == 20);
    CHECK(autoRun(a, MC_AUTO_TRIAL, 20, 5) == MC_AUTO_BACK && a.replay && !a.trying);
    CHECK(a.pause == MC_AUTO_PAUSE && a.rest == MC_AUTO_PAUSE);
    // and after the rest again, with the same end: twice the rest, up to the longest
    unsigned want = MC_AUTO_PAUSE;
    for (int i = 0; i < 6; ++i) {
        CHECK(autoRun(a, (int)a.rest, 20, 5) == MC_AUTO_NONE);
        CHECK(autoRun(a, MC_AUTO_WINDOW, 20, 5) == MC_AUTO_TRY);
        CHECK(autoRun(a, MC_AUTO_TRIAL, 20, 5) == MC_AUTO_BACK);
        want = want * 2 > MC_AUTO_PAUSE_MAX ? (unsigned)MC_AUTO_PAUSE_MAX : want * 2;
        CHECK(a.pause == want && a.rest == want);
    }
    CHECK(a.pause == MC_AUTO_PAUSE_MAX);
    // a trial kept forgets the pauses
    a.rest = 0;
    CHECK(autoRun(a, MC_AUTO_WINDOW, 20, 5) == MC_AUTO_TRY);
    CHECK(autoRun(a, MC_AUTO_TRIAL, 10, 5) == MC_AUTO_KEEP && !a.replay && a.pause == 0);
}
// no trial where the other way cannot help; the limits themselves
// the way a battle begins with: by its mission
static void testAutoMission() {
    CHECK(mcAutoStart().replay && mcAutoStart(true).replay && !mcAutoStart(false).replay);
    CHECK(mcAutoStart(false).rest == MC_AUTO_START && !mcAutoStart(false).trying);
    // the list: every one of its missions, and none beside them
    static const char *const game[] = {
        "avalon01_spaceport",   "colsm02_medium",       "colsm04_assault",       "concourse pro",
        "exford01_junglerecon", "exford02_stormpatrol", "exford03_beachfight",   "exford04_backstab",
        "fact01_light",         "fact03_heavy",         "fact05_championship",   "hallor02_checkpoint",
        "hesp03_assault",       "hesp04_stealth",       "jungle01_light",        "jungle02_medium",
        "jungle03_heavy",       "jungle04_assault",     "jungle05_championship", "styk01_reinforce",
        "styk02_peace_talks",   "styk04_assassination", "talon04_countrylife"};
    CHECK(sizeof MC_GAME_DRAWS / sizeof *MC_GAME_DRAWS == 23 && sizeof game / sizeof *game == 23);
    for (const char *m : game) CHECK(mcGameDraws(m));
    CHECK(!mcGameDraws("eaton03_bandithideaway") && !mcGameDraws("thark01_overlord") && !mcGameDraws(""));
    CHECK(!mcGameDraws("jungle01") && !mcGameDraws("jungle01_light2") && !mcGameDraws("Jungle01_light"));

    // the mission in a text: after the mark, up to the next backslash, in small letters
    const char *mark = "|missions\\";
    char out[16], big[32];
    const char *one = "x: resource\\missions\\hallor04.mw4|missions\\Hallor04_IndustryR\\a.lights\n";
    memset(big, 'x', sizeof big), big[31] = 0;
    CHECK(mcMissionOf(one, strlen(one), mark, big, sizeof big) && strcmp(big, "hallor04_industryr") == 0);
    strcpy(out, "kept"); // (18 letters: too long for out)
    CHECK(!mcMissionOf(one, strlen(one), mark, out, sizeof out) && strcmp(out, "kept") == 0);
    // (a name as long as out has room for but for the end: not taken; one shorter: taken)
    const char *n15 = "|missions\\abcdefghijklmno\\", *n16 = "|missions\\abcdefghijklmnop\\";
    CHECK(mcMissionOf(n15, strlen(n15), mark, out, sizeof out) && strcmp(out, "abcdefghijklmno") == 0);
    strcpy(out, "kept");
    CHECK(!mcMissionOf(n16, strlen(n16), mark, out, sizeof out) && strcmp(out, "kept") == 0);
    // only letters A..Z are made small
    const char *edge = "|missions\\@AZ[`az{ 9\\";
    CHECK(mcMissionOf(edge, strlen(edge), mark, out, sizeof out) && strcmp(out, "@az[`az{ 9") == 0);
    // the last mark decides: a whole name after it, or none - not ended, a line's end first, no name at all,
    // nothing behind the mark
    const char *two = "|missions\\first\\x\n|missions\\second\\y\n";
    CHECK(mcMissionOf(two, strlen(two), mark, out, sizeof out) && strcmp(out, "second") == 0);
    const char *cut = "|missions\\first\\x\n|missions\\secon";
    CHECK(!mcMissionOf(cut, strlen(cut), mark, out, sizeof out));
    const char *line = "|missions\\first\\x\n|missions\\second\nthird\\";
    CHECK(!mcMissionOf(line, strlen(line), mark, out, sizeof out));
    const char *none = "|missions\\first\\x\n|missions\\\\y";
    CHECK(!mcMissionOf(none, strlen(none), mark, out, sizeof out));
    const char *end = "|missions\\a\\|missions\\";
    CHECK(!mcMissionOf(end, strlen(end), mark, out, sizeof out));
    CHECK(mcMissionOf(end, strlen(end) - 1, mark, out, sizeof out) && strcmp(out, "a") == 0);
    // the text ends where it is said to: the backslash behind it does not end the name
    const char *beyond = "|missions\\abc\\";
    strcpy(out, "kept");
    CHECK(!mcMissionOf(beyond, strlen(beyond) - 1, mark, out, sizeof out) && strcmp(out, "kept") == 0);
    CHECK(mcMissionOf(beyond, strlen(beyond), mark, out, sizeof out) && strcmp(out, "abc") == 0);
    const char *later = "xx|missions\\abc\\"; // (the same with the mark not at the text's beginning)
    CHECK(!mcMissionOf(later, strlen(later) - 1, mark, out, sizeof out));
    CHECK(mcMissionOf(later, strlen(later), mark, out, sizeof out) && strcmp(out, "abc") == 0);
    // a text shorter than the mark, no text, no mark
    strcpy(out, "kept");
    CHECK(!mcMissionOf(mark, 3, mark, out, sizeof out) && !mcMissionOf("", 0, mark, out, sizeof out));
    CHECK(!mcMissionOf("missions\\a\\", 11, mark, out, sizeof out) && strcmp(out, "kept") == 0);
}
static void testAutoChange() {
    // a trial that brought nothing: the way stays for 900 frames - unless the frames get much longer than they
    // were at that trial (another scene)
    McAuto a = {};
    CHECK(autoRun(a, MC_AUTO_WINDOW, 16, 0.5) == MC_AUTO_TRY);
    CHECK(autoRun(a, MC_AUTO_TRIAL, 16, 0.5) == MC_AUTO_BACK && a.rest == MC_AUTO_PAUSE && a.seen == 16);
    CHECK(a.rn == 0 && a.rsum == 0);
    // ... as long: the rest runs on, window after window
    CHECK(autoRun(a, MC_AUTO_WINDOW - 1, 24, 0.5) == MC_AUTO_NONE && a.rn == MC_AUTO_WINDOW - 1);
    CHECK(a.rsum == 24.0 * (MC_AUTO_WINDOW - 1));
    CHECK(mcAutoStep(a, 24, 0.5, 0) == MC_AUTO_NONE && a.rest == MC_AUTO_PAUSE - MC_AUTO_WINDOW && a.rn == 0);
    CHECK(a.rsum == 0); // (24 = 16 x 1.5: just not longer)
    // ... longer: the rest is over, the next window decides
    CHECK(autoRun(a, MC_AUTO_WINDOW - 1, 24, 0.5) == MC_AUTO_NONE && a.rest == MC_AUTO_PAUSE - 2 * MC_AUTO_WINDOW + 1);
    CHECK(mcAutoStep(a, 24.5, 0.5, 0) == MC_AUTO_NONE && a.rest == 0 && a.rn == 0 && a.rsum == 0 && !a.trying);
    CHECK(autoRun(a, MC_AUTO_WINDOW, 40, 0.5) == MC_AUTO_TRY && a.replay && a.before == 40);
    // kept: the frames it was kept at are the measure from then on
    CHECK(autoRun(a, MC_AUTO_TRIAL, 28, 0.5) == MC_AUTO_KEEP && a.seen == 28 && a.rest == MC_AUTO_KEPT_REST);
    CHECK(autoRun(a, MC_AUTO_WINDOW, 42, 0.5) == MC_AUTO_NONE && a.rest == MC_AUTO_KEPT_REST - MC_AUTO_WINDOW);
    CHECK(autoRun(a, MC_AUTO_WINDOW, 42.5, 0.5) == MC_AUTO_NONE && a.rest == 0);
    // what was added up in a rest does not count after a trial
    a = {};
    a.rest = 10, a.seen = 1;
    CHECK(autoRun(a, 10, 1, 0.5) == MC_AUTO_NONE && a.rn == 10 && a.rsum == 10 && a.rest == 0);
    CHECK(autoRun(a, MC_AUTO_WINDOW, 16, 0.5) == MC_AUTO_TRY);
    CHECK(autoRun(a, MC_AUTO_TRIAL, 12, 0.5) == MC_AUTO_KEEP && a.rn == 0 && a.rsum == 0);
    a = {};
    a.rest = 10, a.seen = 1;
    CHECK(autoRun(a, 10, 1, 0.5) == MC_AUTO_NONE && autoRun(a, MC_AUTO_WINDOW, 16, 0.5) == MC_AUTO_TRY);
    CHECK(autoRun(a, MC_AUTO_TRIAL, 16, 0.5) == MC_AUTO_BACK && a.rn == 0 && a.rsum == 0);
    // (a measure of one millisecond is a measure)
    a = {};
    a.rest = 100, a.seen = 1;
    CHECK(autoRun(a, MC_AUTO_WINDOW, 2, 0.5) == MC_AUTO_NONE && a.rest == 0);
    // a battle's first rest ends by its count alone (no trial yet to measure by)
    a = mcAutoStart();
    CHECK(a.seen == 0 && autoRun(a, MC_AUTO_START - 1, 500, 0.5) == MC_AUTO_NONE && a.rest == 1);
}
static void testAutoLimits() {
    g_test = "autoLimits";
    struct {
        bool replay;
        double frame, wait;
        bool tries;
    } const cases[] = {
        {false, 30, 0.5, true},            // the game's way, slow, the processor: replay
        {false, 30, 5, false},             // ... the graphics card: replay would cost it more
        {false, 30, MC_AUTO_CPU, false},   // (at the limit: not under it)
        {false, MC_AUTO_SLOW, 0.5, false}, // not slower than the headset
        {false, MC_AUTO_SLOW + 0.125, 0.5, true},
        {false, 11, 0.1, false},
        {true, 30, 5, true},            // replay, slow, the graphics card: the game's way
        {true, 30, 0.5, false},         // ... the processor: the game's way costs it a pass more
        {true, 30, MC_AUTO_GPU, false}, // (at the limit: not above it)
        {true, 30, 1.5, false},         // between the two limits: neither
        {false, 30, 1.5, false},
        {true, MC_AUTO_SLOW, 5, false},
        {true, MC_AUTO_SLOW + 0.125, 5, true},
    };
    for (const auto &c : cases) {
        McAuto a = {};
        a.replay = c.replay;
        int r = autoRun(a, MC_AUTO_WINDOW, c.frame, c.wait);
        CHECK(r == (c.tries ? MC_AUTO_TRY : MC_AUTO_NONE));
        CHECK(a.replay == (c.tries ? !c.replay : c.replay) && a.trying == c.tries && a.n == 0 && a.waits == 0);
    }
    // the window's mean counts, not a frame of it: one long frame among short ones is no reason
    McAuto a = {};
    CHECK(autoRun(a, MC_AUTO_WINDOW - 1, 11, 0.1) == MC_AUTO_NONE);
    CHECK(mcAutoStep(a, 30, 0.1, 0) == MC_AUTO_NONE && !a.trying);
    // ... and the waits' mean: one long wait among none
    CHECK(autoRun(a, MC_AUTO_WINDOW - 1, 30, 0) == MC_AUTO_NONE);
    CHECK(mcAutoStep(a, 30, 22.5, 0) == MC_AUTO_TRY); // (22.5 / 45 = 0.5: under the limit)
    // The game's own way is tried only where it can win: the processor's part of a frame (20 less a wait of 5)
    // and three quarters of the camera's pass again must stay under fifteen sixteenths of the frame (18.75).
    struct {
        double pass;
        bool tries;
    } const second[] = {{0, true}, {4.875, true}, {5, false}, {10, false}};
    for (const auto &c : second) {
        a = {};
        a.replay = true;
        CHECK(autoRun(a, MC_AUTO_WINDOW, 20, 5, c.pass) == (c.tries ? MC_AUTO_TRY : MC_AUTO_NONE));
        CHECK(a.replay == !c.tries && a.passes == 0);
    }
    // ... the mean of the passes: one long pass among none (225 / 45 = 5: too much; a little less: not)
    a = {};
    a.replay = true;
    CHECK(autoRun(a, MC_AUTO_WINDOW - 1, 20, 5, 0) == MC_AUTO_NONE);
    CHECK(mcAutoStep(a, 20, 5, 225) == MC_AUTO_NONE);
    CHECK(autoRun(a, MC_AUTO_WINDOW - 1, 20, 5, 0) == MC_AUTO_NONE);
    CHECK(mcAutoStep(a, 20, 5, 216) == MC_AUTO_TRY);
    // from the game's own way the pass does not count: replay saves one, whatever it is
    a = {};
    CHECK(autoRun(a, MC_AUTO_WINDOW, 30, 0.5, 25) == MC_AUTO_TRY && a.replay);
    // kept only when faster by a sixteenth: 32 -> 30 is just that much, and not enough; a little under is
    a = {};
    CHECK(autoRun(a, MC_AUTO_WINDOW, 32, 0.1) == MC_AUTO_TRY);
    CHECK(autoRun(a, MC_AUTO_TRIAL, 30, 0.1) == MC_AUTO_BACK && !a.replay);
    a = {};
    CHECK(autoRun(a, MC_AUTO_WINDOW, 32, 0.1) == MC_AUTO_TRY);
    CHECK(autoRun(a, MC_AUTO_TRIAL, 29.875, 0.1) == MC_AUTO_KEEP && a.replay);
    // the trial's mean is of the frames that count alone
    a = {};
    CHECK(autoRun(a, MC_AUTO_WINDOW, 32, 0.1) == MC_AUTO_TRY);
    CHECK(autoRun(a, MC_AUTO_SETTLE, 1, 0.1) == MC_AUTO_NONE); // (fast frames that do not count)
    CHECK(autoRun(a, MC_AUTO_TRIAL - MC_AUTO_SETTLE, 31, 0.1) == MC_AUTO_BACK && a.after == 31);
}

// ---------------------------------------------------------------- the cockpit's sway from its place
static void testAltBottom() {
    // a monitor's second part grows with the menu it shows; a part elsewhere, or none, stays as set
    const float a[4] = {0, 0, 0.243f, 0.435f}, none[4] = {};
    CHECK(mcAltBottom(a, 0.12f, 0.05f, 0) == 0.435f);
    CHECK(mcAltBottom(a, 0.12f, 0.05f, 0.4f) == 0.435f);
    CHECK(mcAltBottom(a, 0.12f, 0.05f, 0.58f) == 0.58f);
    CHECK(mcAltBottom(a, 0.5f, 0.05f, 0.58f) == 0.435f);
    CHECK(mcAltBottom(none, 0.12f, 0.05f, 0.58f) == 0);
}

static void testHangs() {
    // what hangs below a part: the rows that hold something from the part's edge on, short gaps passed over
    unsigned char c[1024] = {};
    CHECK(mcHangsTo(c, 1024, 100, 10) == 100); // nothing
    for (int i = 100; i < 300; ++i) c[i] = MC_WIDE_ALPHA;
    CHECK(mcHangsTo(c, 1024, 100, 10) == 300);
    CHECK(mcHangsTo(c, 1024, 200, 10) == 300);
    c[150] = MC_WIDE_ALPHA - 1; // (a row with too little alpha is a gap)
    CHECK(mcHangsTo(c, 1024, 100, 0) == 150);
    CHECK(mcHangsTo(c, 1024, 100, 1) == 300);
    // ... something further down does not belong to it, unless the gap is short enough
    for (int i = 320; i < 340; ++i) c[i] = 255;
    CHECK(mcHangsTo(c, 1024, 100, 10) == 300);
    CHECK(mcHangsTo(c, 1024, 100, 19) == 300);
    CHECK(mcHangsTo(c, 1024, 100, 20) == 340);
    // ... nor something that begins below the edge by more than a gap
    CHECK(mcHangsTo(c, 1024, 60, 10) == 60);
    CHECK(mcHangsTo(c, 1024, 90, 10) == 300);
    // the column's ends
    for (int i = 1000; i < 1024; ++i) c[i] = 255;
    CHECK(mcHangsTo(c, 1024, 1000, 10) == 1024);
    CHECK(mcHangsTo(c, 1024, 1024, 10) == 1024);
    CHECK(mcHangsTo(c, 1024, 2000, 10) == 2000);
    CHECK(mcHangsTo(c, 1024, -5, 10) == -5);
    c[0] = c[1] = 255;
    CHECK(mcHangsTo(c, 1024, -1, 10) == -1);
    CHECK(mcHangsTo(c, 1024, 0, 10) == 2);
    // what else stands in a run of rows: c holds 0..1, 100..149, 151..299, 320..339, 1000..1023
    CHECK(mcColumnHolds(c, 1024, 300, 320) == false);
    CHECK(mcColumnHolds(c, 1024, 300, 321) == true);
    CHECK(mcColumnHolds(c, 1024, 339, 400) == true);
    CHECK(mcColumnHolds(c, 1024, 340, 400) == false);
    CHECK(mcColumnHolds(c, 1024, 150, 151) == false); // (too little alpha)
    CHECK(mcColumnHolds(c, 1024, 149, 150) == true);  // (just enough)
    CHECK(mcColumnHolds(c, 1024, 320, 320) == false); // (no rows)
    CHECK(mcColumnHolds(c, 1024, 0, 1) == true);
    CHECK(mcColumnHolds(c, 1024, -1, 2) == false); // (the edge above the column: no answer)
    CHECK(mcColumnHolds(c, 1024, 340, 5000) == true);
    CHECK(mcColumnHolds(c, 1000, 340, 5000) == false); // (not beyond the column's end)
}

static void testBand() {
    // the band: the rows where all three columns hold something, the longest run
    unsigned char l[1024] = {}, m[1024] = {}, r[1024] = {};
    int r0 = -1, r1 = -1;
    CHECK(!mcWideBand(l, m, r, 1024, &r0, &r1) && r0 == -1 && r1 == -1);
    for (int i = 100; i < 140; ++i) l[i] = m[i] = r[i] = MC_WIDE_ALPHA;
    CHECK(mcWideBand(l, m, r, 1024, &r0, &r1) && r0 == 100 && r1 == 140);
    // ... every one of the three is needed, and at least that much alpha
    unsigned char *const cols[3] = {l, m, r};
    for (unsigned char *c : cols) {
        c[120] = MC_WIDE_ALPHA - 1;
        CHECK(mcWideBand(l, m, r, 1024, &r0, &r1) && r0 == 100 && r1 == 120); // (the first of two runs as long)
        c[119] = 0;
        CHECK(mcWideBand(l, m, r, 1024, &r0, &r1) && r0 == 100 && r1 == 119);
        c[118] = 0;
        CHECK(mcWideBand(l, m, r, 1024, &r0, &r1) && r0 == 121 && r1 == 140);
        c[118] = 255;
        c[119] = c[120] = 255;
        CHECK(mcWideBand(l, m, r, 1024, &r0, &r1) && r0 == 100 && r1 == 140);
    }
    // ... a longer run elsewhere wins; one column alone is nothing
    for (int i = 500; i < 541; ++i) l[i] = m[i] = r[i] = 200;
    for (int i = 700; i < 1000; ++i) m[i] = 255;
    CHECK(mcWideBand(l, m, r, 1024, &r0, &r1) && r0 == 500 && r1 == 541);
    // ... not a sliver, not a sheet: from a 256th of the rows to a fifth of them
    memset(l, 0, sizeof l), memset(m, 0, sizeof m), memset(r, 0, sizeof r);
    for (int i = 0; i < 3; ++i) l[i] = m[i] = r[i] = 255;
    r0 = r1 = -1;
    CHECK(!mcWideBand(l, m, r, 1024, &r0, &r1) && r0 == -1 && r1 == -1);
    l[3] = m[3] = r[3] = 255;
    CHECK(mcWideBand(l, m, r, 1024, &r0, &r1) && r0 == 0 && r1 == 4);
    CHECK(!mcWideBand(l, m, r, 1025, &r0, &r1));
    for (int i = 0; i < 204; ++i) l[i] = m[i] = r[i] = 255;
    CHECK(mcWideBand(l, m, r, 1020, &r0, &r1) && r0 == 0 && r1 == 204);
    CHECK(!mcWideBand(l, m, r, 1019, &r0, &r1));
    l[1023] = m[1023] = r[1023] = 255; // (the last row is looked at)
    for (int i = 1000; i < 1023; ++i) l[i] = m[i] = r[i] = 255;
    memset(l, 0, 204), memset(m, 0, 204), memset(r, 0, 204);
    CHECK(mcWideBand(l, m, r, 1024, &r0, &r1) && r0 == 1000 && r1 == 1024);

    // ... one row of 256 is a band, at its place; rows beyond the count are not looked at
    memset(l, 0, sizeof l), memset(m, 0, sizeof m), memset(r, 0, sizeof r);
    l[0] = m[0] = r[0] = 255;
    CHECK(mcWideBand(l, m, r, 256, &r0, &r1) && r0 == 0 && r1 == 1);
    l[0] = 0, l[5] = m[5] = r[5] = 255;
    CHECK(mcWideBand(l, m, r, 256, &r0, &r1) && r0 == 5 && r1 == 6);
    for (int i = 990; i < 1006; ++i) l[i] = m[i] = r[i] = 255;
    CHECK(mcWideBand(l, m, r, 1000, &r0, &r1) && r0 == 990 && r1 == 1000);

    // taken out of the near frame's and the far parts, added as one far part
    McRects q = {};
    mcAdd(q, 0.25f, 0.125f, 0.75f, 0.75f, MC_FAR);      // the middle
    mcAdd(q, 0, 0.125f, 0.25f, 0.75f, MC_NEAR);         // a column
    mcAdd(q, 0, 0, 1, 0.125f, MC_NEAR);                 // the top strip: not met
    mcAdd(q, 0.5f, 0.25f, 0.625f, 0.5f, MC_FAR_OPAQUE); // a window: left as it is
    mcAdd(q, 0.375f, 0.25f, 0.5f, 0.5f, MC_FAR_SMALL);  // a moved part: left as it is
    mcAdd(q, 0.75f, 0.25f, 1, 0.5f, (float)MC_GLASS);   // a part on the glass: left as it is
    mcAdd(q, 0.75f, 0.3125f, 1, 0.75f, MC_NEAR);        // a part that begins in the band: nothing above
    mcAdd(q, 0.75f, 0.125f, 1, 0.34375f, MC_NEAR);      // one that ends in it: nothing below
    McRects before = q;
    mcWideOut(q, 0.375f, 0.375f, 1.25f); // (empty: nothing happens)
    CHECK(q.n == before.n && memcmp(&q, &before, sizeof q) == 0);
    mcWideOut(q, 0.3125f, 0.375f, 1.25f);
    CHECK(q.n == 13);
    const float want[13][5] = {{0.25f, 0.125f, 0.75f, 0.3125f, MC_FAR},
                               {0, 0.125f, 0.25f, 0.3125f, MC_NEAR},
                               {0, 0, 1, 0.125f, MC_NEAR},
                               {0.5f, 0.25f, 0.625f, 0.5f, MC_FAR_OPAQUE},
                               {0.375f, 0.25f, 0.5f, 0.5f, MC_FAR_SMALL},
                               {0.75f, 0.25f, 1, 0.3125f, (float)MC_GLASS},
                               {0.75f, 0.375f, 1, 0.5f, (float)MC_GLASS},
                               {0.75f, 0.3125f, 1, 0.3125f, MC_NEAR},
                               {0.75f, 0.125f, 1, 0.3125f, MC_NEAR},
                               {0.25f, 0.375f, 0.75f, 0.75f, MC_FAR},
                               {0, 0.375f, 0.25f, 0.75f, MC_NEAR},
                               {0.75f, 0.375f, 1, 0.75f, MC_NEAR},
                               {0, 0.3125f, 1.25f, 0.375f, MC_FAR}};
    for (int i = 0; i < 13; ++i)
        for (int k = 0; k < 5; ++k) CHECK(q.rc[i][k] == want[i][k]);
    // a part that only touches the band is not met
    q = {};
    mcAdd(q, 0, 0.125f, 1, 0.25f, MC_NEAR);
    mcAdd(q, 0, 0.5f, 1, 0.75f, MC_FAR);
    const float stale[5] = {0, 0.125f, 1, 0.75f, MC_NEAR}; // (what lies behind the list's end is not a part)
    memcpy(q.rc[2], stale, sizeof stale);
    mcWideOut(q, 0.25f, 0.5f, 1);
    CHECK(q.n == 3 && q.rc[0][3] == 0.25f && q.rc[1][1] == 0.5f && q.rc[2][1] == 0.25f && q.rc[2][3] == 0.5f);

    // the HUD's parts with a band: as without, the band taken out; none: as ever
    McHud h = {};
    h.T = 0.125f, h.B = 0.75f, h.L = 0.25f, h.R = 0.75f, h.UR = 1;
    McRects a = {}, b = {};
    mcHudRects(h, a);
    h.wide[0] = 0.3125f, h.wide[1] = 0.375f;
    mcHudRects(h, b);
    CHECK(mcWideOut(a, 0.3125f, 0.375f, 1));
    mcToFront(a); // (the band is the first part)
    CHECK(a.n == b.n && a.n == 9 && memcmp(a.rc, b.rc, sizeof(float) * 5 * a.n) == 0);
    CHECK(b.rc[0][4] == MC_FAR && b.rc[0][0] == 0 && b.rc[0][1] == 0.3125f && b.rc[0][2] == 1 && b.rc[0][3] == 0.375f);
    // ... also where the parts are too many for the cross-hair's own
    h.cross[0] = 0.4375f, h.cross[1] = 0.4375f, h.cross[2] = 0.5625f, h.cross[3] = 0.5625f;
    mcHudRects(h, b, 9);
    CHECK(b.n == a.n && memcmp(a.rc, b.rc, sizeof(float) * 5 * a.n) == 0);
    mcHudRects(h, b);
    CHECK(b.n == 13 && b.rc[0][4] == MC_FAR && b.rc[0][0] == 0 && b.rc[0][2] == 1 && b.rc[0][1] == 0.3125f);

    // windows: one outside the band's rows (the target's view in its corner) leaves the band in one piece; one
    // in its rows keeps the parts as they are without a band
    McHud w = {};
    w.T = 0.125f, w.B = 0.75f, w.L = 0.25f, w.R = 0.75f, w.UR = 1;
    w.nCam = 1;
    const float corner[4] = {0, 0.8125f, 0.125f, 0.9375f};
    memcpy(w.cam[0], corner, sizeof corner);
    McRects plain = {}, banded = {};
    mcHudRects(w, plain);
    w.wide[0] = 0.3125f, w.wide[1] = 0.375f;
    CHECK(mcWideFree(w));
    mcHudRects(w, banded);
    mcWideOut(plain, 0.3125f, 0.375f, 1);
    mcToFront(plain);
    CHECK(banded.n == plain.n && memcmp(banded.rc, plain.rc, sizeof(float) * 5 * plain.n) == 0);
    CHECK(banded.rc[0][4] == MC_FAR && banded.rc[0][1] == 0.3125f);
    // (a window that ends where the band begins, or begins where it ends, is not in its rows)
    w.cam[0][1] = 0.125f, w.cam[0][3] = 0.3125f;
    CHECK(mcWideFree(w));
    w.cam[0][1] = 0.375f, w.cam[0][3] = 0.5f;
    CHECK(mcWideFree(w));
    w.cam[0][1] = 0.125f, w.cam[0][3] = 0.34375f; // (its lower part in the band)
    CHECK(!mcWideFree(w));
    w.cam[0][1] = 0.34375f, w.cam[0][3] = 0.5f; // (its upper part)
    CHECK(!mcWideFree(w));
    w.wide[0] = w.wide[1] = 0;
    mcHudRects(w, plain);
    w.wide[0] = 0.3125f, w.wide[1] = 0.375f;
    mcHudRects(w, banded);
    CHECK(banded.n == plain.n && memcmp(banded.rc, plain.rc, sizeof(float) * 5 * plain.n) == 0);
    mcHudRects(w, banded, 1); // (... also where the parts are too many)
    McRects few = {};
    w.wide[0] = w.wide[1] = 0;
    mcHudRects(w, few, 1);
    CHECK(banded.n == few.n && memcmp(banded.rc, few.rc, sizeof(float) * 5 * few.n) == 0);
    // every window counts: the second one in the band's rows; a window that is not one (beyond nCam) does not
    w.wide[0] = 0.3125f, w.wide[1] = 0.375f;
    memcpy(w.cam[0], corner, sizeof corner);
    const float mid[4] = {0.375f, 0.25f, 0.625f, 0.5f};
    memcpy(w.cam[1], mid, sizeof mid);
    CHECK(mcWideFree(w));
    w.nCam = 2;
    CHECK(!mcWideFree(w));

    // the list's last part made its first: the others move on, one place each; a list of one stays
    McRects tf = {};
    mcAdd(tf, 0, 0, 0.25f, 0.25f, 10), mcAdd(tf, 0.25f, 0, 0.5f, 0.25f, 11), mcAdd(tf, 0.5f, 0.125f, 0.75f, 0.5f, 12);
    const float beyond5[5] = {9, 9, 9, 9, 99};
    memcpy(tf.rc[3], beyond5, sizeof beyond5);
    mcToFront(tf);
    CHECK(tf.n == 3 && tf.rc[0][4] == 12 && tf.rc[1][4] == 10 && tf.rc[2][4] == 11 && tf.rc[3][4] == 99);
    CHECK(tf.rc[0][0] == 0.5f && tf.rc[0][1] == 0.125f && tf.rc[0][2] == 0.75f && tf.rc[0][3] == 0.5f);
    CHECK(tf.rc[1][0] == 0 && tf.rc[1][2] == 0.25f && tf.rc[2][0] == 0.25f && tf.rc[2][2] == 0.5f);
    tf.n = 1;
    mcToFront(tf);
    CHECK(tf.rc[0][4] == 12 && tf.rc[1][4] == 10);
    // no band: nothing happens, and it is told
    McRects nb = {};
    mcAdd(nb, 0, 0, 1, 1, MC_FAR);
    CHECK(!mcWideOut(nb, 0.5f, 0.5f, 1) && !mcWideOut(nb, 0.5f, 0.25f, 1) && nb.n == 1 && nb.rc[0][3] == 1);
    // the parts on the glass kept as they are (asked for so): the band out of the others alone
    McRects gk = {};
    mcAdd(gk, 0, 0, 0.25f, 0.5f, MC_GLASS), mcAdd(gk, 0.25f, 0.125f, 0.75f, 0.75f, MC_FAR);
    CHECK(mcWideOut(gk, 0.3125f, 0.375f, 1, false) && gk.n == 4);
    CHECK(gk.rc[0][4] == MC_GLASS && gk.rc[0][3] == 0.5f && gk.rc[1][3] == 0.3125f && gk.rc[2][1] == 0.375f);
    CHECK(gk.rc[3][4] == MC_FAR && gk.rc[3][1] == 0.3125f);
    // the HUD's parts where the runtime shows fewer than there are: first without the cross-hair's own part, then
    // with the glass's parts whole - and with as many as it shows, nothing is given up
    McHud many = {};
    many.T = 0.125f, many.B = 0.75f, many.L = 0.25f, many.R = 0.75f, many.UR = 1;
    many.cross[0] = 0.4375f, many.cross[1] = 0.4375f, many.cross[2] = 0.5625f, many.cross[3] = 0.5625f;
    many.nGlass = 1;
    const float pane[4] = {0, 0.25f, 0.25f, 0.5f};
    memcpy(many.glass[0], pane, sizeof pane);
    many.wide[0] = 0.3125f, many.wide[1] = 0.375f;
    McRects full = {}, noCross = {}, glassWhole = {}, got = {};
    mcHudRectsWith(many, full, true);
    mcWideOut(full, 0.3125f, 0.375f, 1), mcToFront(full);
    mcHudRectsWith(many, noCross, false);
    mcWideOut(noCross, 0.3125f, 0.375f, 1), mcToFront(noCross);
    mcHudRectsWith(many, glassWhole, false);
    mcWideOut(glassWhole, 0.3125f, 0.375f, 1, false), mcToFront(glassWhole);
    CHECK(full.n > noCross.n && noCross.n == glassWhole.n + 1);
    mcHudRects(many, got, full.n);
    CHECK(got.n == full.n && memcmp(got.rc, full.rc, sizeof(float) * 5 * full.n) == 0);
    mcHudRects(many, got, full.n - 1);
    CHECK(got.n == noCross.n && memcmp(got.rc, noCross.rc, sizeof(float) * 5 * noCross.n) == 0);
    mcHudRects(many, got, noCross.n);
    CHECK(got.n == noCross.n && memcmp(got.rc, noCross.rc, sizeof(float) * 5 * noCross.n) == 0);
    mcHudRects(many, got, noCross.n - 1);
    CHECK(got.n == glassWhole.n && memcmp(got.rc, glassWhole.rc, sizeof(float) * 5 * glassWhole.n) == 0);
    mcHudRects(many, got, 1); // (fewer still: the last way stands, the band first)
    CHECK(got.n == glassWhole.n && got.rc[0][4] == MC_FAR && got.rc[0][1] == 0.3125f && got.rc[0][2] == 1);

    // a part put into the list at a place: what stood there moves on; slivers and a full list: not
    McRects li = {};
    mcAdd(li, 0, 0, 0.25f, 0.25f, 10), mcAdd(li, 0.25f, 0, 0.5f, 0.25f, 11), mcAdd(li, 0.5f, 0, 0.75f, 0.25f, 12);
    CHECK(mcInsert(li, 1, 0.125f, 0.5f, 0.375f, 0.75f, 13) && li.n == 4);
    CHECK(li.rc[0][4] == 10 && li.rc[1][4] == 13 && li.rc[2][4] == 11 && li.rc[3][4] == 12);
    CHECK(li.rc[1][0] == 0.125f && li.rc[1][1] == 0.5f && li.rc[1][2] == 0.375f && li.rc[1][3] == 0.75f);
    CHECK(li.rc[2][0] == 0.25f && li.rc[2][2] == 0.5f && li.rc[3][0] == 0.5f && li.rc[3][2] == 0.75f);
    CHECK(mcInsert(li, 4, 0, 0.5f, 0.125f, 0.75f, 14) && li.n == 5 && li.rc[4][4] == 14 && li.rc[3][4] == 12);
    CHECK(mcInsert(li, 0, 0, 0.5f, 0.125f, 0.75f, 15) && li.n == 6 && li.rc[0][4] == 15 && li.rc[5][4] == 14);
    CHECK(!mcInsert(li, 1, 0.5f, 0.5f, 0.505f, 0.75f, 16) && !mcInsert(li, 1, 0.5f, 0.5f, 0.75f, 0.505f, 16));
    CHECK(mcInsert(li, 1, 0.5f, 0.5f, 0.50625f, 0.75f, 16) && mcInsert(li, 1, 0.5f, 0.5f, 0.75f, 0.50625f, 17));
    CHECK(li.n == 8 && li.rc[1][4] == 17 && li.rc[2][4] == 16 && li.rc[3][4] == 10);
    CHECK(!mcInsert(li, 1, 0, 0.5f, 0.005f, 0.75f, 16) && !mcInsert(li, 1, 0.5f, 0, 0.75f, 0.005f, 16) && li.n == 8);
    while (li.n < MC_RECTS - 1) mcAdd(li, 0, 0, 0.5f, 0.5f, 1);
    CHECK(mcInsert(li, 2, 0, 0, 0.5f, 0.5f, 18) && li.n == MC_RECTS && li.rc[2][4] == 18 && li.rc[3][4] == 16);
    CHECK(!mcInsert(li, 2, 0, 0, 0.5f, 0.5f, 19) && li.n == MC_RECTS && li.rc[2][4] == 18);

    // the parts on the glass lose the band's rows as well; what is left of one below the band comes right after
    // it, before the window that lies over it. A window, and a part that does not reach the band, stay
    McRects g = {};
    mcAdd(g, 0, 0, 0.25f, 0.5f, MC_GLASS);                      // (the band runs through it)
    mcAdd(g, 0.0625f, 0.4375f, 0.1875f, 0.5f, MC_GLASS_OPAQUE); // (a window in its lower part)
    mcAdd(g, 0.75f, 0, 1, 0.25f, MC_GLASS + 1);                 // (above the band)
    mcAdd(g, 0.75f, 0.25f, 1, 0.34375f, MC_GLASS_OPAQUE - 1);   // (ends in the band: nothing left below it)
    mcAdd(g, 0.25f, 0.125f, 0.75f, 0.75f, MC_FAR);
    mcAdd(g, 0.5f, 0.25f, 0.75f, 0.5f, MC_GLASS_OPAQUE + 1); // (a window in the band's rows: left as it is)
    mcAdd(g, 0.5f, 0.25f, 0.75f, 0.5f, MC_FAR_SMALL);        // (a moved part: as well)
    mcWideOut(g, 0.3125f, 0.375f, 1);
    CHECK(g.n == 10);
    CHECK(g.rc[0][4] == MC_GLASS && g.rc[0][1] == 0 && g.rc[0][3] == 0.3125f);
    CHECK(g.rc[1][4] == MC_GLASS && g.rc[1][0] == 0 && g.rc[1][1] == 0.375f && g.rc[1][2] == 0.25f &&
          g.rc[1][3] == 0.5f);
    CHECK(g.rc[2][4] == MC_GLASS_OPAQUE && g.rc[2][1] == 0.4375f && g.rc[2][3] == 0.5f);
    CHECK(g.rc[3][4] == MC_GLASS + 1 && g.rc[3][1] == 0 && g.rc[3][3] == 0.25f);
    CHECK(g.rc[4][4] == MC_GLASS_OPAQUE - 1 && g.rc[4][1] == 0.25f && g.rc[4][3] == 0.3125f);
    CHECK(g.rc[5][4] == MC_FAR && g.rc[5][3] == 0.3125f);
    CHECK(g.rc[6][4] == MC_GLASS_OPAQUE + 1 && g.rc[6][1] == 0.25f && g.rc[6][3] == 0.5f);
    CHECK(g.rc[7][4] == MC_FAR_SMALL && g.rc[7][1] == 0.25f && g.rc[7][3] == 0.5f);
    CHECK(g.rc[8][4] == MC_FAR && g.rc[8][1] == 0.375f && g.rc[8][3] == 0.75f);
    CHECK(g.rc[9][4] == MC_FAR && g.rc[9][0] == 0 && g.rc[9][1] == 0.3125f && g.rc[9][2] == 1 && g.rc[9][3] == 0.375f);
    // (two parts on the glass one after the other, both cut: the second one is found behind the first one's rest)
    g = {};
    mcAdd(g, 0, 0, 0.25f, 0.5f, MC_GLASS), mcAdd(g, 0.75f, 0.25f, 1, 0.75f, MC_GLASS + 1);
    mcWideOut(g, 0.3125f, 0.375f, 1);
    CHECK(g.n == 5 && g.rc[0][4] == MC_GLASS && g.rc[1][4] == MC_GLASS && g.rc[2][4] == MC_GLASS + 1);
    CHECK(g.rc[3][4] == MC_GLASS + 1 && g.rc[2][3] == 0.3125f && g.rc[3][1] == 0.375f && g.rc[3][3] == 0.75f);
    CHECK(g.rc[1][3] == 0.5f && g.rc[4][4] == MC_FAR);
    // (the first window's kind is no part on the glass; and what lies behind the list's end is no part at all)
    g = {};
    mcAdd(g, 0, 0.25f, 0.25f, 0.5f, MC_GLASS_OPAQUE);
    const float staleGlass[5] = {0.5f, 0.25f, 0.75f, 0.5f, MC_GLASS};
    memcpy(g.rc[1], staleGlass, sizeof staleGlass);
    mcWideOut(g, 0.3125f, 0.375f, 1);
    CHECK(g.n == 2 && g.rc[0][4] == MC_GLASS_OPAQUE && g.rc[0][1] == 0.25f && g.rc[0][3] == 0.5f);
    CHECK(g.rc[1][4] == MC_FAR && g.rc[1][1] == 0.3125f && g.rc[1][3] == 0.375f);
    // (a part on the glass that only touches the band - ends where it begins, begins where it ends - is not met)
    g = {};
    mcAdd(g, 0, 0.125f, 0.25f, 0.3125f, MC_GLASS), mcAdd(g, 0, 0.375f, 0.25f, 0.5f, MC_GLASS);
    mcWideOut(g, 0.3125f, 0.375f, 1);
    CHECK(g.n == 3 && g.rc[0][3] == 0.3125f && g.rc[1][1] == 0.375f && g.rc[1][4] == MC_GLASS && g.rc[2][4] == MC_FAR);
    // (a part on the glass that begins in the band: nothing of it above, the rest below)
    g = {};
    mcAdd(g, 0, 0.34375f, 0.25f, 0.5f, MC_GLASS);
    mcWideOut(g, 0.3125f, 0.375f, 1);
    CHECK(g.n == 3 && g.rc[0][1] == 0.34375f && g.rc[0][3] == 0.34375f && g.rc[1][1] == 0.375f && g.rc[1][3] == 0.5f);
}
static void testFogCut() {
    // to a sixteenth beyond the fog's end
    CHECK(mcFogCut(195, 910, 4000) == 966.875f);
    CHECK(mcFogCut(0, 1024, 4000) == 1088);
    // not where the far plane is as near already, and never farther than it
    CHECK(mcFogCut(0, 1024, 1088) == 0 && mcFogCut(0, 1024, 1089) == 1088 && mcFogCut(0, 1024, 500) == 0);
    // no fog (mw4hook gives 0, 0), or what is no fog's values
    CHECK(mcFogCut(0, 0, 4000) == 0);
    CHECK(mcFogCut(-1, 1024, 4000) == 0 && mcFogCut(1024, 1024, 4000) == 0 && mcFogCut(1025, 1024, 4000) == 0);
    CHECK(mcFogCut(0, MC_FOG_LEAST, 4000) == 68 && mcFogCut(0, MC_FOG_LEAST - 1, 4000) == 0);
    CHECK(mcFogCut(8, 16, 4000) == 0 && mcFogCut(NAN, 1024, 4000) == 0 && mcFogCut(0, NAN, 4000) == 0);
    CHECK(mcFogCut(0, 1024, NAN) == 0);
}
static void testHold() {
    // a menu: nothing held, whatever the state was
    McHold h = {MC_HOLD_FADING, 7, 2, 5};
    CHECK(mcHoldStep(h, false, true, 100) == 0 && h.state == MC_HOLD_IDLE && h.n == 0 && h.ok == 0 && h.t0 == 0);
    // a battle begins: dark, until the complete world was drawn three frames in a row
    CHECK(mcHoldStep(h, true, false, 1000) == 1 && h.state == MC_HOLD_DARK && h.n == 1 && h.ok == 0 && h.t0 == 1000);
    CHECK(mcHoldStep(h, true, true, 1010) == 1 && h.n == 2 && h.ok == 1 && h.t0 == 1000);
    CHECK(mcHoldStep(h, true, true, 1020) == 1 && h.ok == 2);
    CHECK(mcHoldStep(h, true, false, 1030) == 1 && h.ok == 0 && h.state == MC_HOLD_DARK); // (not in a row)
    CHECK(mcHoldStep(h, true, true, 1040) == 1 && mcHoldStep(h, true, true, 1050) == 1 && h.state == MC_HOLD_DARK);
    CHECK(mcHoldStep(h, true, true, 2000) == 1 && h.state == MC_HOLD_FADING && h.t0 == 2000 && h.n == 7 && h.ok == 3);
    // ... then the picture comes up in half a second
    CHECK(mcHoldStep(h, true, false, 2125) == 0.75f && h.state == MC_HOLD_FADING && h.n == 7);
    CHECK(mcHoldStep(h, true, false, 2375) == 0.25f && h.t0 == 2000);
    CHECK(mcHoldStep(h, true, false, 2500) == 0 && h.state == MC_HOLD_SHOWN);
    CHECK(mcHoldStep(h, true, false, 2600) == 0 && h.state == MC_HOLD_SHOWN && h.t0 == 2000);
    CHECK(mcHoldStep(h, true, true, 9000) == 0 && h.state == MC_HOLD_SHOWN && h.n == 7);
    // a world complete from the first frame on (back from the options' screen): shown without the fade
    h = {};
    CHECK(mcHoldStep(h, true, true, 50) == 1 && mcHoldStep(h, true, true, 60) == 1);
    CHECK(mcHoldStep(h, true, true, 70) == 0 && h.state == MC_HOLD_SHOWN && h.t0 == 70);
    CHECK(mcHoldStep(h, true, false, 80) == 0);
    // a world that never says so: shown after four seconds, with the fade
    h = {};
    CHECK(mcHoldStep(h, true, false, 100) == 1 && mcHoldStep(h, true, false, 4099) == 1 && h.state == MC_HOLD_DARK);
    CHECK(mcHoldStep(h, true, false, 4100) == 1 && h.state == MC_HOLD_FADING && h.t0 == 4100 && h.n == 3);
    CHECK(mcHoldStep(h, true, false, 4350) == 0.5f);
    CHECK(mcHoldStep(h, true, false, 4601) == 0 && h.state == MC_HOLD_SHOWN);
    // ... complete in the very frame the time is up: the complete world's way
    h = {};
    CHECK(mcHoldStep(h, true, false, 0) == 1 && mcHoldStep(h, true, true, 10) == 1 &&
          mcHoldStep(h, true, true, 20) == 1);
    CHECK(mcHoldStep(h, true, true, 5000) == 1 && h.state == MC_HOLD_FADING && h.t0 == 5000);
    // the next battle is held anew
    CHECK(mcHoldStep(h, false, false, 6000) == 0 && mcHoldStep(h, true, false, 7000) == 1 && h.t0 == 7000 && h.n == 1);
}
static void testShape() {
    g_test = "shape";
    const float m[12] = {0, 1, 0, 2, 0, 0, 1, 3, 1, 0, 0, 5}, want[12] = {0, 0, 1, -5, 1, 0, 0, -2, 0, 1, 0, -3};
    float o[13];
    o[12] = -77;
    mcInv34(m, o);
    for (int i = 0; i < 12; ++i) CHECK(o[i] == want[i]);
    CHECK(o[12] == -77);
    // the cockpit against the camera, in the view's axes
    const float base[12] = {0, 0, 1, 100, 1, 0, 0, 20, 0, 1, 0, -50}, A[9] = {0, 1, 0, 0, 0, 1, 1, 0, 0},
                shape[12] = {0, 0, 1, 102, 0.8, -0.6, 0, 20.5, 0.6, 0.8, 0, -50.25},
                rel[12] = {0, 0, 1, 2, 0.8, -0.6, 0, 0.5, 0.6, 0.8, 0, -0.25};
    float r[13];
    r[12] = -77;
    mcShapeInView(base, A, shape, r);
    for (int i = 0; i < 12; ++i) NEAR(r[i], rel[i]);
    CHECK(r[12] == -77);
    mcShapeInView(base, A, base, r); // the camera itself: no turn, no shift, the view's axes
    const float same[12] = {0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0};
    for (int i = 0; i < 12; ++i) NEAR(r[i], same[i]);
    const float unit[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}, A2[9] = {1, 2, 3, 4, 5, 6, 7, 8, 10};
    mcShapeInView(unit, A2, unit, r); // (axes that are all different: each is taken from its own place)
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) CHECK(r[i * 4 + j] == A2[j * 3 + i]);
        CHECK(r[i * 4 + 3] == 0);
    }
    // a step: the small turn and shift between two poses
    const float prev[12] = {0, 1, 0, 2, 0, 0, 1, 3, 1, 0, 0, 5},
                cur[12] = {-0.015625, 1,         -0.00390625, 1.92578125, -0.0078125, 0.00390625,
                           1,         2.9609375, 1,           0.015625,   0.0078125,  5.05859375};
    float w[3], d[3];
    CHECK(mcShapeStep(prev, cur, w, d));
    NEAR(w[0], 0.0078125f), NEAR(w[1], -0.015625f), NEAR(w[2], 0.00390625f);
    NEAR(d[0], 0.015625f), NEAR(d[1], -0.0078125f), NEAR(d[2], 0.00390625f);
    CHECK(mcShapeStep(prev, prev, w, d) && w[0] == 0 && w[1] == 0 && w[2] == 0 && d[0] == 0 && d[1] == 0 && d[2] == 0);
    // no step: a shift or a turn at the limit (under it: one)
    const float id[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    float far[12];
    for (int a = 0; a < 3; ++a) {
        memcpy(far, id, sizeof far), far[a * 4 + 3] = MC_STEP_MAX;
        CHECK(!mcShapeStep(id, far, w, d) && d[a] == MC_STEP_MAX);
        far[a * 4 + 3] = -MC_STEP_MAX;
        CHECK(!mcShapeStep(id, far, w, d));
        far[a * 4 + 3] = MC_STEP_MAX / 2;
        CHECK(mcShapeStep(id, far, w, d) && d[a] == MC_STEP_MAX / 2);
    }
    memcpy(far, id, sizeof far), far[3] = 0.04f; // (4 cm, whatever the constant says)
    CHECK(!mcShapeStep(id, far, w, d));
    far[3] = 0.03f;
    CHECK(mcShapeStep(id, far, w, d));
    const int up[3][2] = {{9, 6}, {2, 8}, {4, 1}}; // (the elements a turn about x, y, z shows in)
    for (int a = 0; a < 3; ++a) {
        memcpy(far, id, sizeof far), far[up[a][0]] = MC_STEP_MAX, far[up[a][1]] = -MC_STEP_MAX;
        CHECK(!mcShapeStep(id, far, w, d) && w[a] == MC_STEP_MAX);
        far[up[a][0]] = MC_STEP_MAX / 2, far[up[a][1]] = -MC_STEP_MAX / 2;
        CHECK(mcShapeStep(id, far, w, d) && w[a] == MC_STEP_MAX / 2 && w[(a + 1) % 3] == 0 && w[(a + 2) % 3] == 0);
        far[up[a][0]] = -MC_STEP_MAX, far[up[a][1]] = MC_STEP_MAX;
        CHECK(!mcShapeStep(id, far, w, d) && w[a] == -MC_STEP_MAX);
    }
}

// ---------------------------------------------------------------- the video of both eyes
static void testRec() {
    g_test = "rec";
    CHECK(mcRecTick(0, 60) == 0);
    CHECK(mcRecTick(0.5, 60) == 30);
    CHECK(mcRecTick(0.0166, 60) == 0 && mcRecTick(0.0167, 60) == 1);
    CHECK(mcRecTick(2.27, 50) == 113);
    CHECK(mcRecTick(-1, 60) == -1 && mcRecTick(-0.001, 60) == -1);
    // a quarter turn about z takes x to y; about x, y to z
    const float h = 0.70710678f, qz[4] = {0, 0, h, h}, qx[4] = {h, 0, 0, h}, ex[3] = {1, 0, 0}, ey[3] = {0, 1, 0};
    float o[4];
    o[3] = -77;
    mcQuatTurn(qz, ex, o);
    NEAR(o[0], 0), NEAR(o[1], 1), NEAR(o[2], 0);
    mcQuatTurn(qx, ey, o);
    NEAR(o[0], 0), NEAR(o[1], 0), NEAR(o[2], 1);
    // any turn, against its matrix
    const float q[4] = {0.1f, 0.2f, 0.3f, 0.92736185f}, v[3] = {1, -2, 3};
    mcQuatTurn(q, v, o);
    NEAR(o[0], 3.0656684f), NEAR(o[1], -1.2f), NEAR(o[2], 1.7781105f);
    CHECK(o[3] == -77);
    CHECK(mcTanPlace(-1.376f, -1.376f, 0.839f) == -1 && mcTanPlace(0.839f, -1.376f, 0.839f) == 1);
    NEAR(mcTanPlace(0, -1.376f, 0.839f), 0.2424379f);
    NEAR(mcTanPlace(0.5f, -1, 3), -0.25f);
    // an eye that looks straight ahead from the origin, its picture between tangents of 1: a point 2 m ahead
    const float none[4] = {0, 0, 0, 1}, zero[3] = {0, 0, 0}, sym[4] = {-1, 1, 1, -1}, a[3] = {1, 0.5f, -2};
    mcEyeClip(a, zero, none, sym, o);
    NEAR(o[0], 1), NEAR(o[1], 0.5f), NEAR(o[2], 2);
    CHECK(o[3] == -77);
    // a headset's own view (wider to the outside and downwards), the eye turned and not at the origin
    const float tn[4] = {-1.376f, 0.839f, 0.966f, -1.428f}, p[3] = {0.5f, 1.25f, -3}, pos[3] = {0.1f, -0.2f, 0.3f};
    mcEyeClip(p, pos, q, tn, o);
    NEAR(o[0], 2.6759133f), NEAR(o[1], 0.5125773f), NEAR(o[2], 2.8925570f);
    // behind the eye: no positive distance
    const float b[3] = {0, 0, 1};
    mcEyeClip(b, zero, none, sym, o);
    CHECK(o[2] == -1);
}

int main() {
    testRec();
    testM4(), testM4Inv(), testProj(), testEyeStep(), testStereo(), testAuto(), testAutoLimits(), testAutoChange(),
        testAutoMission(), testShape(), testHold(), testBand(), testHangs(), testAltBottom(), testFogCut();
    testHolds(), testTurnNorm(), testTurnFit(), testEyeInMesh(), testHudCross(), testHudGlass(), testGlassPose(),
        testGlassScale(), testGlassAim();
    testGridMark(), testGridPlace(), testGridTri(), testGridCells(), testGridFree(), testGridRim(), testGridFit();
    testMul34(), testViewAxis();
    testFrustum(), testFrustumSameView(), testFrustumTangent(), testFrustumNear(), testFrustumLayout();
    testWide();
    testAdd(), testMeet(), testAddCut(), testMoveOut(), testSpans(), testLeftAlt(), testRightAlt();
    testHudPlain(), testHudZoom(), testHudZoomEdges(), testHudScreens(), testHudScreensZoom();
    testSeat();
    testSheet();
    testCockpitFile();
    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
