// ---------------------------------------------------------------------------------------------
// A video of what the headset shows (Ctrl+F5 starts and ends it): both eyes side by side, left | right, at the size
// they are rendered at, with the game's sound, as mw4vr_videos\mw4vr_<date>_<time>.mp4 (HEVC and AAC). Beside it a
// .json with what a later tool needs to cut or reproject the picture: sizes, pictures a second, each eye's field of
// view as tangents.
//
// Nothing of a picture passes through the processor's memory (the game is a 32-bit process, two eyes are some 50 MB
// a picture):
//   - the render thread draws both eyes and the HUD's layers - as the headset's runtime would put them together -
//     into one texture of a small pool, at most once for every picture of the video's own clock (rec_fps, wall
//     time). It never waits: with no texture free the picture is left out.
//   - the pool's textures are shared with a device of the video's own. On that device, on a thread of its own,
//     Media Foundation's sink writer turns the picture into the encoder's format and hands it to the graphics card's
//     encoder. The game's device is not touched by it (no lock on it, no call from another thread).
//   - a third thread reads the game's sound (the loopback of this process alone, Windows 10 2004 and later; else of
//     the default output device) and writes it into the same file, by the same clock.
// The file is complete only when the video is ended with the key: a game that is closed while it runs leaves a file
// that does not play.

#include "mod.h"
#include <objbase.h>
#include <propidl.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <atomic>
#include <deque>

void *volatile g_recDev;

enum { REC_POOL = 6, REC_IMAGES = 8, REC_DRAWS = 96, REC_RATE = 48000, REC_LIGHT_PX = 24 };
enum { REC_IDLE, REC_STARTING, REC_RUNNING, REC_STOPPING };
enum : UINT { REC_MAX_SIDE = 4096 }; // an eye's picture at most (the encoder takes 8192 pixels across)

typedef void(STDMETHODCALLTYPE *RecPFN_Viewports)(ID3D11DeviceContext *, UINT, const D3D11_VIEWPORT *);
typedef void(STDMETHODCALLTYPE *RecPFN_Draw)(ID3D11DeviceContext *, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE *RecPFN_CreateIL)(ID3D11Device *, const D3D11_INPUT_ELEMENT_DESC *, UINT,
                                                    const void *, SIZE_T, ID3D11InputLayout **);
typedef HRESULT(WINAPI *RecPFN_Activate)(LPCWSTR, REFIID, PROPVARIANT *, IActivateAudioInterfaceCompletionHandler *,
                                         IActivateAudioInterfaceAsyncOperation **);

static const char g_recHlsl[] = R"HLSL(
Texture2D t : register(t0);
SamplerState s : register(s0);
struct A { float4 p : POSITION; float4 u : TEXCOORD0; };
struct V { float4 p : SV_Position; float4 u : TEXCOORD0; };
V vs(A a) {
    V o;
    o.p = a.p;
    o.u = a.u;
    return o;
}
// u.z: 0 the texture's values as they are (encoded for a display already), 1 they are linear light and are encoded
// here, 2 black. u.w: 1 the picture is opaque whatever its alpha says.
float4 ps(V i) : SV_Target {
    if (i.u.z > 1.5) return float4(0, 0, 0, 1);
    float4 c = t.Sample(s, i.u.xy);
    if (i.u.z > 0.5) {
        float3 l = saturate(c.rgb);
        c.rgb = lerp(1.055 * pow(abs(l), 1 / 2.4) - 0.055, l * 12.92, step(l, 0.0031308));
    }
    if (i.u.w > 0.5) c.a = 1;
    return c;
}
)HLSL";

// ---- what the render thread and the video's threads share ----
struct RecSlot {                    // a picture on its way to the encoder
    ID3D11Texture2D *tex = nullptr; // on the game's device
    ID3D11RenderTargetView *rtv = nullptr;
    HANDLE shared = nullptr;
    ID3D11Texture2D *texEnc = nullptr; // the same picture on the video's device
    std::atomic<int> busy{0};          // drawn into and not yet through the encoder
};
struct RecSettings {
    int fps = 60, mbit = 0;
    bool hud = true, audio = true, light = true;
};
static RecSlot g_recPool[REC_POOL];
static RecSettings g_recSet;
static std::atomic<int> g_recState{REC_IDLE};
static HANDLE g_recThread, g_recWake;
static std::mutex g_recMx;                               // the queue, the message, the eyes' fields of view
static std::deque<std::pair<int, long long>> g_recQueue; // (slot, the picture's number)
static std::wstring g_recPath, g_recName, g_recMsg;      // the file without its ending; its name; what to tell
static LARGE_INTEGER g_recT0, g_recFreq;
static UINT g_recEyeW, g_recH; // one eye's part of the video's picture
static std::atomic<long long> g_recFrames{0}, g_recDropped{0};
static float g_recTan[2][4] = {{-1, 1, 1, -1}, {-1, 1, 1, -1}}; // each eye's view in the video: left right up down
static float g_recIpd;
static bool g_recFovKnown;

static double recNow() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - g_recT0.QuadPart) / (double)g_recFreq.QuadPart;
}

// ---------------------------------------------------------------------------------------------
// the render thread: the picture

struct RecImage { // what was last put into a swap chain's image
    XrSwapchain sc = XR_NULL_HANDLE;
    ID3D11Texture2D *tex = nullptr; // a reference is held
    bool own = false;               // a copy of ours (else the mod's own texture of that picture)
};
struct RecVertex {
    float p[4], u[4];
};
struct RecDraw {
    ID3D11Texture2D *tex;
    int eye;
    bool blend;
};
static RecImage g_recImg[REC_IMAGES];
static bool g_recNow; // this frame's picture goes into the video
static int g_recSlot;
static long long g_recTick, g_recLastTick;
static ID3D11VertexShader *g_recVS;
static ID3D11PixelShader *g_recPS;
static ID3D11InputLayout *g_recIL;
static ID3D11Buffer *g_recVB;
static ID3D11BlendState *g_recBlend[2]; // opaque; over what is there (colour times alpha already)
static ID3D11DepthStencilState *g_recDepth;
static ID3D11RasterizerState *g_recRaster;
static ID3D11SamplerState *g_recSampler;
static int g_recGfx; // 0: not made yet, 1: ready, -1: failed

