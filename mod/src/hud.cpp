// ---------------------------------------------------------------------------------------------
// HUD layer: while the game draws its top-level "after 3D: HUD, 2D" pass, the frame's render target
// is replaced by a transparent texture of our own. The eye images then hold the world alone, and
// the HUD goes to the headset as quads fixed to the mech (vr.cpp).

#include "mod.h"

volatile bool g_hudLayerOn; // set by vr.cpp: tracking at the camera and [vr] hud_layer=1
ID3D11Texture2D *g_hudTex;
ID3D11RenderTargetView *g_hudRtv;
ID3D11RenderTargetView *g_hudFrameRtv;        // the game's target, replaced while the HUD pass runs (a reference held)
static ID3D11RenderTargetView *g_hudKnownRtv; // the game's frame target as last seen in a HUD pass (a reference held)
UINT g_hudW, g_hudH;
unsigned g_hudClearedFrame = ~0u, g_hudDrawnFrame = ~0u;
bool g_hudInternal; // our own OMSetRenderTargets calls pass the hook untouched

// The game lays its HUD out wider than a square picture: the weapon list and the damage display run past
// the right edge. The layer texture is therefore wider than the frame ([vr] hud_extra_width, 1 = not): a
// pixel of the game is a pixel of the texture, and what lay beyond the edge is drawn as well - the HUD
// pass's full viewport is widened and its vertex shaders get the wider picture's 2 / width (draws.cpp).
// The depth buffer has the frame's size and cannot go with the wider texture: the HUD is drawn without.
UINT g_hudTexW;              // the layer texture's width (>= g_hudW, the frame's)
volatile bool g_hudWideLive; // the frame constants are known: the part beyond the edge is drawn
static ID3D11DepthStencilView
    *g_hudGameDsv; // what the game wants as depth while the layer is the target (a reference held)
UINT hudWideW(UINT w) {
    static float k = -1;
    if (k < 0) {
        wchar_t b[64];
        GetPrivateProfileStringW(L"vr", L"hud_extra_width", L"1.08", b, 64, (g_dir + L"mw4vr.ini").c_str());
        k = mcWideFactor((float)_wtof(b));
    }
    return mcWideWidth(w, k);
}
void hudKeepDsv(ID3D11DepthStencilView *dsv) {
    if (dsv) dsv->AddRef();
    if (g_hudGameDsv) g_hudGameDsv->Release();
    g_hudGameDsv = dsv;
}
static void hudSetOurs(ID3D11DeviceContext *c, ID3D11DepthStencilView *dsv) {
    if (hudWide()) hudKeepDsv(dsv);
    g_hudInternal = true;
    c->OMSetRenderTargets(1, &g_hudRtv, hudWide() ? nullptr : dsv);
    g_hudInternal = false;
}
std::mutex g_frameCbMx;
std::unordered_map<void *, FrameCb> g_frameCb;
void frameCbSeen(void *r, const void *p) {
    if (!hudWide() || !p) return;
    const float *f = (const float *)p;
    float ex = 2.0f / g_hudW, ey = 2.0f / g_hudH;
    bool full = fabsf(f[0] - ex) < ex * 0.01f && fabsf(f[1] - ey) < ey * 0.01f;
    std::lock_guard<std::mutex> lk(g_frameCbMx);
    if (!full) {
        g_frameCb.erase(r); // a small camera's
        return;
    }
    memcpy(g_frameCb[r].v, f, 48);
    if (!g_hudWideLive)
        logf("hud: frame constants seen (buffer %p: %.6f %.6f %.3f %.3f) - the HUD is drawn %u wide", r, f[0], f[1],
             f[2], f[3], g_hudTexW);
    g_hudWideLive = true;
}

HudCamRect g_hudCams[4];
int g_hudCamN;
unsigned g_hudCamFrame = ~0u;
void hudCamRect(int x, int y, int w, int h) {
    if (g_hudCamFrame != g_frame) {
        g_hudCamFrame = g_frame;
        g_hudCamN = 0;
    }
    for (int i = 0; i < g_hudCamN; ++i)
        if (g_hudCams[i].x == x && g_hudCams[i].y == y) return;
    if (g_hudCamN < 4) g_hudCams[g_hudCamN++] = {x, y, w, h};
    // every distinct window once
    static HudCamRect seen[16];
    static int nSeen;
    bool known = false;
    for (int i = 0; i < nSeen; ++i)
        if (seen[i].x == x && seen[i].y == y && seen[i].w == w && seen[i].h == h) known = true;
    if (!known && nSeen < 16) {
        seen[nSeen++] = {x, y, w, h};
        logf("hud: small camera %dx%d at %d,%d (frame %ux%u, game P00 %.3f)", w, h, x, y, g_hudW, g_hudH, g_P00);
    }
}

