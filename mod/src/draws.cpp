// ---------------------------------------------------------------------------------------------
// The draw calls, hooked in Direct3D's own code (mw4hook rewrites the vtable slots): the replacement shader for
// the CPU path, hiding, the HUD's wide pass, the pause sheet - and the hooks of the render targets and viewports.

#include "mod.h"

int e2eCockpit(wchar_t *name, int n) {
    wcsncpy_s(name, (size_t)n, g_mw5Name, _TRUNCATE);
    name[n - 1] = 0;
    return mw5Active() ? 1 : 0;
}
void e2ePass(int e, int *v) {
    bool now = g_m5PassFrame[e] == g_frame;
    v[0] = now ? g_m5PassDraws[e] : 0, v[1] = now ? g_m5PassOrd[e] : 0, v[2] = now ? g_m5PassCk[e] : 0;
}

// CPU path: while in "3D" and an eye is active, draws with VS ad8cee09 run our replacement shader
// (same work + clip.x offset), its offset in our own constant buffer at b12; both restored after.

static ID3D11Buffer *g_eyeCB, *g_eyeCBR;
static bool g_eyeTlOn; // the draw in hand runs our shader
static float g_eyeCBval = 1e30f;

void createEyeVS() {
    if (!g_compile || !g_dev) {
        logf("eye: no D3DCompile or device - CPU path not handled");
        return;
    }
    ID3DBlob *code = nullptr, *err = nullptr;
    HRESULT hr = g_compile(g_eyeTlHlsl, sizeof(g_eyeTlHlsl) - 1, "eye_tl_vs", nullptr, nullptr, "main", "vs_5_0",
                           1 << 15 /* optimization level 3 */, 0, &code, &err);
    if (FAILED(hr) || !code) {
        logf("eye: replacement shader failed to compile (0x%08lx): %s", hr,
             err ? (const char *)err->GetBufferPointer() : "");
        if (err) err->Release();
        return;
    }
    if (err) err->Release();
    ID3D11VertexShader *vs = nullptr;
    hr = ORIG(PFN_CreateShader, g_dev, VT_Dev_CreateVertexShader)(g_dev, code->GetBufferPointer(),
                                                                  code->GetBufferSize(), nullptr, (void **)&vs);
    code->Release();
    D3D11_BUFFER_DESC bd = {144, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    ID3D11Buffer *cb = nullptr;
    HRESULT hr2 = ORIG(PFN_CreateBuffer, g_dev, VT_Dev_CreateBuffer)(g_dev, &bd, nullptr, &cb);
    if (SUCCEEDED(hr2)) hr2 = ORIG(PFN_CreateBuffer, g_dev, VT_Dev_CreateBuffer)(g_dev, &bd, nullptr, &g_eyeCBR);
    if (FAILED(hr) || FAILED(hr2) || !vs || !cb) {
        logf("eye: replacement shader or its buffer not created (0x%08lx, 0x%08lx)", hr, hr2);
        return;
    }
    g_eyeCB = cb;
    g_eyeVS = vs;
    logf("eye: replacement for VS ad8cee09 (CPU path) ready");
}

static bool eyeHidden(ID3D11DeviceContext *c) {
    uint64_t h = g_hideHash;
    if (!h || !g_in3D || ctxOf(c)->foreign) return false;
    ShaderInfo *sh = ctxOf(c)->st[ST_VS].sh;
    return sh && sh->hash == h;
}

static bool eyeTlPre(ID3D11DeviceContext *c) {
    if (!kReady() || !g_in3D || !g_eyeVS) return false;
    CtxState *s = ctxOf(c);
    if (s->foreign) return false;
    ShaderInfo *sh = s->st[ST_VS].sh;
    if (!sh || sh->hash != HK_VS_TL) return false;
    static unsigned cbFrame = ~0u;
    if (cbFrame != g_tlEpoch) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ORIG(PFN_Map, c, VT_Ctx_Map)(c, g_eyeCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            float v[36] = {};
            memcpy(v, g_K, 64);
            v[16] = g_near;
            v[17] = g_kTlOnly ? g_cockpitRange : 1e30f; // camera mode: the cockpit alone
            // our own cockpit instead of the game's: that is hidden. A large one (a large cockpit scale) reaches
            // further than the range its near parts are told by
            v[18] = (g_kTlOnly && mw5Active()) ? g_cockpitRange * max(1.0f, g_vr.cockpitScale / 1.9f) : 0.0f;
            memcpy(m.pData, v, sizeof v);
            ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, g_eyeCB, 0);
            cbFrame = g_tlEpoch;
        }
    }
    ORIG(PFN_SetShader, c, VT_Ctx_VSSetShader)(c, g_eyeVS, nullptr, 0);
    ORIG(PFN_SetCB, c, VT_Ctx_VSSetConstantBuffers)(c, 12, 1, &g_eyeCB);
    InterlockedIncrement(&g_statTlSwaps);
    g_eyeTlOn = true;
    return true;
}

static void eyeTlPost(ID3D11DeviceContext *c) {
    CtxState *s = ctxOf(c);
    g_eyeTlOn = false;
    ORIG(PFN_SetShader, c, VT_Ctx_VSSetShader)(c, s->st[ST_VS].obj, nullptr, 0);
    ID3D11Buffer *prev = (ID3D11Buffer *)s->st[ST_VS].cb[12];
    ORIG(PFN_SetCB, c, VT_Ctx_VSSetConstantBuffers)(c, 12, 1, &prev);
}