static bool recGfxCreate() {
    if (!g_compile || !g_dev) return false;
    ID3DBlob *code[2] = {}, *err[2] = {};
    // (each call written out: tests/test_shaders.py finds them and compiles the same text with fxc)
    HRESULT hr[2];
    hr[0] = g_compile(g_recHlsl, sizeof(g_recHlsl) - 1, "rec", nullptr, nullptr, "vs", "vs_5_0", 1 << 15, 0, &code[0],
                      &err[0]);
    hr[1] = g_compile(g_recHlsl, sizeof(g_recHlsl) - 1, "rec", nullptr, nullptr, "ps", "ps_5_0", 1 << 15, 0, &code[1],
                      &err[1]);
    bool ok = true;
    for (int i = 0; i < 2; ++i) {
        if (FAILED(hr[i]) || !code[i]) {
            logf("video: shader %d failed to compile (0x%08lx): %s", i, hr[i],
                 err[i] ? (const char *)err[i]->GetBufferPointer() : "");
            ok = false;
        }
        if (err[i]) err[i]->Release();
    }
    const D3D11_INPUT_ELEMENT_DESC el[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    // (the device's own functions: these are not the game's shaders, and not a layout of its meshes)
    if (ok)
        ok = SUCCEEDED(ORIG(PFN_CreateShader, g_dev, VT_Dev_CreateVertexShader)(
                 g_dev, code[0]->GetBufferPointer(), code[0]->GetBufferSize(), nullptr, (void **)&g_recVS)) &&
             SUCCEEDED(ORIG(PFN_CreateShader, g_dev, VT_Dev_CreatePixelShader)(
                 g_dev, code[1]->GetBufferPointer(), code[1]->GetBufferSize(), nullptr, (void **)&g_recPS)) &&
             SUCCEEDED(ORIG(RecPFN_CreateIL, g_dev, VT_Dev_CreateInputLayout)(g_dev, el, 2, code[0]->GetBufferPointer(),
                                                                              code[0]->GetBufferSize(), &g_recIL));
    for (ID3DBlob *b : code)
        if (b) b->Release();
    if (!ok) return false;
    D3D11_BUFFER_DESC vd = {};
    vd.ByteWidth = sizeof(RecVertex) * 4 * REC_DRAWS, vd.Usage = D3D11_USAGE_DYNAMIC;
    vd.BindFlags = D3D11_BIND_VERTEX_BUFFER, vd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_BLEND_DESC bo = bd; // a layer's colours are multiplied by their alpha already
    bo.RenderTarget[0].BlendEnable = TRUE;
    bo.RenderTarget[0].SrcBlend = bo.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bo.RenderTarget[0].DestBlend = bo.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bo.RenderTarget[0].BlendOp = bo.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    D3D11_DEPTH_STENCIL_DESC dd = {};
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID, rd.CullMode = D3D11_CULL_NONE, rd.DepthClipEnable = TRUE;
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    return SUCCEEDED(g_dev->CreateBuffer(&vd, nullptr, &g_recVB)) &&
           SUCCEEDED(g_dev->CreateBlendState(&bd, &g_recBlend[0])) &&
           SUCCEEDED(g_dev->CreateBlendState(&bo, &g_recBlend[1])) &&
           SUCCEEDED(g_dev->CreateDepthStencilState(&dd, &g_recDepth)) &&
           SUCCEEDED(g_dev->CreateRasterizerState(&rd, &g_recRaster)) &&
           SUCCEEDED(g_dev->CreateSamplerState(&sd, &g_recSampler));
}

// The format a shader reads a picture in, and whether its values arrive as linear light (and are encoded for the
// video). The eyes' and the layers' pictures hold values for a display; a texture that says so itself (sRGB) can
// only be read decoded.
static DXGI_FORMAT recReadFormat(DXGI_FORMAT f, bool *linear) {
    *linear = false;
    switch (f) {
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        *linear = true;
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
        *linear = true;
        return f;
    default:
        return f;
    }
}
// the format of a copy of ours: the picture's values as they are
static DXGI_FORMAT recCopyFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    default:
        return f;
    }
}

static void recImagesFree() {
    for (RecImage &m : g_recImg) {
        if (m.tex) m.tex->Release();
        m = {};
    }
}

static void recPoolFree() {
    for (RecSlot &s : g_recPool) {
        if (s.rtv) s.rtv->Release();
        if (s.tex) s.tex->Release();
        s.rtv = nullptr, s.tex = nullptr, s.shared = nullptr;
        s.busy = 0;
    }
}

static bool recPoolCreate() {
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = 2 * g_recEyeW, d.Height = g_recH, d.MipLevels = 1, d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM, d.SampleDesc.Count = 1, d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    d.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    for (RecSlot &s : g_recPool) {
        IDXGIResource *res = nullptr;
        bool ok = SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &s.tex)) && s.tex &&
                  SUCCEEDED(g_dev->CreateRenderTargetView(s.tex, nullptr, &s.rtv)) &&
                  SUCCEEDED(s.tex->QueryInterface(__uuidof(IDXGIResource), (void **)&res)) &&
                  SUCCEEDED(res->GetSharedHandle(&s.shared)) && s.shared;
        if (res) res->Release();
        if (!ok) {
            recPoolFree();
            return false;
        }
    }
    return true;
}

// The runtime's own Draw: the mod's hook sits in its code, and looks at the game's shaders the mod remembers.
static void recDraw(ID3D11DeviceContext *c, UINT n, UINT first) {
    void *fn = (*(void ***)c)[VT_Ctx_Draw];
    for (int v = 0; v < HP_VARS; ++v)
        if (g_hpAddr[SL_DRAW][v] == fn && g_tr[SL_DRAW][v]) {
            ((RecPFN_Draw)g_tr[SL_DRAW][v])(c, n, first);
            return;
        }
    ((RecPFN_Draw)fn)(c, n, first); // not one the mod hooked
}

bool recRunning() { return g_recState.load() == REC_RUNNING; }

bool recBegin() {
    g_recNow = false;
    if (g_recState.load() != REC_RUNNING) return false;
    const long long tick = mcRecTick(recNow(), g_recSet.fps);
    if (tick <= g_recLastTick) return false; // the video's clock has no new picture yet
    for (int i = 0; i < REC_POOL; ++i)
        if (!g_recPool[i].busy.load()) {
            g_recSlot = i, g_recTick = tick, g_recNow = true;
            return true;
        }
    g_recLastTick = tick; // the encoder is behind: this picture is left out
    ++g_recDropped;
    return false;
}

void recImage(XrSwapchain sc, ID3D11DeviceContext *ctx, ID3D11Texture2D *tex, bool keep) {
    if (!g_recNow || !ctx || !tex || sc == XR_NULL_HANDLE) return;
    RecImage *m = nullptr;
    for (RecImage &i : g_recImg)
        if (i.sc == sc) m = &i;
    for (RecImage &i : g_recImg)
        if (!m && i.sc == XR_NULL_HANDLE) m = &i;
    if (!m) return;
    m->sc = sc;
    if (keep) {
        tex->AddRef();
        if (m->tex) m->tex->Release();
        m->tex = tex, m->own = false;
        return;
    }
    D3D11_TEXTURE2D_DESC d, o = {};
    tex->GetDesc(&d);
    if (d.SampleDesc.Count != 1) return;
    if (m->tex && m->own) m->tex->GetDesc(&o);
    const DXGI_FORMAT f = recCopyFormat(d.Format);
    if (!m->tex || !m->own || o.Width != d.Width || o.Height != d.Height || o.Format != f) {
        if (m->tex) m->tex->Release();
        m->tex = nullptr, m->own = true;
        D3D11_TEXTURE2D_DESC n = {};
        n.Width = d.Width, n.Height = d.Height, n.MipLevels = 1, n.ArraySize = 1, n.Format = f;
        n.SampleDesc.Count = 1, n.Usage = D3D11_USAGE_DEFAULT, n.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g_dev->CreateTexture2D(&n, nullptr, &m->tex))) m->tex = nullptr;
    }
    if (m->tex) ctx->CopySubresourceRegion(m->tex, 0, 0, 0, 0, tex, 0, nullptr);
}

