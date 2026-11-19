// ---------------------------------------------------------------------------------------------
// The DLL's entry points and the installing: the hooks of mw4hook's annotation labels, of the swap chain and the
// factory, of the mouse; which methods of a device and a context are hooked; D3D11CreateDevice as the game calls
// it; DllMain.

#include "mod.h"

static void STDMETHODCALLTYPE H_ExecuteCommandList(ID3D11DeviceContext *c, ID3D11CommandList *l, BOOL keep) {
    InterlockedIncrement(&g_statExec);
    if (g_cap) {
        CtxState *s = ctxOf(c);
        flushPending(s);
        capLine(s, "ExecuteCommandList %p", l);
    }
    PerfScope perf(PF_EXEC);
    ORIG(PFN_Exec, c, VT_Ctx_ExecuteCommandList)(c, l, keep);
}
typedef HRESULT(STDMETHODCALLTYPE *PFN_Finish)(ID3D11DeviceContext *, BOOL, ID3D11CommandList **);
static HRESULT STDMETHODCALLTYPE H_FinishCommandList(ID3D11DeviceContext *c, BOOL keep, ID3D11CommandList **out) {
    InterlockedIncrement(&g_statFinish);
    HRESULT hr = ORIG(PFN_Finish, c, VT_Ctx_FinishCommandList)(c, keep, out);
    if (g_cap) {
        CtxState *s = ctxOf(c);
        flushPending(s);
        capLine(s, "FinishCommandList -> %p", out ? *out : nullptr);
    }
    return hr;
}

// ---------------------------------------------------------------------------------------------
// hooks: annotations

typedef INT(STDMETHODCALLTYPE *PFN_Begin)(ID3DUserDefinedAnnotation *, LPCWSTR);
// The water's planar reflection (the scene drawn again, mirrored) is drawn once per frame: mw4hook clears its
// "wanted" flag (a byte at mw4hook+0x43d3e0, asked at +0x9e5f0) when the first pass has used it, and the second
// eye's water shows the sky alone. When the right eye's pass comes to the water the flag is set again if the
// left eye's pass had it. Only where mw4hook's code is as known (build 20261107_ea16ef2). [vr] water_both_eyes=0: not.
static void waterBothEyes() {
    static int want = -1, ok = -1;
    static uint8_t *h;
    static uint8_t left;
    if (want < 0) want = GetPrivateProfileIntW(L"vr", L"water_both_eyes", 1, (g_dir + L"mw4vr.ini").c_str());
    if (!want || ok == 0) return;
    if (ok < 0) {
        h = hkBase();
        uint8_t code[7] = {};
        uint32_t at = h ? (uint32_t)(uintptr_t)(h + HK_WATER_REFLECT) : 0;
        ok = h && camRead(h + HK_AT_WATER_CHECK, code, 7) && code[0] == 0x80 && code[1] == 0x3d &&
             memcmp(code + 2, &at, 4) == 0 && code[6] == 0;
        logf("water: reflection for both eyes %s",
             ok ? "on (mw4hook's code as known)" : "off - mw4hook's code is not the known one");
        if (!ok) return;
    }
    uint8_t *flag = h + HK_WATER_REFLECT;
    if (g_camPassEye == 0)
        left = *flag;
    else if (g_camPassEye == 1 && left == 1 && *flag == 0) {
        *flag = 1;
        static bool told;
        if (!told) {
            told = true;
            logf("water: the reflection's flag set again for the right eye's pass (frame %u)", g_frame);
        }
    }
}

