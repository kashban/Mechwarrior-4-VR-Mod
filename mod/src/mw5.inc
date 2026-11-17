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
    bool paint;      // painted metal: drawn with the patches, the wear and the gloss of MW5's own
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
static const float g_m5WeaponsLeft = 0.700f; // where the weapon list's frame begins (its part for a monitor begins inside it)
static const float g_m5GlassDef[M5_PARTS][3] = {{0, -17, 11}, {-26, -14, 8}, {26, -14, 8}, {-32, 20, 9}, {32, 20, 11}, {0, 22, 15}};
static float g_m5Glass[M5_PARTS][4];     // degrees right, up, width; metres away
static bool g_m5GlassSaved[M5_PARTS];    // set by hand (in the cockpit's file); the others are placed automatically
static float g_m5GlassDistDef = 1.2f;    // [cockpit] glass_distance_m: a part's distance where nothing else says
static std::vector<float> g_m5Pos;       // the mesh's vertices (x y z each) and triangles, for finding its windows
static std::vector<uint32_t> g_m5Tri;
static void mw5GlassFit();
static bool g_m5GlassOn = true; // [vr] mw5_glass
static int g_m5GlassLast = -1;  // the part chosen (Ctrl+Alt+Shift+1..6) or placed last: what the Ctrl+Alt+Shift keys change
static ULONGLONG g_m5HiUntil;   // until then that part is framed in the HUD picture, to show which one the keys change
static void mw5HiTouch() { g_m5HiUntil = GetTickCount64() + 6000; }
#define MW5_GROUPS 40
static Mw5Group g_m5[MW5_GROUPS];
static int g_m5N;
static int g_mw5State;             // 0: not loaded yet, 1: ready, -1: failed (stays off)
static volatile bool g_mw5On;
static bool g_mw5IniRead;
static wchar_t g_mw5Name[64];
static float g_mw5Scale = 1, g_mw5Eye[3], g_mw5Ems = 1.5f, g_mw5Amb = 0.45f;
static int g_mw5LinearOut, g_mw5Reversed = 1;
// painted metal (the parts the converter marks; mw4vr_cockpits\mw4vr_paint.mwd, made by it, holds the picture for it)
static ID3D11ShaderResourceView *g_m5Detail;
static bool g_m5DetailTried;
static float g_m5Paint = 1, g_m5PaintWear = 0.5f; // [vr] mw5_paint (0: plain colours), mw5_paint_wear
static float g_mw5DepthFront = 0.92f;
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
static float g_m5Turn[MW5_SCREENS];    // degrees the monitor's picture is turned clockwise within the monitor
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
    float4 scr2;          // z: gain; w: dark red times this at most
    float4 alt;           // a second part, shown while the HUD image is not empty at the probe point (z <= x: none)
    float4 alt2;          // zw: the probe point
    float4 rot;           // the monitor's uv (about its middle) -> the part's (about its middle): xy the row for u, zw for v
    float4 rot2;          // as rot, for the second part
    float4 eye;           // xyz: the eye in the mesh; w > 0: painted metal, as strongly
    float4 pnt;           // x: how much of the paint is worn off
};
struct VSIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; float3 mp : TEXCOORD1; };
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
    o.mp = v.pos;
    return o;
}
Texture2D tc : register(t0);
Texture2D te : register(t1);
Texture2D th : register(t2);
Texture2D td : register(t3); // painted metal: rg the fine grain's slope, b the patches' lightness
SamplerState sm : register(s0);
// the picture laid flat on the mesh from its three sides (w: the normal's squares), s times a metre
float4 flatOn(float3 p, float3 w, float s) {
    return td.Sample(sm, p.zy * s) * w.x + td.Sample(sm, p.xz * s) * w.y + td.Sample(sm, p.xy * s) * w.z;
}
float4 ps(VSOut i) : SV_Target {
    if (par.w > 0) { // a monitor: its part of the HUD image, whole, on black
        float4 r = scr;
        float4 f = rot;
        if (alt.z > alt.x) {
            float4 t = th.SampleLevel(sm, alt2.zw, 0);
            if (max(max(t.r, t.g), max(t.b, t.a)) > 0.02) {
                r = alt;
                f = rot2;
            }
        }
        float2 q = float2(dot(i.uv - 0.5, f.xy), dot(i.uv - 0.5, f.zw));
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
    float3 a = tc.Sample(sm, i.uv).rgb;
    float gloss = 0;
    if (eye.w > 0) {
        // MW5's painted metal has no picture: a colour, large patches of lighter and darker over it, the paint worn
        // off to the metal in places, a fine grain, and gloss
        float3 w = n * n;
        float patch = flatOn(i.mp, w, 1.3).b;
        // (dark paint shows little of it: black parts are no painted steel)
        float worn = pnt.x * smoothstep(0.50, 0.38, flatOn(i.mp + 0.37, w, 3.7).b) * saturate(0.1 + 4 * dot(a, 0.333));
        float2 g = (flatOn(i.mp, w, 9).rg * 2 - 1) * 1.5 * eye.w;
        a *= lerp(1, saturate(0.55 + 0.6 * patch), eye.w);
        a = lerp(a, float3(0.42, 0.42, 0.44), worn * eye.w);
        n = normalize(n + float3(0, g.y, g.x) * w.x + float3(g.x, 0, g.y) * w.y + float3(g.x, g.y, 0) * w.z);
        float3 h = normalize(normalize(eye.xyz - i.mp) + lamp.xyz);
        gloss = pow(saturate(dot(n, h)), 28) * lerp(0.18, 0.50, worn) * eye.w;
    }
    float l = par.y + (1 - par.y) * saturate(dot(n, lamp.xyz) * 0.5 + 0.5);
    float3 c = a * l + gloss + te.Sample(sm, i.uv).rgb * par.x;
    if (par.z > 0) c = pow(saturate(c), 2.2);
    return float4(c, 1);
}
)HLSL";

static inline bool mw5Active() { return g_mw5On && g_mw5State == 1; }

// Without a cockpit of our own ([vr] hud_parts=1): the same parts of the HUD as panes before the pilot, in a row
// below the view ahead ([vr] hud_part_<name> = degrees right, up, width, metres away), moved with the same keys.
static bool g_m5PlainOn = true;
static const float g_m5PlainDef[M5_PARTS][4] = {{0, -24, 11, 1.2f}, {-15, -24, 8, 1.2f}, {15, -24, 8, 1.2f},
                                                {-27, -24, 9, 1.2f}, {28, -24, 11, 1.2f}, {0, -14, 15, 1.2f}};