static ID3D11Texture2D *recImageOf(XrSwapchain sc) {
    for (const RecImage &i : g_recImg)
        if (i.sc == sc && sc != XR_NULL_HANDLE) return i.tex;
    return nullptr;
}

// a rectangle's four corners (left top, right top, left bottom, right bottom), each x y w as mcEyeClip gives them
static void recQuad(RecVertex *v, const float (*c)[3], const float *uv, float kind, float opaque) {
    for (int i = 0; i < 4; ++i) {
        v[i].p[0] = c[i][0], v[i].p[1] = c[i][1], v[i].p[2] = 0.5f * c[i][2], v[i].p[3] = c[i][2];
        v[i].u[0] = uv[(i & 1) ? 2 : 0], v[i].u[1] = uv[(i & 2) ? 3 : 1], v[i].u[2] = kind, v[i].u[3] = opaque;
    }
}

// the part of a picture a layer shows, as parts of that picture
static bool recUv(ID3D11Texture2D *tex, const XrRect2Di &r, float *uv, bool *linear) {
    D3D11_TEXTURE2D_DESC d;
    tex->GetDesc(&d);
    recReadFormat(d.Format, linear);
    if (!d.Width || !d.Height || d.SampleDesc.Count != 1) return false;
    // (an empty rectangle: the whole picture)
    const float w = r.extent.width > 0 ? (float)r.extent.width : (float)d.Width;
    const float h = r.extent.height > 0 ? (float)r.extent.height : (float)d.Height;
    uv[0] = (float)r.offset.x / d.Width, uv[1] = (float)r.offset.y / d.Height;
    uv[2] = fminf(1.0f, ((float)r.offset.x + w) / d.Width), uv[3] = fminf(1.0f, ((float)r.offset.y + h) / d.Height);
    return true;
}

