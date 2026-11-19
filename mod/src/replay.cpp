// ---------------------------------------------------------------------------------------------
// The right eye's picture from the left eye's work ([vr] replay_right=1).
//
// mw4hook's processor time for a pass of the main camera limits the frame rate on heavy maps
// (docs/PERFORMANCE.md), and nearly all of it is the same for the eye 6 cm to the side: what to draw, with
// which states, in which order. So the game draws the main camera once, for the left eye, and every draw of
// that pass is made a second time at once, for the right eye:
//
//   - into twins of the pass's render targets and depth buffer (made when first needed); where the game reads
//     one of those textures, the right eye's draw reads the twin;
//   - with the constants that hold the eye's view (matrices into and out of clip space, the camera's position)
//     worked out for the right eye, from a copy of what the game wrote, in a constant buffer of our own;
//   - the instances' matrices (a structured buffer the game fills as it goes) made for the right eye in a twin
//     of that buffer.
//
// What the graphics card works out for both eyes alike stays the left eye's: culling, levels of detail, the
// light clusters. What is made once a frame anyway (the sun's shadows, the terrain's pages) is not repeated.
//
// The eyes share the projection P; the right eye's view is the left one's moved by Hv (in view space). A matrix
// M into the left eye's clip space is Kc M for the right eye, Kc = P Hv P^-1; one out of it M Kc^-1; a position
// in the world p becomes Kw^-1 p, Kw = C0 C1^-1 (the eyes' camera matrices).
//
// [vr] replay_right: 0 = off, the game draws both eyes; 1 = on; 2 = off, and once the right eye's constants
// are compared with what this unit would have made (mw4vr_replay.txt); 3 = off, and once what both passes do is
// written down (mw4vr_replay.txt). 2 and 3 are for diagnosis.

#include "mod.h"

int g_rpMode;
volatile bool g_rpLive;
volatile LONG g_rpOwn;
static bool g_rpBroken;   // something this unit cannot do was met: the game draws both eyes again
static int g_rpSkip = -1; // the label depth a part made once a frame began at; -1: not in one
static int g_rpEye = -1;  // the pass that is running

void rpReadIni() { g_rpMode = GetPrivateProfileIntW(L"vr", L"replay_right", 0, (g_dir + L"mw4vr.ini").c_str()); }

// ---------------------------------------------------------------------------------------------
// what the game has bound on the immediate context (beside what state.cpp follows)

enum { RP_SRVS = 128, RP_UAVS = 8, RP_RTS = 8 };
static ID3D11ShaderResourceView *g_rpSrv[ST_N][RP_SRVS];
static ID3D11UnorderedAccessView *g_rpCsUav[RP_UAVS];
static ID3D11RenderTargetView *g_rpRtv[RP_RTS];
static ID3D11DepthStencilView *g_rpDsv;
static UINT g_rpRtvN;
static ID3D11UnorderedAccessView *g_rpOmUav[RP_UAVS];
static UINT g_rpOmUavStart, g_rpOmUavN;

void rpSrvSet(int stage, UINT start, UINT n, ID3D11ShaderResourceView *const *v) {
    if (g_rpOwn) return;
    for (UINT i = 0; i < n && start + i < RP_SRVS; ++i) g_rpSrv[stage][start + i] = v ? v[i] : nullptr;
}
void rpOmSet(UINT n, ID3D11RenderTargetView *const *rtv, ID3D11DepthStencilView *dsv, bool uavs, UINT us, UINT un,
             ID3D11UnorderedAccessView *const *uav) {
    if (g_rpOwn) return;
    if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) {
        g_rpRtvN = n < RP_RTS ? n : RP_RTS;
        for (UINT i = 0; i < RP_RTS; ++i) g_rpRtv[i] = i < g_rpRtvN && rtv ? rtv[i] : nullptr;
        g_rpDsv = dsv;
    }
    if (!uavs) {
        g_rpOmUavN = 0;
    } else if (un != D3D11_KEEP_UNORDERED_ACCESS_VIEWS) {
        g_rpOmUavStart = us;
        g_rpOmUavN = un < RP_UAVS ? un : RP_UAVS;
        for (UINT i = 0; i < g_rpOmUavN; ++i) g_rpOmUav[i] = uav ? uav[i] : nullptr;
    }
}

// ---------------------------------------------------------------------------------------------
// this frame's matrices (core.h has the arithmetic)

static McProj g_rpP;       // the projection, from the main camera's world -> clip matrix
static McStereo g_rpK;     // the right eye's matrices from the left eye's
static double g_rpEye0[3]; // the left eye's place in the world
static float g_rpKcF[16];  // g_rpK.kc as floats, for the instances
static bool g_rpKOk;
static double g_rpErr; // how far kc W and W kw were apart in the left pass (largest element)

// view.cpp: the main camera's world -> clip matrix of this frame (as the game wrote it, for the eye of the pass)
void rpLearnProj(const float *w) {
    if (!g_rpMode) return;
    McProj p = mcProjFrom(w);
    if (!p.ok) return;
    if (g_rpKOk && g_rpEye == 0) { // the two ways to the right eye's matrix must meet
        McM4 W;
        for (int i = 0; i < 16; ++i) W.m[i] = w[i];
        g_rpErr = mcM4Apart(mcM4Mul(g_rpK.kc, W), mcM4Mul(W, g_rpK.kw));
    }
    g_rpP = p;
}

// the right eye's matrices, from the eyes' poses (head.cpp: g_vrR, g_vrT) and their camera matrices (cam.cpp)
static bool rpBuildK(const float *c0, const float *c1) {
    g_rpKOk = g_vrPoseOk[0] && g_vrPoseOk[1] &&
              mcStereo(g_rpP, mcEyeStep(g_vrR[0], g_vrT[0], g_vrR[1], g_vrT[1]), c0, c1, &g_rpK);
    for (int i = 0; i < 3; ++i) g_rpEye0[i] = c0[i * 4 + 3];
    for (int i = 0; i < 16; ++i) g_rpKcF[i] = (float)g_rpK.kc.m[i];
    return g_rpKOk;
}

// ---------------------------------------------------------------------------------------------
// the constants: which hold the eye's view (mw4hook_abi.h names them), a copy of what the game wrote, and
// the right eye's values in a buffer of our own

// kinds (HK_RP_VARS): a matrix into clip space; one out of it; a position in the world; and, in a buffer that
// holds an object's matrix into the world too (HK_RP_OBJ_VAR), the object's matrix into clip space. An object
// that sits at the eye (the sky) goes with the eye: the right eye sees it as the left one does, but for the
// turn between the eyes.
enum { RK_NONE, RK_TOCLIP, RK_FROMCLIP, RK_POS, RK_TOCLIP_OBJ };
struct RpVar {
    UINT off;
    int kind;
};
struct RpPlan {
    std::vector<RpVar> vars;
    UINT obj = ~0u; // where the object's matrix into the world lies (3x4)
};
struct RpCb {
    std::vector<uint8_t> d; // what the game wrote last
    unsigned ver = 0;       // counts its writes
    ID3D11Buffer *twin = nullptr;
    size_t twinBytes = 0;
    unsigned twinVer = ~0u, twinFrame = ~0u;
};
static std::unordered_map<void *, RpCb> g_rpCb;
static std::unordered_map<const CBInfo *, RpPlan> g_rpPlans;

