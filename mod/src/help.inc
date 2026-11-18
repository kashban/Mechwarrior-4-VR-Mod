// ---------------------------------------------------------------------------------------------
// Ctrl+F1 in a battle: the mod's keys as a page in the headset, the game waiting behind it.
// The page is drawn with GDI into a bitmap and put into the far middle of the HUD layer's picture
// (as note.inc does with its line). While it shows, Present does not return: the same frame is
// handed to the headset again and again, so the game does nothing - and so that it does not see
// one very long frame afterwards, the clock functions the game and mw4hook call are wrapped and
// the time the page was up is taken off what they return ([vr] help_pause=0: the game goes on
// behind the page; help_clock=0: the clocks are left alone).
// Included into main.cpp before vr.inc; helpPause() is called from onPresent.

static volatile bool g_helpOn;
static bool g_helpPause = true, g_helpClock = true;
static int g_helpSize = 60; // [vr] help_size: the page's size, per cent of the far middle of the HUD's picture
static std::vector<uint32_t> g_helpBits; // BGRA, top row first
static UINT g_helpW, g_helpH;

struct HelpLine {
    const wchar_t *key; // nullptr: a heading
    const wchar_t *what;
};
static const HelpLine g_helpText[] = {
    {nullptr, L"MW4VR keys   -   Ctrl+F1 or Esc closes this page, the game waits"},
    {nullptr, L"View and seat"},
    {L"Ctrl+Home", L"recentre the view"},
    {L"Ctrl+Up / Down", L"seat forward / back"},
    {L"Ctrl+Right / Left", L"seat higher / lower"},
    {L"Ctrl+Ins / Del", L"the cockpit's size (with Shift: the world's)"},
    {L"Ctrl+F10", L"this cockpit fits: remember it for this mech"},
    {L"Ctrl+PgUp / PgDn / End", L"cross-hair and target marks farther / nearer / far"},
    {nullptr, L"The HUD's parts:  1 radar  2 target  3 damage  4 lance list  5 weapons  6 top strip"},
    {L"Ctrl+Alt+1..6", L"put that part where you look"},
    {L"Ctrl+Alt+Shift+1..6", L"choose a part, then with Ctrl+Alt+Shift held:"},
    {L"      arrows", L"move it"},
    {L"      PgUp / PgDn", L"larger / smaller"},
    {L"      Ins / Del", L"farther / nearer"},
    {L"      0", L"hide it"},
    {L"Ctrl+Alt+Shift+8", L"all parts together (no cockpit of ours): then the same keys"},
    {L"Ctrl+Alt+7", L"all parts back to their automatic places"},
    {nullptr, L"MechWarrior 5 cockpits"},
    {L"Ctrl+F11", L"the cockpits on / off"},
    {L"Ctrl+Alt+F11", L"the next cockpit for this mech (with Shift: the one before)"},
    {L"Ctrl+Alt+arrows", L"your eye's place in the cockpit"},
    {L"Ctrl+Alt+PgUp / PgDn", L"the cockpit mesh's size"},
    {L"Ctrl+Alt+1..5", L"looking at a monitor: it shows that part"},
    {L"Ctrl+Alt+0", L"the monitor you look at goes dark"},
    {L"Ctrl+Alt+9", L"this cockpit's monitors off / on"},
    {L"Ctrl+Alt+Shift+9", L"the chosen part leaves its monitor and floats"},
    {L"      on a monitor", L"arrows move what it shows, PgUp / PgDn nearer / more, Ins / Del longer / shorter"},
    {L"      on a monitor", L"Home / End turn the picture left / right (a monitor that hangs askew)"},
    {nullptr, L"Other"},
    {L"Ctrl+F12", L"a picture of both eyes into mw4vr_shots"},
    {L"Ctrl+F1", L"this page"},
};
enum { HELP_LINES = sizeof g_helpText / sizeof g_helpText[0] };