static INT STDMETHODCALLTYPE H_BeginEvent(ID3DUserDefinedAnnotation *a, LPCWSTR name) {
    InterlockedIncrement(&g_statLabels);
    CtxState *s = ctxOfAnn(a);
    if (!g_labelsSeen) {
        g_labelsSeen = true;
        logf("first annotation label: '%s'", narrow(name).c_str());
    }
    if (s->depth == 0) {
        std::lock_guard<std::mutex> lk(g_labelMx);
        if (g_frameLabels.size() < 1500) {
            if (!g_frameLabels.empty()) g_frameLabels += " > ";
            if (s->deferred) g_frameLabels += "[c" + std::to_string(s->id) + "d] ";
            g_frameLabels += narrow(name);
        }
    }
    if (s->depth == 0 && !s->deferred) {
        g_in3D = name && wcscmp(name, HK_LBL_3D) == 0;
        if (g_in3D) g_in3DSeen = true;
    }
    if (name && wcsstr(name, HK_LBL_TONEMAP)) InterlockedIncrement(&g_tonemapCount);
    if (g_camInMain && s->depth <= 6 && !s->deferred && name && wcsncmp(name, HK_LBL_WATER, 11) == 0) waterBothEyes();
    if (g_lblDump && g_camInMain && s->depth <= 6) { // the labels of both eyes' passes, once (cam.cpp)
        std::lock_guard<std::mutex> lk(g_labelMx);
        std::string &b = g_lblBuf[g_lblEye & 1];
        if (b.size() < 60000) {
            b.append(2 * (size_t)s->depth, ' ');
            if (s->deferred) b += "[c" + std::to_string(s->id) + "d] ";
            b += narrow(name);
            b += "\n";
        }
    }
    if (s->depth == 0 && !s->deferred) {
        g_inHud = name && wcsncmp(name, HK_LBL_HUD, 8) == 0;
        if (g_inHud) hudBegin((ID3D11DeviceContext *)s->self);
        if (g_inHud && (hudShift() != 0 || hudWideNow())) applyViewports((ID3D11DeviceContext *)s->self, s, true);
    }
    if (s->depth == 1 && !s->deferred && g_in3D && name && g_hudW) {
        // inside "3D": a camera smaller than the frame (drawn with "SSAA 2x2") is a window of the HUD
        bool smallCam = wcsncmp(name, HK_LBL_SSAA, 8) == 0;
        if (smallCam) { // "... a camera at twice the resolution, 225x170 at 0,1188": where its window lies
            const wchar_t *v = wcsstr(name, HK_LBL_SSAA_SIZE);
            int x = 0, y = 0, w = 0, hh = 0;
            if (v && swscanf_s(v + 12, L"%dx%d at %d,%d", &w, &hh, &x, &y) == 4 && w > 0) {
                float cu = (x + 0.5f * w) / g_hudW;
                g_smallCamSide = cu < 0.3f || cu > 0.7f;
            }
        }
        if (!smallCam && wcsncmp(name, HK_LBL_CAMERA, 7) == 0) {
            const wchar_t *v = wcsstr(name, HK_LBL_VIEWPORT);
            int x = 0, y = 0, w = 0, hh = 0;
            if (v && swscanf_s(v, L"viewport %d,%d %dx%d", &x, &y, &w, &hh) == 4 && w > 0 &&
                (UINT)w * 10 < g_hudW * 7) {
                smallCam = true;
                hudCamRect(x, y, w, hh);
            }
        }
        if (smallCam) hudBeginKnown((ID3D11DeviceContext *)s->self);
    }
    if (g_cap) {
        flushPending(s);
        capLine(s, "+ %s", narrow(name).c_str());
        if (s->depth == 0 && !s->deferred) {
            std::string what = "slots at label '" + narrow(name) + "':";
            dumpSlots(g_imm, what.c_str());
        }
    }
    if (!s->deferred) rpLabel(name, s->depth, true);
    ++s->depth;
    {
        std::string l = narrow(name);
        if (l.size() > 70) l.resize(70);
        s->labels.push_back(l);
    }
    return ORIG(PFN_Begin, a, VT_Ann_BeginEvent)(a, name);
}
typedef INT(STDMETHODCALLTYPE *PFN_End)(ID3DUserDefinedAnnotation *);
static INT STDMETHODCALLTYPE H_EndEvent(ID3DUserDefinedAnnotation *a) {
    CtxState *s = ctxOfAnn(a);
    if (g_cap) flushPending(s);
    if (s->depth > 0) --s->depth;
    if (!s->deferred) rpLabel(nullptr, s->depth, false);
    if (!s->labels.empty()) s->labels.pop_back();
    if (s->depth == 0 && !s->deferred) {
        if (g_in3D) hudEnd((ID3D11DeviceContext *)s->self); // a small camera's redirect ends with its "3D"
        g_smallCamSide = false;
        g_in3D = false;
        if (g_inHud && (hudShift() != 0 || hudWideNow())) applyViewports((ID3D11DeviceContext *)s->self, s, false);
        if (g_inHud) hudEnd((ID3D11DeviceContext *)s->self);
        g_inHud = false;
    }
    return ORIG(PFN_End, a, VT_Ann_EndEvent)(a);
}
typedef void(STDMETHODCALLTYPE *PFN_Marker)(ID3DUserDefinedAnnotation *, LPCWSTR);
static void STDMETHODCALLTYPE H_SetMarker(ID3DUserDefinedAnnotation *a, LPCWSTR name) {
    if (g_cap) {
        CtxState *s = ctxOfAnn(a);
        flushPending(s);
        capLine(s, "* %s", narrow(name).c_str());
    }
    ORIG(PFN_Marker, a, VT_Ann_SetMarker)(a, name);
}
typedef BOOL(STDMETHODCALLTYPE *PFN_Status)(ID3DUserDefinedAnnotation *);
static BOOL STDMETHODCALLTYPE H_GetStatus(ID3DUserDefinedAnnotation *a) {
    ORIG(PFN_Status, a, VT_Ann_GetStatus)(a);
    return TRUE; // pretend a capture tool is attached, so the engine emits its labels
}