static inline UINT rpBytes(int kind) { return kind == RK_POS ? 12 : 64; }
static const RpPlan &rpPlanOf(const CBInfo &cb) {
    auto it = g_rpPlans.find(&cb);
    if (it != g_rpPlans.end()) return it->second;
    static const struct {
        const char *name;
        int kind;
        UINT at; // bytes into the variable
    } table[] = {HK_RP_VARS};
    RpPlan p;
    for (const VarInfo &v : cb.vars) {
        if (v.name == HK_RP_OBJ_VAR && v.size >= 48) p.obj = v.offset;
        for (const auto &e : table)
            if (v.name == e.name && e.at + rpBytes(e.kind) <= v.size && v.offset + v.size <= cb.size)
                p.vars.push_back({v.offset + e.at, e.kind});
    }
    if (p.obj == ~0u)
        for (RpVar &v : p.vars) v.kind = v.kind == RK_TOCLIP_OBJ ? RK_TOCLIP : v.kind;
    return g_rpPlans[&cb] = p;
}
// the right eye's values of a buffer's variables: d, in place (it holds the left eye's)
static void rpApplyPlan(const RpPlan &plan, uint8_t *d) {
    bool atEye = false;
    if (plan.obj != ~0u) {
        float w[12];
        memcpy(w, d + plan.obj, sizeof w);
        atEye =
            fabs(w[3] - g_rpEye0[0]) < 0.005 && fabs(w[7] - g_rpEye0[1]) < 0.005 && fabs(w[11] - g_rpEye0[2]) < 0.005;
    }
    for (const RpVar &v : plan.vars) {
        float f[16];
        UINT n = rpBytes(v.kind);
        memcpy(f, d + v.off, n);
        if (v.kind == RK_POS)
            mcM4Point(g_rpK.kwInv, f);
        else if (v.kind == RK_FROMCLIP)
            mcM4Right(g_rpK.kcInv, f);
        else
            mcM4Left(v.kind == RK_TOCLIP_OBJ && atEye ? g_rpK.kcTurn : g_rpK.kc, f);
        memcpy(d + v.off, f, n);
    }
}

// ---------------------------------------------------------------------------------------------
// diagnosis (replay_right=3): what a pass does. (replay_right=2): the right eye's constants, the game's
// against ours

#define RP_METHODS(X)                                                                                                  \
    X(GSSetShaderResources, (UINT s, UINT n, ID3D11ShaderResourceView *const *v), (s, n, v))                           \
    X(HSSetShaderResources, (UINT s, UINT n, ID3D11ShaderResourceView *const *v), (s, n, v))                           \
    X(DSSetShaderResources, (UINT s, UINT n, ID3D11ShaderResourceView *const *v), (s, n, v))                           \
    X(VSSetSamplers, (UINT s, UINT n, ID3D11SamplerState *const *v), (s, n, v))                                        \
    X(PSSetSamplers, (UINT s, UINT n, ID3D11SamplerState *const *v), (s, n, v))                                        \
    X(GSSetSamplers, (UINT s, UINT n, ID3D11SamplerState *const *v), (s, n, v))                                        \
    X(HSSetSamplers, (UINT s, UINT n, ID3D11SamplerState *const *v), (s, n, v))                                        \
    X(DSSetSamplers, (UINT s, UINT n, ID3D11SamplerState *const *v), (s, n, v))                                        \
    X(CSSetSamplers, (UINT s, UINT n, ID3D11SamplerState *const *v), (s, n, v))                                        \
    X(GSSetConstantBuffers, (UINT s, UINT n, ID3D11Buffer *const *b), (s, n, b))                                       \
    X(HSSetConstantBuffers, (UINT s, UINT n, ID3D11Buffer *const *b), (s, n, b))                                       \
    X(DSSetConstantBuffers, (UINT s, UINT n, ID3D11Buffer *const *b), (s, n, b))                                       \
    X(GSSetShader, (ID3D11GeometryShader * sh, ID3D11ClassInstance *const *ci, UINT n), (sh, ci, n))                   \
    X(HSSetShader, (ID3D11HullShader * sh, ID3D11ClassInstance *const *ci, UINT n), (sh, ci, n))                       \
    X(DSSetShader, (ID3D11DomainShader * sh, ID3D11ClassInstance *const *ci, UINT n), (sh, ci, n))                     \
    X(IASetInputLayout, (ID3D11InputLayout * l), (l))                                                                  \
    X(IASetVertexBuffers, (UINT s, UINT n, ID3D11Buffer *const *b, const UINT *st, const UINT *of), (s, n, b, st, of)) \
    X(IASetIndexBuffer, (ID3D11Buffer * b, DXGI_FORMAT f, UINT o), (b, f, o))                                          \
    X(IASetPrimitiveTopology, (D3D11_PRIMITIVE_TOPOLOGY t), (t))                                                       \
    X(OMSetBlendState, (ID3D11BlendState * b, const FLOAT *f, UINT m), (b, f, m))                                      \
    X(OMSetDepthStencilState, (ID3D11DepthStencilState * d, UINT r), (d, r))                                           \
    X(RSSetState, (ID3D11RasterizerState * r), (r))                                                                    \
    X(RSSetScissorRects, (UINT n, const D3D11_RECT *r), (n, r))                                                        \
    X(SetResourceMinLOD, (ID3D11Resource * r, FLOAT l), (r, l))                                                        \
    X(CopySubresourceRegion1,                                                                                          \
      (ID3D11Resource * d, UINT ds, UINT x, UINT y, UINT z, ID3D11Resource * sr, UINT ss, const D3D11_BOX *b,          \
       UINT fl),                                                                                                       \
      (d, ds, x, y, z, sr, ss, b, fl))                                                                                 \
    X(CopyStructureCount, (ID3D11Buffer * b, UINT o, ID3D11UnorderedAccessView * u), (b, o, u))                        \
    X(ResolveSubresource, (ID3D11Resource * d, UINT ds, ID3D11Resource * sr, UINT ss, DXGI_FORMAT f),                  \
      (d, ds, sr, ss, f))                                                                                              \
    X(DiscardResource, (ID3D11Resource * r), (r))                                                                      \
    X(DiscardView, (ID3D11View * v), (v))                                                                              \
    X(DiscardView1, (ID3D11View * v, const D3D11_RECT *r, UINT n), (v, r, n))                                          \
    X(ClearView, (ID3D11View * v, const FLOAT *f, const D3D11_RECT *r, UINT n), (v, f, r, n))                          \
    X(Begin, (ID3D11Asynchronous * a), (a))                                                                            \
    X(End, (ID3D11Asynchronous * a), (a))                                                                              \
    X(SetPredication, (ID3D11Predicate * p, BOOL v), (p, v))                                                           \
    X(SOSetTargets, (UINT n, ID3D11Buffer *const *b, const UINT *o), (n, b, o))

#define RP_X(...) __VA_ARGS__
enum {
#define X(Name, P, A) RPI_##Name,
    RP_METHODS(X)
#undef X
        RPI_DrawAuto,
    RPI_ClearState,
    RPI_Flush,
    RPI_PSSetShaderResources,
    RPI_CSSetShaderResources,
    RPI_CSSetUnorderedAccessViews,
    RPI_N
};
static const char *const g_rpName[RPI_N] = {
#define X(Name, P, A) #Name,
    RP_METHODS(X)
#undef X
        "DrawAuto",
    "ClearState",
    "Flush",
    "PSSetShaderResources",
    "CSSetShaderResources",
    "CSSetUnorderedAccessViews"};

struct RpWrite {
    std::string what, label;
    unsigned n = 0;
    UINT lo = ~0u, hi = 0;
    uint64_t bytes = 0;
};
struct RpMap {
    ID3D11Resource *r;
    UINT sub;
    D3D11_MAP t;
    void *p;
};
static bool g_rpDump;                // this frame's passes are written down
static unsigned g_rpCount[2][RPI_N]; // calls in each eye's pass
static unsigned g_rpAll[RPI_N];      // calls since the mod was loaded, wherever
static std::unordered_map<std::string, unsigned> g_rpWork[2];
static std::vector<RpWrite> g_rpWrites[2];
static std::vector<RpMap> g_rpMaps;
static std::unordered_map<void *, std::vector<uint8_t>> g_rpShadow; // a buffer's bytes when it was last unmapped

static inline bool rpGame(void *c) { return c == (void *)g_imm; }
static void rpSeen(int i) {
    ++g_rpAll[i];
    if (g_rpEye >= 0) ++g_rpCount[g_rpEye][i];
}