// The right eye's copy of a CPU-path draw (replay.cpp): our shader with the step from the left eye's view to
// the right eye's for every vertex, and for what is near (the cockpit) the right eye's own movement on top.
static float g_eyeRNear[16];
static bool g_eyeRNearOn;
void eyeTlRightSet(const float *k, bool on) {
    memcpy(g_eyeRNear, k, sizeof g_eyeRNear);
    g_eyeRNearOn = on;
}
bool eyeTlRightPre(ID3D11DeviceContext *c) {
    ShaderInfo *sh = ctxOf(c)->st[ST_VS].sh;
    if (!g_eyeVS || !g_eyeCBR || !sh || sh->hash != HK_VS_TL) return false;
    static unsigned frame = ~0u;
    if (frame != g_frame) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(ORIG(PFN_Map, c, VT_Ctx_Map)(c, g_eyeCBR, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
        float v[36] = {}, k[32];
        rpTlMatrices(g_eyeRNear, g_eyeRNearOn, k);
        memcpy(v, k, 64);
        memcpy(v + 20, k + 16, 64);
        v[16] = g_near;
        v[17] = g_eyeRNearOn ? g_cockpitRange : 0.0f;
        v[18] = (g_kTlOnly && mw5Active()) ? g_cockpitRange * max(1.0f, g_vr.cockpitScale / 1.9f) : 0.0f;
        v[19] = 1;
        memcpy(m.pData, v, sizeof v);
        ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, g_eyeCBR, 0);
        frame = g_frame;
    }
    ORIG(PFN_SetShader, c, VT_Ctx_VSSetShader)(c, g_eyeVS, nullptr, 0);
    ORIG(PFN_SetCB, c, VT_Ctx_VSSetConstantBuffers)(c, 12, 1, &g_eyeCBR);
    return true;
}
// back to what the left eye's draw runs with
void eyeTlRightPost(ID3D11DeviceContext *c) {
    CtxState *s = ctxOf(c);
    if (g_eyeTlOn) {
        ORIG(PFN_SetCB, c, VT_Ctx_VSSetConstantBuffers)(c, 12, 1, &g_eyeCB);
        return;
    }
    ORIG(PFN_SetShader, c, VT_Ctx_VSSetShader)(c, s->st[ST_VS].obj, nullptr, 0);
    ID3D11Buffer *prev = (ID3D11Buffer *)s->st[ST_VS].cb[12];
    ORIG(PFN_SetCB, c, VT_Ctx_VSSetConstantBuffers)(c, 12, 1, &prev);
}

// The HUD pass with the wider layer texture (hud.cpp): the vertex shaders turn pixels into clip space with
// g_screen.x = 2 / width of cb b0 'Frame'; a copy of the game's with the texture's width is bound for the draw.
static ID3D11Buffer *g_wideCB;
static bool hudWidePre(ID3D11DeviceContext *c) {
    if (!hudWideNow()) return false;
    CtxState *s = ctxOf(c);
    if (s->foreign || s->deferred) return false;
    void *b = s->st[ST_VS].cb[0];
    if (!b) return false;
    float d[12];
    {
        std::lock_guard<std::mutex> lk(g_frameCbMx);
        auto it = g_frameCb.find(b);
        if (it == g_frameCb.end()) return false;
        memcpy(d, it->second.v, 48);
    }
    d[0] *= (float)g_hudW / (float)g_hudTexW;
    if (!g_wideCB) {
        D3D11_BUFFER_DESC bd = {48, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
        if (FAILED(g_dev->CreateBuffer(&bd, nullptr, &g_wideCB)) || !g_wideCB) return false;
    }
    static float last[12];
    static bool have;
    if (!have || memcmp(last, d, 48) != 0) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(ORIG(PFN_Map, c, VT_Ctx_Map)(c, g_wideCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
        memcpy(m.pData, d, 48);
        ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, g_wideCB, 0);
        memcpy(last, d, 48);
        have = true;
    }
    ORIG(PFN_SetCB, c, VT_Ctx_VSSetConstantBuffers)(c, 0, 1, &g_wideCB);
    // a scissor at the frame's right edge would cut what lies beyond it
    D3D11_RECT sr[4];
    UINT ns = 4;
    c->RSGetScissorRects(&ns, sr);
    if (ns >= 1 && ns <= 4 && sr[0].left <= 0 && sr[0].right == (LONG)g_hudW) {
        sr[0].right = (LONG)g_hudTexW;
        c->RSSetScissorRects(ns, sr);
    }
    static bool told;
    if (!told) {
        told = true;
        logf("hud: wide HUD pass running (g_screen.x %.6f -> %.6f, scissor rects %u, first right edge %ld)",
             d[0] * g_hudTexW / g_hudW, d[0], ns, ns ? sr[0].right : 0L);
    }
    return true;
}
static void hudWidePost(ID3D11DeviceContext *c) {
    ID3D11Buffer *prev = (ID3D11Buffer *)ctxOf(c)->st[ST_VS].cb[0];
    ORIG(PFN_SetCB, c, VT_Ctx_VSSetConstantBuffers)(c, 0, 1, &prev);
}

// A small camera (the locked target's view, the zoom window) starts with the frame behind its window as
// its backdrop: mw4hook's PS 3c49bfee copies the scene, twice as large, into the camera's own target. With
// the HUD as a layer that is a piece of the eye's picture - the cockpit, moving with the head - behind the
// target. The backdrop is black instead.
static bool smallCamBackdrop(ID3D11DeviceContext *c) {
    if (!g_hudLayerOn || !g_smallCamSide) return false; // the zoom window keeps its backdrop
    CtxState *s = ctxOf(c);
    if (s->foreign || s->deferred) return false;
    ShaderInfo *sh = s->st[ST_PS].sh;
    if (!sh || sh->hash != HK_PS_BACKDROP) return false;
    ID3D11RenderTargetView *rtv = nullptr;
    c->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv) return false;
    const float black[4] = {0, 0, 0, 1};
    c->ClearRenderTargetView(rtv, black);
    rtv->Release();
    static bool told;
    if (!told) {
        told = true;
        logf("hud: a small camera's backdrop (the frame behind its window) is black now");
    }
    return true;
}