// ---------------------------------------------------------------------------------------------
// hooks: swap chain and factory

typedef HRESULT(STDMETHODCALLTYPE *PFN_Present)(IDXGISwapChain *, UINT, UINT);
static HRESULT STDMETHODCALLTYPE H_Present(IDXGISwapChain *sc, UINT si, UINT fl) {
    DWORD t0 = GetTickCount();
    if (!(fl & DXGI_PRESENT_TEST)) {
        PerfScope perf(PF_OURS);
        onPresent(sc);
    }
    DWORD t1 = GetTickCount();
    HRESULT hr;
    LONGLONG q0 = perfNow();
    {
        PerfScope perf(PF_SYS);
        hr = ORIG(PFN_Present, sc, VT_Sc_Present)(sc, si, fl);
    }
    if (!(fl & DXGI_PRESENT_TEST)) rpFrameDone((double)(perfNow() - q0) * 1000.0 / (double)perfFreq());
    g_presOurs = t1 - t0, g_presReal = GetTickCount() - t1;
    if (!(fl & DXGI_PRESENT_TEST)) perfFrame(e2eTime());
    return hr;
}
typedef HRESULT(STDMETHODCALLTYPE *PFN_Present1)(IDXGISwapChain1 *, UINT, UINT, const DXGI_PRESENT_PARAMETERS *);
static HRESULT STDMETHODCALLTYPE H_Present1(IDXGISwapChain1 *sc, UINT si, UINT fl, const DXGI_PRESENT_PARAMETERS *pp) {
    if (!(fl & DXGI_PRESENT_TEST)) onPresent(sc);
    return ORIG(PFN_Present1, sc, VT_Sc_Present1)(sc, si, fl, pp);
}
typedef HRESULT(STDMETHODCALLTYPE *PFN_Resize)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
static HRESULT STDMETHODCALLTYPE H_ResizeBuffers(IDXGISwapChain *sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl) {
    logf("ResizeBuffers %u x %ux%u fmt %u", n, w, h, (unsigned)f);
    return ORIG(PFN_Resize, sc, VT_Sc_ResizeBuffers)(sc, n, w, h, f, fl);
}

static void hookSwapChain(IDXGISwapChain *sc) {
    patch(sc, VT_Sc_Present, (void *)H_Present);
    patch(sc, VT_Sc_ResizeBuffers, (void *)H_ResizeBuffers);
    IDXGISwapChain1 *sc1 = nullptr;
    if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void **)&sc1))) {
        patch(sc1, VT_Sc_Present1, (void *)H_Present1);
        sc1->Release();
    }
}

typedef HRESULT(STDMETHODCALLTYPE *PFN_CSCHwnd)(IDXGIFactory2 *, IUnknown *, HWND, const DXGI_SWAP_CHAIN_DESC1 *,
                                                const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *, IDXGIOutput *,
                                                IDXGISwapChain1 **);
static HRESULT STDMETHODCALLTYPE H_CreateSwapChainForHwnd(IDXGIFactory2 *f, IUnknown *dev, HWND w,
                                                          const DXGI_SWAP_CHAIN_DESC1 *d,
                                                          const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fs, IDXGIOutput *o,
                                                          IDXGISwapChain1 **out) {
    DXGI_SWAP_CHAIN_DESC1 d2;
    if (d && g_renderOverride) { // larger than its window: shown stretched to it
        d2 = *d;
        d2.Scaling = DXGI_SCALING_STRETCH;
        d = &d2;
    }
    HRESULT hr = ORIG(PFN_CSCHwnd, f, VT_Fac_CreateSwapChainForHwnd)(f, dev, w, d, fs, o, out);
    if (d)
        logf("CreateSwapChainForHwnd: %ux%u fmt %u buffers %u effect %u flags 0x%x -> 0x%08lx", d->Width, d->Height,
             (unsigned)d->Format, d->BufferCount, (unsigned)d->SwapEffect, d->Flags, hr);
    if (SUCCEEDED(hr) && out && *out) hookSwapChain(*out);
    return hr;
}
typedef HRESULT(STDMETHODCALLTYPE *PFN_CSC)(IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **);
static HRESULT STDMETHODCALLTYPE H_CreateSwapChain(IDXGIFactory *f, IUnknown *dev, DXGI_SWAP_CHAIN_DESC *d,
                                                   IDXGISwapChain **out) {
    HRESULT hr = ORIG(PFN_CSC, f, VT_Fac_CreateSwapChain)(f, dev, d, out);
    if (d)
        logf("CreateSwapChain: %ux%u fmt %u buffers %u effect %u -> 0x%08lx", d->BufferDesc.Width, d->BufferDesc.Height,
             (unsigned)d->BufferDesc.Format, d->BufferCount, (unsigned)d->SwapEffect, hr);
    if (SUCCEEDED(hr) && out && *out) hookSwapChain(*out);
    return hr;
}

