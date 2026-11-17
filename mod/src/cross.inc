// ---------------------------------------------------------------------------------------------
// The cross-hair's part of the HUD picture, split by colour (vr.inc, vrCrossImage). The cross-hair with its
// pitch scale and weapon groups is green, its range yellow; a target's mark is red or blue. The smaller cross-hair
// is a layer of its own - with the whole part in it a target's mark became smaller too and left its target. So:
// what is green or yellow (and the dark inside of the green boxes) goes into the cross-hair's image, everything
// else stays in the HUD's image, where it was.
// Two passes of one triangle over the part; the device context's state is put back. The calls that the mod hooks
// go to the runtime's own functions, so the mod's book-keeping of the game's state stays as it is.
// Included into main.cpp after the hooks of the context's methods.

typedef void(STDMETHODCALLTYPE *CrPFN_Viewports)(ID3D11DeviceContext *, UINT, const D3D11_VIEWPORT *);
typedef void(STDMETHODCALLTYPE *CrPFN_Draw)(ID3D11DeviceContext *, UINT, UINT);

static const char g_crHlsl[] = R"HLSL(
Texture2D t : register(t0);
struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };
V vs(uint id : SV_VertexID) {
    V o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.p = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 at(float2 uv) {
    uint w, h;
    t.GetDimensions(w, h);
    return t.Load(int3(uv * float2(w, h), 0));
}
bool ours(float4 c) {
    bool green = c.g > 1.3 * c.r && c.g > 1.3 * c.b;
    bool yellow = c.r > 0.35 && c.g > 0.35 && c.b < 0.5 * min(c.r, c.g);
    bool dark = max(c.r, max(c.g, c.b)) < 0.12 && c.a > 0.5;
    return c.a > 0 && (green || yellow || dark);
}
float4 ps_cross(V i) : SV_Target { float4 c = at(i.uv); return ours(c) ? c : float4(0, 0, 0, 0); }
float4 ps_rest(V i) : SV_Target { float4 c = at(i.uv); return ours(c) ? float4(0, 0, 0, 0) : c; }
)HLSL";

static ID3D11VertexShader *g_crVS;
static ID3D11PixelShader *g_crPS[2];
static ID3D11BlendState *g_crBlend;
static ID3D11DepthStencilState *g_crDepth;
static ID3D11RasterizerState *g_crRaster;
static int g_crState; // 0: not made yet, 1: ready, -1: failed (the whole part goes into the cross-hair's image)

static bool crCreate() {
    if (!g_compile || !g_dev) return false;
    ID3DBlob *code[3] = {}, *err[3] = {};
    // (each call written out: tests/test_shaders.py finds them and compiles the same text with fxc)
    HRESULT hr[3];
    hr[0] = g_compile(g_crHlsl, sizeof(g_crHlsl) - 1, "cross", nullptr, nullptr, "vs", "vs_5_0", 1 << 15, 0, &code[0], &err[0]);
    hr[1] = g_compile(g_crHlsl, sizeof(g_crHlsl) - 1, "cross", nullptr, nullptr, "ps_cross", "ps_5_0", 1 << 15, 0, &code[1], &err[1]);
    hr[2] = g_compile(g_crHlsl, sizeof(g_crHlsl) - 1, "cross", nullptr, nullptr, "ps_rest", "ps_5_0", 1 << 15, 0, &code[2], &err[2]);
    bool ok = true;
    for (int i = 0; i < 3; ++i) {
        if (FAILED(hr[i]) || !code[i]) {
            logf("hud: the cross-hair's shader %d failed to compile (0x%08lx): %s", i, hr[i], err[i] ? (const char *)err[i]->GetBufferPointer() : "");
            ok = false;
        }
        if (err[i]) err[i]->Release();
    }
    if (ok)
        ok = SUCCEEDED(ORIG(PFN_CreateShader, g_dev, VT_Dev_CreateVertexShader)(g_dev, code[0]->GetBufferPointer(), code[0]->GetBufferSize(), nullptr, (void **)&g_crVS)) &&
             SUCCEEDED(ORIG(PFN_CreateShader, g_dev, VT_Dev_CreatePixelShader)(g_dev, code[1]->GetBufferPointer(), code[1]->GetBufferSize(), nullptr, (void **)&g_crPS[0])) &&
             SUCCEEDED(ORIG(PFN_CreateShader, g_dev, VT_Dev_CreatePixelShader)(g_dev, code[2]->GetBufferPointer(), code[2]->GetBufferSize(), nullptr, (void **)&g_crPS[1]));
    for (ID3DBlob *b : code)
        if (b) b->Release();
    if (!ok) return false;
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; // no blending: the pixels as they are
    D3D11_DEPTH_STENCIL_DESC dd = {};
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID, rd.CullMode = D3D11_CULL_NONE, rd.DepthClipEnable = FALSE;
    return SUCCEEDED(g_dev->CreateBlendState(&bd, &g_crBlend)) && SUCCEEDED(g_dev->CreateDepthStencilState(&dd, &g_crDepth)) &&
           SUCCEEDED(g_dev->CreateRasterizerState(&rd, &g_crRaster));
}

// a view to draw into a layer's image (the runtime's textures may be typeless: the view names the format)
static ID3D11RenderTargetView *crView(ID3D11Texture2D *tex, DXGI_FORMAT fmt) {
    struct Kept {
        ID3D11Texture2D *tex;
        ID3D11RenderTargetView *view;
    };
    static Kept kept[16];
    static int n;
    for (int i = 0; i < n; ++i)
        if (kept[i].tex == tex) return kept[i].view;
    D3D11_RENDER_TARGET_VIEW_DESC d = {};
    d.Format = fmt, d.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ID3D11RenderTargetView *v = nullptr;
    if (FAILED(g_dev->CreateRenderTargetView(tex, &d, &v))) v = nullptr;
    if (n < 16) kept[n++] = {tex, v}; // (a layer has a few images, for the whole session; a failure is remembered too)
    return v;
}