// A paused battle's menu: the game lays its blue sheet over the HUD it has drawn - the cross-hair and all the
// rest then show through behind the menu. While the menu is up (vr.cpp tells: g_fadeMenu) the HUD's image is
// emptied right before the sheet is drawn, so it holds the sheet and the menu alone.
// Which draw is the sheet? The HUD's quads are indirect draws (their vertices are made on the GPU, nothing of them
// can be read here). So it is found by its effect: a point of the HUD image that is empty otherwise is read back
// after a draw - the draw that fills it is the sheet. In every frame anew: how many things the HUD draws before the
// sheet changes from frame to frame (a mark that blinks), and with one number kept for all frames the cross-hair
// flickered through the menu. The image is emptied before each draw from a little before where the sheet was last
// (before every draw, the first time) until the point is filled; that is a few read-backs a frame.
static int g_sheetFrom =
    -1; // the indirect draw of the HUD pass (from 1) the emptying begins with; -1: not known, from the first
// the point's alpha, 0..255; -1: cannot be read
static int hudSheetPixel(ID3D11DeviceContext *c) {
    static ID3D11Texture2D *stage;
    static DXGI_FORMAT fmt;
    if (!g_hudTex || !g_hudW || !g_hudH) return -1;
    D3D11_TEXTURE2D_DESC d;
    g_hudTex->GetDesc(&d);
    int f = (int)d.Format;
    if (d.SampleDesc.Count != 1 || !((f >= 27 && f <= 31) || (f >= 87 && f <= 88) || (f >= 90 && f <= 93))) return -1;
    if (!stage || fmt != d.Format) {
        if (stage) stage->Release();
        stage = nullptr, fmt = d.Format;
        D3D11_TEXTURE2D_DESC n = d;
        n.Width = 1, n.Height = 1, n.MipLevels = 1, n.ArraySize = 1;
        n.Usage = D3D11_USAGE_STAGING, n.BindFlags = 0, n.CPUAccessFlags = D3D11_CPU_ACCESS_READ, n.MiscFlags = 0;
        if (FAILED(g_dev->CreateTexture2D(&n, nullptr, &stage)) || !stage) return -1;
    }
    UINT x = (UINT)(g_fadePts[0][0] * g_hudW), y = (UINT)(g_fadePts[0][1] * g_hudH);
    D3D11_BOX b = {x, y, 0, x + 1, y + 1, 1};
    c->CopySubresourceRegion(stage, 0, 0, 0, 0, g_hudTex, 0, &b);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ORIG(MechPFN_Map, c, VT_Ctx_Map)(c, stage, 0, D3D11_MAP_READ, 0, &m)) || !m.pData) return -1;
    int a = ((const uint8_t *)m.pData)[3];
    ORIG(MechPFN_Unmap, c, VT_Ctx_Unmap)(c, stage, 0);
    return a;
}
static unsigned g_sheetFrame = ~0u;
static int g_sheetN,
    g_sheetTries; // indirect draws into the HUD's image in this frame; searches from the first draw in this menu
static bool g_sheetLook, g_sheetFound; // this frame: the sheet is looked for; it was found (the image is clean)
// before an indirect draw. true: the image was emptied before it - hudSheetPost after it
static bool hudSheetPre(ID3D11DeviceContext *c) {
    if (!g_fadeMenu) {
        g_sheetFrom = -1, g_sheetTries = 0;
        return false;
    }
    if (!g_hudFrameRtv || !g_hudRtv) return false;
    ID3D11RenderTargetView *rt = nullptr;
    c->OMGetRenderTargets(1, &rt, nullptr);
    bool ours = rt == g_hudRtv;
    if (rt) rt->Release();
    if (!ours) return false;
    if (g_sheetFrame != g_frame) {
        static int fails;
        // not found in the frame before: the sheet came earlier than looked for (or it fades out) - from the first draw
        // again
        fails = g_sheetLook && !g_sheetFound ? fails + 1 : 0;
        if (fails >= 2) g_sheetFrom = -1, fails = 0;
        g_sheetFrame = g_frame, g_sheetN = 0, g_sheetFound = false;
        g_sheetLook =
            g_sheetFrom > 0 ||
            g_sheetTries < 8; // (a search from the first draw reads back after every draw: some a menu, not for ever)
        if (g_sheetLook && g_sheetFrom < 0) ++g_sheetTries;
    }
    ++g_sheetN;
    if (!g_sheetLook || g_sheetFound || g_sheetN < g_sheetFrom) return false;
    const float zero[4] = {0, 0, 0, 0};
    c->ClearRenderTargetView(g_hudRtv, zero);
    return true;
}
static void hudSheetPost(ID3D11DeviceContext *c) {
    if (hudSheetPixel(c) <= 0) return; // not the sheet: the next draw empties the image again
    g_sheetFound = true;
    g_sheetCleanFrame = g_frame;
    int from = g_sheetN > 4 ? g_sheetN - 4 : 1;
    static bool told;
    if (!told) {
        told = true;
        logf("hud: the paused battle's sheet is indirect draw %d of the HUD pass (frame %u) - the HUD's image is "
             "emptied before it, found anew in every frame",
             g_sheetN, g_frame);
    }
    if (g_sheetFrom < 0 || from < g_sheetFrom) g_sheetFrom = from;
}

