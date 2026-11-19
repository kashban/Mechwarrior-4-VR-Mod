// ---------------------------------------------------------------------------------------------
// The hooks of the device's and the context's methods (vtable slots): creating shaders, buffers and deferred
// contexts, binding shaders and constant buffers, Map / Unmap / UpdateSubresource, the draws - and what happens
// at Present.

#include "mod.h"

// a picture that stands still: who held the frame up - the game, the system's Present, or our own work in it?
DWORD g_presOurs, g_presReal, g_presWait; // ms: our part of the last Present, the system's, xrWaitFrame in ours

// The keys' page is up (help.cpp): the frame just handed over is handed over again and again, until Ctrl+F1 is
// pressed once more. The game is not returned to in that time, so it does nothing; what the page took is taken
// off the clocks it reads.
static void helpPause(IDXGISwapChain *sc, int eye) {
    if (!g_helpOn || !g_helpPause || !g_vr.ok) return;
    LARGE_INTEGER q0, q1;
    QueryPerformanceCounter(&q0);
    DWORD t0 = GetTickCount();
    logf("help: the keys' page is up, the game waits (frame %u)", g_frame);
    while (g_helpOn && g_vr.ok) {
        MSG msg;
        PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE); // (the window stays "responding"; nothing is taken out)
        helpKey(true);
        if (!g_helpOn) break;
        shotKeys(); // Ctrl+F12 takes a picture of the page too
        DWORD a = GetTickCount();
        vrOnPresent(sc, eye);
        if (GetTickCount() - a < 3) Sleep(8); // no frame of the runtime's to wait for: not a busy loop
        if (GetTickCount() - t0 > 15 * 60 * 1000) g_helpOn = false; // a quarter of an hour: back to the game
    }
    g_helpOn = false;
    for (int i = 0; i < 100 && (GetAsyncKeyState(VK_ESCAPE) & 0x8000); ++i)
        Sleep(10); // Esc let go: its release is not the game's either
    helpForgetKeys();
    QueryPerformanceCounter(&q1);
    DWORD ms = GetTickCount() - t0;
    if (g_helpClock) g_helpOffQpc += q1.QuadPart - q0.QuadPart, g_helpOffMs += ms;
    logf("help: closed after %.1f s%s", ms / 1000.0f,
         g_helpClock ? "" : " (the clocks are left alone: the game sees one long frame)");
}

static void presentGap() {
    static DWORD last;
    DWORD now = GetTickCount();
    if (last && now - last >= 3000)
        logf("present: no frame for %.1f s (frame %u; of the last Present our part took %.1f s - xrWaitFrame %.1f s -, "
             "the system's %.1f s; "
             "the rest was the game's)",
             (now - last) / 1000.0f, g_frame, g_presOurs / 1000.0f, g_presWait / 1000.0f, g_presReal / 1000.0f);
    last = now;
}

