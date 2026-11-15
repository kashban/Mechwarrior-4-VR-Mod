// ---------------------------------------------------------------------------------------------
// A cockpit of our own: a mesh (tools/cockpit_convert.py -> mw4vr_cockpits\<name>.mwc) drawn in
// place of the game's cockpit. The game's cockpit is its CPU-path geometry nearer than the cockpit
// range; our replacement shader for that path throws those vertices away while this is on, and at
// the first such draw of each eye's pass of the main camera the mesh is drawn into the same
// targets: fixed in the game camera's own space (the head pose undone, like the game's cockpit),
// with the game's projection, depth and TAA jitter. Lighting: a fixed lamp in the cockpit plus the
// emissive map. Off until Ctrl+F11 ([vr] mw5_cockpit names the file; mw5_on=1 starts with it on).
// Ctrl+Alt+arrows: the eye forward / back / up / down in the mesh (2 cm); Ctrl+Alt+PgUp / PgDn: its size.
// A cockpit's own settings - the eye, the size, what its monitors show - are in mw4vr_cockpits\<name>.ini
// ([cockpit], written by the converter); without that file the [vr] mw5_* values of mw4vr.ini hold.
// Ctrl+Alt+F11 takes the next cockpit file (with Shift the one before) and remembers it for the mech one sits
// in (mw4vr_mechs.ini: mw5_cockpit in the mech's section), or as [vr] mw5_cockpit while no mech is known.
// Included into main.cpp after vr.inc.

typedef void(STDMETHODCALLTYPE *Mw5PFN_DrawIdx)(ID3D11DeviceContext *, UINT, UINT, INT);

struct Mw5Group {
    ID3D11Buffer *vb, *ib;
    UINT nIdx;
    ID3D11ShaderResourceView *clr, *ems;
    int screen;   // 0: the mesh; n + 1: monitor n, shown with a part of the game's HUD
    float aspect; // a monitor's width / height
    float centre[3]; // its middle in the mesh
};
static volatile int g_m5Looked; // the monitor the pilot looks at (n + 1; 0: none), for Ctrl+Alt+1..5
// The parts of the HUD image (u0 v0 u1 v1): radar, target, damage, lance list, weapons; and the top strip between
// the last two (compass, mission texts; its lower edge is [vr] hud_top). What no monitor shows of them is shown on
// the cockpit's glass: [cockpit] glass_<name> = degrees right, degrees up, width in degrees (width 0: not shown).
#define M5_PARTS 6
static const wchar_t *const g_m5PartKey[M5_PARTS] = {L"radar", L"target", L"damage", L"lance", L"weapons", L"top"};
static const wchar_t *const g_m5PartName[M5_PARTS] = {L"radar", L"target", L"damage", L"lance list", L"weapons", L"top strip"};
static const float g_m5Part[M5_PARTS][4] = {{0.320f, 0.770f, 0.690f, 0.990f}, {0.000f, 0.772f, 0.180f, 1.000f}, {0.820f, 0.772f, 1.005f, 1.000f},
                                            {0.000f, 0.000f, 0.243f, 0.108f}, {0.716f, 0.000f, 1.000f, 0.124f}, {0.243f, 0.000f, 0.716f, 0.120f}};
static const float g_m5LanceMenu = 0.435f; // the lance list with its open orders menu reaches this far down
static const float g_m5GlassDef[M5_PARTS][3] = {{0, -17, 22}, {-26, -14, 16}, {26, -14, 16}, {-32, 20, 18}, {32, 20, 22}, {0, 22, 30}};
static float g_m5Glass[M5_PARTS][3];
static float g_m5GlassDist = 1.2f;
static bool g_m5GlassOn = true; // [vr] mw5_glass
static int g_m5GlassLast = -1;  // the part placed last (Ctrl+Alt+Shift+PgUp / PgDn: its size)
#define MW5_GROUPS 40
static Mw5Group g_m5[MW5_GROUPS];
static int g_m5N;
static int g_mw5State;             // 0: not loaded yet, 1: ready, -1: failed (stays off)
static volatile bool g_mw5On;
static bool g_mw5IniRead;
static wchar_t g_mw5Name[64];
static float g_mw5Scale = 1, g_mw5Eye[3], g_mw5Ems = 1.5f, g_mw5Amb = 0.45f;
static int g_mw5LinearOut, g_mw5Reversed = 1;
static float g_mw5DepthFront = 0.5f;
static ID3D11VertexShader *g_m5VS;
static ID3D11PixelShader *g_m5PS;
static ID3D11InputLayout *g_m5IL;
static ID3D11Buffer *g_m5CB;
static ID3D11SamplerState *g_m5Samp;
static ID3D11BlendState *g_m5Blend;
static ID3D11RasterizerState *g_m5RS;
static ID3D11DepthStencilState *g_m5DS[2]; // [1]: reversed depth
// the monitors: the game's HUD image of the last frame (a copy, made at Present), and the part of it each shows
#define MW5_SCREENS 8
static ID3D11Texture2D *g_m5Hud;
static ID3D11ShaderResourceView *g_m5HudSrv;
static UINT g_m5HudW, g_m5HudH;
static int g_m5Screens;
static float g_m5Rect[MW5_SCREENS][4]; // u0 v0 u1 v1 of the HUD image; u1 <= u0: dark
static float g_m5Alt[MW5_SCREENS][6];  // a second part (u0 v0 u1 v1) and its probe point (u v); u1 <= u0: none
static float g_m5ScreenGain = 1, g_m5RedBoost = 6;
static int g_mw5ScreensWanted = 1;
static bool g_mw5Own;       // this cockpit has a settings file of its own
static int g_mw5Mech = -2;  // the mech profile the cockpit was chosen for (-1: none known)

static std::wstring mw5OwnIni() { return g_dir + L"mw4vr_cockpits\\" + g_mw5Name + L".ini"; }