void e2eState(int *v) {
    static unsigned last, lastFrame = ~0u;
    static int hud;
    v[0] =
        g_fadeMenu && !g_menuMode ? 1 : 0; // (a menu's own screen draws no HUD: the sheet's state is an old one there)
    v[1] = (int)(g_m5AllDraws - last);
    last = g_m5AllDraws;
    if (g_sheetFrame != lastFrame)
        lastFrame = g_sheetFrame, hud = g_sheetN; // (counted in the frame before: this one's HUD is still to come)
    v[2] = g_fadeMenu ? hud : 0;
    v[3] = g_fadeMenu && !g_menuMode && g_sheetCleanFrame == g_frame ? 1 : 0;
}

// our own cockpit, in the right eye's copy of the draw the left eye's got it at (replay.cpp)
static void mw5Right(ID3D11DeviceContext *c, Mw5PFN_DrawIdx draw, int ck) {
    g_camPassEye = 1;
    mw5Draw(c, draw, ck);
    g_camPassEye = 0;
}

template <int V> static void STDMETHODCALLTYPE HP_Draw(ID3D11DeviceContext *c, UINT a, UINT b) {
    PerfScope perfHook(PF_HOOK);
    onWork(c, "Draw", false);
    if ((void *)c == (void *)g_imm) rpWork("Draw");
    RpOwn own;
    rpRingFeed(c, 1);
    ++g_m5AllDraws;
    if (eyeHidden(c)) return;
    if (smallCamBackdrop(c)) return;
    bool sw = eyeTlPre(c);
    int ck = sw && mechMeasuring() ? mechDraw(c, false, a, b, 0, g_camPassEye == 0) : 0;
    if (sw && g_mw5On) mw5Draw(c, (Mw5PFN_DrawIdx)g_tr[SL_DRAWIDX][V], ck);
    bool hw = hudWidePre(c);
    {
        PerfScope perfD3d(PF_D3D);
        ((PFN_U2)g_tr[SL_DRAW][V])(c, a, b);
    }
    if (rpStereo(c) && rpRightBegin(c, 1)) {
        if (sw && g_mw5On) mw5Right(c, (Mw5PFN_DrawIdx)g_tr[SL_DRAWIDX][V], ck);
        ((PFN_U2)g_tr[SL_DRAW][V])(c, a, b);
        rpRightEnd(c);
    }
    if (hw) hudWidePost(c);
    if (sw) eyeTlPost(c);
}
template <int V> static void STDMETHODCALLTYPE HP_DrawIndexed(ID3D11DeviceContext *c, UINT a, UINT b, INT d) {
    PerfScope perfHook(PF_HOOK);
    onWork(c, "DrawIndexed", false);
    if ((void *)c == (void *)g_imm) rpWork("DrawIndexed");
    RpOwn own;
    rpRingFeed(c, 1);
    ++g_m5AllDraws;
    if (eyeHidden(c)) return;
    bool sw = eyeTlPre(c);
    int ck = sw && mechMeasuring() ? mechDraw(c, true, a, b, d, g_camPassEye == 0) : 0;
    if (sw && g_mw5On) mw5Draw(c, (Mw5PFN_DrawIdx)g_tr[SL_DRAWIDX][V], ck);
    bool hw = hudWidePre(c);
    {
        PerfScope perfD3d(PF_D3D);
        ((PFN_U2I)g_tr[SL_DRAWIDX][V])(c, a, b, d);
    }
    if (rpStereo(c) && rpRightBegin(c, 1)) {
        if (sw && g_mw5On) mw5Right(c, (Mw5PFN_DrawIdx)g_tr[SL_DRAWIDX][V], ck);
        ((PFN_U2I)g_tr[SL_DRAWIDX][V])(c, a, b, d);
        rpRightEnd(c);
    }
    if (hw) hudWidePost(c);
    if (sw) eyeTlPost(c);
}
template <int V>
static void STDMETHODCALLTYPE HP_DrawInstanced(ID3D11DeviceContext *c, UINT a, UINT b, UINT d, UINT e) {
    PerfScope perfHook(PF_HOOK);
    onWork(c, "DrawInstanced", false);
    if ((void *)c == (void *)g_imm) rpWork("DrawInstanced");
    RpOwn own;
    rpRingFeed(c, b);
    if (eyeHidden(c)) return;
    bool sw = eyeTlPre(c);
    bool hw = hudWidePre(c);
    {
        PerfScope perfD3d(PF_D3D);
        ((PFN_U4)g_tr[SL_DRAWINST][V])(c, a, b, d, e);
    }
    if (rpStereo(c) && rpRightBegin(c, b)) {
        ((PFN_U4)g_tr[SL_DRAWINST][V])(c, a, b, d, e);
        rpRightEnd(c);
    }
    if (hw) hudWidePost(c);
    if (sw) eyeTlPost(c);
}
template <int V>
static void STDMETHODCALLTYPE HP_DrawIndexedInstanced(ID3D11DeviceContext *c, UINT a, UINT b, UINT d, INT e, UINT f) {
    PerfScope perfHook(PF_HOOK);
    onWork(c, "DrawIndexedInstanced", false);
    if ((void *)c == (void *)g_imm) rpWork("DrawIndexedInstanced");
    RpOwn own;
    rpRingFeed(c, b);
    if (eyeHidden(c)) return;
    bool sw = eyeTlPre(c);
    bool hw = hudWidePre(c);
    {
        PerfScope perfD3d(PF_D3D);
        ((PFN_U3IU)g_tr[SL_DRAWIDXINST][V])(c, a, b, d, e, f);
    }
    if (rpStereo(c) && rpRightBegin(c, b)) {
        ((PFN_U3IU)g_tr[SL_DRAWIDXINST][V])(c, a, b, d, e, f);
        rpRightEnd(c);
    }
    if (hw) hudWidePost(c);
    if (sw) eyeTlPost(c);
}
template <int V>
static void STDMETHODCALLTYPE HP_DrawInstancedIndirect(ID3D11DeviceContext *c, ID3D11Buffer *b, UINT o) {
    PerfScope perfHook(PF_HOOK);
    onWork(c, "DrawInstancedIndirect", false);
    if ((void *)c == (void *)g_imm) rpWork("DrawInstancedIndirect");
    RpOwn own;
    rpRingFeed(c, 0);
    if (eyeHidden(c)) return;
    bool sheet = hudSheetPre(c);
    bool hw = hudWidePre(c);
    {
        PerfScope perfD3d(PF_D3D);
        ((PFN_BU)g_tr[SL_DRAWINSTIND][V])(c, b, o);
    }
    if (rpStereo(c) && rpRightBegin(c, 0)) {
        ((PFN_BU)g_tr[SL_DRAWINSTIND][V])(c, b, o);
        rpRightEnd(c);
    }
    if (hw) hudWidePost(c);
    if (sheet) hudSheetPost(c);
}
template <int V>
static void STDMETHODCALLTYPE HP_DrawIndexedInstancedIndirect(ID3D11DeviceContext *c, ID3D11Buffer *b, UINT o) {
    PerfScope perfHook(PF_HOOK);
    onWork(c, "DrawIndexedInstancedIndirect", false);
    if ((void *)c == (void *)g_imm) rpWork("DrawIndexedInstancedIndirect");
    RpOwn own;
    rpRingFeed(c, 0);
    if (eyeHidden(c)) return;
    bool hw = hudWidePre(c);
    {
        PerfScope perfD3d(PF_D3D);
        ((PFN_BU)g_tr[SL_DRAWIDXINSTIND][V])(c, b, o);
    }
    if (rpStereo(c) && rpRightBegin(c, 0)) {
        ((PFN_BU)g_tr[SL_DRAWIDXINSTIND][V])(c, b, o);
        rpRightEnd(c);
    }
    if (hw) hudWidePost(c);
}
template <int V> static void STDMETHODCALLTYPE HP_Dispatch(ID3D11DeviceContext *c, UINT x, UINT y, UINT z) {
    PerfScope perfHook(PF_HOOK);
    onWork(c, "Dispatch", true);
    if ((void *)c == (void *)g_imm) rpWork("Dispatch");
    RpOwn own;
    {
        PerfScope perfD3d(PF_D3D);
        ((PFN_U3)g_tr[SL_DISP][V])(c, x, y, z);
    }
    if (rpStereo(c) && g_rpOwn == 1 && rpRightBeginCs(c)) {
        ((PFN_U3)g_tr[SL_DISP][V])(c, x, y, z);
        rpRightEndCs(c);
    }
}
template <int V> static void STDMETHODCALLTYPE HP_DispatchIndirect(ID3D11DeviceContext *c, ID3D11Buffer *b, UINT o) {
    PerfScope perfHook(PF_HOOK);
    onWork(c, "DispatchIndirect", true);
    if ((void *)c == (void *)g_imm) rpWork("DispatchIndirect");
    RpOwn own;
    {
        PerfScope perfD3d(PF_D3D);
        ((PFN_BU)g_tr[SL_DISPIND][V])(c, b, o);
    }
    if (rpStereo(c) && g_rpOwn == 1 && rpRightBeginCs(c)) {
        ((PFN_BU)g_tr[SL_DISPIND][V])(c, b, o);
        rpRightEndCs(c);
    }
}
template <int V>
static void STDMETHODCALLTYPE HP_Upd(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub, const D3D11_BOX *box,
                                     const void *src, UINT rp, UINT dp) {
    if (!ctxOf(c)->foreign) updateCommon(r, box, src);
    if ((void *)c == (void *)g_imm) rpUpdated(r, sub, box, src);
    if (g_cap) snapshot(r, src, box ? box->left : 0, box ? box->right - box->left : 0);
    ((PFN_Update)g_tr[SL_UPD][V])(c, r, sub, box, src, rp, dp);
}
template <int V>
static void STDMETHODCALLTYPE HP_Upd1(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub, const D3D11_BOX *box,
                                      const void *src, UINT rp, UINT dp, UINT fl) {
    if (!ctxOf(c)->foreign) updateCommon(r, box, src);
    if ((void *)c == (void *)g_imm) rpUpdated(r, sub, box, src);
    if (g_cap) snapshot(r, src, box ? box->left : 0, box ? box->right - box->left : 0);
    ((PFN_Update1)g_tr[SL_UPD1][V])(c, r, sub, box, src, rp, dp, fl);
}

