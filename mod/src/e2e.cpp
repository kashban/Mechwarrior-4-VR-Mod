// ---------------------------------------------------------------------------------------------
// End-to-end tests (tools/e2e/run.ps1): the game is started with the environment variable MW4VR_E2E naming a plan
// file, and the mod then
//   - writes a line for every frame with both eyes to mw4vr_e2e.csv: the time, the eyes' mean colours, whether the
//     camera sits in the mech and whose cockpit is drawn, the sway (what the test's script judges);
//   - turns the head as the plan says (a runtime without a headset gives one pose for ever);
//   - takes pictures at the plan's times (shot.cpp);
//   - presses keys at the plan's times, as input to the window in front (the game's): walking, firing, the view from
//     outside, the menu.
// The plan, an .ini:   [e2e]  yaw=2:0 6:45 10:45 14:0     seconds:degrees to the right, straight lines between
//                             shots=5 12                  seconds
//                             keys=13:LMB:0.3 18:W:5      seconds:key:seconds held - a letter or digit, ESC, SPACE,
//                                                         ENTER, TAB, F1..F12, LEFT, RIGHT, UP, DOWN, CTRL, ALT, SHIFT,
//                                                         LMB / RMB (the mouse's buttons)
//                                  43:MOVE:0.5,0.47       the mouse's pointer to this place of the picture (parts of
//                                                         its width and height), for a click in a menu
// Seconds count from the frame the camera first sits in the mech. Without MW4VR_E2E none of this does anything.

#include "mod.h"

bool g_e2eOn;
static int g_e2ePerf; // the plan asks for the frames' times (perf.cpp), not for the eyes' colours
static FILE *g_e2eCsv;
static ULONGLONG g_e2eStart, g_e2eSeat; // ticks: the first frame, the first frame in the mech (0: not yet)
std::vector<std::pair<float, float>> g_e2eYaw;
std::vector<std::pair<float, float>> g_e2ePitch;
static std::vector<float> g_e2eShots;
static size_t g_e2eShotNext;
struct E2eKey {
    float at, hold;
    int vk;       // a virtual key; -1 / -2: the left / right mouse button; -3: the pointer is moved
    float mx, my; // -3: where to, parts of the picture
    int state;    // 0 waits, 1 is down, 2 done
    char name[8];
};
static std::vector<E2eKey> g_e2eKeys;

// indirect ones into the HUD's image while the menu is up (how much the menu shows);
// 1 - the menu's picture of this frame has no HUD under the sheet

static int e2eVk(const wchar_t *s, size_t n) {
    if (n == 1 && iswalnum(s[0])) return towupper(s[0]);
    if (n == 3 && !_wcsnicmp(s, L"ESC", 3)) return VK_ESCAPE;
    if (n == 5 && !_wcsnicmp(s, L"SPACE", 5)) return VK_SPACE;
    if (n == 5 && !_wcsnicmp(s, L"ENTER", 5)) return VK_RETURN;
    if (n == 3 && !_wcsnicmp(s, L"TAB", 3)) return VK_TAB;
    if (n == 4 && !_wcsnicmp(s, L"CTRL", 4)) return VK_CONTROL; // (held over another key's time: Ctrl+F11)
    if (n == 3 && !_wcsnicmp(s, L"ALT", 3)) return VK_MENU;
    if (n == 5 && !_wcsnicmp(s, L"SHIFT", 5)) return VK_SHIFT;
    if (n == 4 && !_wcsnicmp(s, L"LEFT", 4)) return VK_LEFT;
    if (n == 5 && !_wcsnicmp(s, L"RIGHT", 5)) return VK_RIGHT;
    if (n == 2 && !_wcsnicmp(s, L"UP", 2)) return VK_UP;
    if (n == 4 && !_wcsnicmp(s, L"DOWN", 4)) return VK_DOWN;
    if (n == 3 && !_wcsnicmp(s, L"LMB", 3)) return -1;
    if (n == 3 && !_wcsnicmp(s, L"RMB", 3)) return -2;
    if (n == 4 && !_wcsnicmp(s, L"MOVE", 4)) return -3;
    if ((n == 2 || n == 3) && towupper(s[0]) == L'F') {
        int k = _wtoi(s + 1);
        if (k >= 1 && k <= 12) return VK_F1 + k - 1;
    }
    return 0;
}
static void e2eSend(const E2eKey &k, bool down) {
    // only into the game: with another program's window in front the key would be typed there
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) {
        logf("e2e: %s %s NOT SENT - the game's window is not in front", k.name, down ? "down" : "up");
        return;
    }
    if (k.vk == -3) { // the picture fills the window's inside
        if (!down) return;
        HWND w = GetForegroundWindow();
        RECT rc = {};
        GetClientRect(w, &rc);
        POINT pt = {(LONG)(k.mx * rc.right), (LONG)(k.my * rc.bottom)};
        ClientToScreen(w, &pt);
        BOOL ok = SetCursorPos(pt.x, pt.y);
        logf("e2e: the pointer to %.3f, %.3f of the picture (screen %ld, %ld)%s", k.mx, k.my, pt.x, pt.y,
             ok ? "" : " - NOT MOVED");
        return;
    }
    INPUT in = {};
    if (k.vk < 0) {
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = k.vk == -1 ? (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP)
                                   : (down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP);
    } else { // by its scan code: that is what DirectInput reads
        in.type = INPUT_KEYBOARD;
        in.ki.wScan = (WORD)MapVirtualKeyW((UINT)k.vk, MAPVK_VK_TO_VSC);
        in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
        // (the arrow keys share their scan codes with the number pad's: the extended flag tells them apart)
        if (k.vk >= VK_LEFT && k.vk <= VK_DOWN) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    UINT sent = SendInput(1, &in, sizeof in);
    logf("e2e: %s %s at %.2f s%s", k.name, down ? "down" : "up",
         g_e2eSeat ? (float)(GetTickCount64() - g_e2eSeat) / 1000.0f : -1.0f, sent ? "" : " - NOT SENT");
}