void recFrame(ID3D11DeviceContext *c, const XrCompositionLayerBaseHeader *const *layers, uint32_t n) {
    if (!g_recNow) return;
    g_recNow = false;
    if (!c || !n || g_recState.load() != REC_RUNNING || g_recGfx != 1) return;
    RecSlot &slot = g_recPool[g_recSlot];

    // the eyes the video looks through: those of the world's layer, with the game's widest view (while the game
    // zooms, its picture is shown smaller than that - in the headset too); without such a layer (a menu, a film)
    // two eyes at the place the game's camera stands for
    const XrCompositionLayerProjection *proj = nullptr;
    for (uint32_t i = 0; i < n && !proj; ++i)
        if (layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION)
            proj = (const XrCompositionLayerProjection *)layers[i];
    XrPosef view[2];
    if (proj && proj->viewCount == 2) {
        float tan[2][4];
        for (int e = 0; e < 2; ++e) {
            view[e] = proj->views[e].pose;
            const XrFovf &f = proj->views[e].fov;
            if (g_srcCamActive && g_vr.baseP00 > 0 && g_vr.baseP11 > 0)
                tan[e][0] = -1 / g_vr.baseP00, tan[e][1] = 1 / g_vr.baseP00, tan[e][2] = 1 / g_vr.baseP11,
                tan[e][3] = -1 / g_vr.baseP11;
            else
                tan[e][0] = tanf(f.angleLeft), tan[e][1] = tanf(f.angleRight), tan[e][2] = tanf(f.angleUp),
                tan[e][3] = tanf(f.angleDown);
        }
        const XrVector3f a = view[0].position, b = view[1].position;
        std::lock_guard<std::mutex> lk(g_recMx);
        memcpy(g_recTan, tan, sizeof tan);
        g_recIpd = len3(a.x - b.x, a.y - b.y, a.z - b.z);
        g_recFovKnown = true;
    } else {
        proj = nullptr;
        const bool ref = g_vr.tracking && g_vr.haveRef;
        const XrQuaternionf q = ref ? g_vr.ref.orientation : XrQuaternionf{0, 0, 0, 1};
        const XrVector3f p = ref ? g_vr.ref.position : XrVector3f{0, 0, 0};
        for (int e = 0; e < 2; ++e) {
            const XrVector3f o = qrot(q, {(e ? 0.5f : -0.5f) * g_vr.ipd, 0, 0});
            view[e].orientation = q;
            view[e].position = {p.x + o.x, p.y + o.y, p.z + o.z};
        }
    }

    static RecVertex vtx[4 * REC_DRAWS];
    static RecDraw draw[REC_DRAWS];
    int nd = 0;
    for (int e = 0; e < 2; ++e) {
        const float *tn = g_recTan[e];
        if (nd < REC_DRAWS) { // black, where no layer is
            const float all[4][3] = {{-1, 1, 1}, {1, 1, 1}, {-1, -1, 1}, {1, -1, 1}}, uv[4] = {0, 0, 1, 1};
            recQuad(vtx + 4 * nd, all, uv, 2, 1);
            draw[nd++] = {nullptr, e, false};
        }
        for (uint32_t i = 0; i < n && nd < REC_DRAWS; ++i) {
            float corner[4][3], uv[4];
            bool linear = false;
            if (layers[i] == (const XrCompositionLayerBaseHeader *)proj) {
                const XrCompositionLayerProjectionView &pv = proj->views[e];
                ID3D11Texture2D *tex = recImageOf(pv.subImage.swapchain);
                if (!tex || !recUv(tex, pv.subImage.imageRect, uv, &linear)) continue;
                const float x0 = mcTanPlace(tanf(pv.fov.angleLeft), tn[0], tn[1]),
                            x1 = mcTanPlace(tanf(pv.fov.angleRight), tn[0], tn[1]),
                            y0 = mcTanPlace(tanf(pv.fov.angleUp), tn[3], tn[2]),
                            y1 = mcTanPlace(tanf(pv.fov.angleDown), tn[3], tn[2]);
                const float rc[4][3] = {{x0, y0, 1}, {x1, y0, 1}, {x0, y1, 1}, {x1, y1, 1}};
                recQuad(vtx + 4 * nd, rc, uv, linear ? 1.0f : 0.0f, 1);
                draw[nd++] = {tex, e, false};
            } else if (layers[i]->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
                const XrCompositionLayerQuad &q = *(const XrCompositionLayerQuad *)layers[i];
                if (q.eyeVisibility == XR_EYE_VISIBILITY_LEFT && e != 0) continue;
                if (q.eyeVisibility == XR_EYE_VISIBILITY_RIGHT && e != 1) continue;
                // (rec_hud=0: the eyes' pictures alone - and a menu's screen, which is made of them)
                const bool ofEyes = q.subImage.swapchain == g_vr.sc[0] || q.subImage.swapchain == g_vr.sc[1];
                if (!g_recSet.hud && !ofEyes) continue;
                ID3D11Texture2D *tex = recImageOf(q.subImage.swapchain);
                if (!tex || !recUv(tex, q.subImage.imageRect, uv, &linear)) continue;
                bool ahead = false;
                for (int k = 0; k < 4; ++k) {
                    const XrVector3f o = qrot(q.pose.orientation, {(k & 1 ? 0.5f : -0.5f) * q.size.width,
                                                                   (k & 2 ? -0.5f : 0.5f) * q.size.height, 0});
                    const float p[3] = {q.pose.position.x + o.x, q.pose.position.y + o.y, q.pose.position.z + o.z};
                    mcEyeClip(p, &view[e].position.x, &view[e].orientation.x, tn, corner[k]);
                    ahead = ahead || corner[k][2] > 0;
                }
                if (!ahead) continue;
                const bool blend = (q.layerFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT) != 0;
                recQuad(vtx + 4 * nd, corner, uv, linear ? 1.0f : 0.0f, blend ? 0.0f : 1.0f);
                draw[nd++] = {tex, e, blend};
            }
        }
    }

    // what is bound now
    ID3D11RenderTargetView *oRT[8] = {};
    ID3D11DepthStencilView *oDSV = nullptr;
    ID3D11BlendState *oBlend = nullptr;
    ID3D11DepthStencilState *oDS = nullptr;
    ID3D11RasterizerState *oRS = nullptr;
    ID3D11InputLayout *oIL = nullptr;
    ID3D11Buffer *oVB = nullptr;
    ID3D11VertexShader *oVS = nullptr;
    ID3D11PixelShader *oPS = nullptr;
    ID3D11GeometryShader *oGS = nullptr;
    ID3D11HullShader *oHS = nullptr;
    ID3D11DomainShader *oDom = nullptr;
    ID3D11ShaderResourceView *oSRV = nullptr;
    ID3D11SamplerState *oSamp = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY oTopo;
    D3D11_VIEWPORT oVP[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT oVPN = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE, oMask = 0, oRef = 0, oStride = 0, oOff = 0;
    float oFactor[4];
    c->OMGetRenderTargets(8, oRT, &oDSV);
    c->OMGetBlendState(&oBlend, oFactor, &oMask);
    c->OMGetDepthStencilState(&oDS, &oRef);
    c->RSGetState(&oRS);
    c->RSGetViewports(&oVPN, oVP);
    c->IAGetInputLayout(&oIL);
    c->IAGetPrimitiveTopology(&oTopo);
    c->IAGetVertexBuffers(0, 1, &oVB, &oStride, &oOff);
    c->VSGetShader(&oVS, nullptr, nullptr);
    c->PSGetShader(&oPS, nullptr, nullptr);
    c->GSGetShader(&oGS, nullptr, nullptr);
    c->HSGetShader(&oHS, nullptr, nullptr);
    c->DSGetShader(&oDom, nullptr, nullptr);
    c->PSGetShaderResources(0, 1, &oSRV);
    c->PSGetSamplers(0, 1, &oSamp);

    D3D11_MAPPED_SUBRESOURCE map = {};
    bool drawn = false;
    if (SUCCEEDED(ORIG(MechPFN_Map, c, VT_Ctx_Map)(c, g_recVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &map)) && map.pData) {
        memcpy(map.pData, vtx, sizeof(RecVertex) * 4 * nd);
        ORIG(MechPFN_Unmap, c, VT_Ctx_Unmap)(c, g_recVB, 0);
        const float one[4] = {1, 1, 1, 1};
        const UINT stride = sizeof(RecVertex), off = 0;
        ID3D11ShaderResourceView *none = nullptr;
        c->IASetInputLayout(g_recIL);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        c->IASetVertexBuffers(0, 1, &g_recVB, &stride, &off);
        ORIG(PFN_SetShader, c, VT_Ctx_VSSetShader)(c, g_recVS, nullptr, 0);
        ORIG(PFN_SetShader, c, VT_Ctx_PSSetShader)(c, g_recPS, nullptr, 0);
        c->GSSetShader(nullptr, nullptr, 0);
        c->HSSetShader(nullptr, nullptr, 0);
        c->DSSetShader(nullptr, nullptr, 0);
        c->OMSetDepthStencilState(g_recDepth, 0);
        c->RSSetState(g_recRaster);
        c->PSSetSamplers(0, 1, &g_recSampler);
        c->PSSetShaderResources(0, 1, &none); // (a picture bound to be read cannot be drawn into)
        ORIG(PFN_OMSet, c, VT_Ctx_OMSetRenderTargets)(c, 1, &slot.rtv, nullptr);
        for (int i = 0; i < nd; ++i) {
            const D3D11_VIEWPORT vp = {(float)(draw[i].eye * g_recEyeW), 0, (float)g_recEyeW, (float)g_recH, 0, 1};
            ORIG(RecPFN_Viewports, c, VT_Ctx_RSSetViewports)(c, 1, &vp);
            ID3D11ShaderResourceView *srv = nullptr;
            if (draw[i].tex) {
                D3D11_TEXTURE2D_DESC d;
                draw[i].tex->GetDesc(&d);
                bool linear;
                D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
                sd.Format = recReadFormat(d.Format, &linear), sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                sd.Texture2D.MipLevels = 1;
                if (FAILED(g_dev->CreateShaderResourceView(draw[i].tex, &sd, &srv)) || !srv) continue;
            }
            c->PSSetShaderResources(0, 1, &srv);
            c->OMSetBlendState(g_recBlend[draw[i].blend ? 1 : 0], one, 0xffffffff);
            recDraw(c, 4, 4 * i);
            if (srv) {
                c->PSSetShaderResources(0, 1, &none);
                srv->Release();
            }
        }
        drawn = true;
    }

    // back
    ORIG(PFN_OMSet, c, VT_Ctx_OMSetRenderTargets)(c, 8, oRT, oDSV);
    if (oVPN) ORIG(RecPFN_Viewports, c, VT_Ctx_RSSetViewports)(c, oVPN, oVP);
    c->PSSetShaderResources(0, 1, &oSRV);
    c->PSSetSamplers(0, 1, &oSamp);
    ORIG(PFN_SetShader, c, VT_Ctx_VSSetShader)(c, oVS, nullptr, 0);
    ORIG(PFN_SetShader, c, VT_Ctx_PSSetShader)(c, oPS, nullptr, 0);
    c->GSSetShader(oGS, nullptr, 0);
    c->HSSetShader(oHS, nullptr, 0);
    c->DSSetShader(oDom, nullptr, 0);
    c->OMSetBlendState(oBlend, oFactor, oMask);
    c->OMSetDepthStencilState(oDS, oRef);
    c->RSSetState(oRS);
    c->IASetInputLayout(oIL);
    c->IASetPrimitiveTopology(oTopo);
    c->IASetVertexBuffers(0, 1, &oVB, &oStride, &oOff);
    for (ID3D11RenderTargetView *v : oRT)
        if (v) v->Release();
    IUnknown *rel[] = {oDSV, oBlend, oDS, oRS, oIL, oVB, oVS, oPS, oGS, oHS, oDom, oSRV, oSamp};
    for (IUnknown *u : rel)
        if (u) u->Release();
    if (!drawn) return;

    // The picture is handed over: what was drawn must be on its way to the graphics card before the other device
    // reads it.
    c->Flush();
    g_recLastTick = g_recTick;
    slot.busy = 1;
    {
        std::lock_guard<std::mutex> lk(g_recMx);
        g_recQueue.push_back({g_recSlot, g_recTick});
    }
    SetEvent(g_recWake);
}

void recLight(ID3D11DeviceContext *ctx, ID3D11Texture2D *tex, UINT w, UINT h) {
    if (g_recState.load() != REC_RUNNING || !g_recSet.light || !ctx || !tex) return;
    if (w < 40 * REC_LIGHT_PX || h < 40 * REC_LIGHT_PX) return;
    static uint32_t px[REC_LIGHT_PX * REC_LIGHT_PX]; // BGRA, colour times alpha: a red disc
    if (!px[REC_LIGHT_PX * REC_LIGHT_PX / 2 + REC_LIGHT_PX / 2])
        for (int y = 0; y < REC_LIGHT_PX; ++y)
            for (int x = 0; x < REC_LIGHT_PX; ++x) {
                const float dx = x + 0.5f - REC_LIGHT_PX / 2, dy = y + 0.5f - REC_LIGHT_PX / 2;
                px[y * REC_LIGHT_PX + x] =
                    dx * dx + dy * dy < (REC_LIGHT_PX / 2 - 1) * (REC_LIGHT_PX / 2 - 1) ? 0xffe01010u : 0;
            }
    // in the far middle of the HUD (what is always shown), up and to the left of the cross-hair
    const UINT x = (UINT)(w * 0.32f), y = (UINT)(h * 0.18f);
    const D3D11_BOX box = {x, y, 0, x + REC_LIGHT_PX, y + REC_LIGHT_PX, 1};
    ctx->UpdateSubresource(tex, 0, &box, px, REC_LIGHT_PX * 4, 0);
}

// ---------------------------------------------------------------------------------------------
// the video's own threads: the file, the encoder, the sound

// a picture's texture is free again when the encoder has let go of its sample
struct RecFreed : IMFAsyncCallback {
    int slot = 0;
    STDMETHODIMP QueryInterface(REFIID id, void **out) override {
        if (!out) return E_POINTER;
        if (id == __uuidof(IUnknown) || id == __uuidof(IMFAsyncCallback)) {
            *out = static_cast<IMFAsyncCallback *>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; } // (one for each texture, for as long as the mod is loaded)
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP GetParameters(DWORD *, DWORD *) override { return E_NOTIMPL; }
    STDMETHODIMP Invoke(IMFAsyncResult *) override {
        g_recPool[slot].busy = 0;
        return S_OK;
    }
};
static RecFreed g_recFreed[REC_POOL];

// the answer to the request for this process's sound
struct RecActivated : IActivateAudioInterfaceCompletionHandler, IAgileObject {
    std::atomic<ULONG> refs{1};
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    virtual ~RecActivated() {
        if (done) CloseHandle(done);
    }
    STDMETHODIMP QueryInterface(REFIID id, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IActivateAudioInterfaceCompletionHandler))
            *out = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
        else if (id == __uuidof(IAgileObject))
            *out = static_cast<IAgileObject *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG n = --refs;
        if (!n) delete this;
        return n;
    }
    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation *) override {
        SetEvent(done);
        return S_OK;
    }
};