// what the main camera's pass clears and copies, it clears and copies for the right eye too (replay.cpp)
typedef void(STDMETHODCALLTYPE *PFN_ClearRtv)(ID3D11DeviceContext *, ID3D11RenderTargetView *, const FLOAT *);
typedef void(STDMETHODCALLTYPE *PFN_ClearDsv)(ID3D11DeviceContext *, ID3D11DepthStencilView *, UINT, FLOAT, UINT8);
typedef void(STDMETHODCALLTYPE *PFN_ClearUavU)(ID3D11DeviceContext *, ID3D11UnorderedAccessView *, const UINT *);
typedef void(STDMETHODCALLTYPE *PFN_ClearUavF)(ID3D11DeviceContext *, ID3D11UnorderedAccessView *, const FLOAT *);
typedef void(STDMETHODCALLTYPE *PFN_CopyRes)(ID3D11DeviceContext *, ID3D11Resource *, ID3D11Resource *);
typedef void(STDMETHODCALLTYPE *PFN_CopyReg)(ID3D11DeviceContext *, ID3D11Resource *, UINT, UINT, UINT, UINT,
                                             ID3D11Resource *, UINT, const D3D11_BOX *);
typedef void(STDMETHODCALLTYPE *PFN_GenMips)(ID3D11DeviceContext *, ID3D11ShaderResourceView *);
template <int V>
static void STDMETHODCALLTYPE HP_ClearRtv(ID3D11DeviceContext *c, ID3D11RenderTargetView *v, const FLOAT *f) {
    ((PFN_ClearRtv)g_tr[SL_CLRRTV][V])(c, v, f);
    if (ID3D11RenderTargetView *t = rpClearTwin(c, v)) ((PFN_ClearRtv)g_tr[SL_CLRRTV][V])(c, t, f);
}
template <int V>
static void STDMETHODCALLTYPE HP_ClearDsv(ID3D11DeviceContext *c, ID3D11DepthStencilView *v, UINT fl, FLOAT d,
                                          UINT8 st) {
    ((PFN_ClearDsv)g_tr[SL_CLRDSV][V])(c, v, fl, d, st);
    if (ID3D11DepthStencilView *t = rpClearTwin(c, v)) ((PFN_ClearDsv)g_tr[SL_CLRDSV][V])(c, t, fl, d, st);
}
template <int V>
static void STDMETHODCALLTYPE HP_ClearUavU(ID3D11DeviceContext *c, ID3D11UnorderedAccessView *v, const UINT *f) {
    ((PFN_ClearUavU)g_tr[SL_CLRUAVU][V])(c, v, f);
    if (ID3D11UnorderedAccessView *t = rpClearTwin(c, v)) ((PFN_ClearUavU)g_tr[SL_CLRUAVU][V])(c, t, f);
}
template <int V>
static void STDMETHODCALLTYPE HP_ClearUavF(ID3D11DeviceContext *c, ID3D11UnorderedAccessView *v, const FLOAT *f) {
    ((PFN_ClearUavF)g_tr[SL_CLRUAVF][V])(c, v, f);
    if (ID3D11UnorderedAccessView *t = rpClearTwin(c, v)) ((PFN_ClearUavF)g_tr[SL_CLRUAVF][V])(c, t, f);
}
template <int V>
static void STDMETHODCALLTYPE HP_CopyRes(ID3D11DeviceContext *c, ID3D11Resource *d, ID3D11Resource *sr) {
    ((PFN_CopyRes)g_tr[SL_COPYRES][V])(c, d, sr);
    ID3D11Resource *td, *ts;
    if (rpCopyTwins(c, d, sr, &td, &ts)) ((PFN_CopyRes)g_tr[SL_COPYRES][V])(c, td, ts);
}
template <int V>
static void STDMETHODCALLTYPE HP_CopyReg(ID3D11DeviceContext *c, ID3D11Resource *d, UINT ds, UINT x, UINT y, UINT z,
                                         ID3D11Resource *sr, UINT ss, const D3D11_BOX *b) {
    ((PFN_CopyReg)g_tr[SL_COPYREG][V])(c, d, ds, x, y, z, sr, ss, b);
    ID3D11Resource *td, *ts;
    if (rpCopyTwins(c, d, sr, &td, &ts)) ((PFN_CopyReg)g_tr[SL_COPYREG][V])(c, td, ds, x, y, z, ts, ss, b);
}
template <int V> static void STDMETHODCALLTYPE HP_GenMips(ID3D11DeviceContext *c, ID3D11ShaderResourceView *v) {
    ((PFN_GenMips)g_tr[SL_GENMIPS][V])(c, v);
    if (ID3D11ShaderResourceView *t = rpMipsTwin(c, v)) ((PFN_GenMips)g_tr[SL_GENMIPS][V])(c, t);
}