void onPresent(IDXGISwapChain *sc) {
    presentGap();
    if (!g_firstPresentLogged) {
        g_firstPresentLogged = true;
        DXGI_SWAP_CHAIN_DESC d;
        if (SUCCEEDED(sc->GetDesc(&d)))
            logf("first Present: %ux%u fmt %u buffers %u swap effect %u", d.BufferDesc.Width, d.BufferDesc.Height,
                 (unsigned)d.BufferDesc.Format, d.BufferCount, (unsigned)d.SwapEffect);
    }
    if (g_cap) endCapture();
    { // the eye this frame was rendered for (as eyePatch chose it during the frame)
        int mode = g_eyeMode, eye = -1;
        if (mode == 1)
            eye = 1;
        else if (mode == 2)
            eye = 0;
        else if (mode == 3)
            eye = (g_frame & 1) ? 0 : 1;
        mouseInstall();
        menuModeUpdate();
        if (g_menuMode) rpIdle();
        mouseCapture();
        camKeepKey();
        mw5OnPresent();
        shotKeys();
        bool inBattle = g_vr.ok && g_hudLayerOn && g_srcCamActive && g_camEyeFrame != ~0u &&
                        g_frame - g_camEyeFrame < 30 && !g_menuMode;
        if (inBattle) helpClocks();
        helpKey(inBattle);
        vrOnPresent(sc, eye);
        helpPause(sc, eye);
        camAtPresent();
    }
    ++g_frame;
    ++g_statFrames;
    eyeOnPresent();
    buildK();
    vrAfterBuildK();

    if (g_imm) {
        if (*(void ***)g_imm != g_immVt) {
            logf("immediate context vtable pointer changed %p -> %p", g_immVt, *(void ***)g_imm);
            g_immVt = *(void ***)g_imm;
        }
        hookContext(g_imm, false, true); // repairs slots the runtime rewrote in place
        hotpatchScan(g_imm);
    }

    {
        std::lock_guard<std::mutex> lk(g_labelMx);
        if (g_frameLabels != g_lastFrameLabels) {
            if (g_dbg)
                logf("frame %u top-level labels: %s", g_frame,
                     g_frameLabels.empty() ? "(none)" : g_frameLabels.c_str());
            g_lastFrameLabels = g_frameLabels;
        }
        g_frameLabels.clear();
    }

    DWORD now = GetTickCount();
    if (!g_statT0) g_statT0 = now;
    if (now - g_statT0 >= 5000) {
        float s = (now - g_statT0) / 1000.0f, f = g_statFrames ? (float)g_statFrames : 1.0f;
        static unsigned reports;
        bool all = g_dbg != 0; // else the first line alone, every sixth time (30 s)
        if (all || reports++ % 6 == 0)
            logf("%.1f fps | per frame: %.0f draws imm, %.0f draws deferred, %.0f dispatches, %.0f maps, "
                 "%.0f labels, %.1f FinishCommandList, %.1f ExecuteCommandList | shaders %u, contexts %u",
                 g_statFrames / s, g_statDrawsImm / f, g_statDrawsDef / f, g_statDisp / f, g_statMaps / f,
                 g_statLabels / f, g_statFinish / f, g_statExec / f, g_shaderCount, (unsigned)g_ctx.size());
        if (all) logf("   CPU-path draws with the replacement shader per frame %.1f", g_statTlSwaps / f);
        g_statTlSwaps = 0;
        g_statT0 = now;
        g_statFrames = 0;
        g_statDrawsImm = g_statDrawsDef = g_statDisp = g_statMaps = g_statLabels = g_statExec = g_statFinish = 0;
    }

    static bool keyWas;
    bool key = g_dbg && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
               (GetAsyncKeyState(VK_F11) & 0x8000);
    bool trig = key && !keyWas;
    keyWas = key;
    if (g_dbg && g_frame % 30 == 0) {
        std::wstring f = g_dir + L"mw4vr_capture.txt";
        if (GetFileAttributesW(f.c_str()) != INVALID_FILE_ATTRIBUTES) {
            DeleteFileW(f.c_str());
            trig = true;
        }
    }
    if (trig) startCapture();
}

// ---------------------------------------------------------------------------------------------
// hooks: device

HRESULT STDMETHODCALLTYPE H_CreateBuffer(ID3D11Device *d, const D3D11_BUFFER_DESC *desc,
                                         const D3D11_SUBRESOURCE_DATA *init, ID3D11Buffer **out) {
    HRESULT hr = ORIG(PFN_CreateBuffer, d, VT_Dev_CreateBuffer)(d, desc, init, out);
    if (d == g_foreignDev) return hr;
    if (SUCCEEDED(hr) && out && *out && desc && (desc->BindFlags & D3D11_BIND_CONSTANT_BUFFER)) {
        std::lock_guard<std::mutex> lk(g_shMx);
        g_cbSize[*out] = desc->ByteWidth;
    }
    return hr;
}