struct RecFile { // what the video's thread holds while a video runs
    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    IMFDXGIDeviceManager *mgr = nullptr;
    IMFSinkWriter *writer = nullptr;
    DWORD video = 0, audio = 0;
    IAudioClient *client = nullptr;
    IAudioCaptureClient *capture = nullptr;
    HANDLE audioEvent = nullptr, audioThread = nullptr;
    std::atomic<bool> audioStop{false};
    const char *sound = "none";
    long long written = 0; // the sound's frames in the file
    bool mf = false;
};
static RecFile g_recFile;

template <class T> static void recRelease(T *&p) {
    if (p) p->Release();
    p = nullptr;
}

// this process's sound alone (and its child processes'); null: not on this Windows
static IAudioClient *recSoundOfProcess() {
    HMODULE m = LoadLibraryExW(L"mmdevapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    RecPFN_Activate activate = m ? (RecPFN_Activate)GetProcAddress(m, "ActivateAudioInterfaceAsync") : nullptr;
    if (!activate) return nullptr;
    AUDIOCLIENT_ACTIVATION_PARAMS par = {};
    par.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    par.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
    par.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT pv = {};
    pv.vt = VT_BLOB, pv.blob.cbSize = sizeof par, pv.blob.pBlobData = (BYTE *)&par;
    RecActivated *done = new (std::nothrow) RecActivated;
    if (!done || !done->done) {
        if (done) done->Release();
        return nullptr;
    }
    IActivateAudioInterfaceAsyncOperation *op = nullptr;
    IAudioClient *client = nullptr;
    if (SUCCEEDED(activate(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv, done, &op)) && op &&
        WaitForSingleObject(done->done, 5000) == WAIT_OBJECT_0) {
        HRESULT hr = E_FAIL;
        IUnknown *unk = nullptr;
        if (SUCCEEDED(op->GetActivateResult(&hr, &unk)) && SUCCEEDED(hr) && unk)
            unk->QueryInterface(__uuidof(IAudioClient), (void **)&client);
        if (unk) unk->Release();
    }
    if (op) op->Release();
    done->Release();
    return client;
}

// everything the default output device plays
static IAudioClient *recSoundOfDevice() {
    IMMDeviceEnumerator *en = nullptr;
    IMMDevice *dev = nullptr;
    IAudioClient *client = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                   (void **)&en)) &&
        en && SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) && dev)
        dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&client);
    if (dev) dev->Release();
    if (en) en->Release();
    return client;
}