#define HP_ROW(f) {(void *)&f<0>, (void *)&f<1>, (void *)&f<2>, (void *)&f<3>}
static void *const g_hpHook[SL_N][HP_VARS] = {HP_ROW(HP_Draw),
                                              HP_ROW(HP_DrawIndexed),
                                              HP_ROW(HP_DrawInstanced),
                                              HP_ROW(HP_DrawIndexedInstanced),
                                              HP_ROW(HP_DrawInstancedIndirect),
                                              HP_ROW(HP_DrawIndexedInstancedIndirect),
                                              HP_ROW(HP_Dispatch),
                                              HP_ROW(HP_DispatchIndirect),
                                              HP_ROW(HP_Upd),
                                              HP_ROW(HP_Upd1),
                                              HP_ROW(HP_ClearRtv),
                                              HP_ROW(HP_ClearDsv),
                                              HP_ROW(HP_ClearUavU),
                                              HP_ROW(HP_ClearUavF),
                                              HP_ROW(HP_CopyRes),
                                              HP_ROW(HP_CopyReg),
                                              HP_ROW(HP_GenMips)};

static bool hotpatch(void *fn, void *hook) {
    uint8_t *p = (uint8_t *)fn;
    if (p[0] != 0x8B || p[1] != 0xFF) return false;
    for (int i = 1; i <= 5; ++i)
        if (p[-i] != 0xCC && p[-i] != 0x90) return false;
    DWORD old;
    if (!VirtualProtect(p - 5, 7, PAGE_EXECUTE_READWRITE, &old)) return false;
    p[-5] = 0xE9;
    *(int32_t *)(p - 4) = (int32_t)((uint8_t *)hook - p);
    *(volatile uint16_t *)p = 0xF9EB; // jmp short -5 (one aligned 2-byte store replaces "mov edi,edi")
    VirtualProtect(p - 5, 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p - 5, 7);
    return true;
}