static float g_m5Plain[M5_PARTS][4];
static inline bool mw5Plain() { return !mw5Active() && g_m5PlainOn && g_m5GlassOn; }
// a part's place in use: the cockpit's glass, or the row without a cockpit
static inline float *mw5G(int p) { return mw5Active() ? g_m5Glass[p] : g_m5Plain[p]; }
static inline float mw5DefW(int p) { return mw5Active() ? g_m5GlassDef[p][2] : g_m5PlainDef[p][2]; }
static inline const wchar_t *mw5Who() { return mw5Active() ? g_mw5Name : L"HUD"; }
// Where the row's places are kept: for each mech in its section of mw4vr_mechs.ini (true), or in [vr] of mw4vr.ini
// while no mech is known - those are also the places of a mech that has none of its own.
static bool mw5PlainWhere(std::wstring *file, wchar_t *sec) {
    if (g_mechOn && g_mechCur >= 0 && g_mechCur < g_mechN) {
        *file = mechIni();
        swprintf(sec, 32, L"mech_%d", g_mechCur + 1);
        return true;
    }
    *file = g_dir + L"mw4vr.ini";
    wcscpy_s(sec, 32, L"vr");
    return false;
}

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
    g_m5Paint = mw5GetF(L"mw5_paint", 1);
    if (!(g_m5Paint >= 0 && g_m5Paint <= 1)) g_m5Paint = 1;
    g_m5PaintWear = mw5GetF(L"mw5_paint_wear", 0.5f);
    if (!(g_m5PaintWear >= 0 && g_m5PaintWear <= 1)) g_m5PaintWear = 0.5f;
    // What of the world is nearer than the game's near plane / this share still shows in front of the mesh: at 0.5
    // the mech's own hull beside a narrow cockpit did (the Archer's launchers, within 2 m). At least 0.9.
    g_mw5DepthFront = mw5GetF(L"mw5_depth_front", 0.92f);
    if (!(g_mw5DepthFront >= 0.9f && g_mw5DepthFront <= 0.98f)) g_mw5DepthFront = 0.92f;
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
        // screen_<n>_turn = degrees: the picture turned clockwise within the monitor (a monitor that hangs askew)
        swprintf(k, 32, L"screen_%d_turn", i);
        mw5CockpitStr(k, b, 96);
        g_m5Turn[i] = mcTurnNorm((float)_wtof(b));
    }
    g_m5GlassOn = GetPrivateProfileIntW(L"vr", L"mw5_glass", 1, ini.c_str()) != 0;
    g_m5GlassDistDef = mw5CockpitF(L"glass_distance_m", 1.2f);
    if (!(g_m5GlassDistDef >= 0.3f && g_m5GlassDistDef <= 20)) g_m5GlassDistDef = 1.2f;
    for (int i = 0; i < M5_PARTS; ++i) { // glass_<part> = degrees right, up, width [metres away]; none: placed automatically
        wchar_t k[32], b[96];
        swprintf(k, 32, L"glass_%ls", g_m5PartKey[i]);
        mw5CockpitStr(k, b, 96);
        float *g = g_m5Glass[i];
        g[3] = g_m5GlassDistDef;
        g_m5GlassSaved[i] = swscanf(b, L"%f %f %f %f", g, g + 1, g + 2, g + 3) >= 3;
        if (!g_m5GlassSaved[i]) memcpy(g, g_m5GlassDef[i], sizeof g_m5GlassDef[i]), g[3] = g_m5GlassDistDef;
        if (!(g[2] <= 120)) g[2] = g_m5GlassDef[i][2];
        if (!(g[3] >= 0.3f && g[3] <= 20)) g[3] = g_m5GlassDistDef;
    }
    g_m5PlainOn = GetPrivateProfileIntW(L"vr", L"hud_parts", 1, ini.c_str()) != 0;
    for (int i = 0; i < M5_PARTS; ++i) {
        wchar_t k[32], b[96];
        swprintf(k, 32, L"hud_part_%ls", g_m5PartKey[i]);
        std::wstring file;
        wchar_t sec[32];
        b[0] = 0;
        if (mw5PlainWhere(&file, sec)) GetPrivateProfileStringW(sec, k, L"", b, 96, file.c_str()); // this mech's own place
        if (!b[0]) GetPrivateProfileStringW(L"vr", k, L"", b, 96, ini.c_str());                    // else the one for all
        float *g = g_m5Plain[i];
        memcpy(g, g_m5PlainDef[i], sizeof g_m5PlainDef[i]);
        float v[4];
        int n = swscanf(b, L"%f %f %f %f", v, v + 1, v + 2, v + 3);
        if (n >= 3) g[0] = v[0], g[1] = v[1], g[2] = v[2];
        if (n >= 4) g[3] = v[3];
        if (!(g[2] <= 120)) g[2] = g_m5PlainDef[i][2];
        if (!(g[3] >= 0.3f && g[3] <= 20)) g[3] = g_m5PlainDef[i][3];
    }
    mw5GlassFit();
    if (!g_mw5IniRead) {
        g_mw5IniRead = true;
        // wanted from the start; without a cockpit file it has no effect (the game's cockpit, the HUD's parts in a row)
        g_mw5On = GetPrivateProfileIntW(L"vr", L"mw5_on", 1, ini.c_str()) != 0;
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

// the picture painted metal is drawn with (once; without it such parts stay plainly coloured)
static void mw5LoadDetail() {
    if (g_m5DetailTried) return;
    g_m5DetailTried = true;
    std::wstring path = g_dir + L"mw4vr_cockpits\\mw4vr_paint.mwd";
    FILE *f = _wfopen(path.c_str(), L"rb");
    char magic[4];
    uint32_t size = 0;
    if (f && fread(magic, 1, 4, f) == 4 && !memcmp(magic, "MWD1", 4) && fread(&size, 4, 1, f) == 1 && size >= 4 && size <= 4096 && !(size & (size - 1)))
        g_m5Detail = mw5Texture(f, size);
    if (f) fclose(f);
    logf(g_m5Detail ? "mw5: painted metal from mw4vr_paint.mwd (%u)" : "mw5: no mw4vr_paint.mwd - painted metal stays plainly coloured", size);
}

static bool mw5LoadFile() {
    mw5LoadDetail();
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
            uint32_t base = (uint32_t)(g_m5Pos.size() / 3); // a copy of the shape for mw5GlassFit
            for (uint32_t k = 0; k < h.nv; ++k) {
                const float *vp = (const float *)(v.data() + (size_t)k * 32);
                g_m5Pos.insert(g_m5Pos.end(), vp, vp + 3);
            }
            for (uint32_t k = 0; k < h.ni; ++k) g_m5Tri.push_back(base + *(const uint32_t *)(ix.data() + (size_t)k * 4));
            for (uint32_t k = 0; k < h.nv; ++k) {
                const float *vp = (const float *)(v.data() + (size_t)k * 32);
                for (int a = 0; a < 3; ++a) g.centre[a] += vp[a] / (float)h.nv;
            }
            h.name[47] = 0;
            g.paint = !strncmp(h.name, "PAINT:", 6);
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
    D3D11_BUFFER_DESC bd = {240, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
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
    logf(g_mw5State == 1 ? "mw5: cockpit '%ls' loaded, %d parts" : "mw5: cockpit '%ls' not usable - the game's own cockpit stays", g_mw5Name, g_m5N);
}

// the monitors show the HUD (the near HUD quads are then left out, vr.inc)
static bool mw5ScreensOn() { return mw5Active() && g_mw5ScreensWanted && g_m5Screens > 0 && g_m5HudSrv; }

// a monitor shows this part of the HUD (its middle)
static bool mw5PartOnMonitor(int p) {
    const float *r = g_m5Part[p];
    for (int i = 0; i < g_m5N && mw5ScreensOn(); ++i)
        if (g_m5[i].screen && mcHolds(g_m5Rect[g_m5[i].screen - 1], 0.5f * (r[0] + r[2]), 0.5f * (r[1] + r[3]))) return true;
    return false;
}

// the monitor that shows this part (0..MW5_SCREENS-1; -1: none)
static int mw5MonitorOf(int p) {
    const float *r = g_m5Part[p];
    for (int i = 0; i < g_m5N && mw5ScreensOn(); ++i)
        if (g_m5[i].screen && mcHolds(g_m5Rect[g_m5[i].screen - 1], 0.5f * (r[0] + r[2]), 0.5f * (r[1] + r[3]))) return g_m5[i].screen - 1;
    return -1;
}

// a monitor's piece of the HUD image that reaches below the top strip (a list set longer by hand): the far HUD
// does not show that again
static void mw5MonitorLists(McHud &hd, float T) {
    for (int i = 0; i < g_m5N && mw5ScreensOn(); ++i) {
        if (!g_m5[i].screen) continue;
        const float *r = g_m5Rect[g_m5[i].screen - 1];
        if (!(r[2] > r[0] && r[1] < T && r[3] > T + 0.01f)) continue;
        if (r[0] > 0.5f && r[0] < hd.R) hd.R = r[0];
        if (r[2] < 0.5f && r[2] > hd.L) hd.L = r[2];
        hd.leftAlt = mcLeftAlt(r, hd.L, hd.leftAlt);
        hd.rightAlt = mcRightAlt(r, hd.R, hd.rightAlt);
    }
}

// The parts of the HUD shown on the glass: those no monitor shows, with a width. 0: none (no cockpit of our own).
static int mw5GlassItems(GlassItem *out) {
    if (!(mw5Active() && g_m5GlassOn) && !mw5Plain()) return 0;
    int n = 0;
    for (int p = 0; p < M5_PARTS; ++p) {
        const float *r = g_m5Part[p], *g = mw5G(p);
        if (!(g[2] > 0.5f)) continue;
        if (mw5PartOnMonitor(p)) continue;
        GlassItem &it = out[n++];
        memcpy(it.base, r, sizeof it.base), memcpy(it.rc, r, sizeof it.rc);
        if (p == 3) it.rc[3] = g_m5LanceMenu;
        if (p == 4) it.rc[0] = g_m5WeaponsLeft, it.rc[3] = g_m5LanceMenu; // the list is as long as the mech has weapons
        if (p == 5) it.base[3] = it.rc[3] = g_vr.hudTop, it.rc[2] = g_m5WeaponsLeft;
        it.right = g[0], it.up = g[1], it.width = g[2], it.dist = g[3];
    }
    return n;
}

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
    static unsigned done[2] = {~0u, ~0u}, seen[2] = {~0u, ~0u};
    static void *usual[2]; // the target the eye's cockpit was drawn into last
    static int looks[2];
    // Seen in a video of the headset: every few seconds a frame without the cockpit, though it was drawn. In such a
    // frame a CPU-path draw comes early in the pass, and what the game draws after it (it clears, or draws the
    // world over it) takes the cockpit away. So: when many of the game's draws came after ours and another
    // CPU-path draw of the pass comes by, the cockpit is drawn once more.
    static LONG at[2];
    static bool again[2];
    bool redo = false;
    if (done[e] == g_frame) {
        if (again[e] || g_workNo - at[e] <= 60) return;
        redo = true;
    }
    if (!redo && seen[e] != g_frame) {
        if (done[e] != seen[e]) usual[e] = nullptr; // not drawn at all in the frame before: any target again
        seen[e] = g_frame, looks[e] = 0;
    }
    { // a small camera (the target's view) drawn inside the main camera's pass: not there
        D3D11_VIEWPORT vp[1] = {};
        UINT nvp = 1;
        c->RSGetViewports(&nvp, vp);
        if (nvp && g_eyeTexW && vp[0].Width < 0.9f * g_eyeTexW) {
            g_m5Why[e] = 1;
            return;
        }
    }
    { // Now and then the pass's first such draw goes somewhere else than into the eye's picture (seen: the cockpit
      // missing in one eye for a frame): the cockpit waits for the target it was drawn into before, with a depth
      // buffer - for the first 200 draws of the pass, then it takes what there is
        ID3D11RenderTargetView *rt = nullptr;
        ID3D11DepthStencilView *dv = nullptr;
        c->OMGetRenderTargets(1, &rt, &dv);
        void *rtp = rt;
        bool depth = dv != nullptr;
        if (rt) rt->Release();
        if (dv) dv->Release();
        bool other = !rtp || !depth || (usual[e] && rtp != usual[e]);
        if (redo) {
            if (other) return; // not the eye's picture: a later draw may be
            again[e] = true;
            static int told;
            if (told++ < 40) logf("mw5: frame %u, eye %d: %ld of the game's draws came after the cockpit - drawn once more", g_frame, e, (long)(g_workNo - at[e]));
        } else if (other && ++looks[e] <= 200) {
            static int told;
            if (looks[e] == 1 && told < 40) {
                ++told;
                logf("mw5: frame %u, eye %d: the pass's first cockpit draw has another target (%p, depth %d; usual %p) - waiting for the usual one",
                     g_frame, e, rtp, (int)depth, usual[e]);
            }
            g_m5Why[e] = 2;
            return;
        }
        // The usual target is the one the cockpit was drawn into without waiting. One taken after the wait is the
        // usual one only when it was taken three frames running (the game draws into another from now on): a
        // single such frame must not make the next one wait for the wrong target.
        static void *cand[2];
        static int candN[2];
        if (looks[e] > 200) {
            static int told;
            if (told++ < 10) logf("mw5: frame %u, eye %d: the usual target did not come - drawn into %p (depth %d)", g_frame, e, rtp, (int)depth);
            candN[e] = rtp == cand[e] ? candN[e] + 1 : 1;
            cand[e] = rtp;
            if (candN[e] >= 3) usual[e] = rtp, candN[e] = 0;
        } else {
            usual[e] = rtp;
            candN[e] = 0;
        }
    }
    done[e] = g_frame;
    if (!redo) at[e] = g_workNo, again[e] = false;
    if (g_mw5State == 0 && g_mw5Name[0]) mw5Load();
    if (g_mw5State != 1 || !g_vrPoseOk[e] || g_P00 <= 0 || g_P11 <= 0 || g_near <= 0) {
        g_m5Why[e] = 3;
        return;
    }
    g_m5DrawnFrame[e] = g_frame;

    // what is bound now
    ID3D11InputLayout *oIL = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY oTopo;
    ID3D11Buffer *oVB = nullptr, *oIB = nullptr, *oVCB = nullptr, *oPCB = nullptr;
    UINT oStride = 0, oOff = 0, oIOff = 0, oMask = 0, oRef = 0;
    DXGI_FORMAT oIFmt = DXGI_FORMAT_UNKNOWN;
    ID3D11VertexShader *oVS = nullptr;
    ID3D11PixelShader *oPS = nullptr;
    ID3D11ShaderResourceView *oSRV[4] = {};
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
    c->PSGetShaderResources(0, 4, oSRV);
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
    float cb[60] = {};
    float o[3];
    for (int i = 0; i < 3; ++i) o[i] = -S * g_mw5Eye[i] - T[i] - raw[i] * g_cockpitDelta;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) cb[i * 4 + j] = R[j * 3 + i] * S;
        cb[i * 4 + 3] = R[0 * 3 + i] * o[0] + R[1 * 3 + i] * o[1] + R[2 * 3 + i] * o[2];
    }
    cb[15] = 1;
    mcEyeInMesh(cb, S, cb + 52);
    cb[56] = g_m5PaintWear;
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
    // the mesh has a near plane of its own, 2 cm of the cockpit: the game's (1 m of its world) cuts the ceiling above the head
    cb[16] = g_P00, cb[17] = g_P11, cb[18] = min(g_near, 0.02f * S), cb[19] = (float)rev;
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
            ID3D11ShaderResourceView *sv[4] = {g.clr, g.ems, nullptr, g_m5Detail};
            bool scr = g.screen && g_mw5ScreensWanted;
            float paint = g.paint && g_m5Detail ? g_m5Paint : 0;
            if (cb[55] != paint) {
                cb[55] = paint;
                if (!scr && !put()) continue;
            }
            if (scr) { // the part of the HUD image, whole and undistorted in the monitor
                const float *r = g_m5Rect[g.screen - 1];
                bool have = g_m5HudSrv && r[2] > r[0] && r[3] > r[1] && g_m5HudH;
                // the rectangles count in pictures (u = 1: the game's right edge); the HUD image may be wider (hud.inc)
                float pw = g_hudW ? (float)g_hudW : (float)g_m5HudW, ku = g_m5HudW ? pw / g_m5HudW : 1;
                float ar = have ? (r[2] - r[0]) * pw / ((r[3] - r[1]) * g_m5HudH) : 1;
                cb[27] = 1;
                cb[28] = r[0] * ku, cb[29] = r[1], cb[30] = (have ? r[2] : r[0]) * ku, cb[31] = r[3];
                cb[34] = g_m5ScreenGain, cb[35] = g_m5RedBoost;
                mcTurnFit(g.aspect, ar, g_m5Turn[g.screen - 1], cb + 44);
                const float *a = g_m5Alt[g.screen - 1];
                bool haveA = have && a[2] > a[0] && a[3] > a[1];
                float aa = haveA ? (a[2] - a[0]) * pw / ((a[3] - a[1]) * g_m5HudH) : 1;
                cb[36] = a[0] * ku, cb[37] = a[1], cb[38] = (haveA ? a[2] : a[0]) * ku, cb[39] = a[3];
                cb[42] = a[4] * ku, cb[43] = a[5];
                mcTurnFit(g.aspect, aa, g_m5Turn[g.screen - 1], cb + 48);
                sv[2] = have ? g_m5HudSrv : nullptr;
                if (!put()) continue;
            }
            c->PSSetShaderResources(0, 4, sv);
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
    c->PSSetShaderResources(0, 4, oSRV);
    c->PSSetSamplers(0, 1, &oSamp);
    c->OMSetBlendState(oBlend, oFactor, oMask);
    c->OMSetDepthStencilState(oDS, oRef);
    c->RSSetState(oRS);
    IUnknown *rel[] = {oIL, oVB, oIB, oVS, oPS, oVCB, oPCB, oSRV[0], oSRV[1], oSRV[2], oSRV[3], oSamp, oBlend, oDS, oRS};
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
    g_m5Pos.clear(), g_m5Tri.clear();
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
    mw5ReadIni();              // (this mech's own places of the HUD's parts)
    wchar_t name[64] = L"", sec[32];
    swprintf(sec, 32, L"mech_%d", g_mechCur + 1);
    const wchar_t *model = g_mechs[g_mechCur].model;
    const char *from = "chosen for it";
    GetPrivateProfileStringW(sec, L"mw5_cockpit", L"", name, 64, mechIni().c_str());
    if (!name[0] && model[0]) { // the list of mechs and their cockpits
        // several names, the best first (a mod's cockpit, then one of the game's): the first there is a file of
        wchar_t list[256] = L"";
        GetPrivateProfileStringW(L"mechs", model, L"", list, 256, (g_dir + L"mw4vr_cockpit_map.ini").c_str());
        from = "from mw4vr_cockpit_map.ini";
        for (wchar_t *p = list; *p && !name[0];) {
            wchar_t *e = wcschr(p, L',');
            if (e) *e = 0;
            while (*p == L' ') ++p;
            size_t n = wcslen(p);
            while (n && p[n - 1] == L' ') p[--n] = 0;
            if (n && n < 64 && GetFileAttributesW((g_dir + L"mw4vr_cockpits\\" + p + L".mwc").c_str()) != INVALID_FILE_ATTRIBUTES) wcscpy(name, p);
            if (!e) break;
            p = e + 1;
        }
        if (list[0] && !name[0]) logf("mw5: mw4vr_cockpit_map.ini has no cockpit file for the mech '%ls'", model);
    }
    if (!name[0]) {
        GetPrivateProfileStringW(L"vr", L"mw5_cockpit", L"", name, 64, (g_dir + L"mw4vr.ini").c_str());
        from = "the one for all ([vr] mw5_cockpit)";
    }
    if (!name[0]) return;
    wchar_t note[160];
    if (model[0]) swprintf(note, 160, L"Mech: %ls   Cockpit: %ls", model, name);
    else swprintf(note, 160, L"Cockpit: %ls  (mech %d)", name, g_mechCur + 1);
    noteShow(note);
    if (!_wcsicmp(name, g_mw5Name)) return;
    logf("mw5: cockpit '%ls' for mech profile %d '%ls' (%s)", name, g_mechCur + 1, model, from);
    mw5Switch(name);
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