static const char g_m5Hlsl[] = R"HLSL(
cbuffer Frame : register(b0) {
    float4 g_screen;   // z > 0: TAA jitter on
    float4 g_rtJitter; // zw: jitter in clip units
    float4 g_rtOrigin;
};
cbuffer C : register(b13) {
    row_major float4x4 M; // mesh -> this eye's view space (x right, y up, z forward)
    float4 proj;          // P00, P11, n, reversed depth
    float4 lamp;          // xyz: towards the lamp (mesh space); w: the share of the depth range kept for the mesh alone
    float4 par;           // emissive, ambient, linear output, > 0: a monitor
    float4 scr;           // a monitor: the part of the HUD image, u0 v0 u1 v1
    float4 scr2;          // xy: the monitor's uv (about its middle) -> the part's (about its middle); z: gain; w: dark red times this at most
    float4 alt;           // a second part, shown while the HUD image is not empty at the probe point (z <= x: none)
    float4 alt2;          // xy: as scr2.xy, for the second part; zw: the probe point
};
struct VSIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };
VSOut vs(VSIn v) {
    VSOut o;
    float3 p = mul(M, float4(v.pos, 1)).xyz;
    // depth: the mesh is drawn larger than life (the cockpit's scale), so the ground would cut through its floor;
    // its depth is pressed into the nearest part of the range, in front of everything of the world
    float k = lamp.w;
    o.pos = float4(proj.x * p.x, proj.y * p.y, proj.w > 0 ? k * p.z + (1 - k) * proj.z : (1 - k) * (p.z - proj.z), p.z);
    if (g_screen.z > 0) o.pos.xy += g_rtJitter.zw * p.z;
    o.nrm = v.nrm;
    o.uv = v.uv;
    return o;
}
Texture2D tc : register(t0);
Texture2D te : register(t1);
Texture2D th : register(t2);
SamplerState sm : register(s0);
float4 ps(VSOut i) : SV_Target {
    if (par.w > 0) { // a monitor: its part of the HUD image, whole, on black
        float4 r = scr;
        float2 f = scr2.xy;
        if (alt.z > alt.x) {
            float4 t = th.SampleLevel(sm, alt2.zw, 0);
            if (max(max(t.r, t.g), max(t.b, t.a)) > 0.02) {
                r = alt;
                f = alt2.xy;
            }
        }
        float2 q = (i.uv - 0.5) * f;
        float3 h = 0;
        if (r.z > r.x && abs(q.x) < 0.5 && abs(q.y) < 0.5) h = th.SampleLevel(sm, lerp(r.xy, r.zw, q + 0.5), 0).rgb * scr2.z;
        // dark red (an enemy's weapon list: black letters on a translucent red bar, dim without the scene behind it): brighter
        // (a weapon's state is its bar's colour - green, yellow, red: the red stays clearly red, less of its green)
        if (scr2.w > 1 && h.r > 0.06 && h.r > 1.6 * h.g && h.r > 1.6 * h.b) {
            float k = min(scr2.w, max(1.0, 0.9 / h.r));
            h *= float3(k, 1 + (k - 1) * 0.4, k);
        }
        h += 0.012;
        if (par.z > 0) h = pow(saturate(h), 2.2);
        return float4(h, 1);
    }
    float3 n = normalize(i.nrm);
    float l = par.y + (1 - par.y) * saturate(dot(n, lamp.xyz) * 0.5 + 0.5);
    float3 c = tc.Sample(sm, i.uv).rgb * l + te.Sample(sm, i.uv).rgb * par.x;
    if (par.z > 0) c = pow(saturate(c), 2.2);
    return float4(c, 1);
}
)HLSL";

static inline bool mw5Active() { return g_mw5On && g_mw5State == 1; }

// a cockpit's value: from its own file ([cockpit] <key>) or, without one, from mw4vr.ini ([vr] mw5_<key>)
static void mw5CockpitStr(const wchar_t *key, wchar_t *b, DWORD n) {
    if (g_mw5Own) {
        GetPrivateProfileStringW(L"cockpit", key, L"", b, n, mw5OwnIni().c_str());
    } else {
        wchar_t k[48];
        swprintf(k, 48, L"mw5_%ls", key);
        GetPrivateProfileStringW(L"vr", k, L"", b, n, (g_dir + L"mw4vr.ini").c_str());
    }
}
static float mw5CockpitF(const wchar_t *key, float def) {
    wchar_t b[64];
    mw5CockpitStr(key, b, 64);
    return b[0] ? (float)_wtof(b) : def;
}
static void mw5CockpitPutF(const wchar_t *key, float v) {
    wchar_t b[32], k[48];
    swprintf(b, 32, L"%.3f", v);
    swprintf(k, 48, L"mw5_%ls", key);
    if (g_mw5Own) WritePrivateProfileStringW(L"cockpit", key, b, mw5OwnIni().c_str());
    else WritePrivateProfileStringW(L"vr", k, b, (g_dir + L"mw4vr.ini").c_str());
}