void hotpatchScan(ID3D11DeviceContext *ctx) {
    if (!ctx || !g_real) return;
    static std::mutex mx;
    std::lock_guard<std::mutex> lk(mx);
    const int slots[SL_N] = {VT_Ctx_Draw,
                             VT_Ctx_DrawIndexed,
                             VT_Ctx_DrawInstanced,
                             VT_Ctx_DrawIndexedInstanced,
                             VT_Ctx_DrawInstancedIndirect,
                             VT_Ctx_DrawIndexedInstancedIndirect,
                             VT_Ctx_Dispatch,
                             VT_Ctx_DispatchIndirect,
                             VT_Ctx_UpdateSubresource,
                             VT_Ctx_UpdateSubresource1,
                             VT_Ctx_ClearRenderTargetView,
                             VT_Ctx_ClearDepthStencilView,
                             VT_Ctx_ClearUnorderedAccessViewUint,
                             VT_Ctx_ClearUnorderedAccessViewFloat,
                             VT_Ctx_CopyResource,
                             VT_Ctx_CopySubresourceRegion,
                             VT_Ctx_GenerateMips};
    void **vt = *(void ***)ctx;
    // (the clears' and copies' hooks are for replay.cpp alone: not placed without it)
    for (int s = 0; s < (g_rpMode ? SL_N : SL_CLRRTV); ++s) {
        void *fn = vt[slots[s]];
        HMODULE m = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)fn, &m);
        if (m != g_real) continue; // already ours or someone else's
        int free = -1;
        bool known = false;
        for (int v = 0; v < HP_VARS; ++v) {
            if (g_hpAddr[s][v] == fn) known = true;
            if (!g_hpAddr[s][v] && free < 0) free = v;
        }
        if (known) continue;
        if (free < 0) {
            static int warned;
            if (warned++ < 10) logf("hotpatch: %s - a fifth variant %p, not hooked", g_slName[s], fn);
            continue;
        }
        g_tr[s][free] = (uint8_t *)fn + 2;
        if (hotpatch(fn, g_hpHook[s][free])) {
            g_hpAddr[s][free] = fn;
            logf("hotpatch: %s variant %d at d3d11.dll+%lx", g_slName[s], free,
                 (unsigned long)((uint8_t *)fn - (uint8_t *)g_real));
        } else {
            g_tr[s][free] = nullptr;
            g_hpAddr[s][free] = fn; // do not try again
            uint8_t *p = (uint8_t *)fn;
            logf("hotpatch: %s at d3d11.dll+%lx is not patchable (%02x %02x %02x %02x %02x | %02x %02x)", g_slName[s],
                 (unsigned long)(p - (uint8_t *)g_real), p[-5], p[-4], p[-3], p[-2], p[-1], p[0], p[1]);
        }
    }
}