static const WAVEFORMATEX g_recWave = {WAVE_FORMAT_PCM, 2, REC_RATE, REC_RATE * 4, 4, 16, 0};

static bool recSoundOpen(RecFile &f) {
    for (int way = 0; way < 2 && !f.capture; ++way) {
        IAudioClient *c = way ? recSoundOfDevice() : recSoundOfProcess();
        if (!c) continue;
        const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | (way ? AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY : 0);
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HRESULT hr = c->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 2000000, 0, &g_recWave, nullptr);
        if (SUCCEEDED(hr) && ev) hr = c->SetEventHandle(ev);
        if (SUCCEEDED(hr)) hr = c->GetService(__uuidof(IAudioCaptureClient), (void **)&f.capture);
        if (SUCCEEDED(hr) && f.capture && ev) {
            f.client = c, f.audioEvent = ev;
            f.sound = way ? "device" : "process";
            return true;
        }
        logf("video: the sound of %s cannot be read (0x%08lx)", way ? "the output device" : "the game alone", hr);
        recRelease(f.capture);
        if (ev) CloseHandle(ev);
        c->Release();
    }
    return false;
}

// n frames of sound into the file (data null: silence)
static void recSoundWrite(RecFile &f, const BYTE *data, UINT32 n) {
    if (!n) return;
    const DWORD bytes = n * g_recWave.nBlockAlign;
    IMFMediaBuffer *buf = nullptr;
    IMFSample *s = nullptr;
    BYTE *p = nullptr;
    if (SUCCEEDED(MFCreateMemoryBuffer(bytes, &buf)) && buf && SUCCEEDED(buf->Lock(&p, nullptr, nullptr)) && p) {
        if (data)
            memcpy(p, data, bytes);
        else
            memset(p, 0, bytes);
        buf->Unlock();
        buf->SetCurrentLength(bytes);
        if (SUCCEEDED(MFCreateSample(&s)) && s) {
            s->AddBuffer(buf);
            s->SetSampleTime(f.written * 10000000LL / REC_RATE);
            s->SetSampleDuration((LONGLONG)n * 10000000LL / REC_RATE);
            f.writer->WriteSample(f.audio, s);
        }
    }
    f.written += n;
    if (s) s->Release();
    if (buf) buf->Release();
}

// The sound's place in the file follows the video's clock: the loopback hands out nothing while nothing plays, so
// what is missing is filled with silence - also while it stays silent, or the file's writer would wait for sound
// and keep every picture until some comes.
static DWORD WINAPI recSoundThread(void *) {
    RecFile &f = g_recFile;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const long long slack = REC_RATE / 20; // 50 ms
    if (SUCCEEDED(f.client->Start())) {
        while (!f.audioStop.load()) {
            WaitForSingleObject(f.audioEvent, 10);
            UINT32 next = 0;
            while (SUCCEEDED(f.capture->GetNextPacketSize(&next)) && next) {
                BYTE *data = nullptr;
                UINT32 n = 0;
                DWORD flags = 0;
                if (FAILED(f.capture->GetBuffer(&data, &n, &flags, nullptr, nullptr))) break;
                const long long now = (long long)(recNow() * REC_RATE), begin = now - n;
                if (begin - f.written > slack) recSoundWrite(f, nullptr, (UINT32)(begin - f.written));
                if (f.written - now < 4 * slack) // (far ahead of the clock: left out)
                    recSoundWrite(f, (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? nullptr : data, n);
                f.capture->ReleaseBuffer(n);
            }
            const long long now = (long long)(recNow() * REC_RATE);
            if (now - f.written > 2 * slack) recSoundWrite(f, nullptr, (UINT32)(now - f.written - slack));
        }
        f.client->Stop();
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return 0;
}

static bool recStep(HRESULT hr, const char *what) {
    if (SUCCEEDED(hr)) return true;
    logf("video: %s failed (0x%08lx)", what, hr);
    return false;
}

// the video's own device on the game's graphics card, and the pictures' textures opened on it
static bool recDeviceOpen(RecFile &f) {
    IDXGIDevice *xd = nullptr;
    IDXGIAdapter *ad = nullptr;
    // (made once and kept for as long as the mod is loaded: the mod's hooks know it by its address)
    bool ok = f.dev || (SUCCEEDED(g_dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&xd)) && xd &&
                        SUCCEEDED(xd->GetAdapter(&ad)) && ad);
    const D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    if (ok && !f.dev) {
        ok = recStep(r_CreateDevice(ad, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                    D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, fl, 2,
                                    D3D11_SDK_VERSION, &f.dev, nullptr, &f.ctx),
                     "a device for the video") &&
             f.dev && f.ctx;
        if (ok) {
            // it shares its functions with the game's device: the mod's hooks leave it alone, as they do the VR
            // runtime's
            g_recDev = f.dev;
            ctxOf(f.ctx)->foreign = true;
            ID3D10Multithread *mt = nullptr;
            if (SUCCEEDED(f.dev->QueryInterface(__uuidof(ID3D10Multithread), (void **)&mt)) && mt) {
                mt->SetMultithreadProtected(TRUE);
                mt->Release();
            }
        }
    }
    if (ad) ad->Release();
    if (xd) xd->Release();
    if (!ok) return false;
    for (RecSlot &s : g_recPool)
        if (!recStep(f.dev->OpenSharedResource(s.shared, __uuidof(ID3D11Texture2D), (void **)&s.texEnc),
                     "opening a picture on the video's device"))
            return false;
    return true;
}

static void recColours(IMFMediaType *t, bool rgb) {
    t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    t->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
    t->SetUINT32(MF_MT_TRANSFER_FUNCTION, rgb ? MFVideoTransFunc_sRGB : MFVideoTransFunc_709);
    t->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, rgb ? MFNominalRange_0_255 : MFNominalRange_16_235);
    if (!rgb) t->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
    MFSetAttributeSize(t, MF_MT_FRAME_SIZE, 2 * g_recEyeW, g_recH);
    MFSetAttributeRatio(t, MF_MT_FRAME_RATE, (UINT32)g_recSet.fps, 1);
    MFSetAttributeRatio(t, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
}

static bool recWriterOpen(RecFile &f, UINT32 bitrate) {
    UINT token = 0;
    IMFAttributes *at = nullptr;
    IMFMediaType *out = nullptr, *in = nullptr, *aout = nullptr, *ain = nullptr;
    bool ok = recStep(MFCreateDXGIDeviceManager(&token, &f.mgr), "the encoder's device manager") &&
              recStep(f.mgr->ResetDevice(f.dev, token), "the encoder's device") &&
              recStep(MFCreateAttributes(&at, 4), "the writer's settings");
    if (ok) {
        at->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 1);
        at->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, 1); // (the pool of pictures is the limit)
        at->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, f.mgr);
        ok = recStep(MFCreateSinkWriterFromURL((g_recPath + L".mp4").c_str(), nullptr, at, &f.writer), "the file");
    }
    ok = ok && recStep(MFCreateMediaType(&out), "a media type") && recStep(MFCreateMediaType(&in), "a media type");
    if (ok) {
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_HEVC);
        out->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
        recColours(out, false);
        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);
        recColours(in, true);
        ok = recStep(f.writer->AddStream(out, &f.video), "the video's stream (HEVC)") &&
             recStep(f.writer->SetInputMediaType(f.video, in, nullptr),
                     "the graphics card's HEVC encoder for pictures of this size");
    }
    if (ok && f.capture) { // the sound: AAC, 192 kbit/s
        bool aok = SUCCEEDED(MFCreateMediaType(&aout)) && SUCCEEDED(MFCreateMediaType(&ain));
        if (aok) {
            aout->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            aout->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
            aout->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            aout->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, REC_RATE);
            aout->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
            aout->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
            aok = SUCCEEDED(MFInitMediaTypeFromWaveFormatEx(ain, &g_recWave, sizeof g_recWave)) &&
                  recStep(f.writer->AddStream(aout, &f.audio), "the sound's stream (AAC)") &&
                  recStep(f.writer->SetInputMediaType(f.audio, ain, nullptr), "the sound's encoder");
        }
        if (!aok) { // (a stream once added cannot be taken out: without sound the file is begun anew)
            logf("video: no sound in this video");
            recRelease(f.capture), recRelease(f.client);
            f.sound = "none";
            recRelease(aout), recRelease(ain), recRelease(out), recRelease(in), recRelease(at);
            recRelease(f.writer), recRelease(f.mgr);
            return recWriterOpen(f, bitrate);
        }
    }
    ok = ok && recStep(f.writer->BeginWriting(), "beginning the file");
    recRelease(aout), recRelease(ain), recRelease(out), recRelease(in), recRelease(at);
    return ok;
}