static float mw5GetF(const wchar_t *key, float def) {
    wchar_t b[64], d[32];
    swprintf(d, 32, L"%g", def);
    GetPrivateProfileStringW(L"vr", key, d, b, 64, (g_dir + L"mw4vr.ini").c_str());
    return (float)_wtof(b);
}
static void mw5ReadIni() {
    std::wstring ini = g_dir + L"mw4vr.ini";
    if (!g_mw5IniRead) GetPrivateProfileStringW(L"vr", L"mw5_cockpit", L"", g_mw5Name, 64, ini.c_str());
    g_mw5Own = g_mw5Name[0] && GetFileAttributesW(mw5OwnIni().c_str()) != INVALID_FILE_ATTRIBUTES;
    g_mw5Scale = mw5CockpitF(L"scale", 1);
    if (!(g_mw5Scale > 0.05f)) g_mw5Scale = 1;
    g_mw5Eye[0] = mw5CockpitF(L"eye_right_m", 0);
    g_mw5Eye[1] = mw5CockpitF(L"eye_up_m", 0);
    g_mw5Eye[2] = mw5CockpitF(L"eye_forward_m", 0);
    g_mw5Ems = mw5GetF(L"mw5_emissive", 1.5f);
    g_mw5Amb = mw5GetF(L"mw5_ambient", 0.45f);
    g_mw5DepthFront = mw5GetF(L"mw5_depth_front", 0.5f);
    if (!(g_mw5DepthFront >= 0 && g_mw5DepthFront <= 0.95f)) g_mw5DepthFront = 0.5f;
    g_mw5LinearOut = GetPrivateProfileIntW(L"vr", L"mw5_linear_output", 0, ini.c_str());
    g_mw5Reversed = GetPrivateProfileIntW(L"vr", L"mw5_reversed_depth", 1, ini.c_str());
    g_mw5ScreensWanted = GetPrivateProfileIntW(L"vr", L"mw5_screens", 1, ini.c_str()) && mw5CockpitF(L"screens", 1) != 0;
    g_m5ScreenGain = mw5GetF(L"mw5_screen_gain", 1);
    g_m5RedBoost = mw5GetF(L"mw5_screen_red_boost", 6);
    for (int i = 0; i < MW5_SCREENS; ++i) { // mw5_screen_<n> = u0 v0 u1 v1 (of the HUD image, 0..1)
        wchar_t k[32], b[96];
        swprintf(k, 32, L"screen_%d", i);
        mw5CockpitStr(k, b, 96);
        float *r = g_m5Rect[i];
        if (swscanf(b, L"%f %f %f %f", r, r + 1, r + 2, r + 3) != 4) r[0] = r[1] = r[2] = r[3] = 0;
        // mw5_screen_<n>_alt = u0 v0 u1 v1 pu pv: this part instead, while something is drawn at pu pv (an open menu)
        swprintf(k, 32, L"screen_%d_alt", i);
        mw5CockpitStr(k, b, 96);
        float *a = g_m5Alt[i];
        if (swscanf(b, L"%f %f %f %f %f %f", a, a + 1, a + 2, a + 3, a + 4, a + 5) != 6) a[0] = a[1] = a[2] = a[3] = a[4] = a[5] = 0;
    }
    g_m5GlassOn = GetPrivateProfileIntW(L"vr", L"mw5_glass", 1, ini.c_str()) != 0;
    g_m5GlassDist = mw5CockpitF(L"glass_distance_m", 1.2f);
    if (!(g_m5GlassDist >= 0.3f && g_m5GlassDist <= 20)) g_m5GlassDist = 1.2f;
    for (int i = 0; i < M5_PARTS; ++i) {
        wchar_t k[32], b[96];
        swprintf(k, 32, L"glass_%ls", g_m5PartKey[i]);
        mw5CockpitStr(k, b, 96);
        float *g = g_m5Glass[i];
        if (swscanf(b, L"%f %f %f", g, g + 1, g + 2) != 3) memcpy(g, g_m5GlassDef[i], sizeof g_m5GlassDef[i]);
        if (!(g[2] <= 120)) g[2] = g_m5GlassDef[i][2];
    }
    if (!g_mw5IniRead) {
        g_mw5IniRead = true;
        g_mw5On = g_mw5Name[0] && GetPrivateProfileIntW(L"vr", L"mw5_on", 0, ini.c_str()) != 0;
    }
    static wchar_t told[64];
    if (wcscmp(told, g_mw5Name)) {
        wcscpy(told, g_mw5Name);
        logf("mw5: cockpit '%ls' (%s), settings from %s, size %.2f, eye %.2f m right %.2f m up %.2f m forward", g_mw5Name,
             g_mw5On ? "on" : "off - Ctrl+F11", g_mw5Own ? "its own file" : "mw4vr.ini", g_mw5Scale, g_mw5Eye[0], g_mw5Eye[1], g_mw5Eye[2]);
    }
}

static ID3D11ShaderResourceView *mw5Texture(FILE *f, UINT size) {
    std::vector<uint8_t> px;
    std::vector<D3D11_SUBRESOURCE_DATA> sub;
    size_t total = 0;
    UINT levels = 0;
    for (UINT s = size; s; s >>= 1) total += (size_t)s * s * 4, ++levels;
    px.resize(total);
    if (fread(px.data(), 1, total, f) != total) return nullptr;
    size_t off = 0;
    for (UINT s = size; s; s >>= 1) {
        sub.push_back({px.data() + off, s * 4, 0});
        off += (size_t)s * s * 4;
    }
    D3D11_TEXTURE2D_DESC td = {size, size, levels, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
    ID3D11Texture2D *t = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&td, sub.data(), &t)) || !t) return nullptr;
    ID3D11ShaderResourceView *v = nullptr;
    g_dev->CreateShaderResourceView(t, nullptr, &v);
    t->Release();
    return v;
}