static std::string rpLabelPath() {
    CtxState *s = ctxOf(g_imm);
    std::string l;
    for (size_t i = 1; i < s->labels.size() && i < 5; ++i) { // (0 is "3D")
        if (!l.empty()) l += " > ";
        l += s->labels[i].substr(0, 34);
    }
    return l;
}
static void rpNote(const std::string &what, UINT lo, UINT hi) {
    std::string label = rpLabelPath();
    auto &v = g_rpWrites[g_rpEye];
    RpWrite *w = nullptr;
    for (auto &x : v)
        if (x.what == what && x.label == label) w = &x;
    if (!w) {
        v.emplace_back();
        w = &v.back();
        w->what = what, w->label = label;
    }
    ++w->n;
    if (hi > lo) {
        w->lo = min(w->lo, lo), w->hi = max(w->hi, hi);
        w->bytes += hi - lo;
    }
}
static std::string rpDescribe(ID3D11Resource *r, UINT *bytes) {
    *bytes = 0;
    D3D11_RESOURCE_DIMENSION dim;
    r->GetType(&dim);
    char b[160];
    if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) {
        D3D11_BUFFER_DESC d;
        ((ID3D11Buffer *)r)->GetDesc(&d);
        *bytes = d.ByteWidth;
        snprintf(b, sizeof b, "buffer %p %u B usage %d bind 0x%x misc 0x%x stride %u", (void *)r, d.ByteWidth,
                 (int)d.Usage, d.BindFlags, d.MiscFlags, d.StructureByteStride);
    } else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        D3D11_TEXTURE2D_DESC d;
        ((ID3D11Texture2D *)r)->GetDesc(&d);
        snprintf(b, sizeof b, "texture %p %ux%u fmt %d usage %d", (void *)r, d.Width, d.Height, (int)d.Format,
                 (int)d.Usage);
    } else {
        snprintf(b, sizeof b, "resource %p dimension %d", (void *)r, (int)dim);
    }
    return b;
}

// the comparison (replay_right=2): the left pass's writes of each constant buffer in their order, and for
// every variable how far the right pass's values were from them, and from what this unit makes of them
struct RpCmp {
    double raw = 0, ours = 0, size = 0;
    unsigned n = 0;
    int kind = 0;
    double el[24] = {}, elOurs[24] = {}; // left - right, ours - right of the first elements
    unsigned off = 0;                    // writes in which ours was more than 0.003 off
};
static bool g_rpCheck; // this frame's passes are compared
static std::unordered_map<void *, std::vector<std::vector<uint8_t>>> g_rpSeq;
static std::unordered_map<void *, unsigned> g_rpSeqAt;
static std::unordered_map<void *, const CBInfo *> g_rpCbInfo; // the layout a buffer was last used with
static std::unordered_map<std::string, RpCmp> g_rpCmp;
static unsigned g_rpCmpMissing;

static void rpCompare(void *b, const uint8_t *right, UINT sz) {
    unsigned k = g_rpSeqAt[b]++;
    auto it = g_rpSeq.find(b);
    auto in = g_rpCbInfo.find(b);
    if (it == g_rpSeq.end() || k >= it->second.size() || it->second[k].size() != sz || in == g_rpCbInfo.end()) {
        ++g_rpCmpMissing;
        return;
    }
    const CBInfo &cb = *in->second;
    const uint8_t *left = it->second[k].data();
    const RpPlan &plan = rpPlanOf(cb);
    std::vector<uint8_t> ours(left, left + sz);
    rpApplyPlan(plan, ours.data());
    for (const VarInfo &v : cb.vars) {
        if (v.offset + v.size > sz || v.size < 4) continue;
        RpCmp &c = g_rpCmp[cb.name + "." + v.name];
        for (const RpVar &pv : plan.vars)
            if (pv.off >= v.offset && pv.off < v.offset + v.size) c.kind = pv.kind;
        ++c.n;
        double worst = 0;
        for (UINT i = 0; i + 4 <= v.size; i += 4) {
            float l, r, o;
            memcpy(&l, left + v.offset + i, 4), memcpy(&r, right + v.offset + i, 4);
            memcpy(&o, ours.data() + v.offset + i, 4);
            if (!(fabsf(l) < 1e30f) || !(fabsf(r) < 1e30f)) continue; // (integers read as floats, not numbers)
            c.raw = max(c.raw, (double)fabsf(l - r));
            if (i / 4 < 24 && fabs((double)l - r) > fabs(c.el[i / 4])) c.el[i / 4] = (double)l - r;
            if (i / 4 < 24 && fabs((double)o - r) > fabs(c.elOurs[i / 4])) c.elOurs[i / 4] = (double)o - r;
            worst = max(worst, (double)fabsf(o - r));
            c.size = max(c.size, (double)fabsf(r));
        }
        c.ours = max(c.ours, worst);
        if (worst > 0.003) ++c.off;
    }
}

// hooks.cpp: the game wrote a constant buffer anew (all of it)
void rpCbData(ID3D11Resource *r, const void *p, UINT sz) {
    if (!g_rpMode || !p || !sz || sz > 8192 || g_rpOwn) return;
    if (g_rpMode == 1) {
        RpCb &c = g_rpCb[r];
        c.d.assign((const uint8_t *)p, (const uint8_t *)p + sz);
        ++c.ver;
    }
    if (g_rpCheck && g_rpSkip < 0) {
        if (g_rpEye == 0)
            g_rpSeq[r].emplace_back((const uint8_t *)p, (const uint8_t *)p + sz);
        else if (g_rpEye == 1)
            rpCompare(r, (const uint8_t *)p, sz);
    }
}

void rpMapped(ID3D11Resource *r, UINT sub, D3D11_MAP t, const D3D11_MAPPED_SUBRESOURCE *m) {
    if (!g_rpDump || g_rpEye < 0 || g_rpOwn || !m) return;
    g_rpMaps.push_back({r, sub, t, m->pData});
}
void rpUnmapping(ID3D11Resource *r, UINT sub) {
    if (g_rpOwn) return;
    if (!g_rpDump || g_rpEye < 0) return;
    for (size_t i = 0; i < g_rpMaps.size(); ++i) {
        RpMap mp = g_rpMaps[i];
        if (mp.r != r || mp.sub != sub) continue;
        g_rpMaps.erase(g_rpMaps.begin() + (ptrdiff_t)i);
        UINT bytes;
        std::string what = rpDescribe(r, &bytes);
        static const char *const types[] = {"?", "READ", "WRITE", "READ_WRITE", "DISCARD", "NO_OVERWRITE"};
        std::string head = "Map ";
        head += types[mp.t <= 5 ? mp.t : 0];
        head += " ";
        head += what;
        what.swap(head);
        UINT lo = 0, hi = 0;
        if (bytes && mp.t != D3D11_MAP_READ && mp.p) { // which bytes are others than at the unmap before
            std::vector<uint8_t> &sh = g_rpShadow[r];
            const uint8_t *p = (const uint8_t *)mp.p;
            if (sh.size() == bytes) {
                while (lo < bytes && sh[lo] == p[lo]) ++lo;
                hi = bytes;
                while (hi > lo && sh[hi - 1] == p[hi - 1]) --hi;
            } else {
                hi = bytes;
            }
            sh.assign(p, p + bytes);
        }
        rpNote(what, lo, hi);
        return;
    }
}
void rpUpdated(ID3D11Resource *r, UINT sub, const D3D11_BOX *box, const void *src) {
    (void)sub, (void)src;
    if (!g_rpDump || g_rpEye < 0 || g_rpOwn) return;
    UINT bytes;
    std::string what = "UpdateSubresource " + rpDescribe(r, &bytes);
    rpNote(what, box ? box->left : 0, box ? box->right : bytes);
}
void rpWork(const char *kind) {
    if (g_rpCheck && g_rpEye >= 0 && !g_rpOwn) { // which layout each bound constant buffer has
        CtxState *s = ctxOf(g_imm);
        for (auto &st : s->st)
            if (st.sh)
                for (const CBInfo &cb : st.sh->cbs)
                    if (cb.slot < CB_SLOTS && st.cb[cb.slot]) g_rpCbInfo[st.cb[cb.slot]] = &cb;
    }
    if (!g_rpDump || g_rpEye < 0 || g_rpOwn) return;
    ++g_rpWork[g_rpEye][std::string(kind) + " | " + rpLabelPath()];
}

