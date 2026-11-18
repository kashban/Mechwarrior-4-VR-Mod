// ---------------------------------------------------------------------------------------------
// End-to-end tests (tools/e2e/run.ps1): the game is started with the environment variable MW4VR_E2E naming a plan
// file, and the mod then
//   - writes a line for every frame with both eyes to mw4vr_e2e.csv: the time, the eyes' mean colours, whether the
//     camera sits in the mech and whose cockpit is drawn, the sway (what the test's script judges);
//   - turns the head as the plan says (a runtime without a headset gives one pose for ever);
//   - takes pictures at the plan's times (shot.inc).
// The plan, an .ini:   [e2e]  yaw=2:0 6:45 10:45 14:0     seconds:degrees to the right, straight lines between
//                             shots=5 12                  seconds
// Seconds count from the frame the camera first sits in the mech. Without MW4VR_E2E none of this does anything.
// Included into main.cpp after shot.inc, before vr.inc.

static bool g_e2eOn;
static FILE *g_e2eCsv;
static ULONGLONG g_e2eStart, g_e2eSeat; // ticks: the first frame, the first frame in the mech (0: not yet)
static std::vector<std::pair<float, float>> g_e2eYaw;
static std::vector<float> g_e2eShots;
static size_t g_e2eShotNext;

static int e2eCockpit(wchar_t *name, int n); // main.cpp, after mw5.inc: 1 - a cockpit of our own is drawn

static void e2eInit() {
    static bool done;
    if (done) return;
    done = true;
    wchar_t plan[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"MW4VR_E2E", plan, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    wchar_t b[512];
    GetPrivateProfileStringW(L"e2e", L"yaw", L"", b, 512, plan);
    for (const wchar_t *p = b; *p;) {
        wchar_t *end;
        float t = wcstof(p, &end);
        if (end == p || *end != L':') break;
        p = end + 1;
        float deg = wcstof(p, &end);
        if (end == p) break;
        g_e2eYaw.push_back({t, deg});
        p = end;
    }
    GetPrivateProfileStringW(L"e2e", L"shots", L"", b, 512, plan);
    for (const wchar_t *p = b; *p;) {
        wchar_t *end;
        float t = wcstof(p, &end);
        if (end == p) break;
        g_e2eShots.push_back(t);
        p = end;
    }
    g_e2eCsv = _wfsopen((g_dir + L"mw4vr_e2e.csv").c_str(), L"w", _SH_DENYNO);
    if (!g_e2eCsv) {
        logf("e2e: mw4vr_e2e.csv cannot be written - no test");
        return;
    }
    fprintf(g_e2eCsv, "frame,ms,t,seated,own,yaw,lr,lg,lb,rr,rg,rb,sx,sy,sz,cockpit\n");
    g_e2eStart = GetTickCount64();
    g_e2eOn = true;
    logf("e2e: test run - plan %ls, %u turns of the head, %u pictures", plan, (unsigned)g_e2eYaw.size(), (unsigned)g_e2eShots.size());
}

// seconds since the camera first sat in the mech; below 0: not yet
static float e2eTime() { return g_e2eSeat ? (float)(GetTickCount64() - g_e2eSeat) / 1000.0f : -1.0f; }

// the head's turn to the right the plan asks for now, in degrees
static float e2eYawNow() {
    e2eInit();
    if (!g_e2eOn || g_e2eYaw.empty()) return 0;
    float t = e2eTime();
    if (t <= g_e2eYaw.front().first) return t < 0 ? 0 : g_e2eYaw.front().second;
    for (size_t i = 1; i < g_e2eYaw.size(); ++i) {
        const auto &a = g_e2eYaw[i - 1], &b = g_e2eYaw[i];
        if (t <= b.first) return b.first > a.first ? a.second + (b.second - a.second) * (t - a.first) / (b.first - a.first) : b.second;
    }
    return g_e2eYaw.back().second;
}

// an eye's mean colour (0..255, as it is shown), from some thousand of its pixels
static bool e2eMean(ID3D11DeviceContext *ctx, ID3D11Texture2D *tex, float *rgb) {
    D3D11_TEXTURE2D_DESC d;
    tex->GetDesc(&d);
    UINT step = d.Width / 48 ? d.Width / 48 : 1, w = d.Width / step, h = d.Height / step;
    static std::vector<uint8_t> px;
    px.resize((size_t)w * h * 3);
    if (!w || !h || !shotRead(ctx, tex, step, px.data(), w, 0, w, h)) return false;
    double s[3] = {};
    for (size_t i = 0; i < (size_t)w * h; ++i)
        for (int k = 0; k < 3; ++k) s[k] += px[i * 3 + k];
    for (int k = 0; k < 3; ++k) rgb[k] = (float)(s[2 - k] / ((double)w * h)); // (the pixels are B G R)
    return true;
}

// once for every frame with both eyes, before shot.inc's pictures are taken
static void e2eFrame(ID3D11DeviceContext *ctx, ID3D11Texture2D *l, ID3D11Texture2D *r) {
    e2eInit();
    if (!g_e2eOn || !ctx || !l || !r) return;
    if (g_mechSeated && !g_e2eSeat) {
        g_e2eSeat = GetTickCount64();
        logf("e2e: the camera sits in the mech at frame %u - the plan's clock starts", g_frame);
    }
    float t = e2eTime();
    if (g_e2eShotNext < g_e2eShots.size() && t >= g_e2eShots[g_e2eShotNext]) {
        ++g_e2eShotNext;
        g_shotOne = 1;
    }
    float a[3] = {-1, -1, -1}, b[3] = {-1, -1, -1};
    e2eMean(ctx, l, a);
    e2eMean(ctx, r, b);
    wchar_t name[64] = L"";
    int own = e2eCockpit(name, 64);
    fprintf(g_e2eCsv, "%u,%llu,%.3f,%d,%d,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.4f,%.4f,%.4f,%ls\n", g_frame, GetTickCount64() - g_e2eStart, t,
            g_mechSeated ? 1 : 0, own, e2eYawNow(), a[0], a[1], a[2], b[0], b[1], b[2], g_swayValid ? g_swayT[0] : 0.0f,
            g_swayValid ? g_swayT[1] : 0.0f, g_swayValid ? g_swayT[2] : 0.0f, name);
    if ((g_frame & 31) == 0) fflush(g_e2eCsv);
}