// ---- the parts on the glass: where they go ----

// a part's height / width as shown (its base: the lance list without its menu)
static float mw5GlassAspect(int p) {
    const float *r = g_m5Part[p];
    float v1 = p == 5 ? g_vr.hudTop : r[3];
    float px = g_vr.w && g_vr.h ? (float)g_vr.h / (float)g_vr.w : 1.0f;
    return (v1 - r[1]) / (r[2] - r[0]) * px;
}

// The cockpit as seen from the seat (the head at the reference pose), in the grid of directions. A vertex v of
// the mesh lies at size (v - eye) - seat / cockpit scale: metres right, up, ahead.
static McGrid g_m5Grid;
static void mw5GlassGrid() {
    memset(&g_m5Grid, 0, sizeof g_m5Grid);
    const float cs = g_vr.cockpitScale > 0.05f ? g_vr.cockpitScale : 1.0f, S = g_mw5Scale;
    const float o[3] = {g_mw5Eye[0] * S, g_mw5Eye[1] * S + g_vr.seatUp / cs, g_mw5Eye[2] * S + g_vr.seatFwd / cs};
    const size_t nv = g_m5Pos.size() / 3;
    for (size_t t = 0; t + 2 < g_m5Tri.size(); t += 3) {
        float p[3][3];
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            size_t i = g_m5Tri[t + k];
            ok = i < nv;
            for (int a = 0; a < 3 && ok; ++a) p[k][a] = g_m5Pos[i * 3 + a] * S - o[a];
        }
        if (ok) mcGridTri(g_m5Grid, p[0], p[1], p[2]);
    }
}