#define SHADER_HOOK(Kind, letter)                                                                                      \
    HRESULT STDMETHODCALLTYPE H_Create##Kind(ID3D11Device *d, const void *code, SIZE_T len, ID3D11ClassLinkage *cl,    \
                                             void **out) {                                                             \
        HRESULT hr = ORIG(PFN_CreateShader, d, VT_Dev_Create##Kind)(d, code, len, cl, out);                            \
        if (SUCCEEDED(hr) && out && *out && d != g_foreignDev) onShader(letter, code, len, *out);                      \
        return hr;                                                                                                     \
    }
SHADER_HOOK(VertexShader, 'V')
SHADER_HOOK(PixelShader, 'P')
SHADER_HOOK(ComputeShader, 'C')
SHADER_HOOK(GeometryShader, 'G')
SHADER_HOOK(HullShader, 'H')
SHADER_HOOK(DomainShader, 'D')

typedef HRESULT(STDMETHODCALLTYPE *PFN_CreateDeferred)(void *, UINT, void **);
#define DEFERRED_HOOK(Sfx)                                                                                             \
    HRESULT STDMETHODCALLTYPE H_CreateDeferredContext##Sfx(void *d, UINT fl, void **out) {                             \
        HRESULT hr = ORIG(PFN_CreateDeferred, d, VT_Dev_CreateDeferredContext##Sfx)(d, fl, out);                       \
        logf("CreateDeferredContext" #Sfx " on thread %lu -> 0x%08lx ctx %p", GetCurrentThreadId(), hr,                \
             out ? *out : nullptr);                                                                                    \
        if (SUCCEEDED(hr) && out && *out) hookContext((ID3D11DeviceContext *)*out, true, false);                       \
        return hr;                                                                                                     \
    }
DEFERRED_HOOK()
DEFERRED_HOOK(1)
DEFERRED_HOOK(2)
DEFERRED_HOOK(3)

// ---------------------------------------------------------------------------------------------
// hooks: context

#define SETSHADER_HOOK(St, idx)                                                                                        \
    void STDMETHODCALLTYPE H_##St##SetShader(ID3D11DeviceContext *c, void *s, ID3D11ClassInstance *const *ci,          \
                                             UINT n) {                                                                 \
        ctxOf(c)->st[idx].sh = lookupShader(s);                                                                        \
        ctxOf(c)->st[idx].obj = s;                                                                                     \
        ORIG(PFN_SetShader, c, VT_Ctx_##St##SetShader)(c, s, ci, n);                                                   \
    }
SETSHADER_HOOK(VS, ST_VS)
SETSHADER_HOOK(PS, ST_PS)
SETSHADER_HOOK(CS, ST_CS)

static void trackCBs(void *c, int st, UINT start, UINT n, ID3D11Buffer *const *b, const UINT *first) {
    CtxState *s = ctxOf(c);
    for (UINT i = 0; i < n && start + i < CB_SLOTS; ++i) {
        s->st[st].cb[start + i] = b ? b[i] : nullptr;
        s->st[st].cbOff[start + i] = first ? first[i] * 16 : 0;
    }
}
typedef void(STDMETHODCALLTYPE *PFN_SetCB1)(ID3D11DeviceContext *, UINT, UINT, ID3D11Buffer *const *, const UINT *,
                                            const UINT *);
#define SETCB_HOOK(St, idx)                                                                                            \
    void STDMETHODCALLTYPE H_##St##SetConstantBuffers(ID3D11DeviceContext *c, UINT s, UINT n,                          \
                                                      ID3D11Buffer *const *b) {                                        \
        trackCBs(c, idx, s, n, b, nullptr);                                                                            \
        ORIG(PFN_SetCB, c, VT_Ctx_##St##SetConstantBuffers)(c, s, n, b);                                               \
    }                                                                                                                  \
    void STDMETHODCALLTYPE H_##St##SetConstantBuffers1(ID3D11DeviceContext *c, UINT s, UINT n, ID3D11Buffer *const *b, \
                                                       const UINT *f, const UINT *nc) {                                \
        trackCBs(c, idx, s, n, b, f);                                                                                  \
        ORIG(PFN_SetCB1, c, VT_Ctx_##St##SetConstantBuffers1)(c, s, n, b, f, nc);                                      \
    }
SETCB_HOOK(VS, ST_VS)
SETCB_HOOK(PS, ST_PS)
SETCB_HOOK(CS, ST_CS)

typedef void(STDMETHODCALLTYPE *PFN_SetSRV)(ID3D11DeviceContext *, UINT, UINT, ID3D11ShaderResourceView *const *);
static std::mutex g_viewMx;
static std::unordered_map<void *, void *> g_viewBuf; // shader-resource view -> its buffer (null: not a buffer)
static void *viewBuffer(ID3D11ShaderResourceView *v) {
    if (!v) return nullptr;
    {
        std::lock_guard<std::mutex> lk(g_viewMx);
        auto it = g_viewBuf.find(v);
        if (it != g_viewBuf.end()) return it->second;
    }
    ID3D11Resource *r = nullptr;
    v->GetResource(&r);
    void *b = nullptr;
    if (r) {
        D3D11_RESOURCE_DIMENSION dim;
        r->GetType(&dim);
        if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) b = r;
        r->Release();
    }
    std::lock_guard<std::mutex> lk(g_viewMx);
    g_viewBuf[v] = b;
    return b;
}
void STDMETHODCALLTYPE H_VSSetShaderResources(ID3D11DeviceContext *c, UINT start, UINT n,
                                              ID3D11ShaderResourceView *const *v) {
    CtxState *s = ctxOf(c);
    for (UINT i = 0; i < n && start + i < 64; ++i) s->st[ST_VS].srvBuf[start + i] = viewBuffer(v ? v[i] : nullptr);
    ORIG(PFN_SetSRV, c, VT_Ctx_VSSetShaderResources)(c, start, n, v);
}