// one picture of the pool into the file
static void recWritePicture(RecFile &f, int slot, long long tick) {
    RecSlot &s = g_recPool[slot];
    IMFMediaBuffer *buf = nullptr;
    IMF2DBuffer *b2 = nullptr;
    IMFTrackedSample *ts = nullptr;
    IMFSample *sm = nullptr;
    bool ok = SUCCEEDED(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), s.texEnc, 0, FALSE, &buf)) && buf;
    if (ok && SUCCEEDED(buf->QueryInterface(__uuidof(IMF2DBuffer), (void **)&b2)) && b2) {
        DWORD len = 0;
        b2->GetContiguousLength(&len);
        buf->SetCurrentLength(len);
        b2->Release();
    }
    ok = ok && SUCCEEDED(MFCreateTrackedSample(&ts)) && ts &&
         SUCCEEDED(ts->QueryInterface(__uuidof(IMFSample), (void **)&sm)) && sm;
    if (ok) {
        g_recFreed[slot].slot = slot;
        ok = SUCCEEDED(ts->SetAllocator(&g_recFreed[slot], nullptr)); // from here on its release frees the picture
        if (!ok) s.busy = 0;
        sm->AddBuffer(buf);
        sm->SetSampleTime(tick * 10000000LL / g_recSet.fps);
        sm->SetSampleDuration(10000000LL / g_recSet.fps);
        if (ok && SUCCEEDED(f.writer->WriteSample(f.video, sm)))
            ++g_recFrames;
        else
            ++g_recDropped;
    } else {
        s.busy = 0;
        ++g_recDropped;
    }
    if (sm) sm->Release();
    if (ts) ts->Release();
    if (buf) buf->Release();
}

static void recJson(const RecFile &f, double seconds, UINT32 bitrate) {
    FILE *j = fileOpen((g_recPath + L".json").c_str(), L"w");
    if (!j) return;
    float tn[2][4], ipd;
    bool known;
    {
        std::lock_guard<std::mutex> lk(g_recMx);
        memcpy(tn, g_recTan, sizeof tn);
        ipd = g_recIpd, known = g_recFovKnown;
    }
    fprintf(j, "{\n  \"video\": \"%s.mp4\",\n  \"layout\": \"left_right\",\n", narrow(g_recName.c_str()).c_str());
    fprintf(j, "  \"width\": %u,\n  \"height\": %u,\n  \"eye_width\": %u,\n  \"eye_height\": %u,\n", 2 * g_recEyeW,
            g_recH, g_recEyeW, g_recH);
    fprintf(j, "  \"fps\": %d,\n  \"frames\": %lld,\n  \"frames_left_out\": %lld,\n  \"seconds\": %.3f,\n",
            g_recSet.fps, g_recFrames.load(), g_recDropped.load(), seconds);
    fprintf(j, "  \"codec\": \"hevc\",\n  \"bitrate_mbps\": %.1f,\n  \"sound\": \"%s\",\n  \"hud\": %s,\n",
            bitrate / 1000000.0, f.sound, g_recSet.hud ? "true" : "false");
    // each eye's picture spans these tangents (a flat, rectilinear picture; left and down are negative)
    fprintf(j, "  \"fov_known\": %s,\n  \"eyes\": [\n", known ? "true" : "false");
    for (int e = 0; e < 2; ++e)
        fprintf(j,
                "    {\"eye\": \"%s\", \"tan_left\": %.5f, \"tan_right\": %.5f, \"tan_up\": %.5f, \"tan_down\": %.5f, "
                "\"h_fov_deg\": %.3f, \"v_fov_deg\": %.3f}%s\n",
                e ? "right" : "left", tn[e][0], tn[e][1], tn[e][2], tn[e][3],
                (atanf(tn[e][1]) - atanf(tn[e][0])) * 57.29578f, (atanf(tn[e][2]) - atanf(tn[e][3])) * 57.29578f,
                e ? "" : ",");
    fprintf(j, "  ],\n  \"eye_distance_m\": %.4f\n}\n", ipd);
    fclose(j);
}