// ---------------------------------------------------------------------------------------------
// installing

int hookContext(ID3D11DeviceContext *ctx, bool deferred, bool repair) {
    CtxState *s = ctxOf(ctx);
    s->deferred = deferred;
    int n = 0;
    std::string names;
#define PT(obj, idx, hook)                                                                                             \
    if (patch(obj, idx, (void *)(hook))) {                                                                             \
        ++n;                                                                                                           \
        names += " " #idx;                                                                                             \
        if (repair && g_frame < 8) names += "(was " + moduleOf(g_lastReplaced) + ")";                                  \
    }
    PT(ctx, VT_Ctx_VSSetShader, H_VSSetShader);
    PT(ctx, VT_Ctx_PSSetShader, H_PSSetShader);
    PT(ctx, VT_Ctx_CSSetShader, H_CSSetShader);
    PT(ctx, VT_Ctx_VSSetConstantBuffers, H_VSSetConstantBuffers);
    PT(ctx, VT_Ctx_VSSetShaderResources, H_VSSetShaderResources);
    PT(ctx, VT_Ctx_PSSetConstantBuffers, H_PSSetConstantBuffers);
    PT(ctx, VT_Ctx_CSSetConstantBuffers, H_CSSetConstantBuffers);
    PT(ctx, VT_Ctx_Map, H_Map);
    PT(ctx, VT_Ctx_Unmap, H_Unmap);
    PT(ctx, VT_Ctx_OMSetRenderTargets, H_OMSetRenderTargets);
    PT(ctx, VT_Ctx_OMSetRenderTargetsAndUAVs, H_OMSetRenderTargetsAndUAVs);
    PT(ctx, VT_Ctx_RSSetViewports, H_RSSetViewports);
    PT(ctx, VT_Ctx_ExecuteCommandList, H_ExecuteCommandList);
    PT(ctx, VT_Ctx_FinishCommandList, H_FinishCommandList);
    n += rpHookContext(ctx);

    ID3D11DeviceContext1 *c1 = nullptr;
    if (SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&c1))) {
        if ((void *)c1 != (void *)ctx) logf("note: ID3D11DeviceContext1 pointer differs from the context");
        PT(c1, VT_Ctx_VSSetConstantBuffers1, H_VSSetConstantBuffers1);
        PT(c1, VT_Ctx_PSSetConstantBuffers1, H_PSSetConstantBuffers1);
        PT(c1, VT_Ctx_CSSetConstantBuffers1, H_CSSetConstantBuffers1);
        c1->Release();
    }
    ID3DUserDefinedAnnotation *ann = nullptr;
    if (SUCCEEDED(ctx->QueryInterface(__uuidof(ID3DUserDefinedAnnotation), (void **)&ann))) {
        {
            std::lock_guard<std::mutex> lk(g_ctxMx);
            g_annCtx[ann] = ctx;
        }
        PT(ann, VT_Ann_BeginEvent, H_BeginEvent);
        PT(ann, VT_Ann_EndEvent, H_EndEvent);
        PT(ann, VT_Ann_SetMarker, H_SetMarker);
        PT(ann, VT_Ann_GetStatus, H_GetStatus);
        ann->Release(); // the context keeps it alive; we only use the pointer as a key
    }
#undef PT
    static int repairs;
    if (!repair)
        logf("context c%d %p (%s) vtable %p: %d slots patched now", s->id, ctx, deferred ? "deferred" : "immediate",
             *(void ***)ctx, n);
    else if (n && repairs++ < 40)
        logf("frame %u: context c%d - %d slots had been overwritten, patched again:%s", g_frame, s->id, n,
             names.c_str());
    return n;
}