static void rpWriteDump() {
    FILE *f = fileOpen((g_dir + L"mw4vr_replay.txt").c_str(), L"wb");
    if (!f) return;
    fprintf(f, "frame %u: what each eye's pass of the main camera did on the immediate context\n", g_frame);
    for (int e = 0; e < 2; ++e) {
        fprintf(f, "\n==== %s eye's pass ====\ncalls of the methods hooked here:\n", e ? "right" : "left");
        for (int i = 0; i < RPI_N; ++i)
            if (g_rpCount[e][i]) fprintf(f, "  %6u  %s\n", g_rpCount[e][i], g_rpName[i]);
        fprintf(f, "draws and dispatches, by label:\n");
        std::vector<std::pair<std::string, unsigned>> w(g_rpWork[e].begin(), g_rpWork[e].end());
        std::sort(w.begin(), w.end());
        for (auto &x : w) fprintf(f, "  %6u  %s\n", x.second, x.first.c_str());
        fprintf(f, "writes (times; bytes that changed, their span in the buffer), by label:\n");
        for (auto &x : g_rpWrites[e])
            fprintf(f, "  %6u  %s | %s | %llu B changed, span %u..%u\n", x.n, x.what.c_str(), x.label.c_str(),
                    (unsigned long long)x.bytes, x.lo == ~0u ? 0 : x.lo, x.hi);
    }
    fprintf(f, "\ncalls since the start, in and out of the passes:\n");
    for (int i = 0; i < RPI_N; ++i) fprintf(f, "  %8u  %s\n", g_rpAll[i], g_rpName[i]);
    fclose(f);
    logf("replay: both passes written to mw4vr_replay.txt (frame %u)", g_frame);
}
static void rpWriteCheck() {
    FILE *f = fileOpen((g_dir + L"mw4vr_replay.txt").c_str(), L"wb");
    if (!f) return;
    fprintf(f,
            "frame %u: the right eye's constants as the game wrote them, against the left eye's (left - right)\n"
            "and against what replay.cpp makes of the left eye's (ours - right): the largest difference of an\n"
            "element. the two ways to the world -> clip matrix were %.3g apart; %u writes had no partner.\n"
            "the right eye's place in the world, less the left eye's: %.5f %.5f %.5f\n\n"
            "%-34s %6s %12s %12s %12s  %s\n",
            g_frame, g_rpErr, g_rpCmpMissing, g_rpK.kwInv.m[3], g_rpK.kwInv.m[7], g_rpK.kwInv.m[11], "variable",
            "writes", "largest", "left-right", "ours-right", "kind");
    std::vector<std::pair<std::string, RpCmp>> v(g_rpCmp.begin(), g_rpCmp.end());
    std::sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    static const char *const kinds[] = {"", "into clip", "out of clip", "position", "object into clip"};
    for (auto &x : v)
        if (x.second.raw > 0 || x.second.kind) {
            fprintf(f, "%-34s %6u %12.5g %12.5g %12.5g  %s\n", x.first.c_str(), x.second.n, x.second.size, x.second.raw,
                    x.second.ours, kinds[x.second.kind]);
            fprintf(f, "        left-right:"); // which of its elements
            for (int i = 0; i < 24; ++i)
                if (fabs(x.second.el[i]) > 1e-4) fprintf(f, " [%d] %.5g", i, x.second.el[i]);
            fprintf(f, "\n        ours-right, in %u writes more than 0.003 off:", x.second.off);
            for (int i = 0; i < 24; ++i)
                if (fabs(x.second.elOurs[i]) > 1e-4) fprintf(f, " [%d] %.5g", i, x.second.elOurs[i]);
            fprintf(f, "\n");
        }
    fclose(f);
    logf("replay: the comparison of the eyes' constants written to mw4vr_replay.txt (frame %u)", g_frame);
}

// ---------------------------------------------------------------------------------------------
// twins: a texture of our own for each texture the pass writes, and views of it like the game's

static std::unordered_map<ID3D11Resource *, ID3D11Texture2D *> g_rpTwin; // (a reference held on both)
static std::unordered_map<ID3D11View *, ID3D11View *> g_rpTwinView;      // (a reference held on both)
static std::unordered_map<ID3D11View *, unsigned> g_rpNoTwin; // views of textures without a twin, at g_rpTwinGen
static unsigned g_rpTwinGen;
static unsigned g_rpMiss; // this pass: something had no twin

static void rpPurge() {
    for (auto &x : g_rpTwinView) x.first->Release(), x.second->Release();
    for (auto &x : g_rpTwin) x.first->Release(), x.second->Release();
    g_rpTwinView.clear(), g_rpTwin.clear(), g_rpNoTwin.clear();
    ++g_rpTwinGen;
}
// the twin of a texture; make: it is made when there is none (null: this kind of texture has none)
static ID3D11Texture2D *rpTwinRes(ID3D11Resource *r, bool make) {
    auto it = g_rpTwin.find(r);
    if (it != g_rpTwin.end()) return it->second;
    if (!make) return nullptr;
    D3D11_RESOURCE_DIMENSION dim;
    r->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return nullptr;
    D3D11_TEXTURE2D_DESC d;
    ((ID3D11Texture2D *)r)->GetDesc(&d);
    if (d.Usage != D3D11_USAGE_DEFAULT) return nullptr;
    ID3D11Texture2D *t = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &t)) || !t) return nullptr;
    {
        RpOwn own;
        g_imm->CopyResource(t, r); // what the game's holds now: a first frame like the left eye's
    }
    r->AddRef();
    g_rpTwin[r] = t;
    ++g_rpTwinGen;
    if (g_rpTwin.size() <= 64 || g_dbg)
        logf("replay: twin %u of texture %p: %ux%u, %u levels, format %d, bind 0x%x", (unsigned)g_rpTwin.size(),
             (void *)r, d.Width, d.Height, d.MipLevels, (int)d.Format, d.BindFlags);
    return t;
}
// D3D11's view types have no common way to make one: each its own
static ID3D11View *rpMakeView(ID3D11RenderTargetView *v, ID3D11Texture2D *t) {
    D3D11_RENDER_TARGET_VIEW_DESC d;
    v->GetDesc(&d);
    ID3D11RenderTargetView *o = nullptr;
    g_dev->CreateRenderTargetView(t, &d, &o);
    return o;
}
static ID3D11View *rpMakeView(ID3D11DepthStencilView *v, ID3D11Texture2D *t) {
    D3D11_DEPTH_STENCIL_VIEW_DESC d;
    v->GetDesc(&d);
    ID3D11DepthStencilView *o = nullptr;
    g_dev->CreateDepthStencilView(t, &d, &o);
    return o;
}
static ID3D11View *rpMakeView(ID3D11UnorderedAccessView *v, ID3D11Texture2D *t) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC d;
    v->GetDesc(&d);
    ID3D11UnorderedAccessView *o = nullptr;
    g_dev->CreateUnorderedAccessView(t, &d, &o);
    return o;
}
static ID3D11View *rpMakeView(ID3D11ShaderResourceView *v, ID3D11Texture2D *t) {
    D3D11_SHADER_RESOURCE_VIEW_DESC d;
    v->GetDesc(&d);
    ID3D11ShaderResourceView *o = nullptr;
    g_dev->CreateShaderResourceView(t, &d, &o);
    return o;
}
// the view of the twin that is like v. make: the twin is made when there is none (a view the pass writes
// through). Without a twin: v itself (what both eyes share)
template <class V> static V *rpTwinView(V *v, bool make) {
    if (!v) return nullptr;
    auto it = g_rpTwinView.find(v);
    if (it != g_rpTwinView.end()) return (V *)it->second;
    if (!make) {
        auto no = g_rpNoTwin.find(v);
        if (no != g_rpNoTwin.end() && no->second == g_rpTwinGen) return v;
    }
    ID3D11Resource *r = nullptr;
    v->GetResource(&r);
    if (!r) return v;
    ID3D11Texture2D *t = rpTwinRes(r, make);
    r->Release();
    if (!t) {
        g_rpNoTwin[v] = g_rpTwinGen;
        return v;
    }
    ID3D11View *o = rpMakeView(v, t);
    if (!o) {
        ++g_rpMiss;
        return v;
    }
    v->AddRef();
    g_rpTwinView[v] = o;
    return (V *)o;
}