static void e2eInit() {
    static bool done;
    if (done) return;
    done = true;
    wchar_t plan[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"MW4VR_E2E", plan, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    wchar_t b[512];
    // perf=1: the frames' times are recorded (perf.cpp) and the eyes' pictures are not read back for their colours -
    // reading them makes the graphics card wait
    g_e2ePerf = GetPrivateProfileIntW(L"e2e", L"perf", 0, plan); // (2: with the sampler)
    for (int k = 0; k < 2; ++k) { // yaw=, and pitch= (degrees downwards) written the same way
        GetPrivateProfileStringW(L"e2e", k ? L"pitch" : L"yaw", L"", b, 512, plan);
        for (const wchar_t *p = b; *p;) {
            wchar_t *end;
            float t = wcstof(p, &end);
            if (end == p || *end != L':') break;
            p = end + 1;
            float deg = wcstof(p, &end);
            if (end == p) break;
            (k ? g_e2ePitch : g_e2eYaw).push_back({t, deg});
            p = end;
        }
    }
    GetPrivateProfileStringW(L"e2e", L"shots", L"", b, 512, plan);
    for (const wchar_t *p = b; *p;) {
        wchar_t *end;
        float t = wcstof(p, &end);
        if (end == p) break;
        g_e2eShots.push_back(t);
        p = end;
    }
    GetPrivateProfileStringW(L"e2e", L"keys", L"", b, 512, plan);
    for (const wchar_t *p = b; *p;) {
        while (*p == L' ') ++p;
        wchar_t *end;
        E2eKey k = {};
        k.at = wcstof(p, &end);
        if (end == p || *end != L':') break;
        const wchar_t *name = end + 1, *q = name;
        while (*q && *q != L':' && *q != L' ') ++q;
        k.vk = e2eVk(name, (size_t)(q - name));
        snprintf(k.name, sizeof k.name, "%.*ls", (int)min((size_t)7, (size_t)(q - name)), name);
        k.hold = 0.1f;
        p = q;
        if (*p == L':') {
            k.hold = wcstof(p + 1, &end);
            p = end;
            if (k.vk == -3) { // x,y
                k.mx = k.hold, k.hold = 0;
                if (*p == L',') k.my = wcstof(p + 1, &end), p = end;
            }
        }
        if (!k.vk) {
            logf("e2e: key '%s' of the plan is not known - left out", k.name);
            continue;
        }
        g_e2eKeys.push_back(k);
    }
    g_e2eCsv = _wfsopen((g_dir + L"mw4vr_e2e.csv").c_str(), L"w", _SH_DENYNO);
    if (!g_e2eCsv) {
        logf("e2e: mw4vr_e2e.csv cannot be written - no test");
        return;
    }
    fprintf(g_e2eCsv, "frame,ms,t,seated,own,yaw,lr,lg,lb,rr,rg,rb,sx,sy,sz,ld,lo,lk,rd,ro,rk,outside,menu,draws,hud,"
                      "flat,clean,cockpit\n");
    g_e2eStart = GetTickCount64();
    g_e2eOn = true;
    if (g_e2ePerf) perfStart(g_e2ePerf >= 2);
    logf("e2e: test run - plan %ls, %u turns of the head, %u pictures, %u keys", plan, (unsigned)g_e2eYaw.size(),
         (unsigned)g_e2eShots.size(), (unsigned)g_e2eKeys.size());
}

// seconds since the camera first sat in the mech; below 0: not yet
float e2eTime() { return g_e2eSeat ? (float)(GetTickCount64() - g_e2eSeat) / 1000.0f : -1.0f; }

// what a plan of seconds:degrees asks for now, straight lines between
static float e2ePlanNow(const std::vector<std::pair<float, float>> &plan) {
    e2eInit();
    if (!g_e2eOn || plan.empty()) return 0;
    float t = e2eTime();
    if (t <= plan.front().first) return t < 0 ? 0 : plan.front().second;
    for (size_t i = 1; i < plan.size(); ++i) {
        const auto &a = plan[i - 1], &b = plan[i];
        if (t <= b.first)
            return b.first > a.first ? a.second + (b.second - a.second) * (t - a.first) / (b.first - a.first)
                                     : b.second;
    }
    return plan.back().second;
}

// the head's turn to the right the plan asks for now, in degrees
float e2eYawNow() { return e2ePlanNow(g_e2eYaw); }

// and its nod downwards
float e2ePitchNow() { return e2ePlanNow(g_e2ePitch); }

// once for every picture the game presents (a paused battle's menu too): the plan's keys go down and up
void e2eKeysStep() {
    if (!g_e2eOn || !g_e2eSeat) return;
    float t = (float)(GetTickCount64() - g_e2eSeat) / 1000.0f;
    for (E2eKey &k : g_e2eKeys) {
        if (k.state == 0 && t >= k.at)
            e2eSend(k, true), k.state = 1;
        else if (k.state == 1 && t >= k.at + k.hold)
            e2eSend(k, false), k.state = 2;
    }
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

// once for every frame with both eyes, before shot.cpp's pictures are taken - and for every flat picture of a menu's
// screen (r is null then; the record's "flat" is 1 and both eyes have the one picture's colour)
void e2eFrame(ID3D11DeviceContext *ctx, ID3D11Texture2D *l, ID3D11Texture2D *r) {
    e2eInit();
    if (!g_e2eOn || !ctx || !l) return;
    const bool flat = !r;
    if (flat) r = l;
    // In the mech: the game draws its cockpit and the camera is not outside - for half a second without a break. (A
    // battle begins with a flight around the mech; its very first frame has the cockpit, and started the clock.)
    static int inside;
    const bool in = g_mechSeated && !g_mechSeat.outside && !flat;
    if (!flat) inside = in ? inside + 1 : 0;
    if (inside >= 45 && !g_e2eSeat) {
        g_e2eSeat = GetTickCount64();
        logf("e2e: the camera sits in the mech at frame %u - the plan's clock starts", g_frame);
    }
    float t = e2eTime();
    // (a time below zero is a frame's number: pictures of a battle's first frames, before the plan's clock runs)
    if (g_e2eShotNext < g_e2eShots.size()) {
        const float s = g_e2eShots[g_e2eShotNext];
        if (s < 0 ? !flat && (float)g_frame >= -s : t >= s) {
            ++g_e2eShotNext;
            g_shotOne = 1;
        }
    }
    float a[3] = {-1, -1, -1}, b[3] = {-1, -1, -1};
    if (!g_e2ePerf) {
        e2eMean(ctx, l, a);
        e2eMean(ctx, r, b);
    }
    wchar_t name[64] = L"";
    int own = e2eCockpit(name, 64);
    int pl[3], pr[3], st[4];
    e2ePass(0, pl), e2ePass(1, pr), e2eState(st);
    fprintf(g_e2eCsv,
            "%u,%llu,%.3f,%d,%d,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.4f,%.4f,%.4f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%"
            "ls\n",
            g_frame, GetTickCount64() - g_e2eStart, t, in ? 1 : 0, own, e2eYawNow(), a[0], a[1], a[2], b[0], b[1], b[2],
            g_swayValid ? g_swayT[0] : 0.0f, g_swayValid ? g_swayT[1] : 0.0f, g_swayValid ? g_swayT[2] : 0.0f, pl[0],
            pl[1], pl[2], pr[0], pr[1], pr[2], g_mechSeat.outside ? 1 : 0, st[0], st[1], st[2], flat ? 1 : 0, st[3],
            name);
    if ((g_frame & 31) == 0) fflush(g_e2eCsv);
}