static void hookDevice(ID3D11Device *dev) {
    patch(dev, VT_Dev_CreateBuffer, (void *)H_CreateBuffer);
    patch(dev, VT_Dev_CreateVertexShader, (void *)H_CreateVertexShader);
    patch(dev, VT_Dev_CreatePixelShader, (void *)H_CreatePixelShader);
    patch(dev, VT_Dev_CreateComputeShader, (void *)H_CreateComputeShader);
    patch(dev, VT_Dev_CreateGeometryShader, (void *)H_CreateGeometryShader);
    patch(dev, VT_Dev_CreateHullShader, (void *)H_CreateHullShader);
    patch(dev, VT_Dev_CreateDomainShader, (void *)H_CreateDomainShader);
    patch(dev, VT_Dev_CreateDeferredContext, (void *)H_CreateDeferredContext);
    ID3D11Device3 *d3 = nullptr;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(ID3D11Device3), (void **)&d3))) {
        patch(d3, VT_Dev_CreateDeferredContext1, (void *)H_CreateDeferredContext1);
        patch(d3, VT_Dev_CreateDeferredContext2, (void *)H_CreateDeferredContext2);
        patch(d3, VT_Dev_CreateDeferredContext3, (void *)H_CreateDeferredContext3);
        d3->Release();
    } else {
        logf("no ID3D11Device3 - CreateDeferredContext1..3 not hooked");
    }

    ID3D11DeviceContext *ctx = nullptr;
    dev->GetImmediateContext(&ctx);
    if (ctx) {
        hookContext(ctx, false, false);
        if (!g_dev) g_dev = dev; // the game's device is the first one
        if (!g_imm) {
            g_imm = ctx; // keep the reference: watched for vtable swaps at every Present
            g_immVt = *(void ***)ctx;
        } else {
            ctx->Release();
        }
    }

    IDXGIDevice *dd = nullptr;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dd))) {
        IDXGIAdapter *ad = nullptr;
        if (SUCCEEDED(dd->GetAdapter(&ad))) {
            DXGI_ADAPTER_DESC adesc;
            if (SUCCEEDED(ad->GetDesc(&adesc))) logf("adapter: %s", narrow(adesc.Description).c_str());
            IDXGIFactory2 *f = nullptr;
            if (SUCCEEDED(ad->GetParent(__uuidof(IDXGIFactory2), (void **)&f))) {
                patch(f, VT_Fac_CreateSwapChainForHwnd, (void *)H_CreateSwapChainForHwnd);
                patch(f, VT_Fac_CreateSwapChain, (void *)H_CreateSwapChain);
                f->Release();
            } else {
                logf("adapter parent is not an IDXGIFactory2 - swap chain not hooked");
            }
            ad->Release();
        }
        dd->Release();
    }
    if (g_imm) hotpatchScan(g_imm);
    logf("device %p hooked (thread %lu)", dev, GetCurrentThreadId());
}

// ---------------------------------------------------------------------------------------------
// exported entry points

// render size independent of the window: mw4hook keeps the size of its output (the client area of
// its child window, a RECT at mw4hook.dll+0x4b90d0, written once by the instruction at +0x4e60b)
// and makes its swap chain and render targets from it after the device is created. Enlarged here,
// the game renders larger than its window; the swap chain is shown stretched (see above).
// mw4hook asks for the game window's client area in two places that decide the output size (when
// it makes its child window, and when it checks whether the window changed): there it gets the
// render size. Its other calls (the mouse) get the real one. Its import slot is replaced.
static uint8_t *g_hookBase;
static LONG g_renderW, g_renderH;
typedef BOOL(WINAPI *PFN_GetClientRect)(HWND, RECT *);
static PFN_GetClientRect g_realGetClientRect;
static BOOL WINAPI fakeGetClientRect(HWND h, RECT *r) {
    BOOL ok = g_realGetClientRect(h, r);
    if (ok && r && g_renderOverride) {
        uint8_t *ra = (uint8_t *)_ReturnAddress();
        if (ra == g_hookBase + 0x571af || ra == g_hookBase + 0x4e5a0) {
            r->right = r->left + g_renderW;
            r->bottom = r->top + g_renderH;
        }
    }
    return ok;
}