// Places for the parts not set by hand: each in the open view (a window of the cockpit) nearest to where it
// usually is, clear of the cross-hair's surroundings and of each other, as far away as the window's frame.
static void mw5GlassFit() {
    if (g_mw5State != 1 || g_m5Tri.empty() || !g_m5GlassOn) return;
    mw5GlassGrid();
    float taken[M5_PARTS + 1][4] = {{-10, -8, 10, 8}};
    int nt = 1, fitted = 0, failed = 0;
    for (int p = 0; p < M5_PARTS; ++p) { // those set by hand keep their places
        const float *g = g_m5Glass[p];
        if (!g_m5GlassSaved[p] || !(g[2] > 0.5f) || mw5PartOnMonitor(p)) continue;
        float w = g[2], h = w * mw5GlassAspect(p);
        float *t = taken[nt++];
        t[0] = g[0] - 0.5f * w, t[1] = g[1] - 0.5f * h, t[2] = g[0] + 0.5f * w, t[3] = g[1] + 0.5f * h;
    }
    static const int order[M5_PARTS] = {0, 5, 1, 2, 3, 4}; // the wide ones first: radar, top strip
    for (int oi = 0; oi < M5_PARTS; ++oi) {
        int p = order[oi];
        if (g_m5GlassSaved[p] || mw5PartOnMonitor(p)) continue;
        const float *def = g_m5GlassDef[p];
        float *g = g_m5Glass[p];
        float a = mw5GlassAspect(p), r = def[0], u = def[1], w = def[2];
        bool ok = false;
        static const float shrink[4] = {1.0f, 0.85f, 0.7f, 0.55f}; // smaller if it does not fit whole
        for (int s = 0; s < 4 && !ok; ++s) {
            w = def[2] * shrink[s];
            ok = mcGridFit(g_m5Grid, w + 1, w * a + 1, def[0], def[1], 40, taken, nt, &r, &u); // (half a degree of margin; within 40 degrees)
        }
        if (!ok) r = def[0], u = def[1], w = def[2], ++failed;
        else ++fitted;
        float rim = mcGridRim(g_m5Grid, r, u, w, w * a);
        g[0] = r, g[1] = u, g[2] = w;
        g[3] = rim > 0 ? min(5.0f, max(0.4f, 0.95f * rim)) : g_m5GlassDistDef;
        float *t = taken[nt++];
        t[0] = r - 0.5f * w, t[1] = u - 0.5f * w * a, t[2] = r + 0.5f * w, t[3] = u + 0.5f * w * a;
    }
    static wchar_t told[64];
    static int toldN = -1;
    if (wcscmp(told, g_mw5Name) || toldN != fitted * 16 + failed) {
        wcscpy(told, g_mw5Name);
        toldN = fitted * 16 + failed;
        logf("mw5: cockpit '%ls': %d parts of the HUD placed in its windows, %d found no open place (left where they usually are)", g_mw5Name, fitted, failed);
        for (int p = 0; p < M5_PARTS; ++p)
            if (!mw5PartOnMonitor(p))
                logf("mw5:   %ls %s: %.0f right, %.0f up, %.1f wide, %.2f m away", g_m5PartName[p], g_m5GlassSaved[p] ? "(set by hand)" : "(automatic)",
                     g_m5Glass[p][0], g_m5Glass[p][1], g_m5Glass[p][2], g_m5Glass[p][3]);
    }
}