static bool mw5LoadFile() {
    std::wstring path = g_dir + L"mw4vr_cockpits\\" + g_mw5Name + L".mwc";
    FILE *f = _wfopen(path.c_str(), L"rb");
    if (!f) {
        logf("mw5: %ls not found", path.c_str());
        return false;
    }
    bool ok = false;
    char magic[4];
    uint32_t n = 0;
    if (fread(magic, 1, 4, f) == 4 && !memcmp(magic, "MWC1", 4) && fread(&n, 4, 1, f) == 1 && n >= 1 && n <= MW5_GROUPS) {
        ok = true;
        for (uint32_t i = 0; i < n && ok; ++i) {
            struct { char name[48]; uint32_t nv, ni, cs, es; float k; } h;
            ok = fread(&h, sizeof h, 1, f) == 1 && h.nv && h.nv < 4000000 && h.ni && h.ni < 12000000 && h.cs && h.cs <= 4096 && h.es <= 4096;
            if (!ok) break;
            std::vector<uint8_t> v((size_t)h.nv * 32), ix((size_t)h.ni * 4);
            ok = fread(v.data(), 1, v.size(), f) == v.size() && fread(ix.data(), 1, ix.size(), f) == ix.size();
            if (!ok) break;
            Mw5Group &g = g_m5[g_m5N];
            g = {};
            D3D11_BUFFER_DESC bd = {(UINT)v.size(), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
            D3D11_SUBRESOURCE_DATA sd = {v.data(), 0, 0};
            HRESULT h1 = ORIG(PFN_CreateBuffer, g_dev, VT_Dev_CreateBuffer)(g_dev, &bd, &sd, &g.vb);
            bd.ByteWidth = (UINT)ix.size(), bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
            sd.pSysMem = ix.data();
            HRESULT h2 = ORIG(PFN_CreateBuffer, g_dev, VT_Dev_CreateBuffer)(g_dev, &bd, &sd, &g.ib);
            g.nIdx = h.ni;
            for (uint32_t k = 0; k < h.nv; ++k) {
                const float *vp = (const float *)(v.data() + (size_t)k * 32);
                for (int a = 0; a < 3; ++a) g.centre[a] += vp[a] / (float)h.nv;
            }
            h.name[47] = 0;
            if (!strncmp(h.name, "SCREEN:", 7)) {
                g.screen = atoi(h.name + 7) + 1;
                g.aspect = h.k > 0.05f ? h.k : 1;
                if (g.screen > MW5_SCREENS) g.screen = 0;
                if (g.screen) ++g_m5Screens;
            }
            g.clr = mw5Texture(f, h.cs);
            if (h.es) g.ems = mw5Texture(f, h.es);
            ok = SUCCEEDED(h1) && SUCCEEDED(h2) && g.vb && g.ib && g.clr && (!h.es || g.ems);
            h.name[47] = 0;
            logf("mw5:   %s - %u vertices, %u triangles, textures %u / %u%s", h.name, h.nv, h.ni / 3, h.cs, h.es, ok ? "" : " - FAILED");
            if (ok) ++g_m5N;
        }
    } else {
        logf("mw5: %ls is not a cockpit file", path.c_str());
    }
    fclose(f);
    return ok && g_m5N > 0;
}

static bool mw5CreateStates() {
    if (!g_compile || !g_dev) return false;
    ID3DBlob *vc = nullptr, *pc = nullptr, *err = nullptr;
    HRESULT hr = g_compile(g_m5Hlsl, sizeof(g_m5Hlsl) - 1, "mw5_vs", nullptr, nullptr, "vs", "vs_5_0", 1 << 15, 0, &vc, &err);
    if (SUCCEEDED(hr)) {
        if (err) err->Release(), err = nullptr;
        hr = g_compile(g_m5Hlsl, sizeof(g_m5Hlsl) - 1, "mw5_ps", nullptr, nullptr, "ps", "ps_5_0", 1 << 15, 0, &pc, &err);
    }
    if (FAILED(hr) || !vc || !pc) {
        logf("mw5: shaders failed to compile (0x%08lx): %s", hr, err ? (const char *)err->GetBufferPointer() : "");
        if (err) err->Release();
        if (vc) vc->Release();
        return false;
    }
    if (err) err->Release();
    HRESULT h1 = ORIG(PFN_CreateShader, g_dev, VT_Dev_CreateVertexShader)(g_dev, vc->GetBufferPointer(), vc->GetBufferSize(), nullptr, (void **)&g_m5VS);
    HRESULT h2 = ORIG(PFN_CreateShader, g_dev, VT_Dev_CreatePixelShader)(g_dev, pc->GetBufferPointer(), pc->GetBufferSize(), nullptr, (void **)&g_m5PS);
    const D3D11_INPUT_ELEMENT_DESC il[3] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    HRESULT h3 = g_dev->CreateInputLayout(il, 3, vc->GetBufferPointer(), vc->GetBufferSize(), &g_m5IL);
    vc->Release();
    pc->Release();
    D3D11_BUFFER_DESC bd = {176, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    HRESULT h4 = ORIG(PFN_CreateBuffer, g_dev, VT_Dev_CreateBuffer)(g_dev, &bd, nullptr, &g_m5CB);
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_ANISOTROPIC, sd.MaxAnisotropy = 8;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER, sd.MaxLOD = D3D11_FLOAT32_MAX;
    HRESULT h5 = g_dev->CreateSamplerState(&sd, &g_m5Samp);
    D3D11_BLEND_DESC bl = {};
    bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    bl.RenderTarget[0].SrcBlend = bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bl.RenderTarget[0].DestBlend = bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bl.RenderTarget[0].BlendOp = bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    HRESULT h6 = g_dev->CreateBlendState(&bl, &g_m5Blend);
    D3D11_RASTERIZER_DESC rs = {};
    rs.FillMode = D3D11_FILL_SOLID, rs.CullMode = D3D11_CULL_NONE, rs.DepthClipEnable = TRUE;
    HRESULT h7 = g_dev->CreateRasterizerState(&rs, &g_m5RS);
    HRESULT h8 = S_OK;
    for (int r = 0; r < 2; ++r) {
        D3D11_DEPTH_STENCIL_DESC ds = {};
        ds.DepthEnable = TRUE, ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ds.DepthFunc = r ? D3D11_COMPARISON_GREATER_EQUAL : D3D11_COMPARISON_LESS_EQUAL;
        HRESULT h = g_dev->CreateDepthStencilState(&ds, &g_m5DS[r]);
        if (FAILED(h)) h8 = h;
    }
    bool ok = SUCCEEDED(h1) && SUCCEEDED(h2) && SUCCEEDED(h3) && SUCCEEDED(h4) && SUCCEEDED(h5) && SUCCEEDED(h6) && SUCCEEDED(h7) && SUCCEEDED(h8);
    if (!ok) logf("mw5: states not created (%08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx)", h1, h2, h3, h4, h5, h6, h7, h8);
    return ok;
}

// on the game's render thread, at the first use
static void mw5Load() {
    g_mw5State = -1;
    __try {
        if ((g_m5VS || mw5CreateStates()) && mw5LoadFile()) g_mw5State = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("mw5: exception 0x%08lx while loading", GetExceptionCode());
    }
    logf(g_mw5State == 1 ? "mw5: cockpit '%ls' loaded, %d parts" : "mw5: cockpit '%ls' not usable - stays off", g_mw5Name, g_m5N);
    if (g_mw5State != 1) g_mw5On = false;
}

// the monitors show the HUD (the near HUD quads are then left out, vr.inc)
static bool mw5ScreensOn() { return mw5Active() && g_mw5ScreensWanted && g_m5Screens > 0 && g_m5HudSrv; }

// The parts of the HUD shown on the glass: those no monitor shows, with a width. 0: none (no cockpit of our own).
static int mw5GlassItems(GlassItem *out) {
    if (!(mw5Active() && g_m5GlassOn)) return 0;
    bool mon = mw5ScreensOn();
    int n = 0;
    for (int p = 0; p < M5_PARTS; ++p) {
        const float *r = g_m5Part[p], *g = g_m5Glass[p];
        if (!(g[2] > 0.5f)) continue;
        bool shown = false;
        for (int i = 0; i < g_m5N && mon; ++i)
            if (g_m5[i].screen && mcHolds(g_m5Rect[g_m5[i].screen - 1], 0.5f * (r[0] + r[2]), 0.5f * (r[1] + r[3]))) shown = true;
        if (shown) continue;
        GlassItem &it = out[n++];
        memcpy(it.base, r, sizeof it.base), memcpy(it.rc, r, sizeof it.rc);
        if (p == 3) it.rc[3] = g_m5LanceMenu;
        if (p == 5) it.base[3] = it.rc[3] = g_vr.hudTop;
        it.right = g[0], it.up = g[1], it.width = g[2];
    }
    return n;
}
static float mw5GlassDistance() { return g_m5GlassDist; }

// the parts of the HUD image's top strip (above v = T) the monitors show, as spans of u in rising order
static int mw5TopSpans(float T, float (*iv)[2]) {
    int n = 0;
    for (int i = 0; i < g_m5N; ++i)
        if (g_m5[i].screen) mcTopSpan(iv, n, g_m5Rect[g_m5[i].screen - 1], T);
    return n;
}

// the left side column of the HUD image (u < L): how far down the monitors' second parts reach (0: none)
static float mw5LeftAltBottom(float L) {
    float v = 0;
    for (int i = 0; i < g_m5N; ++i)
        if (g_m5[i].screen) v = mcLeftAlt(g_m5Alt[g_m5[i].screen - 1], L, v);
    return v;
}

// at Present, with the HUD image of this frame complete
static void mw5HudCopy(ID3D11DeviceContext *c, ID3D11Texture2D *hud) {
    if (!mw5Active() || !g_mw5ScreensWanted || !g_m5Screens || !hud || !c) return;
    D3D11_TEXTURE2D_DESC d;
    hud->GetDesc(&d);
    if (d.SampleDesc.Count != 1) return;
    if (!g_m5Hud || d.Width != g_m5HudW || d.Height != g_m5HudH) {
        if (g_m5HudSrv) g_m5HudSrv->Release();
        if (g_m5Hud) g_m5Hud->Release();
        g_m5HudSrv = nullptr, g_m5Hud = nullptr;
        D3D11_TEXTURE2D_DESC n = d;
        n.MipLevels = 1, n.ArraySize = 1, n.Usage = D3D11_USAGE_DEFAULT, n.BindFlags = D3D11_BIND_SHADER_RESOURCE, n.CPUAccessFlags = 0, n.MiscFlags = 0;
        HRESULT hr = g_dev->CreateTexture2D(&n, nullptr, &g_m5Hud);
        if (SUCCEEDED(hr)) hr = g_dev->CreateShaderResourceView(g_m5Hud, nullptr, &g_m5HudSrv);
        g_m5HudW = d.Width, g_m5HudH = d.Height;
        logf("mw5: HUD image for %d monitors, %ux%u format %d -> 0x%08lx", g_m5Screens, d.Width, d.Height, (int)d.Format, hr);
        if (FAILED(hr)) {
            if (g_m5Hud) g_m5Hud->Release();
            g_m5Hud = nullptr, g_m5HudSrv = nullptr;
        }
    }
    if (g_m5Hud) c->CopyResource(g_m5Hud, hud);
}

// at a CPU-path draw of the main camera's pass for an eye (our replacement shader is bound): once per pass
static void mw5Draw(ID3D11DeviceContext *c, Mw5PFN_DrawIdx draw) {
    int e = g_camPassEye;
    if (e < 0 || e > 1 || !g_camInMain || !g_kTlOnly) return;
    static unsigned done[2] = {~0u, ~0u};
    if (done[e] == g_frame) return;
    { // a small camera (the target's view) drawn inside the main camera's pass: not there
        D3D11_VIEWPORT vp[1] = {};
        UINT nvp = 1;
        c->RSGetViewports(&nvp, vp);
        if (nvp && g_eyeTexW && vp[0].Width < 0.9f * g_eyeTexW) return;
    }
    done[e] = g_frame;
    if (g_mw5State == 0) mw5Load();
    if (g_mw5State != 1 || !g_vrPoseOk[e] || g_P00 <= 0 || g_P11 <= 0 || g_near <= 0) return;

    // what is bound now
    ID3D11InputLayout *oIL = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY oTopo;
    ID3D11Buffer *oVB = nullptr, *oIB = nullptr, *oVCB = nullptr, *oPCB = nullptr;
    UINT oStride = 0, oOff = 0, oIOff = 0, oMask = 0, oRef = 0;
    DXGI_FORMAT oIFmt = DXGI_FORMAT_UNKNOWN;
    ID3D11VertexShader *oVS = nullptr;
    ID3D11PixelShader *oPS = nullptr;
    ID3D11ShaderResourceView *oSRV[3] = {};
    ID3D11SamplerState *oSamp = nullptr;
    ID3D11BlendState *oBlend = nullptr;
    ID3D11DepthStencilState *oDS = nullptr;
    ID3D11RasterizerState *oRS = nullptr;
    float oFactor[4];
    c->IAGetInputLayout(&oIL);
    c->IAGetPrimitiveTopology(&oTopo);
    c->IAGetVertexBuffers(0, 1, &oVB, &oStride, &oOff);
    c->IAGetIndexBuffer(&oIB, &oIFmt, &oIOff);
    c->VSGetShader(&oVS, nullptr, nullptr);
    c->PSGetShader(&oPS, nullptr, nullptr);
    c->VSGetConstantBuffers(13, 1, &oVCB);
    c->PSGetConstantBuffers(13, 1, &oPCB);
    c->PSGetShaderResources(0, 3, oSRV);
    c->PSGetSamplers(0, 1, &oSamp);
    c->OMGetBlendState(&oBlend, oFactor, &oMask);
    c->OMGetDepthStencilState(&oDS, &oRef);
    c->RSGetState(&oRS);

    // depth: the way the game's own state compares, when it tells
    int rev = g_mw5Reversed ? 1 : 0;
    D3D11_DEPTH_STENCIL_DESC dd = {};
    if (oDS) {
        oDS->GetDesc(&dd);
        if (dd.DepthEnable && (dd.DepthFunc == D3D11_COMPARISON_GREATER || dd.DepthFunc == D3D11_COMPARISON_GREATER_EQUAL)) rev = 1;
        if (dd.DepthEnable && (dd.DepthFunc == D3D11_COMPARISON_LESS || dd.DepthFunc == D3D11_COMPARISON_LESS_EQUAL)) rev = 0;
    }
    static bool told;
    if (!told) {
        told = true;
        ID3D11RenderTargetView *rt[8] = {};
        ID3D11DepthStencilView *dsv = nullptr;
        c->OMGetRenderTargets(8, rt, &dsv);
        char s[200] = "";
        for (int i = 0; i < 8; ++i)
            if (rt[i]) {
                D3D11_RENDER_TARGET_VIEW_DESC d;
                rt[i]->GetDesc(&d);
                sprintf(s + strlen(s), " %d:fmt%d", i, (int)d.Format);
                rt[i]->Release();
            }
        D3D11_VIEWPORT vp[1] = {};
        UINT nvp = 1;
        c->RSGetViewports(&nvp, vp);
        logf("mw5: first draw - targets%s, depth view %s, game depth state: enable %d write %d func %d -> %s depth; viewport %.0f x %.0f",
             s, dsv ? "yes" : "NO", (int)dd.DepthEnable, (int)dd.DepthWriteMask, (int)dd.DepthFunc, rev ? "reversed" : "forward", vp[0].Width, vp[0].Height);
        if (dsv) dsv->Release();
    }

    // mesh -> this eye's view: c = S (v - eye); p = R^T (c - t - raw (cockpit scale - world scale))
    const float *R = g_vrR[e], *T = g_vrT[e], *raw = g_vrRaw[e];
    float S = g_vr.cockpitScale * g_mw5Scale;
    float cb[44] = {};
    float o[3];
    for (int i = 0; i < 3; ++i) o[i] = -S * g_mw5Eye[i] - T[i] - raw[i] * g_cockpitDelta;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) cb[i * 4 + j] = R[j * 3 + i] * S;
        cb[i * 4 + 3] = R[0 * 3 + i] * o[0] + R[1 * 3 + i] * o[1] + R[2 * 3 + i] * o[2];
    }
    cb[15] = 1;
    if (e == 0) { // which monitor lies nearest the middle of the view (within 12 degrees)?
        int looked = 0;
        float best = 0.978f;
        for (int i = 0; i < g_m5N; ++i) {
            if (!g_m5[i].screen) continue;
            const float *q = g_m5[i].centre;
            float p[3];
            for (int a = 0; a < 3; ++a) p[a] = cb[a * 4] * q[0] + cb[a * 4 + 1] * q[1] + cb[a * 4 + 2] * q[2] + cb[a * 4 + 3];
            float l = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            if (p[2] > 0 && l > 0 && p[2] / l > best) best = p[2] / l, looked = g_m5[i].screen;
        }
        g_m5Looked = looked;
    }
    cb[16] = g_P00, cb[17] = g_P11, cb[18] = g_near, cb[19] = (float)rev;
    cb[20] = 0.25f, cb[21] = 0.85f, cb[22] = 0.45f; // the lamp: above, a little ahead and to the right
    cb[23] = g_mw5DepthFront;
    cb[24] = g_mw5Ems, cb[25] = g_mw5Amb, cb[26] = (float)g_mw5LinearOut;
    auto put = [&]() {
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(ORIG(PFN_Map, c, VT_Ctx_Map)(c, g_m5CB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
        memcpy(m.pData, cb, sizeof cb);
        ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, g_m5CB, 0);
        return true;
    };
    if (put()) {
        c->IASetInputLayout(g_m5IL);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ORIG(PFN_SetShader, c, VT_Ctx_VSSetShader)(c, g_m5VS, nullptr, 0);
        ORIG(PFN_SetShader, c, VT_Ctx_PSSetShader)(c, g_m5PS, nullptr, 0);
        ORIG(PFN_SetCB, c, VT_Ctx_VSSetConstantBuffers)(c, 13, 1, &g_m5CB);
        ORIG(PFN_SetCB, c, VT_Ctx_PSSetConstantBuffers)(c, 13, 1, &g_m5CB);
        c->PSSetSamplers(0, 1, &g_m5Samp);
        const float one[4] = {1, 1, 1, 1};
        c->OMSetBlendState(g_m5Blend, one, 0xffffffff);
        c->OMSetDepthStencilState(g_m5DS[rev], 0);
        c->RSSetState(g_m5RS);
        for (int i = 0; i < g_m5N; ++i) {
            const Mw5Group &g = g_m5[i];
            UINT stride = 32, off = 0;
            c->IASetVertexBuffers(0, 1, &g.vb, &stride, &off);
            c->IASetIndexBuffer(g.ib, DXGI_FORMAT_R32_UINT, 0);
            ID3D11ShaderResourceView *sv[3] = {g.clr, g.ems, nullptr};
            bool scr = g.screen && g_mw5ScreensWanted;
            if (scr) { // the part of the HUD image, whole and undistorted in the monitor
                const float *r = g_m5Rect[g.screen - 1];
                bool have = g_m5HudSrv && r[2] > r[0] && r[3] > r[1] && g_m5HudH;
                // the rectangles count in pictures (u = 1: the game's right edge); the HUD image may be wider (hud.inc)
                float pw = g_hudW ? (float)g_hudW : (float)g_m5HudW, ku = g_m5HudW ? pw / g_m5HudW : 1;
                float ar = have ? (r[2] - r[0]) * pw / ((r[3] - r[1]) * g_m5HudH) : 1;
                cb[27] = 1;
                cb[28] = r[0] * ku, cb[29] = r[1], cb[30] = (have ? r[2] : r[0]) * ku, cb[31] = r[3];
                cb[32] = ar > g.aspect ? 1 : g.aspect / ar, cb[33] = ar > g.aspect ? ar / g.aspect : 1, cb[34] = g_m5ScreenGain, cb[35] = g_m5RedBoost;
                const float *a = g_m5Alt[g.screen - 1];
                bool haveA = have && a[2] > a[0] && a[3] > a[1];
                float aa = haveA ? (a[2] - a[0]) * pw / ((a[3] - a[1]) * g_m5HudH) : 1;
                cb[36] = a[0] * ku, cb[37] = a[1], cb[38] = (haveA ? a[2] : a[0]) * ku, cb[39] = a[3];
                cb[40] = aa > g.aspect ? 1 : g.aspect / aa, cb[41] = aa > g.aspect ? aa / g.aspect : 1, cb[42] = a[4] * ku, cb[43] = a[5];
                sv[2] = have ? g_m5HudSrv : nullptr;
                if (!put()) continue;
            }
            c->PSSetShaderResources(0, 3, sv);
            draw(c, g.nIdx, 0, 0);
            if (scr) {
                cb[27] = 0;
                put();
            }
        }
    }

    // and back
    c->IASetInputLayout(oIL);
    c->IASetPrimitiveTopology(oTopo);
    c->IASetVertexBuffers(0, 1, &oVB, &oStride, &oOff);
    c->IASetIndexBuffer(oIB, oIFmt, oIOff);
    ORIG(PFN_SetShader, c, VT_Ctx_VSSetShader)(c, oVS, nullptr, 0);
    ORIG(PFN_SetShader, c, VT_Ctx_PSSetShader)(c, oPS, nullptr, 0);
    ORIG(PFN_SetCB, c, VT_Ctx_VSSetConstantBuffers)(c, 13, 1, &oVCB);
    ORIG(PFN_SetCB, c, VT_Ctx_PSSetConstantBuffers)(c, 13, 1, &oPCB);
    c->PSSetShaderResources(0, 3, oSRV);
    c->PSSetSamplers(0, 1, &oSamp);
    c->OMSetBlendState(oBlend, oFactor, oMask);
    c->OMSetDepthStencilState(oDS, oRef);
    c->RSSetState(oRS);
    IUnknown *rel[] = {oIL, oVB, oIB, oVS, oPS, oVCB, oPCB, oSRV[0], oSRV[1], oSRV[2], oSamp, oBlend, oDS, oRS};
    for (IUnknown *u : rel)
        if (u) u->Release();
}

// another cockpit file: what is loaded goes, the new one is loaded at its first use
static void mw5Switch(const wchar_t *name) {
    if (!_wcsicmp(name, g_mw5Name) && g_mw5State >= 0) return;
    for (int i = 0; i < g_m5N; ++i) {
        IUnknown *rel[] = {g_m5[i].vb, g_m5[i].ib, g_m5[i].clr, g_m5[i].ems};
        for (IUnknown *u : rel)
            if (u) u->Release();
        g_m5[i] = {};
    }
    g_m5N = g_m5Screens = 0;
    g_mw5State = 0;
    wcsncpy(g_mw5Name, name, 63);
    g_mw5Name[63] = 0;
    mw5ReadIni();
}

// the cockpit files there are, in the order of their names
static std::vector<std::wstring> mw5Files() {
    std::vector<std::wstring> v;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_dir + L"mw4vr_cockpits\\*.mwc").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return v;
    do {
        std::wstring n = fd.cFileName;
        v.push_back(n.substr(0, n.size() - 4));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(v.begin(), v.end(), [](const std::wstring &a, const std::wstring &b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    return v;
}

// the cockpit of the mech one sits in: what mw4vr_mechs.ini remembers for it, else [vr] mw5_cockpit
static void mw5ForMech() {
    if (g_mechCur == g_mw5Mech) return;
    g_mw5Mech = g_mechCur;
    if (g_mechCur < 0) return; // between battles: what is loaded stays
    wchar_t name[64] = L"", sec[32];
    swprintf(sec, 32, L"mech_%d", g_mechCur + 1);
    GetPrivateProfileStringW(sec, L"mw5_cockpit", L"", name, 64, mechIni().c_str());
    if (!name[0]) GetPrivateProfileStringW(L"vr", L"mw5_cockpit", L"", name, 64, (g_dir + L"mw4vr.ini").c_str());
    if (!name[0] || !_wcsicmp(name, g_mw5Name)) return;
    logf("mw5: cockpit '%ls' for mech profile %d", name, g_mechCur + 1);
    mw5Switch(name);
    wchar_t note[160];
    swprintf(note, 160, L"Cockpit: %ls  (mech %d)", name, g_mechCur + 1);
    noteShow(note);
}

// Ctrl+Alt+F11: the next cockpit file (Shift: the one before), remembered for this mech
static void mw5Next(int step) {
    std::vector<std::wstring> v = mw5Files();
    if (v.empty()) {
        logf("mw5: no cockpit files in mw4vr_cockpits");
        Beep(300, 200);
        return;
    }
    int n = (int)v.size(), at = step > 0 ? -1 : 0;
    for (int i = 0; i < n; ++i)
        if (!_wcsicmp(v[i].c_str(), g_mw5Name)) at = i;
    int pick = ((at + step) % n + n) % n;
    const std::wstring &name = v[pick];
    wchar_t sec[32];
    swprintf(sec, 32, L"mech_%d", g_mechCur + 1);
    if (g_mechCur >= 0) WritePrivateProfileStringW(sec, L"mw5_cockpit", name.c_str(), mechIni().c_str());
    else WritePrivateProfileStringW(L"vr", L"mw5_cockpit", name.c_str(), (g_dir + L"mw4vr.ini").c_str());
    logf(g_mechCur >= 0 ? "mw5: cockpit '%ls' chosen for mech profile %d" : "mw5: cockpit '%ls' chosen (no mech known: for all)", name.c_str(), g_mechCur + 1);
    mw5Switch(name.c_str());
    g_mw5On = true;
    wchar_t note[160];
    if (g_mechCur >= 0) swprintf(note, 160, L"Cockpit %d / %d: %ls  (mech %d)", pick + 1, n, name.c_str(), g_mechCur + 1);
    else swprintf(note, 160, L"Cockpit %d / %d: %ls  (no mech known)", pick + 1, n, name.c_str());
    noteShow(note);
    Beep(1000, 40);
}

// a cockpit's value written: into its own file or, without one, into mw4vr.ini (nullptr: the key goes)
static void mw5CockpitPutStr(const wchar_t *key, const wchar_t *val) {
    wchar_t k[48];
    swprintf(k, 48, L"mw5_%ls", key);
    if (g_mw5Own) WritePrivateProfileStringW(L"cockpit", key, val, mw5OwnIni().c_str());
    else WritePrivateProfileStringW(L"vr", k, val, (g_dir + L"mw4vr.ini").c_str());
}

// where the pilot looks at the glass (degrees right and up from the seat); false: no head pose
static bool mw5GlassAim(float *right, float *up) {
    if (!(g_vr.haveRef && g_vr.nextOk[0] && g_vr.nextOk[1])) return false;
    XrQuaternionf ri = qconj(g_vr.ref.orientation);
    const XrVector3f &a = g_vr.next[0].position, &b = g_vr.next[1].position, &o = g_vr.ref.position;
    XrVector3f hp = qrot(ri, XrVector3f{0.5f * (a.x + b.x) - o.x, 0.5f * (a.y + b.y) - o.y, 0.5f * (a.z + b.z) - o.z});
    XrVector3f hf = qrot(ri, qrot(g_vr.next[0].orientation, XrVector3f{0, 0, -1}));
    const float p[3] = {hp.x, hp.y, hp.z}, f[3] = {hf.x, hf.y, hf.z};
    mcGlassAim(p, f, g_m5GlassDist, right, up);
    return true;
}
static void mw5GlassPut(int p) {
    wchar_t k[32], b[96];
    swprintf(k, 32, L"glass_%ls", g_m5PartKey[p]);
    swprintf(b, 96, L"%.1f %.1f %.1f", g_m5Glass[p][0], g_m5Glass[p][1], g_m5Glass[p][2]);
    mw5CockpitPutStr(k, b);
}

// Ctrl+Alt+1..6 (radar, target, damage, lance list, weapons, top strip): the monitor looked at shows this part of the
// HUD from now on; looking elsewhere, the part goes onto the glass there. Ctrl+Alt+0: the monitor looked at goes dark.
// With Shift: the part leaves the glass. Remembered for the cockpit.
static void mw5Assign(int part, bool away) {
    static const wchar_t *lanceAlt = L"0.000 0.000 0.243 0.435 0.210 0.125"; // the list with the open orders menu, and where that shows
    int n = g_m5Looked - 1, p = part - 1;
    wchar_t note[160], k[32];
    if (away && p >= 0) {
        g_m5Glass[p][2] = 0;
        mw5GlassPut(p);
        swprintf(note, 160, L"%ls: %ls not on the glass", g_mw5Name, g_m5PartName[p]);
        noteShow(note, 4);
        logf("mw5: cockpit '%ls': %ls off the glass (saved)", g_mw5Name, g_m5PartName[p]);
        Beep(600, 40);
        return;
    }
    bool onMonitor = n >= 0 && n < MW5_SCREENS && p != 5;
    float right = 0, up = 0;
    if (!onMonitor && (p < 0 || !g_m5GlassOn || !mw5GlassAim(&right, &up))) {
        noteShow(L"Ctrl+Alt+1..6 (radar, target, damage, lance, weapons, top strip): onto the monitor or the glass you look at", 4);
        Beep(300, 150);
        return;
    }
    for (int i = 0; i < MW5_SCREENS && p >= 0 && p < 5; ++i) { // the part leaves the monitor that showed it
        const float *r = g_m5Rect[i], *want = g_m5Part[p];
        if (i == n || fabsf(r[0] - want[0]) + fabsf(r[1] - want[1]) + fabsf(r[2] - want[2]) + fabsf(r[3] - want[3]) > 0.002f) continue;
        swprintf(k, 32, L"screen_%d", i);
        mw5CockpitPutStr(k, nullptr);
        swprintf(k, 32, L"screen_%d_alt", i);
        mw5CockpitPutStr(k, nullptr);
    }
    if (onMonitor) {
        wchar_t b[96] = L"";
        if (p >= 0) swprintf(b, 96, L"%.3f %.3f %.3f %.3f", g_m5Part[p][0], g_m5Part[p][1], g_m5Part[p][2], g_m5Part[p][3]);
        swprintf(k, 32, L"screen_%d", n);
        mw5CockpitPutStr(k, p >= 0 ? b : nullptr);
        swprintf(k, 32, L"screen_%d_alt", n);
        mw5CockpitPutStr(k, p == 3 ? lanceAlt : nullptr);
        if (p >= 0) mw5CockpitPutStr(L"screens", L"1");
        mw5ReadIni();
        swprintf(note, 160, L"%ls: monitor %d shows %ls", g_mw5Name, n, p >= 0 ? g_m5PartName[p] : L"nothing");
        logf("mw5: cockpit '%ls': monitor %d shows %ls (saved)", g_mw5Name, n, p >= 0 ? g_m5PartName[p] : L"nothing");
    } else {
        float *g = g_m5Glass[p];
        g[0] = right, g[1] = up;
        if (!(g[2] > 0.5f)) g[2] = g_m5GlassDef[p][2];
        mw5GlassPut(p);
        mw5ReadIni();
        g_m5GlassLast = p;
        swprintf(note, 160, L"%ls: %ls on the glass, %+.0f right %+.0f up", g_mw5Name, g_m5PartName[p], right, up);
        logf("mw5: cockpit '%ls': %ls on the glass at %.1f right, %.1f up, %.1f wide (saved)", g_mw5Name, g_m5PartName[p], right, up, g[2]);
    }
    noteShow(note, 4);
    Beep(1200, 40);
}

// hot keys, once a frame
static void mw5OnPresent() {
    if (!g_mw5IniRead) mw5ReadIni();
    mw5ForMech();
    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0, alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    {
        static bool was;
        bool down = ctrl && alt && (GetAsyncKeyState(VK_F11) & 0x8000);
        if (down && !was) mw5Next((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
        was = down;
    }
    {
        static bool was;
        bool down = ctrl && !alt && (GetAsyncKeyState(VK_F11) & 0x8000);
        if (down && !was) {
            mw5ReadIni();
            if (!g_mw5Name[0] || g_mw5State < 0) {
                logf("mw5: no usable cockpit ([vr] mw5_cockpit)");
                Beep(300, 200);
            } else {
                g_mw5On = !g_mw5On;
                logf("mw5: cockpit '%ls' %s", g_mw5Name, g_mw5On ? "on" : "off");
                wchar_t note[160];
                swprintf(note, 160, g_mw5On ? L"Cockpit: %ls" : L"Cockpit off (%ls)", g_mw5Name);
                noteShow(note, 3);
                Beep(g_mw5On ? 1200 : 600, 60);
            }
        }
        was = down;
    }
    if (!g_mw5On) return;
    {
        static bool wasN[7];
        for (int k = 0; k < 7; ++k) {
            bool down = ctrl && alt && (GetAsyncKeyState('0' + k) & 0x8000);
            if (down && !wasN[k]) mw5Assign(k, shift);
            wasN[k] = down;
        }
    }
    static bool was[6];
    const int keys[6] = {VK_UP, VK_DOWN, VK_RIGHT, VK_LEFT, VK_PRIOR, VK_NEXT};
    for (int k = 0; k < 6; ++k) {
        bool down = ctrl && alt && (GetAsyncKeyState(keys[k]) & 0x8000);
        if (down && !was[k] && shift && k >= 4) { // with Shift: the size of the part placed on the glass last
            int p = g_m5GlassLast;
            if (p < 0 || !(g_m5Glass[p][2] > 0.5f)) {
                noteShow(L"Place a part on the glass first (Ctrl+Alt+1..6), then Ctrl+Alt+Shift+PgUp / PgDn sizes it", 4);
                Beep(300, 150);
            } else {
                g_m5Glass[p][2] = min(120.0f, max(2.0f, g_m5Glass[p][2] * (k == 4 ? 1.05f : 1 / 1.05f)));
                mw5GlassPut(p);
                wchar_t note[160];
                swprintf(note, 160, L"%ls: %ls %.1f degrees wide", g_mw5Name, g_m5PartName[p], g_m5Glass[p][2]);
                noteShow(note, 3);
                Beep(1400, 30);
            }
        } else if (down && !was[k]) {
            if (k == 0) g_mw5Eye[2] += 0.02f;
            if (k == 1) g_mw5Eye[2] -= 0.02f;
            if (k == 2) g_mw5Eye[1] += 0.02f;
            if (k == 3) g_mw5Eye[1] -= 0.02f;
            if (k == 4) g_mw5Scale *= 1.02f;
            if (k == 5) g_mw5Scale /= 1.02f;
            mw5CockpitPutF(L"eye_up_m", g_mw5Eye[1]);
            mw5CockpitPutF(L"eye_forward_m", g_mw5Eye[2]);
            mw5CockpitPutF(L"scale", g_mw5Scale);
            logf("mw5: eye %.2f m up, %.2f m forward, size %.3f (saved)", g_mw5Eye[1], g_mw5Eye[2], g_mw5Scale);
            wchar_t note[160];
            swprintf(note, 160, L"%ls: eye %+.2f m up, %+.2f m forward, size %.2f", g_mw5Name, g_mw5Eye[1], g_mw5Eye[2], g_mw5Scale);
            noteShow(note, 3);
            Beep(k < 2 ? 800 : k < 4 ? 1000 : 1400, 30);
        }
        was[k] = down;
    }
}