// The mouse. mw4hook hooks the game's GetCursorPos, SetCursorPos and ClipCursor and maps between the
// screen and the game's picture with the picture's rectangle inside the window (mw4hook.dll+0x4931dc).
// With a render size larger than the window that rectangle is in render pixels, the cursor in window
// pixels: the lower part of the menus cannot be reached, and mw4hook's own ClipCursor (which keeps
// the cursor in the picture while the window is in front) is larger than the window, so a click
// beside it takes the focus away. mw4hook calls the real functions through three pointers
// (+0x4c7310, +0x4c7314, +0x4c7318); they are replaced: the cursor's position is scaled about the
// window's corner by render size / window size on the way in, and back on the way out.
typedef BOOL(WINAPI *PFN_GetCursorPos)(POINT *);
typedef BOOL(WINAPI *PFN_SetCursorPos)(int, int);
typedef BOOL(WINAPI *PFN_ClipCursor)(const RECT *);
static PFN_GetCursorPos g_realGetCursorPos;
static PFN_SetCursorPos g_realSetCursorPos;
static PFN_ClipCursor g_realClipCursor;
static uint8_t *g_mouseBase;
static volatile bool g_mouseDone;
// the game window's corner on the screen and render size / window size; false: nothing to do
static bool mouseScale(POINT *org, float *kx, float *ky) {
    if (!g_renderOverride) return false;
    HWND w = *(HWND *)GM_HWND; // the game's window (MW4Mercs.exe)
    RECT r;
    if (!w || !GetClientRect(w, &r) || r.right < 16 || r.bottom < 16) return false;
    org->x = org->y = 0;
    if (!ClientToScreen(w, org)) return false;
    *kx = (float)g_renderW / (float)r.right;
    *ky = (float)g_renderH / (float)r.bottom;
    return true;
}
static inline LONG mouseRound(float v) { return (LONG)floorf(v + 0.5f); }
static BOOL WINAPI mouseGetCursorPos(POINT *p) {
    BOOL ok = g_realGetCursorPos(p);
    POINT o;
    float kx, ky;
    if (ok && p && mouseScale(&o, &kx, &ky)) {
        static bool logged;
        if (!logged) {
            logged = true;
            const LONG *pic = (const LONG *)(g_mouseBase + HK_PICTURE_RECT);
            logf("mouse: cursor scaled by %.3f x %.3f about %ld,%ld (mw4hook's picture %ld,%ld - %ld,%ld, known %d)",
                 kx, ky, o.x, o.y, pic[0], pic[1], pic[2], pic[3], (int)*(g_mouseBase + HK_PICTURE_KNOWN));
        }
        p->x = o.x + mouseRound((p->x - o.x) * kx);
        p->y = o.y + mouseRound((p->y - o.y) * ky);
    }
    return ok;
}
static BOOL WINAPI mouseSetCursorPos(int x, int y) {
    POINT o;
    float kx, ky;
    if (mouseScale(&o, &kx, &ky)) {
        x = o.x + mouseRound((x - o.x) / kx);
        y = o.y + mouseRound((y - o.y) / ky);
    }
    return g_realSetCursorPos(x, y);
}
static BOOL WINAPI mouseClipCursor(const RECT *rc) {
    POINT o;
    float kx, ky;
    if (rc && mouseScale(&o, &kx, &ky)) {
        RECT r;
        r.left = o.x + mouseRound((rc->left - o.x) / kx);
        r.top = o.y + mouseRound((rc->top - o.y) / ky);
        r.right = o.x + mouseRound((rc->right - o.x) / kx);
        r.bottom = o.y + mouseRound((rc->bottom - o.y) / ky);
        return g_realClipCursor(&r);
    }
    return g_realClipCursor(rc);
}

void mouseInstall() {
    if (g_mouseDone) return;
    uint8_t *base = hkBase();
    if (!base) return;
    __try {
        void **pg = (void **)(base + HK_PTR_GETCURSORPOS), **ps = (void **)(base + HK_PTR_SETCURSORPOS),
             **pc = (void **)(base + HK_PTR_CLIPCURSOR);
        // this build? "mov eax, [mw4hook+0x4c7310]" in its GetCursorPos, "mov eax, [mw4hook+0x4c7318]" in its clip
        const uint8_t *i1 = base + HK_AT_GETCURSORPOS, *i2 = base + HK_AT_CLIPCURSOR;
        if (i1[0] != 0xA1 || *(uint32_t *)(i1 + 1) != (uint32_t)(uintptr_t)pg || i2[0] != 0xA1 ||
            *(uint32_t *)(i2 + 1) != (uint32_t)(uintptr_t)pc) {
            logf("mouse: mw4hook's cursor mapping is not where it was - the cursor is not corrected for the render "
                 "size");
            g_mouseDone = true;
            return;
        }
        if (!*pg || !*ps || !*pc) return; // not set up yet: later
        g_mouseBase = base;
        g_realGetCursorPos = (PFN_GetCursorPos)*pg;
        g_realSetCursorPos = (PFN_SetCursorPos)*ps;
        g_realClipCursor = (PFN_ClipCursor)*pc;
        *pg = (void *)mouseGetCursorPos;
        *ps = (void *)mouseSetCursorPos;
        *pc = (void *)mouseClipCursor;
        g_mouseDone = true;
        logf("mouse: mw4hook's cursor mapping corrected for the render size");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("mouse: mw4hook not readable - the cursor is not corrected for the render size");
        g_mouseDone = true;
    }
}