// where the pilot looks at the glass at the distance D (degrees right and up from the seat); false: no head pose
static bool mw5GlassAim(float D, float *right, float *up) {
    if (!(g_vr.haveRef && g_vr.nextOk[0] && g_vr.nextOk[1])) return false;
    XrQuaternionf ri = qconj(g_vr.ref.orientation);
    const XrVector3f &a = g_vr.next[0].position, &b = g_vr.next[1].position, &o = g_vr.ref.position;
    XrVector3f hp = qrot(ri, XrVector3f{0.5f * (a.x + b.x) - o.x, 0.5f * (a.y + b.y) - o.y, 0.5f * (a.z + b.z) - o.z});
    XrVector3f hf = qrot(ri, qrot(g_vr.next[0].orientation, XrVector3f{0, 0, -1}));
    const float p[3] = {hp.x, hp.y, hp.z}, f[3] = {hf.x, hf.y, hf.z};
    mcGlassAim(p, f, D, right, up);
    return true;
}
// a part's place written into the cockpit's file: it is set by hand from now on
static void mw5GlassPut(int p) {
    wchar_t k[32], b[96];
    const float *g = mw5G(p);
    swprintf(b, 96, L"%.1f %.1f %.1f %.2f", g[0], g[1], g[2], g[3]);
    if (!mw5Active()) {
        swprintf(k, 32, L"hud_part_%ls", g_m5PartKey[p]);
        std::wstring file;
        wchar_t sec[32];
        mw5PlainWhere(&file, sec);
        WritePrivateProfileStringW(sec, k, b, file.c_str());
        return;
    }
    swprintf(k, 32, L"glass_%ls", g_m5PartKey[p]);
    mw5CockpitPutStr(k, b);
    g_m5GlassSaved[p] = true;
}