void STDMETHODCALLTYPE H_OMSetRenderTargets(ID3D11DeviceContext *c, UINT n, ID3D11RenderTargetView *const *rtv,
                                            ID3D11DepthStencilView *dsv) {
    if ((void *)c == (void *)g_imm) rpOmSet(n, rtv, dsv, false, 0, 0, nullptr);
    if (g_cap) setRTs(ctxOf(c), n, rtv, dsv);
    if (n >= 1 && n <= 8 && rtv && hudSwap(rtv[0]) != rtv[0]) { // inside the HUD pass: our layer texture
        ID3D11RenderTargetView *v[8];
        memcpy(v, rtv, n * sizeof *v);
        v[0] = hudSwap(rtv[0]);
        if (hudWide()) hudKeepDsv(dsv); // the wider layer texture goes without the frame's depth
        ORIG(PFN_OMSet, c, VT_Ctx_OMSetRenderTargets)(c, n, v, hudWide() ? nullptr : dsv);
        return;
    }
    ORIG(PFN_OMSet, c, VT_Ctx_OMSetRenderTargets)(c, n, rtv, dsv);
}
typedef void(STDMETHODCALLTYPE *PFN_OMSetU)(ID3D11DeviceContext *, UINT, ID3D11RenderTargetView *const *,
                                            ID3D11DepthStencilView *, UINT, UINT, ID3D11UnorderedAccessView *const *,
                                            const UINT *);
void STDMETHODCALLTYPE H_OMSetRenderTargetsAndUAVs(ID3D11DeviceContext *c, UINT n, ID3D11RenderTargetView *const *rtv,
                                                   ID3D11DepthStencilView *dsv, UINT us, UINT un,
                                                   ID3D11UnorderedAccessView *const *uav, const UINT *ic) {
    if (g_cap && n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) setRTs(ctxOf(c), n, rtv, dsv);
    if ((void *)c == (void *)g_imm) rpOmSet(n, rtv, dsv, true, us, un, uav);
    // which buffers stand behind the clutter's g_ctView (u75) and g_ctWind (u76): logged once each
    static int seen75, seen76;
    if (uav && un != D3D11_KEEP_UNORDERED_ACCESS_VIEWS)
        for (UINT i = 0; i < un; ++i) {
            UINT slot = us + i;
            int *seen = slot == 75 ? &seen75 : slot == 76 ? &seen76 : nullptr;
            if (!seen || *seen || !uav[i]) continue;
            *seen = 1;
            ID3D11Resource *res = nullptr;
            uav[i]->GetResource(&res);
            ID3D11Buffer *b = nullptr;
            if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Buffer), (void **)&b))) {
                D3D11_BUFFER_DESC d;
                b->GetDesc(&d);
                logf("eye: UAV u%u = buffer %p, %u bytes, usage %d, bind 0x%x, cpu 0x%x, misc 0x%x, stride %u", slot, b,
                     d.ByteWidth, (int)d.Usage, d.BindFlags, d.CPUAccessFlags, d.MiscFlags, d.StructureByteStride);
                b->Release();
            }
            if (res) res->Release();
        }
    ORIG(PFN_OMSetU, c, VT_Ctx_OMSetRenderTargetsAndUAVs)(c, n, rtv, dsv, us, un, uav, ic);
}
typedef void(STDMETHODCALLTYPE *PFN_RSVp)(ID3D11DeviceContext *, UINT, const D3D11_VIEWPORT *);
float hudShift() {
    int mode = g_eyeMode;
    float px = g_hudPx;
    if (!mode || px == 0) return 0;
    float sign = mode == 1 ? 1.0f : mode == 2 ? -1.0f : ((g_frame & 1) ? -1.0f : 1.0f); // +1 right eye
    return -sign * px; // the right eye's HUD to the left: in front of infinity
}
void STDMETHODCALLTYPE H_RSSetViewports(ID3D11DeviceContext *c, UINT n, const D3D11_VIEWPORT *v) {
    CtxState *s0 = ctxOf(c);
    if (!s0->foreign && !s0->deferred && v && n) {
        s0->vpN = n < 4 ? n : 4;
        memcpy(s0->vp0, v, s0->vpN * sizeof *v);
        if (g_inHud && (hudShift() != 0 || hudWideNow())) {
            applyViewports(c, s0, true);
            return;
        }
    }
    if (g_cap && n && v) {
        char b[96];
        snprintf(b, sizeof b, "%.0fx%.0f@%.0f,%.0f%s", v[0].Width, v[0].Height, v[0].TopLeftX, v[0].TopLeftY,
                 n > 1 ? " (+more)" : "");
        ctxOf(c)->vp = b;
    }
    ORIG(PFN_RSVp, c, VT_Ctx_RSSetViewports)(c, n, v);
}

void applyViewports(ID3D11DeviceContext *c, CtxState *s, bool shifted) {
    if (!s->vpN) return;
    D3D11_VIEWPORT v[4];
    memcpy(v, s->vp0, s->vpN * sizeof *v);
    if (shifted) {
        float dx = hudShift();
        for (UINT i = 0; i < s->vpN; ++i) v[i].TopLeftX += dx;
        if (hudWideNow())
            for (UINT i = 0; i < s->vpN; ++i)
                if (v[i].TopLeftX == dx && v[i].Width == (float)g_hudW) v[i].Width = (float)g_hudTexW;
    }
    ORIG(PFN_RSVp, c, VT_Ctx_RSSetViewports)(c, s->vpN, v);
}