// In a battle the cursor is the mouse's stick: held to one side it runs out of the game's window, and
// the window under it then takes the input. While the game's window is in front and a battle runs the
// cursor is kept inside the window ([vr] mouse_capture=0: not). Who is in front is logged when it changes.
void mouseCapture() {
    static int want = -1;
    if (want < 0) want = GetPrivateProfileIntW(L"vr", L"mouse_capture", 1, (g_dir + L"mw4vr.ini").c_str());
    HWND w = *(HWND *)GM_HWND, fg = GetForegroundWindow();
    static HWND lastFg = (HWND)(INT_PTR)-1;
    if (fg != lastFg) {
        lastFg = fg;
        wchar_t t[80] = L"", cls[60] = L"";
        if (fg) GetWindowTextW(fg, t, 80), GetClassNameW(fg, cls, 60);
        POINT cp = {};
        GetCursorPos(&cp);
        logf("focus: in front now: %s '%ls' (%ls), cursor at %ld,%ld, frame %u", fg == w ? "the game" : "NOT the game",
             t, cls, cp.x, cp.y, g_frame);
    }
    static bool held;
    bool hold = want && w && fg == w && !g_menuMode && g_srcCamActive;
    if (hold) {
        RECT r;
        POINT o = {0, 0};
        if (GetClientRect(w, &r) && r.right > 64 && r.bottom > 64 && ClientToScreen(w, &o)) {
            RECT c = {o.x + 8, o.y + 8, o.x + r.right - 8, o.y + r.bottom - 8};
            ClipCursor(&c); // every frame: other programs and the game itself lift it
            if (!held)
                logf("mouse: cursor kept inside the game's window (%ld,%ld - %ld,%ld)", c.left, c.top, c.right,
                     c.bottom);
            held = true;
        }
    } else if (held) {
        held = false;
        if (fg == w || !fg) ClipCursor(nullptr);
        logf("mouse: cursor free again (%s)", fg != w ? "another window in front" : "no battle");
    }
}