static void helpBuild(UINT w, UINT h) {
    g_helpW = g_helpH = 0;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = (LONG)w, bi.bmiHeader.biHeight = -(LONG)h; // top-down
    bi.bmiHeader.biPlanes = 1, bi.bmiHeader.biBitCount = 32, bi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bm = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!bm || !bits) {
        if (dc) DeleteDC(dc);
        return;
    }
    int line = (int)h / (HELP_LINES + 2), size = line * 3 / 4;
    int left = (int)w / 28, mid = (int)w * 34 / 100, y = line;
    HGDIOBJ oldBm = SelectObject(dc, bm), oldFont = GetCurrentObject(dc, OBJ_FONT);
    HFONT font = nullptr, bold = nullptr;
    for (int tries = 0; tries < 6; ++tries) { // the font so small that no line is cut off
        font =
            CreateFontW(-size, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
        bold =
            CreateFontW(-size, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
        if (!font || !bold) break;
        float over = 1;
        for (int i = 0; i < HELP_LINES; ++i) {
            const HelpLine &t = g_helpText[i];
            SIZE s;
            SelectObject(dc, t.key ? font : bold);
            if (GetTextExtentPoint32W(dc, t.what, (int)wcslen(t.what), &s))
                over = max(over, (float)s.cx / (float)((int)w - left - (t.key ? mid : left)));
            if (t.key && GetTextExtentPoint32W(dc, t.key, (int)wcslen(t.key), &s))
                // NOLINTNEXTLINE(bugprone-integer-division): whole pixels
                over = max(over, (float)s.cx / (float)(mid - left - left / 2 - 6));
        }
        if (over <= 1 || size < 9) break;
        SelectObject(dc, oldFont);
        DeleteObject(font), DeleteObject(bold);
        font = bold = nullptr;
        size = max(8, (int)(size / over) - 1);
    }
    SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    RECT all = {0, 0, (LONG)w, (LONG)h};
    HBRUSH back = CreateSolidBrush(RGB(10, 18, 10));
    FillRect(dc, &all, back);
    DeleteObject(back);
    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < HELP_LINES; ++i, y += line) {
        const HelpLine &t = g_helpText[i];
        if (!t.key) {
            if (bold) SelectObject(dc, bold);
            SetTextColor(dc, i ? RGB(255, 220, 110) : RGB(255, 255, 255));
            RECT r = {left, y, (LONG)w - left, y + line};
            DrawTextW(dc, t.what, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            if (font) SelectObject(dc, font);
            continue;
        }
        SetTextColor(dc, RGB(150, 255, 150));
        RECT k = {left + left / 2, y, mid, y + line};
        DrawTextW(dc, t.key, -1, &k, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SetTextColor(dc, RGB(215, 235, 215));
        RECT d = {mid, y, (LONG)w - left, y + line};
        DrawTextW(dc, t.what, -1, &d, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    GdiFlush();
    g_helpBits.resize((size_t)w * h);
    const uint32_t *src = (const uint32_t *)bits;
    for (size_t i = 0; i < (size_t)w * h; ++i) g_helpBits[i] = src[i] | 0xff000000u; // GDI leaves the alpha at 0
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBm);
    if (font) DeleteObject(font);
    if (bold) DeleteObject(bold);
    DeleteObject(bm);
    DeleteDC(dc);
    g_helpW = w, g_helpH = h;
}

// into the HUD layer's picture tex (w x h used of it), while the page is up: its far middle
static void helpDraw(ID3D11DeviceContext *ctx, ID3D11Texture2D *tex, UINT w, UINT h, float L, float R, float T,
                     float B) {
    if (!g_helpOn || !ctx || !tex || w < 400 || h < 400) return;
    float part = g_helpSize / 100.0f; // of the far middle's width and height
    UINT bw = (UINT)((R - L) * w * part) & ~1u, bh = (UINT)((B - T) * h * part) & ~1u;
    if (bw < 300 || bh < 300) return;
    if (bw != g_helpW || bh != g_helpH) helpBuild(bw, bh);
    if (!g_helpW) return;
    UINT x = (UINT)((L + R) * 0.5f * w) - bw / 2, y = (UINT)((T + B) * 0.5f * h) - bh / 2;
    if (x + bw > w || y + bh > h) return;
    D3D11_BOX box = {x, y, 0, x + bw, y + bh, 1};
    ctx->UpdateSubresource(tex, 0, &box, g_helpBits.data(), bw * 4, 0);
}

// ---- the game's clocks -----------------------------------------------------------------------

typedef BOOL(WINAPI *HelpPFN_Qpc)(LARGE_INTEGER *);
typedef DWORD(WINAPI *HelpPFN_Ms)(void);
static HelpPFN_Qpc g_helpQpc;
static HelpPFN_Ms g_helpTgt, g_helpGtc;
static volatile LONGLONG g_helpOffQpc; // what the page's time adds up to, in the counter's units
static volatile DWORD g_helpOffMs;     // and in milliseconds
static BOOL WINAPI helpW_Qpc(LARGE_INTEGER *p) {
    BOOL r = g_helpQpc(p);
    if (r && p) p->QuadPart -= g_helpOffQpc;
    return r;
}
static DWORD WINAPI helpW_Tgt() { return g_helpTgt() - g_helpOffMs; }
static DWORD WINAPI helpW_Gtc() { return g_helpGtc() - g_helpOffMs; }

// every place in the module's image that holds one of the three clock functions' addresses (its import slots,
// pointers it keeps) gets our wrapper instead; -> how many
static int helpClockModule(HMODULE m) {
    if (!m) return 0;
    int n = 0;
    __try {
        const uint8_t *base = (const uint8_t *)m;
        const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
        const uint8_t *end = base + nt->OptionalHeader.SizeOfImage;
        const uintptr_t from[3] = {(uintptr_t)g_helpQpc, (uintptr_t)g_helpTgt, (uintptr_t)g_helpGtc};
        const uintptr_t to[3] = {(uintptr_t)helpW_Qpc, (uintptr_t)helpW_Tgt, (uintptr_t)helpW_Gtc};
        for (const uint8_t *p = base; p < end;) {
            MEMORY_BASIC_INFORMATION mi;
            if (!VirtualQuery(p, &mi, sizeof mi)) break;
            const uint8_t *re = (const uint8_t *)mi.BaseAddress + mi.RegionSize;
            if (re > end) re = end;
            bool readable = mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                            (mi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
            // (not the code itself: an address in an instruction is not a slot)
            bool data =
                readable && !(mi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
            if (data)
                for (uintptr_t *q = (uintptr_t *)(((uintptr_t)p + 3) & ~(uintptr_t)3); (const uint8_t *)(q + 1) <= re;
                     ++q)
                    for (int k = 0; k < 3; ++k)
                        if (from[k] && *q == from[k]) {
                            DWORD old;
                            if (VirtualProtect(q, sizeof *q, PAGE_READWRITE, &old)) {
                                *q = to[k];
                                VirtualProtect(q, sizeof *q, old, &old);
                                ++n;
                            }
                        }
            p = re;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

// once, in the first battle frame (the game is unpacked and mw4hook loaded by then)
static void helpClocks() {
    static bool done;
    if (done) return;
    done = true;
    std::wstring ini = g_dir + L"mw4vr.ini";
    g_helpPause = GetPrivateProfileIntW(L"vr", L"help_pause", 1, ini.c_str()) != 0;
    g_helpClock = g_helpPause && GetPrivateProfileIntW(L"vr", L"help_clock", 1, ini.c_str()) != 0;
    g_helpSize = min(96, max(30, (int)GetPrivateProfileIntW(L"vr", L"help_size", 60, ini.c_str())));
    if (!g_helpClock) return;
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll"), mm = GetModuleHandleW(L"winmm.dll");
    g_helpQpc = k32 ? (HelpPFN_Qpc)GetProcAddress(k32, "QueryPerformanceCounter") : nullptr;
    g_helpGtc = k32 ? (HelpPFN_Ms)GetProcAddress(k32, "GetTickCount") : nullptr;
    g_helpTgt = mm ? (HelpPFN_Ms)GetProcAddress(mm, "timeGetTime") : nullptr;
    int game = helpClockModule(GetModuleHandleW(nullptr)), hook = helpClockModule(GetModuleHandleW(L"mw4hook.dll"));
    logf("help: the clocks of the game (%d places) and of mw4hook (%d places) are wrapped - time stands still for them "
         "while the keys' page is up%s",
         game, hook, game ? "" : " (none found in the game: it will see the page's time as one long frame)");
}

// What was typed while the game waited is thrown away, so the game does not act on it afterwards (Esc: its own
// pause menu): the key-down messages in the window's queue, and the keyboard's buffer in DirectInput (through
// our dinput.dll, if it is the one loaded). The releases stay, so no key the game knew as held stays held -
// only in DirectInput's buffer they go too: a key held before the page and let go while it was up counts as
// held until it is pressed again.
static void helpForgetKeys() {
    MSG m, ups[64];
    int n = 0, dropped = 0;
    for (int i = 0; i < 4096 && PeekMessageW(&m, nullptr, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE); ++i) {
        if ((m.message == WM_KEYUP || m.message == WM_SYSKEYUP) && n < 64)
            ups[n++] = m;
        else
            ++dropped;
    }
    for (int i = 0; i < n; ++i) PostMessageW(ups[i].hwnd, ups[i].message, ups[i].wParam, ups[i].lParam);
    int di = -2;
    HMODULE d = GetModuleHandleW(L"dinput.dll");
    typedef int(WINAPI * Fn)(void);
    Fn fn = d ? (Fn)GetProcAddress(d, "MW4VR_FlushKeys") : nullptr;
    if (fn) di = fn();
    logf("help: keys typed meanwhile forgotten: %d window messages, DirectInput %s (%d)", dropped,
         di == -2 ? "not ours - nothing done"
         : di < 0 ? "has no buffer to empty"
                  : "events",
         di);
}

// Ctrl+F1, once a frame: the page on / off (on only in a battle, where it can be seen); Esc closes it too
static void helpKey(bool battle) {
    static bool was, wasEsc;
    bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && !(GetAsyncKeyState(VK_MENU) & 0x8000) &&
                (GetAsyncKeyState(VK_F1) & 0x8000);
    bool esc = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (esc && !wasEsc && g_helpOn) g_helpOn = false, Beep(400, 40);
    wasEsc = esc;
    if (down && !was) {
        if (g_helpOn)
            g_helpOn = false;
        else if (battle)
            g_helpOn = true;
        if (g_helpOn || !battle) Beep(g_helpOn ? 1000 : 400, 40);
    }
    was = down;
}