// The part the keys change, for some seconds after it was chosen, placed or changed: a yellow frame around it in
// the HUD picture of this frame - seen wherever the part is shown, on a monitor, on the glass or in the row.
static void mw5HighlightDraw(ID3D11DeviceContext *c) {
    int p = g_m5GlassLast;
    if (p < 0 || p >= M5_PARTS || !g_m5HiUntil || !c) return;
    if (GetTickCount64() > g_m5HiUntil) {
        g_m5HiUntil = 0;
        return;
    }
    if (!(mw5Active() || mw5Plain()) || !g_hudRtv || !g_hudW || !g_hudH) return;
    float rc[4];
    int mon = p < 5 ? mw5MonitorOf(p) : -1;
    if (mon >= 0) {
        memcpy(rc, g_m5Rect[mon], sizeof rc);
    } else {
        if (!(mw5G(p)[2] > 0.5f)) return; // hidden
        memcpy(rc, g_m5Part[p], sizeof rc);
        if (p == 3) rc[3] = g_m5LanceMenu;
        if (p == 4) rc[0] = g_m5WeaponsLeft, rc[3] = g_m5LanceMenu;
        if (p == 5) rc[3] = g_vr.hudTop, rc[2] = g_m5WeaponsLeft;
    }
    LONG W = (LONG)g_hudTexW, H = (LONG)g_hudH, t = max(3L, H / 400);
    LONG x0 = (LONG)(rc[0] * g_hudW), y0 = (LONG)(rc[1] * g_hudH), x1 = (LONG)(rc[2] * g_hudW), y1 = (LONG)(rc[3] * g_hudH);
    x0 = max(0L, x0), y0 = max(0L, y0), x1 = min(W, x1), y1 = min(H, y1);
    if (x1 - x0 < 4 * t || y1 - y0 < 4 * t) return;
    ID3D11DeviceContext1 *c1 = nullptr;
    if (FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&c1)) || !c1) return;
    const D3D11_RECT r[4] = {{x0, y0, x1, y0 + t}, {x0, y1 - t, x1, y1}, {x0, y0, x0 + t, y1}, {x1 - t, y0, x1, y1}};
    const float yellow[4] = {1.0f, 0.8f, 0.0f, 1.0f};
    c1->ClearView(g_hudRtv, yellow, r, 4);
    c1->Release();
}