void menuModeUpdate() {
    static uint8_t *base;
    if (!base) base = hkBase();
    if (!base) return;
    __try {
        const LONG *mode =
            (const LONG *)(base + HK_SCREEN_MODE);            // the game's screen mode (what the cursor is mapped to)
        const RECT *out = (const RECT *)(base + HK_OUT_RECT); // mw4hook's output size
        LONG mw = mode[0], mh = mode[1], ow = out->right, oh = out->bottom;
        bool menu = false;
        if (mw >= 320 && mh >= 200 && mw <= 16384 && mh <= 16384 && ow > 0 && oh > 0) {
            float ma = (float)mw / mh, oa = (float)ow / oh;
            menu = fabsf(ma - 4.0f / 3.0f) < 0.02f && fabsf(oa - 4.0f / 3.0f) > 0.05f;
        }
        static LONG lw, lh;
        if (mw != lw || mh != lh) {
            lw = mw, lh = mh;
            logf("mode: the game's screen mode is %ld x %ld (output %ld x %ld) - %s", mw, mh, ow, oh,
                 menu ? "a menu" : "a battle");
        }
        g_menuMode = menu;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void renderSizeSet(int want) {
    uint8_t *base = hkBase();
    if (!base) return;
    __try {
        // this build of mw4hook? "mov [mw4hook+0x4b90d8], eax" where the width is stored
        const uint8_t *ins = base + HK_AT_WIDTH_STORE;
        if (ins[0] != 0xA3 || *(uint32_t *)(ins + 1) != (uint32_t)(uintptr_t)(base + HK_OUT_WIDTH)) {
            logf("render: mw4hook.dll is not the build this was made for - render_height ignored");
            return;
        }
        HWND child = *(HWND *)(base + HK_OUT_WINDOW);
        RECT *rc = (RECT *)(base + HK_OUT_RECT), real;
        if (!child || !GetClientRect(child, &real) || real.right <= 0 || real.bottom <= 0 || rc->right != real.right ||
            rc->bottom != real.bottom) {
            logf("render: mw4hook's output size (%ld x %ld) is not its window's - render_height ignored", rc->right,
                 rc->bottom);
            return;
        }
        if (want > 8192) want = 8192;
        LONG w = (LONG)((double)real.right * want / real.bottom + 0.5), h = want;
        if (h == real.bottom) return;
        // the import slot of GetClientRect (mw4hook.dll+0x210308), checked by the call that uses it
        void **slot = (void **)(base + HK_IAT_GETCLIENTRECT);
        const uint8_t *call = base + HK_AT_SIZE_CALL;
        if (call[0] != 0xFF || call[1] != 0x15 || *(uint32_t *)(call + 2) != (uint32_t)(uintptr_t)slot) {
            logf("render: mw4hook's size check is not where it was - render_height ignored");
            return;
        }
        g_hookBase = base;
        g_renderW = w;
        g_renderH = h;
        g_realGetClientRect = (PFN_GetClientRect)*slot;
        DWORD old;
        if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) return;
        *slot = (void *)fakeGetClientRect;
        VirtualProtect(slot, sizeof *slot, old, &old);
        // the picture's size the battles are drawn at (mw4hook.dll+0x4931c8 / +0x4931cc, kept once it is
        // worked out; the function at +0x14700 hands it out): the same
        LONG *pic = (LONG *)(base + HK_PICTURE);
        const uint8_t *rd = base + HK_AT_PICTURE_READ; // "mov eax, [mw4hook+0x4931c8]"
        bool picOk = rd[0] == 0xA1 && *(uint32_t *)(rd + 1) == (uint32_t)(uintptr_t)pic;
        if (picOk && ((pic[0] == real.right && pic[1] == real.bottom) || (pic[0] == 0 && pic[1] == 0))) {
            pic[0] = w;
            pic[1] = h;
        } else {
            logf("render: mw4hook's picture size (%ld x %ld, check %d) is not its window's - the battles may stay at "
                 "the window's size",
                 picOk ? pic[0] : 0, picOk ? pic[1] : 0, (int)picOk);
        }
        rc->right = w;
        rc->bottom = h;
        g_renderOverride = true;
        logf("render: the window is %ld x %ld, the game renders at %ld x %ld", real.right, real.bottom, w, h);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("render: mw4hook's output size not readable - render_height ignored");
    }
}

static void renderSizeApply() {
    std::wstring ini = g_dir + L"mw4vr.ini";
    int want = GetPrivateProfileIntW(L"vr", L"render_height", 0, ini.c_str());
    if (!GetPrivateProfileIntW(L"vr", L"enabled", 0, ini.c_str())) return;
    if (GetPrivateProfileIntW(L"vr", L"mouse_capture", 1, ini.c_str())) mouseInstall();
    if (want <= 0) return;
    renderSizeSet(want);
}

extern "C" HRESULT WINAPI Hook_D3D11CreateDevice(IDXGIAdapter *a, D3D_DRIVER_TYPE dt, HMODULE sw, UINT fl,
                                                 const D3D_FEATURE_LEVEL *fls, UINT nfl, UINT sdk, ID3D11Device **dev,
                                                 D3D_FEATURE_LEVEL *ofl, ID3D11DeviceContext **ctx) {
    if (!loadReal() || !r_CreateDevice) return E_FAIL;
    if (!g_dev && !g_vrInitRunning) renderSizeApply();
    HRESULT hr = r_CreateDevice(a, dt, sw, fl, fls, nfl, sdk, dev, ofl, ctx);
    logf("D3D11CreateDevice(adapter %p, driver %d, flags 0x%x, %u levels) -> 0x%08lx", a, (int)dt, fl, nfl, hr);
    if (SUCCEEDED(hr) && dev && *dev) {
        if (g_vrInitRunning) {
            ID3D11DeviceContext *ic = nullptr;
            (*dev)->GetImmediateContext(&ic);
            if (ic) {
                ctxOf(ic)->foreign = true;
                ic->Release();
            }
            g_foreignDev = *dev;
            logf("device %p belongs to the VR runtime - left alone", *dev);
        } else {
            hookDevice(*dev);
        }
    }
    return hr;
}

extern "C" HRESULT WINAPI Hook_D3D11CreateDeviceAndSwapChain(IDXGIAdapter *a, D3D_DRIVER_TYPE dt, HMODULE sw, UINT fl,
                                                             const D3D_FEATURE_LEVEL *fls, UINT nfl, UINT sdk,
                                                             const DXGI_SWAP_CHAIN_DESC *scd, IDXGISwapChain **sc,
                                                             ID3D11Device **dev, D3D_FEATURE_LEVEL *ofl,
                                                             ID3D11DeviceContext **ctx) {
    if (!loadReal() || !r_CreateDeviceAndSwapChain) return E_FAIL;
    HRESULT hr = r_CreateDeviceAndSwapChain(a, dt, sw, fl, fls, nfl, sdk, scd, sc, dev, ofl, ctx);
    logf("D3D11CreateDeviceAndSwapChain -> 0x%08lx", hr);
    if (SUCCEEDED(hr) && dev && *dev) hookDevice(*dev);
    if (SUCCEEDED(hr) && sc && *sc) hookSwapChain(*sc);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        g_self = h;
        wchar_t p[MAX_PATH];
        GetModuleFileNameW(h, p, MAX_PATH);
        if (wchar_t *s = wcsrchr(p, L'\\')) s[1] = 0;
        g_dir = p;
        g_dbg = GetPrivateProfileIntW(L"debug", L"level", 0, (g_dir + L"mw4vr.ini").c_str());
        MoveFileExW((g_dir + L"mw4vr.log").c_str(), (g_dir + L"mw4vr_prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
        g_log = _wfsopen((g_dir + L"mw4vr.log").c_str(), L"w", _SH_DENYNO);
        if (g_dbg) g_shlog = _wfsopen((g_dir + L"mw4vr_shaders.txt").c_str(), L"w", _SH_DENYNO);
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        logf("MW4VR v11.77 (debug level %d) loaded into %s", g_dbg, narrow(exe).c_str());
        if (!loadReal()) logf("ERROR: the system d3d11.dll could not be loaded");
        rpReadIni();
    }
    return TRUE;
}