void hudBegin(ID3D11DeviceContext *c) {
    if (!g_hudLayerOn || !g_dev || !c || g_hudFrameRtv) return;
    ID3D11RenderTargetView *rtv = nullptr;
    ID3D11DepthStencilView *dsv = nullptr;
    c->OMGetRenderTargets(1, &rtv, &dsv);
    if (rtv) {
        ID3D11Resource *res = nullptr;
        rtv->GetResource(&res);
        ID3D11Texture2D *t = nullptr;
        if (res) res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&t);
        if (t) {
            D3D11_TEXTURE2D_DESC d;
            t->GetDesc(&d);
            if (!g_hudTex || d.Width != g_hudW || d.Height != g_hudH) {
                if (g_hudRtv) g_hudRtv->Release();
                if (g_hudTex) g_hudTex->Release();
                g_hudRtv = nullptr;
                g_hudTex = nullptr;
                D3D11_TEXTURE2D_DESC n = d;
                n.Width = hudWideW(d.Width);
                g_hudTexW = n.Width;
                g_hudWideLive = false;
                n.MipLevels = 1;
                n.ArraySize = 1;
                n.Usage = D3D11_USAGE_DEFAULT;
                n.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                n.CPUAccessFlags = 0;
                n.MiscFlags = 0;
                HRESULT hr = g_dev->CreateTexture2D(&n, nullptr, &g_hudTex);
                if (SUCCEEDED(hr)) hr = g_dev->CreateRenderTargetView(g_hudTex, nullptr, &g_hudRtv);
                g_hudW = d.Width;
                g_hudH = d.Height;
                logf("hud: layer texture %ux%u (frame %ux%u) format %d samples %u -> 0x%08lx", n.Width, d.Height,
                     d.Width, d.Height, (int)d.Format, d.SampleDesc.Count, hr);
            }
            if (g_hudRtv) {
                if (g_hudClearedFrame != g_frame) {
                    const float zero[4] = {0, 0, 0, 0};
                    c->ClearRenderTargetView(g_hudRtv, zero);
                    g_hudClearedFrame = g_frame;
                }
                g_hudFrameRtv = rtv;
                rtv->AddRef();
                if (g_hudKnownRtv != rtv) {
                    if (g_hudKnownRtv) g_hudKnownRtv->Release();
                    g_hudKnownRtv = rtv;
                    rtv->AddRef();
                }
                hudSetOurs(c, dsv);
                g_hudDrawnFrame = g_frame;
            }
            t->Release();
        }
        if (res) res->Release();
        rtv->Release();
    }
    if (dsv) dsv->Release();
}

// a small camera drawn into the frame (the zoom window, the target's view): from now until hudEnd the
// frame target is ours wherever the game sets it
void hudBeginKnown(ID3D11DeviceContext *c) {
    if (!g_hudLayerOn || !c || g_hudFrameRtv || !g_hudKnownRtv || !g_hudRtv) return;
    if (g_hudClearedFrame != g_frame) {
        const float zero[4] = {0, 0, 0, 0};
        c->ClearRenderTargetView(g_hudRtv, zero);
        g_hudClearedFrame = g_frame;
    }
    g_hudFrameRtv = g_hudKnownRtv;
    g_hudFrameRtv->AddRef();
    g_hudDrawnFrame = g_frame;
    ID3D11RenderTargetView *cur = nullptr;
    ID3D11DepthStencilView *dsv = nullptr;
    c->OMGetRenderTargets(1, &cur, &dsv);
    if (cur == g_hudKnownRtv)
        hudSetOurs(c, dsv);
    else if (hudWide())
        hudKeepDsv(nullptr);
    if (cur) cur->Release();
    if (dsv) dsv->Release();
}

void hudEnd(ID3D11DeviceContext *c) {
    if (!g_hudFrameRtv || !c) return;
    ID3D11RenderTargetView *cur = nullptr;
    ID3D11DepthStencilView *dsv = nullptr;
    c->OMGetRenderTargets(1, &cur, &dsv);
    ID3D11RenderTargetView *frame = g_hudFrameRtv;
    g_hudFrameRtv = nullptr;
    if (cur == g_hudRtv) { // still ours: back to the game's frame
        g_hudInternal = true;
        c->OMSetRenderTargets(1, &frame, hudWide() ? g_hudGameDsv : dsv);
        g_hudInternal = false;
    }
    hudKeepDsv(nullptr);
    if (cur) cur->Release();
    if (dsv) dsv->Release();
    frame->Release();
}

// both eyes per frame: after a camera's Draw the frame holds that eye's picture (before the HUD and the
// output's gamma); it is copied to a texture of that eye
ID3D11Texture2D *g_eyeTex[2];
unsigned g_eyeTexFrame[2] = {~0u, ~0u};
UINT g_eyeTexW, g_eyeTexH;
bool eyeGrab(int e) {
    if (!g_hudKnownRtv || !g_dev || !g_imm) return false;
    ID3D11Resource *res = nullptr;
    g_hudKnownRtv->GetResource(&res);
    if (!res) return false;
    ID3D11Texture2D *t = nullptr;
    res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&t);
    res->Release();
    if (!t) return false;
    D3D11_TEXTURE2D_DESC d;
    t->GetDesc(&d);
    if (!g_eyeTex[0] || d.Width != g_eyeTexW || d.Height != g_eyeTexH) {
        for (int i = 0; i < 2; ++i) {
            if (g_eyeTex[i]) g_eyeTex[i]->Release();
            g_eyeTex[i] = nullptr;
            D3D11_TEXTURE2D_DESC n = d;
            n.MipLevels = 1;
            n.ArraySize = 1;
            n.Usage = D3D11_USAGE_DEFAULT;
            n.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            n.CPUAccessFlags = 0;
            n.MiscFlags = 0;
            g_dev->CreateTexture2D(&n, nullptr, &g_eyeTex[i]);
        }
        g_eyeTexW = d.Width;
        g_eyeTexH = d.Height;
        logf("eye: textures %ux%u for both eyes per frame", d.Width, d.Height);
    }
    bool ok = g_eyeTex[e] != nullptr && d.SampleDesc.Count == 1;
    if (ok) {
        g_imm->CopyResource(g_eyeTex[e], t);
        g_eyeTexFrame[e] = g_frame;
    }
    t->Release();
    return ok;
}