// Ctrl+Alt+1..6 (radar, target, damage, lance list, weapons, top strip): the monitor looked at shows this part of the
// HUD from now on; looking elsewhere, the part goes onto the glass there. Ctrl+Alt+0: the monitor looked at goes dark.
// Ctrl+Alt+7: all parts on the glass are placed automatically again. Ctrl+Alt+9: this cockpit's monitors off / on
// (off: all of the HUD on the glass); Ctrl+Alt+Shift+9: the chosen part leaves its monitor for the glass.
// With Shift: 1..6 chooses the part the Ctrl+Alt+Shift keys change (arrows: its place, PgUp / PgDn: its size,
// Ins / Del: farther / nearer), 0 takes that part off the glass. Remembered for the cockpit.
static void mw5Assign(int part, bool shift) {
    static const wchar_t *lanceAlt = L"0.000 0.000 0.243 0.435 0.210 0.125"; // the list with the open orders menu, and where that shows
    const bool plain = !mw5Active();
    int n = plain ? -1 : g_m5Looked - 1, p = part - 1;
    wchar_t note[200], k[32];
    if (plain && (part == 8 || part == 9)) return;
    if (part == 7) {
        if (shift) return;
        for (int i = 0; i < M5_PARTS; ++i) {
            swprintf(k, 32, plain ? L"hud_part_%ls" : L"glass_%ls", g_m5PartKey[i]);
            if (plain) { // this mech's own places go (those for all, or the row, are used again)
                std::wstring file;
                wchar_t sec[32];
                mw5PlainWhere(&file, sec);
                WritePrivateProfileStringW(sec, k, nullptr, file.c_str());
            } else {
                mw5CockpitPutStr(k, nullptr);
            }
        }
        mw5ReadIni();
        swprintf(note, 200, plain ? L"%ls: its parts back in their row" : L"%ls: the HUD's parts placed in the windows automatically", mw5Who());
        noteShow(note, 4);
        logf("mw5: cockpit '%ls': the parts on the glass are automatic again", g_mw5Name);
        Beep(1000, 40);
        return;
    }
    if (part == 9 && !shift) { // all of this cockpit's monitors off / on: off, every part is on the glass
        bool on = !g_mw5ScreensWanted;
        mw5CockpitPutStr(L"screens", on ? L"1" : L"0");
        mw5ReadIni();
        swprintf(note, 200, on ? L"%ls: the monitors show the HUD again" : L"%ls: monitors off - all of the HUD on the glass", g_mw5Name);
        noteShow(note, 4);
        logf("mw5: cockpit '%ls': monitors %s (saved)", g_mw5Name, on ? "on" : "off - the HUD on the glass");
        Beep(on ? 1200 : 700, 40);
        return;
    }
    if (part == 9) { // the chosen part leaves its monitor and goes onto the glass
        p = g_m5GlassLast;
        int mon = p >= 0 && p < 5 ? mw5MonitorOf(p) : -1;
        if (mon < 0) {
            noteShow(p < 0 ? L"Choose a part first: Ctrl+Alt+Shift+1..5 (radar, target, damage, lance, weapons)" : L"That part is not on a monitor", 4);
            Beep(300, 150);
            return;
        }
        swprintf(k, 32, L"screen_%d", mon);
        mw5CockpitPutStr(k, nullptr);
        swprintf(k, 32, L"screen_%d_alt", mon);
        mw5CockpitPutStr(k, nullptr);
        if (!(g_m5Glass[p][2] > 0.5f)) { // it was taken off the glass once: placed automatically
            swprintf(k, 32, L"glass_%ls", g_m5PartKey[p]);
            mw5CockpitPutStr(k, nullptr);
        }
        mw5ReadIni();
        swprintf(note, 200, L"%ls: %ls off monitor %d, on the glass now (Ctrl+Alt+Shift + arrows move it)", g_mw5Name, g_m5PartName[p], mon);
        noteShow(note, 5);
        logf("mw5: cockpit '%ls': %ls off monitor %d, onto the glass (saved)", g_mw5Name, g_m5PartName[p], mon);
        Beep(1000, 40);
        return;
    }
    if (part == 8) return;
    if (shift && p >= 0) {
        g_m5GlassLast = p, mw5HiTouch();
        if (p < 5 && mw5MonitorOf(p) >= 0)
            swprintf(note, 200, L"%ls (on a monitor) chosen: Ctrl+Alt+Shift + arrows move what it shows, PgUp/PgDn nearer/more, Ins/Del longer/shorter",
                     g_m5PartName[p]);
        else swprintf(note, 200, L"%ls chosen: Ctrl+Alt+Shift + arrows move it, PgUp/PgDn size, Ins/Del depth, 0 removes", g_m5PartName[p]);
        noteShow(note, 5);
        Beep(900, 40);
        return;
    }
    if (shift) {
        p = g_m5GlassLast;
        if (p < 0) {
            noteShow(L"Choose a part first: Ctrl+Alt+Shift+1..6 (radar, target, damage, lance, weapons, top strip)", 4);
            Beep(300, 150);
            return;
        }
        mw5G(p)[2] = 0;
        mw5GlassPut(p);
        swprintf(note, 200, L"%ls: %ls not shown", mw5Who(), g_m5PartName[p]);
        noteShow(note, 4);
        logf("mw5: cockpit '%ls': %ls off the glass (saved)", g_mw5Name, g_m5PartName[p]);
        Beep(600, 40);
        return;
    }
    bool onMonitor = n >= 0 && n < MW5_SCREENS && p != 5;
    float right = 0, up = 0;
    if (!onMonitor && (p < 0 || !g_m5GlassOn || !mw5GlassAim(mw5G(p)[3], &right, &up))) {
        noteShow(L"Ctrl+Alt+1..6 (radar, target, damage, lance, weapons, top strip): onto the monitor or the glass you look at", 4);
        Beep(300, 150);
        return;
    }
    for (int i = 0; i < MW5_SCREENS && p >= 0 && p < 5 && !plain; ++i) { // the part leaves the monitor that showed it
        const float *r = g_m5Rect[i], *want = g_m5Part[p];
        if (i == n || !mcHolds(r, 0.5f * (want[0] + want[2]), 0.5f * (want[1] + want[3]))) continue;
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
        swprintf(note, 200, L"%ls: monitor %d shows %ls", g_mw5Name, n, p >= 0 ? g_m5PartName[p] : L"nothing");
        logf("mw5: cockpit '%ls': monitor %d shows %ls (saved)", g_mw5Name, n, p >= 0 ? g_m5PartName[p] : L"nothing");
    } else {
        float *g = mw5G(p);
        g[0] = right, g[1] = up;
        if (!(g[2] > 0.5f)) g[2] = mw5DefW(p);
        mw5GlassPut(p);
        mw5ReadIni();
        g = mw5G(p);
        g_m5GlassLast = p, mw5HiTouch();
        swprintf(note, 200, plain ? L"%ls: %ls at %+.0f right %+.0f up" : L"%ls: %ls on the glass, %+.0f right %+.0f up", mw5Who(), g_m5PartName[p], right, up);
        logf("mw5: cockpit '%ls': %ls on the glass at %.1f right, %.1f up, %.1f wide, %.2f m away (saved)", g_mw5Name, g_m5PartName[p], right, up, g[2], g[3]);
    }
    noteShow(note, 4);
    Beep(1200, 40);
}

// Ctrl+Alt+Shift + Home / End: the picture of the chosen part's monitor is turned (step degrees, clockwise)
static void mw5MonitorTurn(float step) {
    int p = g_m5GlassLast, mon = p >= 0 && p < 5 ? mw5MonitorOf(p) : -1;
    if (mon < 0) {
        noteShow(p < 0 ? L"Choose a part first: Ctrl+Alt+Shift+1..5 (radar, target, damage, lance, weapons)" : L"That part is not on a monitor", 4);
        Beep(300, 150);
        return;
    }
    mw5HiTouch();
    g_m5Turn[mon] = mcTurnNorm(g_m5Turn[mon] + step);
    wchar_t key[32], b[32], note[200];
    swprintf(key, 32, L"screen_%d_turn", mon);
    swprintf(b, 32, L"%.0f", g_m5Turn[mon]);
    mw5CockpitPutStr(key, g_m5Turn[mon] ? b : nullptr);
    swprintf(note, 200, L"%ls on monitor %d: turned %+.0f degrees", g_m5PartName[p], mon, g_m5Turn[mon]);
    noteShow(note, 3);
    logf("mw5: cockpit '%ls': monitor %d turned %.0f degrees (saved)", g_mw5Name, mon, g_m5Turn[mon]);
    Beep(1100, 30);
}