static DWORD WINAPI recThreadMain(void *) {
    RecFile &f = g_recFile;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    f.mf = SUCCEEDED(MFStartup(MF_VERSION));
    UINT32 bitrate = (UINT32)g_recSet.mbit * 1000000u;
    if (!bitrate) // 0.15 bits for every pixel of every picture
        bitrate = (UINT32)fmin(400e6, fmax(20e6, 0.15 * 2 * g_recEyeW * g_recH * g_recSet.fps));
    bool ok = f.mf && recDeviceOpen(f);
    if (ok && g_recSet.audio && !recSoundOpen(f)) logf("video: no sound in this video");
    ok = ok && recWriterOpen(f, bitrate);
    if (ok) {
        QueryPerformanceCounter(&g_recT0);
        g_recLastTick = -1;
        if (f.capture) f.audioThread = CreateThread(nullptr, 0, recSoundThread, nullptr, 0, nullptr);
        g_recState = REC_RUNNING;
        logf("video: %ls.mp4 begun - %u x %u (each eye %u x %u), %d pictures a second, HEVC at %.0f Mbit/s, sound: %s",
             g_recName.c_str(), 2 * g_recEyeW, g_recH, g_recEyeW, g_recH, g_recSet.fps, bitrate / 1e6,
             f.capture ? (f.sound[0] == 'p' ? "the game's alone" : "all the output device plays") : "none");
        Beep(1500, 80);
        for (;;) {
            WaitForSingleObject(g_recWake, 50);
            for (;;) {
                std::pair<int, long long> p;
                {
                    std::lock_guard<std::mutex> lk(g_recMx);
                    if (g_recQueue.empty()) break;
                    p = g_recQueue.front();
                    g_recQueue.pop_front();
                }
                recWritePicture(f, p.first, p.second);
            }
            if (g_recState.load() == REC_STOPPING) break;
        }
    }
    const double seconds = ok ? recNow() : 0;
    if (f.audioThread) {
        f.audioStop = true;
        WaitForSingleObject(f.audioThread, 5000);
        CloseHandle(f.audioThread);
    }
    bool saved = false;
    if (ok) saved = recStep(f.writer->Finalize(), "finishing the file");
    recRelease(f.writer);
    for (int i = 0; i < 200; ++i) { // (the encoder has let go of every picture by now; if not, a moment for it)
        bool busy = false;
        for (RecSlot &s : g_recPool) busy = busy || s.busy.load();
        if (!busy) break;
        Sleep(10);
    }
    for (RecSlot &s : g_recPool) recRelease(s.texEnc);
    recRelease(f.capture), recRelease(f.client), recRelease(f.mgr);
    if (f.audioEvent) CloseHandle(f.audioEvent);
    if (saved) recJson(f, seconds, bitrate);
    {
        std::lock_guard<std::mutex> lk(g_recMx);
        g_recQueue.clear();
        wchar_t b[160];
        if (saved)
            swprintf(b, 160, L"Video saved: %ls.mp4 (%.0f s)", g_recName.c_str(), seconds);
        else
            swprintf(b, 160, L"No video - see mw4vr.log");
        g_recMsg = b;
    }
    if (saved)
        logf("video: %ls.mp4 saved - %.1f s, %lld pictures, %lld left out (the encoder was behind)", g_recName.c_str(),
             seconds, g_recFrames.load(), g_recDropped.load());
    else
        logf("video: no video was saved");
    if (f.mf) MFShutdown();
    if (SUCCEEDED(co)) CoUninitialize();
    f.video = f.audio = 0, f.audioEvent = nullptr, f.audioThread = nullptr, f.audioStop = false;
    f.sound = "none", f.written = 0, f.mf = false;
    Beep(saved ? 700 : 300, saved ? 80 : 300);
    g_recState = REC_IDLE;
    return 0;
}

// ---------------------------------------------------------------------------------------------
// the render thread: the key

static void recStart() {
    const std::wstring ini = g_dir + L"mw4vr.ini";
    g_recSet.fps = min(120, max(10, (int)GetPrivateProfileIntW(L"vr", L"rec_fps", 60, ini.c_str())));
    g_recSet.mbit = min(800, max(0, (int)GetPrivateProfileIntW(L"vr", L"rec_bitrate_mbps", 0, ini.c_str())));
    g_recSet.hud = GetPrivateProfileIntW(L"vr", L"rec_hud", 1, ini.c_str()) != 0;
    g_recSet.audio = GetPrivateProfileIntW(L"vr", L"rec_audio", 1, ini.c_str()) != 0;
    g_recSet.light = GetPrivateProfileIntW(L"vr", L"rec_light", 1, ini.c_str()) != 0;
    const char *why = nullptr;
    if (!g_vr.ok || !g_vr.running || !g_vr.w || !g_vr.h || !g_dev)
        why = "the headset's pictures are not there yet";
    else if (!LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) ||
             !LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
        why = "this Windows has no Media Foundation (an N edition without the Media Feature Pack?)";
    if (!why) {
        if (g_recGfx == 0) g_recGfx = recGfxCreate() ? 1 : -1;
        if (g_recGfx != 1) why = "its shaders could not be made";
    }
    if (!why) {
        // each eye as it is rendered - smaller only where the encoder's picture would be too wide; even sizes
        UINT w = g_vr.w, h = g_vr.h;
        if (w > REC_MAX_SIDE || h > REC_MAX_SIDE) {
            const float k = (float)REC_MAX_SIDE / (float)max(w, h);
            w = (UINT)(w * k), h = (UINT)(h * k);
        }
        g_recEyeW = w & ~1u, g_recH = h & ~1u;
        if (g_recEyeW < 64 || g_recH < 64 || !recPoolCreate()) why = "no textures for its pictures";
    }
    if (!why && !g_recWake) g_recWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!why && !g_recWake) why = "no event";
    if (why) {
        logf("video: not begun - %s", why);
        noteShow(L"No video - see mw4vr.log");
        return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[64];
    swprintf(name, 64, L"mw4vr_%04u%02u%02u_%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    const std::wstring dir = g_dir + L"mw4vr_videos";
    CreateDirectoryW(dir.c_str(), nullptr);
    g_recName = name;
    g_recPath = dir + L"\\" + name;
    g_recFrames = 0, g_recDropped = 0;
    g_recFovKnown = false, g_recIpd = g_vr.ipd;
    QueryPerformanceFrequency(&g_recFreq);
    g_recState = REC_STARTING;
    g_recThread = CreateThread(nullptr, 0, recThreadMain, nullptr, 0, nullptr);
    if (!g_recThread) {
        g_recState = REC_IDLE;
        recPoolFree();
        logf("video: not begun - no thread");
    }
}

void recKeys() {
    // a video that has ended: its thread is gone, its textures go, and the headset is told
    if (g_recThread && g_recState.load() == REC_IDLE && WaitForSingleObject(g_recThread, 0) == WAIT_OBJECT_0) {
        CloseHandle(g_recThread);
        g_recThread = nullptr;
        recPoolFree();
        recImagesFree();
        std::lock_guard<std::mutex> lk(g_recMx);
        if (!g_recMsg.empty()) noteShow(g_recMsg.c_str());
        g_recMsg.clear();
    }
    static bool was;
    const bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && !(GetAsyncKeyState(VK_MENU) & 0x8000) &&
                      (GetAsyncKeyState(VK_F5) & 0x8000);
    if (down && !was) {
        const int s = g_recState.load();
        if (s == REC_IDLE && !g_recThread)
            recStart();
        else if (s == REC_RUNNING) {
            g_recState = REC_STOPPING;
            SetEvent(g_recWake);
        }
    }
    was = down;
}