static void STDMETHODCALLTYPE H_Draw(ID3D11DeviceContext *c, UINT a, UINT b) {
    onWork(c, "Draw", false);
    ORIG(PFN_U2, c, VT_Ctx_Draw)(c, a, b);
}
static void STDMETHODCALLTYPE H_DrawIndexed(ID3D11DeviceContext *c, UINT a, UINT b, INT d) {
    onWork(c, "DrawIndexed", false);
    ORIG(PFN_U2I, c, VT_Ctx_DrawIndexed)(c, a, b, d);
}
static void STDMETHODCALLTYPE H_DrawInstanced(ID3D11DeviceContext *c, UINT a, UINT b, UINT d, UINT e) {
    onWork(c, "DrawInstanced", false);
    ORIG(PFN_U4, c, VT_Ctx_DrawInstanced)(c, a, b, d, e);
}
static void STDMETHODCALLTYPE H_DrawIndexedInstanced(ID3D11DeviceContext *c, UINT a, UINT b, UINT d, INT e, UINT f) {
    onWork(c, "DrawIndexedInstanced", false);
    ORIG(PFN_U3IU, c, VT_Ctx_DrawIndexedInstanced)(c, a, b, d, e, f);
}
static void STDMETHODCALLTYPE H_DrawInstancedIndirect(ID3D11DeviceContext *c, ID3D11Buffer *b, UINT o) {
    onWork(c, "DrawInstancedIndirect", false);
    ORIG(PFN_BU, c, VT_Ctx_DrawInstancedIndirect)(c, b, o);
}
static void STDMETHODCALLTYPE H_DrawIndexedInstancedIndirect(ID3D11DeviceContext *c, ID3D11Buffer *b, UINT o) {
    onWork(c, "DrawIndexedInstancedIndirect", false);
    ORIG(PFN_BU, c, VT_Ctx_DrawIndexedInstancedIndirect)(c, b, o);
}
static void STDMETHODCALLTYPE H_Dispatch(ID3D11DeviceContext *c, UINT x, UINT y, UINT z) {
    onWork(c, "Dispatch", true);
    ORIG(PFN_U3, c, VT_Ctx_Dispatch)(c, x, y, z);
}
static void STDMETHODCALLTYPE H_DispatchIndirect(ID3D11DeviceContext *c, ID3D11Buffer *b, UINT o) {
    onWork(c, "DispatchIndirect", true);
    ORIG(PFN_BU, c, VT_Ctx_DispatchIndirect)(c, b, o);
}