// The runtime's own Draw: the mod's hook sits in its code, and looks at the game's shaders the mod remembers.
static void crDraw3(ID3D11DeviceContext *c) {
    void *fn = (*(void ***)c)[13]; // ID3D11DeviceContext::Draw
    for (int v = 0; v < HP_VARS; ++v)
        if (g_hpAddr[SL_DRAW][v] == fn && g_tr[SL_DRAW][v]) {
            ((CrPFN_Draw)g_tr[SL_DRAW][v])(c, 3, 0);
            return;
        }
    ((CrPFN_Draw)fn)(c, 3, 0); // not one the mod hooked
}

// part: the cross-hair's part of the HUD picture (w x h), readable by a shader. crossTex: the cross-hair's image
// (w x h). hudTex: the HUD layer's image; the part lies at x0, y0 in it. fmt: both images' format.
static bool crossSplit(ID3D11DeviceContext *c, ID3D11ShaderResourceView *part, ID3D11Texture2D *crossTex, ID3D11Texture2D *hudTex, UINT x0, UINT y0,
                       UINT w, UINT h, DXGI_FORMAT fmt) {
    if (g_crState == 0) {
        g_crState = crCreate() ? 1 : -1;
        logf(g_crState == 1 ? "hud: the cross-hair is told from a target's mark by its colour (green and yellow: the cross-hair)"
                            : "hud: no shaders to tell the cross-hair from a target's mark - a mark in its part becomes smaller with it");
    }
    if (g_crState != 1) return false;
    ID3D11RenderTargetView *vCross = crView(crossTex, fmt), *vHud = crView(hudTex, fmt);
    if (!vCross || !vHud) {
        static bool told;
        if (!told) logf("hud: no view to draw into the layers' images (format %d) - a mark in the cross-hair's part becomes smaller with it", (int)fmt);
        told = true;
        return false;
    }
    // what is bound now
    ID3D11RenderTargetView *oRT[8] = {};
    ID3D11DepthStencilView *oDSV = nullptr;
    ID3D11BlendState *oBlend = nullptr;
    ID3D11DepthStencilState *oDS = nullptr;
    ID3D11RasterizerState *oRS = nullptr;
    ID3D11InputLayout *oIL = nullptr;
    ID3D11VertexShader *oVS = nullptr;
    ID3D11PixelShader *oPS = nullptr;
    ID3D11GeometryShader *oGS = nullptr;
    ID3D11HullShader *oHS = nullptr;
    ID3D11DomainShader *oDom = nullptr;
    ID3D11ShaderResourceView *oSRV = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY oTopo;
    D3D11_VIEWPORT oVP[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT oVPN = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE, oMask = 0, oRef = 0;
    float oFactor[4];
    c->OMGetRenderTargets(8, oRT, &oDSV);
    c->OMGetBlendState(&oBlend, oFactor, &oMask);
    c->OMGetDepthStencilState(&oDS, &oRef);
    c->RSGetState(&oRS);
    c->RSGetViewports(&oVPN, oVP);
    c->IAGetInputLayout(&oIL);
    c->IAGetPrimitiveTopology(&oTopo);
    c->VSGetShader(&oVS, nullptr, nullptr);
    c->PSGetShader(&oPS, nullptr, nullptr);
    c->GSGetShader(&oGS, nullptr, nullptr);
    c->HSGetShader(&oHS, nullptr, nullptr);
    c->DSGetShader(&oDom, nullptr, nullptr);
    c->PSGetShaderResources(0, 1, &oSRV);

    const float one[4] = {1, 1, 1, 1};
    c->IASetInputLayout(nullptr);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ORIG(PFN_SetShader, c, VT_Ctx_VSSetShader)(c, g_crVS, nullptr, 0);
    c->GSSetShader(nullptr, nullptr, 0);
    c->HSSetShader(nullptr, nullptr, 0);
    c->DSSetShader(nullptr, nullptr, 0);
    c->OMSetBlendState(g_crBlend, one, 0xffffffff);
    c->OMSetDepthStencilState(g_crDepth, 0);
    c->RSSetState(g_crRaster);
    c->PSSetShaderResources(0, 1, &part);
    for (int pass = 0; pass < 2; ++pass) {
        ID3D11RenderTargetView *rt = pass ? vHud : vCross;
        D3D11_VIEWPORT vp = {pass ? (float)x0 : 0.0f, pass ? (float)y0 : 0.0f, (float)w, (float)h, 0, 1};
        ORIG(PFN_OMSet, c, VT_Ctx_OMSetRenderTargets)(c, 1, &rt, nullptr);
        ORIG(CrPFN_Viewports, c, VT_Ctx_RSSetViewports)(c, 1, &vp);
        ORIG(PFN_SetShader, c, VT_Ctx_PSSetShader)(c, g_crPS[pass], nullptr, 0);
        crDraw3(c);
    }

    // back
    ORIG(PFN_OMSet, c, VT_Ctx_OMSetRenderTargets)(c, 8, oRT, oDSV);
    if (oVPN) ORIG(CrPFN_Viewports, c, VT_Ctx_RSSetViewports)(c, oVPN, oVP);
    c->PSSetShaderResources(0, 1, &oSRV);
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
    for (ID3D11RenderTargetView *v : oRT)
        if (v) v->Release();
    IUnknown *rel[] = {oDSV, oBlend, oDS, oRS, oIL, oVS, oPS, oGS, oHS, oDom, oSRV};
    for (IUnknown *u : rel)
        if (u) u->Release();
    return true;
}