// hud.cpp: the right eye's picture is in the twin of the frame's texture
ID3D11Texture2D *rpTwinOf(ID3D11Texture2D *t) { return rpTwinRes(t, false); }

// ---------------------------------------------------------------------------------------------
// The instances: mw4hook's structured buffer of them (HK_RP_INST_BYTES each: a matrix into clip space first,
// last frame's last), which it fills as the pass goes and begins anew several times in a pass. A draw reads the
// records from g_mlrInstBase on, one for each instance. Those records are written, with the right eye's
// matrices, to the same places of a twin of the buffer.
// For that they must be read, and what the game writes into a mapped buffer cannot be: the memory is the
// driver's, made for writing (reading 2.7 MB of it a pass took 14 ms), and which part was written is not told.
// So from the next time the game begins the buffer anew, it writes into memory of ours instead (hooks.cpp asks
// rpRingMap), and every draw that reads the buffer has its records put into the game's buffer first - any draw,
// of any camera, as long as [vr] replay_right=1.
// (An own compute shader made the twin's records on the graphics card at first: some forty dispatches between
// the draws of a pass cost the card 3 ms.)

struct RpRing {
    ID3D11Buffer *twin = nullptr;
    ID3D11ShaderResourceView *srv = nullptr;
    UINT n = 0;
    bool anew = true, bad = false;     // the game began its buffer anew: so does the twin
    bool ours = false, mapped = false; // the game writes into `mem`; it has it mapped
    std::vector<uint8_t> mem;
};
static std::unordered_map<void *, RpRing> g_rpRing; // by the game's buffer
static std::unordered_map<const ShaderInfo *, int>
    g_rpInstBase;          // where a shader's g_mlrInstBase lies: slot * 65536 + offset; -1: nowhere
static UINT g_rpInstances; // the draw in hand: its instances (0: not known)
static unsigned g_rpStatRight, g_rpStatCb, g_rpStatRing, g_rpStatDisp; // this frame's right-eye work

typedef void(STDMETHODCALLTYPE *RPT_SetSrv)(ID3D11DeviceContext *, UINT, UINT, ID3D11ShaderResourceView *const *);
typedef void(STDMETHODCALLTYPE *RPT_SetUav)(ID3D11DeviceContext *, UINT, UINT, ID3D11UnorderedAccessView *const *,
                                            const UINT *);
typedef void(STDMETHODCALLTYPE *RPT_OmU)(ID3D11DeviceContext *, UINT, ID3D11RenderTargetView *const *,
                                         ID3D11DepthStencilView *, UINT, UINT, ID3D11UnorderedAccessView *const *,
                                         const UINT *);
static void rpSetSrv(ID3D11DeviceContext *c, int st, UINT slot, ID3D11ShaderResourceView *v) {
    int idx = st == ST_VS   ? VT_Ctx_VSSetShaderResources
              : st == ST_PS ? VT_Ctx_PSSetShaderResources
                            : VT_Ctx_CSSetShaderResources;
    ORIG(RPT_SetSrv, c, idx)(c, slot, 1, &v);
}
static void rpSetCb(ID3D11DeviceContext *c, int st, UINT slot, ID3D11Buffer *b) {
    int idx = st == ST_VS   ? VT_Ctx_VSSetConstantBuffers
              : st == ST_PS ? VT_Ctx_PSSetConstantBuffers
                            : VT_Ctx_CSSetConstantBuffers;
    ORIG(PFN_SetCB, c, idx)(c, slot, 1, &b);
}