HRESULT STDMETHODCALLTYPE H_Map(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub, D3D11_MAP t, UINT fl,
                                D3D11_MAPPED_SUBRESOURCE *m) {
    HRESULT hr;
    {
        PerfScope perf(PF_MAP);
        hr = ORIG(PFN_Map, c, VT_Ctx_Map)(c, r, sub, t, fl, m);
    }
    InterlockedIncrement(&g_statMaps);
    if (SUCCEEDED(hr) && (void *)c == (void *)g_imm) {
        rpMapped(r, sub, t, m);
        if (m && rpRingMap(c, r, t, m)) return hr; // (the game writes into our memory: nothing is mapped)
    }
    if (SUCCEEDED(hr) && m && t != D3D11_MAP_READ && !ctxOf(c)->foreign) {
        UINT sz = cbSize(r);
        if (sz && (g_cap || g_rpMode || eyeKind(sz) || sz == 48)) { // what is looked at when it is unmapped
            CtxState *s = ctxOf(c);
            s->mapped[r] = m->pData;
            s->mapType[r] = (int)t;
        }
    }
    return hr;
}
void STDMETHODCALLTYPE H_Unmap(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub) {
    if ((void *)c == (void *)g_imm) {
        rpUnmapping(r, sub);
        if (rpRingUnmap(r)) return;
    }
    CtxState *s = ctxOf(c);
    auto it = s->mapped.find(r);
    if (it != s->mapped.end()) {
        int t = s->mapType[r];
        UINT sz = cbSize(r);
        int kind = eyeKind(sz);
        // only whole-buffer rewrites: a NO_OVERWRITE map may only fill a part we know nothing about
        if (kind && t == D3D11_MAP_WRITE_DISCARD) eyeLayoutFrom(kind, (const uint8_t *)it->second, sz);
        if (sz == 48 && t == D3D11_MAP_WRITE_DISCARD) frameCbSeen(r, it->second);
        if (t == D3D11_MAP_WRITE_DISCARD && (void *)c == (void *)g_imm) rpCbData(r, it->second, sz);
        if (g_cap) snapshot(r, it->second, 0, 0);
        s->mapped.erase(it);
        s->mapType.erase(r);
    }
    ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, r, sub);
}
static void STDMETHODCALLTYPE H_UpdateSubresource(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub,
                                                  const D3D11_BOX *box, const void *src, UINT rp, UINT dp) {
    if (g_cap) snapshot(r, src, box ? box->left : 0, box ? box->right - box->left : 0);
    ORIG(PFN_Update, c, VT_Ctx_UpdateSubresource)(c, r, sub, box, src, rp, dp);
}
static void STDMETHODCALLTYPE H_UpdateSubresource1(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub,
                                                   const D3D11_BOX *box, const void *src, UINT rp, UINT dp, UINT fl) {
    if (g_cap) snapshot(r, src, box ? box->left : 0, box ? box->right - box->left : 0);
    ORIG(PFN_Update1, c, VT_Ctx_UpdateSubresource1)(c, r, sub, box, src, rp, dp, fl);
}

// ---------------------------------------------------------------------------------------------
// hotpatches: the runtime rewrites the context's Draw*/Dispatch*/UpdateSubresource* slots every
// frame, so these are hooked in the code of d3d11.dll itself (each function starts with the
// hot-patch prologue "mov edi,edi" and has 5 spare bytes in front of it). The runtime may use more
// than one implementation per slot; every one met in a slot gets its own variant hook.

void *g_tr[SL_N][HP_VARS];     // trampolines (function + 2)
void *g_hpAddr[SL_N][HP_VARS]; // patched functions
const char *g_slName[SL_N] = {"Draw",
                              "DrawIndexed",
                              "DrawInstanced",
                              "DrawIndexedInstanced",
                              "DrawInstancedIndirect",
                              "DrawIndexedInstancedIndirect",
                              "Dispatch",
                              "DispatchIndirect",
                              "UpdateSubresource",
                              "UpdateSubresource1",
                              "ClearRenderTargetView",
                              "ClearDepthStencilView",
                              "ClearUnorderedAccessViewUint",
                              "ClearUnorderedAccessViewFloat",
                              "CopyResource",
                              "CopySubresourceRegion",
                              "GenerateMips"};

// what an UpdateSubresource writes (the data itself is passed on as it is)
void updateCommon(ID3D11Resource *r, const D3D11_BOX *box, const void *&src) {
    if (!src) return;
    UINT csz = cbSize(r);
    if (csz == 48 && !box) frameCbSeen(r, src);
    if (csz && !box) rpCbData(r, src, csz);
    int kind = eyeKind(csz);
    if (kind && !box)
        eyeLayoutFrom(kind, (const uint8_t *)src, csz);
    else if (kind && box && box->right > box->left && box->right <= csz)
        eyeLayoutFrom(kind, (const uint8_t *)src, box->right - box->left, box->left);
}

// ---------------------------------------------------------------------------------------------