// Ctrl+Alt+Shift + arrows, PgUp / PgDn, Ins / Del (k: 0..7): the chosen part's place, size, distance
static void mw5GlassAdjust(int k) {
    int p = g_m5GlassLast;
    if (p < 0) {
        noteShow(L"Choose a part first: Ctrl+Alt+Shift+1..6 (radar, target, damage, lance, weapons, top strip)", 4);
        Beep(300, 150);
        return;
    }
    mw5HiTouch();
    int mon = p < 5 ? mw5MonitorOf(p) : -1;
    if (mon >= 0) { // on a monitor: which piece of the HUD image it shows
        float *r = g_m5Rect[mon], was[4];
        memcpy(was, r, sizeof was);
        float w = r[2] - r[0], h = r[3] - r[1], cu = 0.5f * (r[0] + r[2]), cv = 0.5f * (r[1] + r[3]);
        if (k == 0) r[1] -= 0.01f, r[3] -= 0.01f; // the picture's window moves: up, down, right, left
        if (k == 1) r[1] += 0.01f, r[3] += 0.01f;
        if (k == 2) r[0] += 0.01f, r[2] += 0.01f;
        if (k == 3) r[0] -= 0.01f, r[2] -= 0.01f;
        if (k == 4 || k == 5) { // PgUp: nearer (less of the picture, larger), PgDn: more of it
            float f = k == 4 ? 1 / 1.05f : 1.05f;
            r[0] = cu - 0.5f * w * f, r[2] = cu + 0.5f * w * f, r[1] = cv - 0.5f * h * f, r[3] = cv + 0.5f * h * f;
        }
        if (k == 6) r[3] += 0.02f; // Ins: longer downwards (a list with more lines), Del: shorter
        if (k == 7) r[3] -= 0.02f;
        const float *d = g_m5Part[p];
        bool ok = r[0] >= -0.02f && r[1] >= -0.02f && r[2] <= 1.06f && r[3] <= 1.02f && r[2] - r[0] >= 0.03f && r[3] - r[1] >= 0.03f &&
                  mcHolds(r, 0.5f * (d[0] + d[2]), 0.5f * (d[1] + d[3]));
        wchar_t note[200], key[32], b[96];
        if (!ok) {
            memcpy(r, was, sizeof was);
            swprintf(note, 200, L"%ls on monitor %d: no further", g_m5PartName[p], mon);
            noteShow(note, 3);
            Beep(300, 100);
            return;
        }
        swprintf(key, 32, L"screen_%d", mon);
        swprintf(b, 96, L"%.3f %.3f %.3f %.3f", r[0], r[1], r[2], r[3]);
        mw5CockpitPutStr(key, b);
        swprintf(note, 200, L"%ls on monitor %d: shows %.2f..%.2f across, %.2f..%.2f down", g_m5PartName[p], mon, r[0], r[2], r[1], r[3]);
        noteShow(note, 3);
        Beep(k < 4 ? 1000 : k < 6 ? 1400 : 800, 30);
        return;
    }
    float *g = mw5G(p);
    if (!(g[2] > 0.5f)) g[2] = mw5DefW(p); // it was off the glass
    if (k == 0) g[1] += 1;
    if (k == 1) g[1] -= 1;
    if (k == 2) g[0] += 1;
    if (k == 3) g[0] -= 1;
    if (k == 4) g[2] = min(120.0f, g[2] * 1.05f);
    if (k == 5) g[2] = max(2.0f, g[2] / 1.05f);
    if (k == 6) g[3] = min(20.0f, g[3] * 1.05f);
    if (k == 7) g[3] = max(0.3f, g[3] / 1.05f);
    mw5GlassPut(p);
    mw5GlassFit(); // (the others placed automatically keep clear of it)
    wchar_t note[200];
    swprintf(note, 200, L"%ls: %+.0f right, %+.0f up, %.1f wide, %.2f m away", g_m5PartName[p], g[0], g[1], g[2], g[3]);
    noteShow(note, 3);
    Beep(k < 4 ? 1000 : k < 6 ? 1400 : 800, 30);
}

// hot keys, once a frame
static void mw5OnPresent() {
    if (!g_mw5IniRead) mw5ReadIni();
    mw5ForMech();
    // An eye's picture of this frame without the cockpit, while the one before had it: a flash. Say why.
    for (int e = 0; e < 2; ++e) {
        bool drawnEye = g_camBothFrame == g_frame || (g_camEyeFrame == g_frame && g_camEye == e);
        static int told;
        if (mw5Active() && drawnEye && !g_menuMode && g_m5DrawnFrame[e] != g_frame && g_m5DrawnFrame[e] != ~0u && g_frame - g_m5DrawnFrame[e] <= 3 &&
            told < 60) {
            ++told;
            static const char *why[4] = {"no draw of the main camera's pass came by", "only small cameras' draws came by",
                                         "the usual target did not come", "no head pose or no projection"};
            logf("mw5: frame %u, eye %d: this picture has no cockpit - %s (pose %d, P00 %.3f, near %.3f)", g_frame, e, why[g_m5Why[e] & 3],
                 (int)g_vrPoseOk[e], g_P00, g_near);
        }
        g_m5Why[e] = 0;
    }
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
    if (!g_mw5On && !mw5Plain()) return;
    if (g_mw5On) { // the seat, the eye in the mesh or the mesh itself changed: the automatic places on the glass are found anew
        static float last[8];
        float now[8] = {g_vr.seatUp, g_vr.seatFwd, g_vr.cockpitScale, g_mw5Eye[0], g_mw5Eye[1], g_mw5Eye[2], g_mw5Scale, (float)g_m5Tri.size()};
        if (g_mw5State == 1 && memcmp(last, now, sizeof now)) {
            memcpy(last, now, sizeof now);
            mw5GlassFit();
        }
    }
    {
        static bool wasN[10];
        for (int k = 0; k < 10; ++k) {
            bool down = ctrl && alt && (GetAsyncKeyState('0' + k) & 0x8000);
            if (down && !wasN[k]) mw5Assign(k, shift);
            wasN[k] = down;
        }
    }
    {
        static bool wasT[2];
        const int turn[2] = {VK_HOME, VK_END};
        for (int k = 0; k < 2; ++k) {
            bool down = ctrl && alt && shift && (GetAsyncKeyState(turn[k]) & 0x8000);
            if (down && !wasT[k]) mw5MonitorTurn(k ? 5.0f : -5.0f);
            wasT[k] = down;
        }
    }
    static bool was[8];
    const int keys[8] = {VK_UP, VK_DOWN, VK_RIGHT, VK_LEFT, VK_PRIOR, VK_NEXT, VK_INSERT, VK_DELETE};
    for (int k = 0; k < 8; ++k) {
        bool down = ctrl && alt && (GetAsyncKeyState(keys[k]) & 0x8000);
        if (down && !was[k] && shift) {
            mw5GlassAdjust(k);
        } else if (down && !was[k] && k < 6 && g_mw5On) {
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