// hooks.cpp: the game has mapped a buffer. true: its instances' buffer, and the memory it gets is ours
bool rpRingMap(ID3D11DeviceContext *c, ID3D11Resource *r, D3D11_MAP t, D3D11_MAPPED_SUBRESOURCE *m) {
    if (g_rpMode != 1 || g_rpOwn || g_rpRing.empty()) return false;
    auto it = g_rpRing.find(r);
    if (it == g_rpRing.end() || it->second.bad) return false;
    RpRing &ring = it->second;
    if (t == D3D11_MAP_WRITE_DISCARD) ring.anew = ring.ours = true; // (from a new beginning on: nothing older counts)
    if (!ring.ours || (t != D3D11_MAP_WRITE_DISCARD && t != D3D11_MAP_WRITE_NO_OVERWRITE)) return false;
    ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, r, 0); // (the game's buffer has begun anew with it, if so asked)
    m->pData = ring.mem.data();
    ring.mapped = true;
    return true;
}
// hooks.cpp: the game unmaps a buffer. true: it held our memory - nothing is mapped
bool rpRingUnmap(ID3D11Resource *r) {
    if (g_rpRing.empty()) return false;
    auto it = g_rpRing.find(r);
    if (it == g_rpRing.end() || !it->second.mapped) return false;
    it->second.mapped = false;
    return true;
}
// the first instance's record of the draw in hand, as the vertex shader is told it; false: not known
static bool rpInstBase(CtxState *s, UINT *base) {
    const ShaderInfo *sh = s->st[ST_VS].sh;
    auto it = g_rpInstBase.find(sh);
    if (it == g_rpInstBase.end()) {
        int at = -1;
        for (const CBInfo &cb : sh->cbs)
            for (const VarInfo &v : cb.vars)
                if (v.name == HK_RP_INST_BASE_VAR && cb.slot < CB_SLOTS && v.offset + 4 <= cb.size)
                    at = (int)(cb.slot * 65536 + v.offset);
        it = g_rpInstBase.emplace(sh, at).first;
    }
    if (it->second < 0) return false;
    UINT slot = (UINT)it->second >> 16, off = (UINT)it->second & 0xffff;
    auto cb = g_rpCb.find(s->st[ST_VS].cb[slot]);
    if (cb == g_rpCb.end() || cb->second.d.size() < off + 4) return false;
    memcpy(base, cb->second.d.data() + off, 4);
    return true;
}
// draws.cpp, before any draw of the game's: the records it reads of an instances' buffer the game writes into
// our memory go into the game's buffer. instances: how many the draw has (0: not known - all of the buffer)
void rpRingFeed(ID3D11DeviceContext *c, UINT instances) {
    if (g_rpRing.empty() || g_rpOwn != 1 || (void *)c != (void *)g_imm) return;
    CtxState *s = ctxOf(c);
    const ShaderInfo *sh = s->st[ST_VS].sh;
    if (!sh) return;
    for (int slot : sh->srvSlots) {
        if (slot < 0 || slot >= 64 || !s->st[ST_VS].srvBuf[slot]) continue;
        auto it = g_rpRing.find(s->st[ST_VS].srvBuf[slot]);
        if (it == g_rpRing.end() || !it->second.ours) continue;
        RpRing &ring = it->second;
        UINT base = 0, n = instances;
        if (!n || !rpInstBase(s, &base) || base >= ring.n || n > ring.n - base) {
            base = 0, n = ring.n;
            static int told;
            if (told++ < 5) logf("replay: a draw reads the instances' buffer and does not tell which - all of it");
        }
        D3D11_MAPPED_SUBRESOURCE to;
        if (FAILED(ORIG(PFN_Map, c, VT_Ctx_Map)(c, (ID3D11Buffer *)it->first, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &to)))
            continue;
        memcpy((uint8_t *)to.pData + (size_t)base * HK_RP_INST_BYTES, ring.mem.data() + (size_t)base * HK_RP_INST_BYTES,
               (size_t)n * HK_RP_INST_BYTES);
        ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, (ID3D11Buffer *)it->first, 0);
    }
}
// the twin's view for the buffer behind a vertex shader's resource slot, the draw's records made for the right
// eye in it; null: that buffer holds no instances
static ID3D11ShaderResourceView *rpRingSrv(ID3D11DeviceContext *c, CtxState *s, void *buf) {
    if (!buf) return nullptr;
    auto it = g_rpRing.find(buf);
    if (it == g_rpRing.end()) {
        RpRing ring;
        D3D11_BUFFER_DESC d;
        ((ID3D11Buffer *)buf)->GetDesc(&d);
        ring.bad = !(d.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) ||
                   d.StructureByteStride != HK_RP_INST_BYTES || d.Usage != D3D11_USAGE_DYNAMIC;
        if (!ring.bad) {
            ring.n = d.ByteWidth / d.StructureByteStride;
            ring.mem.resize(d.ByteWidth);
            D3D11_BUFFER_DESC n = {d.ByteWidth,
                                   D3D11_USAGE_DYNAMIC,
                                   D3D11_BIND_SHADER_RESOURCE,
                                   D3D11_CPU_ACCESS_WRITE,
                                   D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                                   d.StructureByteStride};
            if (FAILED(ORIG(PFN_CreateBuffer, g_dev, VT_Dev_CreateBuffer)(g_dev, &n, nullptr, &ring.twin)) ||
                FAILED(g_dev->CreateShaderResourceView(ring.twin, nullptr, &ring.srv))) {
                ring.bad = true;
                g_rpBroken = true;
                logf("replay: no twin for the instances' buffer %p (%u B)", buf, d.ByteWidth);
            } else {
                logf("replay: the instances' buffer %p (%u of %u B) has a twin", buf, ring.n, d.StructureByteStride);
            }
        }
        it = g_rpRing.emplace(buf, ring).first;
    }
    RpRing &ring = it->second;
    if (ring.bad) return nullptr;
    UINT base = 0, n = g_rpInstances;
    if (!n || !rpInstBase(s, &base) || base >= ring.n || n > ring.n - base) {
        ++g_rpMiss; // a draw whose records are not known: the right eye's would be the left eye's
        return ring.srv;
    }
    D3D11_MAPPED_SUBRESOURCE from = {}, to;
    // (until the game writes into our memory its records are read through a map for writing that keeps what is
    // there - slow, a few draws long)
    if (!ring.ours &&
        FAILED(ORIG(PFN_Map, c, VT_Ctx_Map)(c, (ID3D11Buffer *)buf, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &from))) {
        ++g_rpMiss;
        return ring.srv;
    }
    if (FAILED(ORIG(PFN_Map, c, VT_Ctx_Map)(
            c, ring.twin, 0, ring.anew ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &to))) {
        if (!ring.ours) ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, (ID3D11Buffer *)buf, 0);
        ++g_rpMiss;
        return ring.srv;
    }
    ring.anew = false;
    const uint8_t *src = (ring.ours ? ring.mem.data() : (const uint8_t *)from.pData) + (size_t)base * HK_RP_INST_BYTES;
    uint8_t *dst = (uint8_t *)to.pData + (size_t)base * HK_RP_INST_BYTES;
    // (written once and in order: the twin's memory is the driver's, made for that)
    for (UINT i = 0; i < n; ++i, src += HK_RP_INST_BYTES, dst += HK_RP_INST_BYTES) {
        const float *f = (const float *)src;
        float *o = (float *)dst;
        mcM4MulF(g_rpKcF, f, o);
        memcpy(o + 16, f + 16, HK_RP_INST_PREV - 64);
        mcM4MulF(g_rpKcF, f + HK_RP_INST_PREV / 4, o + HK_RP_INST_PREV / 4);
    }
    ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, ring.twin, 0);
    if (!ring.ours) ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, (ID3D11Buffer *)buf, 0);
    g_rpStatRing += n;
    return ring.srv;
}

// ---------------------------------------------------------------------------------------------
// the right eye's draw: between rpRightBegin and rpRightEnd the context is set for it

struct RpSaved {
    int stage;
    UINT slot;
    void *was;
};
static RpSaved g_rpSavedSrv[96], g_rpSavedCb[24];
static int g_rpSavedSrvN, g_rpSavedCbN;
static std::vector<uint8_t> g_rpScratch;
static bool g_rpTl; // the draw's vertex shader is the CPU path's: ours with the right eye's matrices

// the stage's constant buffers that hold the eye's view: twins with the right eye's values
static void rpRightCbs(ID3D11DeviceContext *c, CtxState *s, int st) {
    ShaderInfo *sh = s->st[st].sh;
    if (!sh) return;
    for (const CBInfo &cb : sh->cbs) {
        if (cb.slot >= CB_SLOTS || s->st[st].cbOff[cb.slot]) continue;
        void *b = s->st[st].cb[cb.slot];
        if (!b) continue;
        const RpPlan &plan = rpPlanOf(cb);
        if (plan.vars.empty()) continue;
        RpCb &x = g_rpCb[b];
        if (x.d.size() < cb.size) { // never seen written: nothing to work from
            ++g_rpMiss;
            continue;
        }
        if (x.twin && x.twinBytes != x.d.size()) { // (another buffer at the same address)
            x.twin->Release();
            x.twin = nullptr;
        }
        if (!x.twin) {
            D3D11_BUFFER_DESC bd = {(UINT)((x.d.size() + 15) & ~(size_t)15),
                                    D3D11_USAGE_DYNAMIC,
                                    D3D11_BIND_CONSTANT_BUFFER,
                                    D3D11_CPU_ACCESS_WRITE,
                                    0,
                                    0};
            if (FAILED(ORIG(PFN_CreateBuffer, g_dev, VT_Dev_CreateBuffer)(g_dev, &bd, nullptr, &x.twin)) || !x.twin) {
                ++g_rpMiss;
                continue;
            }
            x.twinBytes = x.d.size();
            x.twinVer = ~0u;
        }
        if (x.twinVer != x.ver || x.twinFrame != g_frame) {
            D3D11_MAPPED_SUBRESOURCE m;
            if (FAILED(ORIG(PFN_Map, c, VT_Ctx_Map)(c, x.twin, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                ++g_rpMiss;
                continue;
            }
            g_rpScratch.assign(x.d.begin(), x.d.end());
            rpApplyPlan(plan, g_rpScratch.data());
            memcpy(m.pData, g_rpScratch.data(), g_rpScratch.size());
            ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, x.twin, 0);
            x.twinVer = x.ver, x.twinFrame = g_frame;
            ++g_rpStatCb;
        }
        if (g_rpSavedCbN < (int)(sizeof g_rpSavedCb / sizeof *g_rpSavedCb)) {
            g_rpSavedCb[g_rpSavedCbN++] = {st, cb.slot, b};
            rpSetCb(c, st, cb.slot, x.twin);
        }
    }
}
// the stage's textures that have a twin, and (vertex shader) the instances' buffer
static void rpRightSrvs(ID3D11DeviceContext *c, CtxState *s, int st) {
    ShaderInfo *sh = s->st[st].sh;
    if (!sh) return;
    for (int slot : sh->srvSlots) {
        if (slot < 0 || slot >= RP_SRVS) continue;
        ID3D11ShaderResourceView *v = g_rpSrv[st][slot], *t = nullptr;
        if (!v) continue;
        if (st == ST_VS && slot < 64 && s->st[ST_VS].srvBuf[slot]) t = rpRingSrv(c, s, s->st[ST_VS].srvBuf[slot]);
        if (!t) t = rpTwinView(v, false);
        if (t == v || g_rpSavedSrvN >= (int)(sizeof g_rpSavedSrv / sizeof *g_rpSavedSrv)) continue;
        g_rpSavedSrv[g_rpSavedSrvN++] = {st, (UINT)slot, v};
        rpSetSrv(c, st, (UINT)slot, t);
    }
}
static void rpRestore(ID3D11DeviceContext *c) {
    for (int i = 0; i < g_rpSavedCbN; ++i)
        rpSetCb(c, g_rpSavedCb[i].stage, g_rpSavedCb[i].slot, (ID3D11Buffer *)g_rpSavedCb[i].was);
    for (int i = 0; i < g_rpSavedSrvN; ++i)
        rpSetSrv(c, g_rpSavedSrv[i].stage, g_rpSavedSrv[i].slot, (ID3D11ShaderResourceView *)g_rpSavedSrv[i].was);
    g_rpSavedCbN = g_rpSavedSrvN = 0;
}

bool rpStereo(void *c) { return g_rpLive && c == (void *)g_imm && g_rpSkip < 0 && !g_rpBroken; }

bool rpRightBegin(ID3D11DeviceContext *c, UINT instances) {
    g_rpInstances = instances;
    if (!g_rpRtvN && !g_rpDsv) return false; // nothing of the pass's pictures is written
    ID3D11RenderTargetView *rt[RP_RTS] = {};
    for (UINT i = 0; i < g_rpRtvN; ++i) rt[i] = rpTwinView(g_rpRtv[i], true);
    ID3D11DepthStencilView *ds = rpTwinView(g_rpDsv, true);
    // a target without a twin (a kind of texture this unit makes none of) would be drawn into twice
    bool same = ds && ds == g_rpDsv;
    for (UINT i = 0; i < g_rpRtvN; ++i) same |= rt[i] && rt[i] == g_rpRtv[i];
    if (same) {
        ++g_rpMiss;
        return false;
    }
    if (g_rpOmUavN) {
        ID3D11UnorderedAccessView *u[RP_UAVS] = {};
        UINT keep[RP_UAVS];
        for (UINT i = 0; i < g_rpOmUavN; ++i) u[i] = rpTwinView(g_rpOmUav[i], true), keep[i] = ~0u;
        ORIG(RPT_OmU, c, VT_Ctx_OMSetRenderTargetsAndUAVs)(c, g_rpRtvN, rt, ds, g_rpOmUavStart, g_rpOmUavN, u, keep);
    } else {
        ORIG(PFN_OMSet, c, VT_Ctx_OMSetRenderTargets)(c, g_rpRtvN, rt, ds);
    }
    CtxState *s = ctxOf(c);
    g_rpSavedCbN = g_rpSavedSrvN = 0;
    for (int st = ST_VS; st <= ST_PS; ++st) rpRightSrvs(c, s, st), rpRightCbs(c, s, st);
    g_rpTl = eyeTlRightPre(c);
    ++g_rpStatRight;
    return true;
}
void rpRightEnd(ID3D11DeviceContext *c) {
    if (g_rpTl) eyeTlRightPost(c);
    rpRestore(c);
    if (g_rpOmUavN) {
        UINT keep[RP_UAVS];
        for (UINT i = 0; i < g_rpOmUavN; ++i) keep[i] = ~0u;
        ORIG(RPT_OmU, c, VT_Ctx_OMSetRenderTargetsAndUAVs)(c, g_rpRtvN, g_rpRtv, g_rpDsv, g_rpOmUavStart, g_rpOmUavN,
                                                           g_rpOmUav, keep);
    } else {
        ORIG(PFN_OMSet, c, VT_Ctx_OMSetRenderTargets)(c, g_rpRtvN, g_rpRtv, g_rpDsv);
    }
}
// A dispatch: again for the right eye only when it writes a texture that is an eye's own (has a twin). What
// compute shaders write otherwise is shared: buffers (culling, levels of detail), textures made once (baked).
bool rpRightBeginCs(ID3D11DeviceContext *c) {
    ID3D11UnorderedAccessView *u[RP_UAVS];
    bool any = false;
    for (int i = 0; i < RP_UAVS; ++i) {
        u[i] = rpTwinView(g_rpCsUav[i], false);
        any |= u[i] != g_rpCsUav[i];
    }
    if (!any) return false;
    CtxState *s = ctxOf(c);
    g_rpSavedCbN = g_rpSavedSrvN = 0;
    rpRightSrvs(c, s, ST_CS), rpRightCbs(c, s, ST_CS);
    ORIG(RPT_SetUav, c, VT_Ctx_CSSetUnorderedAccessViews)(c, 0, RP_UAVS, u, nullptr);
    ++g_rpStatDisp;
    return true;
}
void rpRightEndCs(ID3D11DeviceContext *c) {
    rpRestore(c);
    ORIG(RPT_SetUav, c, VT_Ctx_CSSetUnorderedAccessViews)(c, 0, RP_UAVS, g_rpCsUav, nullptr);
}

// draws.cpp: the CPU path's shader for the right eye's draw - the matrix for what is near (the cockpit: the
// eye's own on top of the step from the left eye) and the one for the rest (that step)
void rpTlMatrices(const float *kNear, bool nearOn, float *out32) {
    McM4 k = g_rpK.kc;
    if (nearOn) {
        McM4 n;
        for (int i = 0; i < 16; ++i) n.m[i] = kNear[i];
        k = mcM4Mul(n, g_rpK.kc);
    }
    for (int i = 0; i < 16; ++i) out32[i] = (float)k.m[i], out32[16 + i] = (float)g_rpK.kc.m[i];
}

// ---------------------------------------------------------------------------------------------
// the passes (cam.cpp)

void rpLabel(const wchar_t *name, int depth, bool open) {
    if (g_rpEye < 0) return;
    if (!open) {
        if (depth <= g_rpSkip) g_rpSkip = -1;
        return;
    }
    if (g_rpSkip >= 0 || !name) return;
    static const wchar_t *const once[] = {HK_RP_ONCE_LABELS};
    for (const wchar_t *o : once)
        if (wcsncmp(name, o, wcslen(o)) == 0) g_rpSkip = depth;
}

// can this frame's right eye be made from the left eye's draws?
bool rpWanted() { return g_rpMode == 1 && !g_rpBroken && g_rpP.ok; }

// before the left eye's pass; c0, c1: the eyes' camera matrices. false: the game draws the right eye
bool rpBegin(const float *c0, const float *c1) {
    if (!rpBuildK(c0, c1)) return false;
    g_rpMiss = 0;
    g_rpStatRight = g_rpStatCb = g_rpStatRing = g_rpStatDisp = 0;
    g_rpLive = true;
    return true;
}
// after it. false: something was missing - the right eye's picture is not to be trusted
bool rpEnd() {
    g_rpLive = false;
    static unsigned bad, told;
    if (g_rpErr > 0.02 || g_rpMiss) {
        if (told++ < 10)
            logf("replay: frame %u: %u things missing, the ways to the right eye's matrix %.3g apart", g_frame,
                 g_rpMiss, g_rpErr);
        if (++bad > 300) {
            g_rpBroken = true;
            logf("replay: given up - the game draws both eyes again");
        }
        return false;
    }
    static unsigned frames;
    if (frames++ % 1800 == 0)
        logf("replay: frame %u: the right eye made from the left eye's draws - %u draws, %u dispatches, %u constant "
             "buffers, %u instances; %u twins of textures, matrices %.2g apart",
             g_frame, g_rpStatRight, g_rpStatDisp, g_rpStatCb, g_rpStatRing, (unsigned)g_rpTwin.size(), g_rpErr);
    return true;
}
// a battle is over (a menu is up): the twins go, the next battle's targets and buffers are others
void rpIdle() {
    if (g_rpTwin.empty() && g_rpCb.empty() && g_rpRing.empty()) return;
    if (!g_rpTwin.empty()) logf("replay: %u twins of textures released", (unsigned)g_rpTwin.size());
    rpPurge();
    for (auto &x : g_rpCb)
        if (x.second.twin) x.second.twin->Release();
    g_rpCb.clear();
    for (auto &x : g_rpRing) {
        if (x.second.srv) x.second.srv->Release();
        if (x.second.twin) x.second.twin->Release();
    }
    g_rpRing.clear();
}

void rpPassBegin(int eye) {
    g_rpSkip = -1;
    if (g_rpMode >= 2 && eye == 0) {
        static unsigned first = ~0u;
        if (first == ~0u && g_camBothFrame != ~0u) first = g_frame;
        // (the dump: the frame before the one written is followed too - what a map changes is told against it)
        bool on = first != ~0u && g_frame - first >= 450;
        if (g_rpMode == 3) {
            if (on && g_rpDump && g_frame - first >= 451) {
                for (auto &m : g_rpCount) memset(m, 0, sizeof m);
                for (int e = 0; e < 2; ++e) g_rpWork[e].clear(), g_rpWrites[e].clear();
            }
            g_rpDump = on;
        } else {
            g_rpCheck = on;
            g_rpSeq.clear(), g_rpSeqAt.clear();
        }
    }
    g_rpEye = eye;
}
void rpPassEnd(int eye) {
    g_rpEye = -1;
    g_rpSkip = -1;
    g_rpMaps.clear();
    static int frames;
    if (g_rpDump && eye == 1 && ++frames == 2) {
        rpWriteDump();
        g_rpDump = false;
        g_rpMode = 0;
        g_rpShadow.clear();
    }
    if (g_rpCheck && eye == 1 && ++frames == 30) { // (some frames: every layout has been met, the largest kept)
        rpWriteCheck();
        g_rpCheck = false;
        g_rpMode = 0;
    }
}
// cam.cpp, replay_right=2: the matrices for the comparison, as rpBegin makes them
void rpCheckK(const float *c0, const float *c1) { rpBuildK(c0, c1); }

// ---------------------------------------------------------------------------------------------
// the hooks

#define X(Name, P, A)                                                                                                  \
    typedef void(STDMETHODCALLTYPE * RPT_##Name)(ID3D11DeviceContext1 *, RP_X P);                                      \
    static void STDMETHODCALLTYPE R_##Name(ID3D11DeviceContext1 *c, RP_X P) {                                          \
        ORIG(RPT_##Name, c, VT_Ctx_##Name)(c, RP_X A);                                                                 \
        if (rpGame(c)) rpSeen(RPI_##Name);                                                                             \
    }
RP_METHODS(X)
#undef X
typedef void(STDMETHODCALLTYPE *RPT_Void)(ID3D11DeviceContext1 *);
static void STDMETHODCALLTYPE R_DrawAuto(ID3D11DeviceContext1 *c) {
    ORIG(RPT_Void, c, VT_Ctx_DrawAuto)(c);
    if (rpGame(c)) rpSeen(RPI_DrawAuto);
}
static void STDMETHODCALLTYPE R_ClearState(ID3D11DeviceContext1 *c) {
    ORIG(RPT_Void, c, VT_Ctx_ClearState)(c);
    if (rpGame(c)) rpSeen(RPI_ClearState);
}
static void STDMETHODCALLTYPE R_Flush(ID3D11DeviceContext1 *c) {
    ORIG(RPT_Void, c, VT_Ctx_Flush)(c);
    if (rpGame(c)) rpSeen(RPI_Flush);
}

static void STDMETHODCALLTYPE R_PSSetShaderResources(ID3D11DeviceContext *c, UINT s, UINT n,
                                                     ID3D11ShaderResourceView *const *v) {
    ORIG(RPT_SetSrv, c, VT_Ctx_PSSetShaderResources)(c, s, n, v);
    if (rpGame(c)) rpSeen(RPI_PSSetShaderResources), rpSrvSet(ST_PS, s, n, v);
}
static void STDMETHODCALLTYPE R_CSSetShaderResources(ID3D11DeviceContext *c, UINT s, UINT n,
                                                     ID3D11ShaderResourceView *const *v) {
    ORIG(RPT_SetSrv, c, VT_Ctx_CSSetShaderResources)(c, s, n, v);
    if (rpGame(c)) rpSeen(RPI_CSSetShaderResources), rpSrvSet(ST_CS, s, n, v);
}
static void STDMETHODCALLTYPE R_CSSetUnorderedAccessViews(ID3D11DeviceContext *c, UINT s, UINT n,
                                                          ID3D11UnorderedAccessView *const *u, const UINT *ic) {
    ORIG(RPT_SetUav, c, VT_Ctx_CSSetUnorderedAccessViews)(c, s, n, u, ic);
    if (!rpGame(c) || g_rpOwn) return;
    rpSeen(RPI_CSSetUnorderedAccessViews);
    for (UINT i = 0; i < n && s + i < RP_UAVS; ++i) g_rpCsUav[s + i] = u ? u[i] : nullptr;
}
// What the pass clears and copies, it clears and copies for the right eye too. (The runtime writes these
// methods' vtable slots anew in every frame, as it does the draws': draws.cpp hooks them in Direct3D's code and
// asks here what the right eye's call is.)
static bool rpGameCall(void *c) { return rpStereo(c) && g_rpOwn == 0; }
ID3D11RenderTargetView *rpClearTwin(void *c, ID3D11RenderTargetView *v) {
    if (!rpGameCall(c)) return nullptr;
    ID3D11RenderTargetView *t = rpTwinView(v, true);
    return t != v ? t : nullptr;
}
ID3D11DepthStencilView *rpClearTwin(void *c, ID3D11DepthStencilView *v) {
    if (!rpGameCall(c)) return nullptr;
    ID3D11DepthStencilView *t = rpTwinView(v, true);
    return t != v ? t : nullptr;
}
ID3D11UnorderedAccessView *rpClearTwin(void *c, ID3D11UnorderedAccessView *v) {
    if (!rpGameCall(c)) return nullptr;
    ID3D11UnorderedAccessView *t = rpTwinView(v, false);
    return t != v ? t : nullptr;
}
ID3D11ShaderResourceView *rpMipsTwin(void *c, ID3D11ShaderResourceView *v) {
    if (!rpGameCall(c)) return nullptr;
    ID3D11ShaderResourceView *t = rpTwinView(v, false);
    return t != v ? t : nullptr;
}
// The right eye's copy, when one of the two textures is an eye's own (has a twin): into the twin of the texture
// written (made now), from the twin of the one read. A copy between textures both eyes share (a texture being
// loaded) is made once; so are copies into buffers and into textures the processor reads.
bool rpCopyTwins(void *c, ID3D11Resource *d, ID3D11Resource *sr, ID3D11Resource **td, ID3D11Resource **ts) {
    if (!rpGameCall(c) || !d || !sr) return false;
    ID3D11Resource *s2 = rpTwinRes(sr, false);
    *td = rpTwinRes(d, s2 != nullptr);
    if (!*td) return false;
    *ts = s2 ? s2 : sr;
    return true;
}

int rpHookContext(ID3D11DeviceContext *ctx) {
    if (!g_rpMode) return 0;
    int n = 0;
    static int told;
#define RP_PATCH(Name)                                                                                                 \
    if (patch(ctx, VT_Ctx_##Name, (void *)R_##Name)) {                                                                 \
        ++n;                                                                                                           \
        if (g_frame > 2 && told < 12) ++told, logf("replay: slot " #Name " had been overwritten (frame %u)", g_frame); \
    }
    RP_PATCH(PSSetShaderResources)
    RP_PATCH(CSSetShaderResources)
    RP_PATCH(CSSetUnorderedAccessViews)
    if (g_rpMode == 3) { // every other method too: which does the pass call?
#define X(Name, P, A) RP_PATCH(Name)
        RP_METHODS(X)
#undef X
        RP_PATCH(DrawAuto)
        RP_PATCH(ClearState)
        RP_PATCH(Flush)
    }
#undef RP_PATCH
    return n;
}
