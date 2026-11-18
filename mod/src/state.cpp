// ---------------------------------------------------------------------------------------------
// What the game has done so far, as the hooks have seen it: the shaders it made and what is known of them, and for
// every device context what is bound (shaders, constant buffers, render targets) and which of mw4hook's labels it
// is inside. The frame's capture for diagnosis writes from here too.

#include "mod.h"

ID3D11VertexShader *g_eyeVS; // replacement for the CPU path's screen-space vertex shader
static PFN_D3DDisassemble g_disasm;

static uint64_t fnv(const void *p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ ((const uint8_t *)p)[i]) * 1099511628211ull;
    return h;
}

static bool looksCamera(const std::string &n) {
    std::string l = n;
    for (auto &c : l) c = (char)tolower((unsigned char)c);
    static const char *keys[] = {"view", "proj", "clip", "cam", "eye", "jitter", "worldto", "toworld", "planes"};
    for (auto k : keys)
        if (l.find(k) != std::string::npos) return true;
    return false;
}

void onShader(char stage, const void *code, SIZE_T len, void *obj) {
    if (!g_reflect) {
        wchar_t p[MAX_PATH];
        GetSystemDirectoryW(p, MAX_PATH);
        wcscat_s(p, L"\\d3dcompiler_47.dll");
        HMODULE m = LoadLibraryW(p);
        if (m) g_reflect = (PFN_D3DReflect)GetProcAddress(m, "D3DReflect");
        if (m) g_disasm = (PFN_D3DDisassemble)GetProcAddress(m, "D3DDisassemble");
        if (m) g_compile = (PFN_D3DCompile)GetProcAddress(m, "D3DCompile");
        if (g_dbg) CreateDirectoryW((g_dir + L"mw4vr_shaders").c_str(), nullptr);
        if (!g_reflect) logf("reflection unavailable (d3dcompiler_47.dll / D3DReflect missing)");
    }
    auto *si = new ShaderInfo();
    si->stage = stage;
    si->hash = fnv(code, len);
    if (g_dbg) { // keep every shader: bytecode and disassembly, once per hash
        wchar_t fn[MAX_PATH];
        swprintf_s(fn, L"%smw4vr_shaders\\%cS_%016llx.dxbc", g_dir.c_str(), stage, (unsigned long long)si->hash);
        if (GetFileAttributesW(fn) == INVALID_FILE_ATTRIBUTES) {
            FILE *f = _wfsopen(fn, L"wb", _SH_DENYNO);
            if (f) {
                fwrite(code, 1, len, f);
                fclose(f);
            }
            ID3DBlob *b = nullptr;
            if (g_disasm && SUCCEEDED(g_disasm(code, len, 0, nullptr, &b)) && b) {
                swprintf_s(fn, L"%smw4vr_shaders\\%cS_%016llx.asm", g_dir.c_str(), stage, (unsigned long long)si->hash);
                f = _wfsopen(fn, L"wb", _SH_DENYNO);
                if (f) {
                    fwrite(b->GetBufferPointer(), 1, b->GetBufferSize() ? b->GetBufferSize() - 1 : 0, f);
                    fclose(f);
                }
                b->Release();
            }
        }
    }
    std::string res;
    bool reflected = false;
    ID3D11ShaderReflection *r = nullptr;
    if (g_reflect && SUCCEEDED(g_reflect(code, len, __uuidof(ID3D11ShaderReflection), (void **)&r))) {
        reflected = true;
        D3D11_SHADER_DESC sd;
        r->GetDesc(&sd);
        for (UINT i = 0; i < sd.BoundResources; ++i) {
            D3D11_SHADER_INPUT_BIND_DESC bd;
            if (FAILED(r->GetResourceBindingDesc(i, &bd))) continue;
            if (bd.Type == D3D_SIT_CBUFFER) {
                CBInfo cb;
                cb.name = bd.Name;
                cb.slot = bd.BindPoint;
                cb.size = 0;
                ID3D11ShaderReflectionConstantBuffer *c = r->GetConstantBufferByName(bd.Name);
                D3D11_SHADER_BUFFER_DESC bdesc;
                if (c && SUCCEEDED(c->GetDesc(&bdesc))) {
                    cb.size = bdesc.Size;
                    for (UINT v = 0; v < bdesc.Variables; ++v) {
                        ID3D11ShaderReflectionVariable *var = c->GetVariableByIndex(v);
                        D3D11_SHADER_VARIABLE_DESC vd;
                        if (FAILED(var->GetDesc(&vd))) continue;
                        D3D11_SHADER_TYPE_DESC td = {};
                        if (auto *t = var->GetType()) t->GetDesc(&td);
                        cb.vars.push_back({vd.Name, vd.StartOffset, vd.Size, td.Rows, td.Columns, td.Elements});
                        if (looksCamera(vd.Name)) cb.camVars.push_back((int)cb.vars.size() - 1);
                        if (isCamPos(vd.Name) && td.Rows == 1 && td.Columns >= 3 && !td.Elements) {
                            si->camPos.push_back({bd.BindPoint, vd.StartOffset});
                            si->camPosNames.push_back(vd.Name);
                        }
                    }
                }
                if (!cb.camVars.empty()) si->camera = true;
                si->cbs.push_back(std::move(cb));
            } else {
                char b[160];
                bool srv = bd.Type == D3D_SIT_TBUFFER || bd.Type == D3D_SIT_TEXTURE || bd.Type == D3D_SIT_STRUCTURED ||
                           bd.Type == D3D_SIT_BYTEADDRESS;
                if (srv) si->srvSlots.push_back((int)bd.BindPoint);
                const char *kind = bd.Type == D3D_SIT_SAMPLER ? "s" : srv ? "t" : "u";
                snprintf(b, sizeof b, " %s%u:%s", kind, bd.BindPoint, bd.Name);
                res += b;
            }
        }
        r->Release();
    }
    si->res = res;
    {
        std::lock_guard<std::mutex> lk(g_shMx);
        g_shaders[obj] = si;
        ++g_shaderCount;
    }
    if (stage == 'V' && si->hash == HK_VS_TL && !g_eyeVS) createEyeVS();
    std::lock_guard<std::mutex> lk(g_logMx);
    if (!g_shlog) return;
    fprintf(g_shlog, "%cS %016llx  %u bytes%s%s\n", stage, (unsigned long long)si->hash, (unsigned)len,
            si->camera ? "  [CAMERA]" : "", reflected ? "" : "  (no reflection)");
    if (!res.empty()) fprintf(g_shlog, "   res:%s\n", res.c_str());
    for (auto &cb : si->cbs) {
        fprintf(g_shlog, "   cb b%u '%s' %u B:", cb.slot, cb.name.c_str(), cb.size);
        for (auto &v : cb.vars) {
            if (v.elems)
                fprintf(g_shlog, " %s@%u(%ux%u[%u])", v.name.c_str(), v.offset, v.rows, v.cols, v.elems);
            else
                fprintf(g_shlog, " %s@%u(%ux%u)", v.name.c_str(), v.offset, v.rows, v.cols);
        }
        fputc('\n', g_shlog);
    }
    fflush(g_shlog);
}

ShaderInfo *lookupShader(void *s) {
    if (!s) return nullptr;
    std::lock_guard<std::mutex> lk(g_shMx);
    auto it = g_shaders.find(s);
    return it == g_shaders.end() ? nullptr : it->second;
}

UINT cbSize(void *b) {
    std::lock_guard<std::mutex> lk(g_shMx);
    auto it = g_cbSize.find(b);
    return it == g_cbSize.end() ? 0 : it->second;
}

// ---------------------------------------------------------------------------------------------
// per-context state

std::mutex g_ctxMx;
std::unordered_map<void *, CtxState *> g_ctx; // context -> state
std::unordered_map<void *, void *> g_annCtx;  // annotation interface -> its context

CtxState *ctxOf(void *c) {
    thread_local void *lastKey;
    thread_local CtxState *lastVal;
    if (c == lastKey && lastVal) return lastVal;
    std::lock_guard<std::mutex> lk(g_ctxMx);
    CtxState *&s = g_ctx[c];
    if (!s) {
        s = new CtxState();
        s->id = (int)g_ctx.size();
        s->self = c;
    }
    lastKey = c;
    lastVal = s;
    return s;
}
CtxState *ctxOfAnn(void *a) {
    void *c;
    {
        std::lock_guard<std::mutex> lk(g_ctxMx);
        auto it = g_annCtx.find(a);
        c = it == g_annCtx.end() ? a : it->second;
    }
    return ctxOf(c);
}

// statistics (all contexts)
volatile LONG g_statDrawsImm, g_statDrawsDef, g_statDisp, g_statMaps, g_statLabels, g_statExec, g_statFinish;
unsigned g_statFrames, g_frame;
DWORD g_statT0;
std::string g_frameLabels, g_lastFrameLabels;
std::mutex g_labelMx;
bool g_labelsSeen, g_firstPresentLogged;
ID3D11DeviceContext *g_imm;
void **g_immVt;

// capture
volatile bool g_cap;
FILE *g_capf;
std::mutex g_capMx;
unsigned g_capIndex;
std::mutex g_snapMx;
std::unordered_map<void *, std::vector<uint8_t>> g_snap;
std::unordered_map<void *, unsigned> g_ver;
std::unordered_set<std::string> g_dumped;

void capLine(CtxState *s, const char *fmt, ...) {
    std::lock_guard<std::mutex> lk(g_capMx);
    if (!g_capf) return;
    fprintf(g_capf, "[c%d%s] ", s->id, s->deferred ? "d" : "");
    for (int i = 0; i < s->depth; ++i) fputs("  ", g_capf);
    va_list a;
    va_start(a, fmt);
    vfprintf(g_capf, fmt, a);
    va_end(a);
    fputc('\n', g_capf);
}

void flushPending(CtxState *s) {
    if (s->pendCount) capLine(s, "%s : %u", s->pendSig.c_str(), s->pendCount);
    s->pendCount = 0;
    s->pendSig.clear();
}

static std::string texDesc(ID3D11View *v) {
    if (!v) return "-";
    ID3D11Resource *r = nullptr;
    v->GetResource(&r);
    if (!r) return "?";
    std::string s = "?";
    ID3D11Texture2D *t = nullptr;
    if (SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&t))) {
        D3D11_TEXTURE2D_DESC d;
        t->GetDesc(&d);
        char b[96];
        snprintf(b, sizeof b, "%ux%u f%u%s%s", d.Width, d.Height, (unsigned)d.Format, d.ArraySize > 1 ? " arr" : "",
                 d.SampleDesc.Count > 1 ? " msaa" : "");
        s = b;
        t->Release();
    }
    r->Release();
    return s;
}

void setRTs(CtxState *s, UINT n, ID3D11RenderTargetView *const *rtv, ID3D11DepthStencilView *dsv) {
    std::string d = "RT[";
    for (UINT i = 0; i < n; ++i) {
        if (i) d += ", ";
        d += texDesc(rtv ? rtv[i] : nullptr);
    }
    d += "] DS ";
    d += texDesc(dsv);
    s->rt = d;
}

static void dumpCamera(CtxState *s, int st) {
    ShaderInfo *sh = s->st[st].sh;
    if (!sh || !sh->camera) return;
    for (auto &cb : sh->cbs) {
        if (cb.camVars.empty() || cb.slot >= CB_SLOTS) continue;
        void *buf = s->st[st].cb[cb.slot];
        UINT off = s->st[st].cbOff[cb.slot];
        std::vector<uint8_t> data;
        unsigned ver = 0;
        bool have = false;
        {
            std::lock_guard<std::mutex> lk(g_snapMx);
            ver = g_ver.count(buf) ? g_ver[buf] : 0;
            char key[128];
            snprintf(key, sizeof key, "%016llx/%u/%p/%u/%u", (unsigned long long)sh->hash, cb.slot, buf, off, ver);
            if (!g_dumped.insert(key).second) continue;
            auto it = g_snap.find(buf);
            if (it != g_snap.end()) {
                data = it->second;
                have = true;
            }
        }
        if (!have) {
            capLine(s, "  cam %cS %016llx cb b%u '%s' buf %p: no update seen this frame", sh->stage,
                    (unsigned long long)sh->hash, cb.slot, cb.name.c_str(), buf);
            continue;
        }
        capLine(s, "  cam %cS %016llx cb b%u '%s' buf %p +%u v%u:", sh->stage, (unsigned long long)sh->hash, cb.slot,
                cb.name.c_str(), buf, off, ver);
        for (int vi : cb.camVars) {
            const VarInfo &v = cb.vars[vi];
            UINT at = off + v.offset;
            UINT nf = v.size / 4;
            if (nf > 24) nf = 24;
            if (at + nf * 4 > data.size()) {
                capLine(s, "    %s: out of range", v.name.c_str());
                continue;
            }
            const float *f = (const float *)(data.data() + at);
            std::string line;
            char b[32];
            for (UINT k = 0; k < nf; ++k) {
                if (k && k % 4 == 0) line += " |";
                snprintf(b, sizeof b, " %.4f", f[k]);
                line += b;
            }
            capLine(s, "    %s:%s", v.name.c_str(), line.c_str());
        }
    }
}

float g_right[3];
volatile bool g_haveRight;

volatile LONG g_statTlSwaps;
std::mutex g_hideMx;
std::vector<uint64_t> g_vsFrame, g_vsList; // vertex shaders drawn in "3D": this frame, last frame
volatile uint64_t g_hideHash;
volatile int g_eyeMode; // 0 off, 1 right eye, 2 left eye, 3 alternating every frame
volatile bool g_vrActive;
volatile bool g_vrInitRunning; // devices created now belong to the VR runtime
// a menu with a 3D picture in it (the MechLab's mech): the game's screen mode is 4:3 there while the
// output is not. It is shown as a menu (one flat screen), the camera is left alone
volatile bool g_menuMode;
bool g_renderOverride;        // the game renders larger than its window ([vr] render_height)
void *volatile g_foreignDev;  // the VR runtime's device (shares the vtable with the game's)
volatile bool g_inHud;        // inside the top-level "after 3D: HUD, 2D" label
volatile bool g_smallCamSide; // the small camera being drawn is a window at the picture's side (the target's
                              // view), not the zoom window
volatile float g_hudPx;       // HUD shift per eye in back-buffer pixels (VR), 0 = none // VR session running: the eyes
                              // alternate, offset from [vr]
volatile bool g_in3D;         // inside the top-level "3D" label: the world, not HUD or menus
volatile bool g_in3DSeen;     // a "3D" label was seen (a battle is running)

void onWork(void *ctx, const char *kind, bool compute) {
    if (ctxOf(ctx)->foreign) return;
    CtxState *s = ctxOf(ctx);
    if (!compute && g_in3D && s->st[ST_VS].sh) {
        uint64_t h = s->st[ST_VS].sh->hash;
        std::lock_guard<std::mutex> lk(g_hideMx);
        bool have = false;
        for (uint64_t x : g_vsFrame) have |= x == h;
        if (!have) g_vsFrame.push_back(h);
    }
    if (compute)
        InterlockedIncrement(&g_statDisp);
    else if (s->deferred)
        InterlockedIncrement(&g_statDrawsDef);
    else
        InterlockedIncrement(&g_statDrawsImm);
    if (!g_cap) return;
    char b[512];
    if (compute)
        snprintf(b, sizeof b, "%s CS %016llx", kind,
                 s->st[ST_CS].sh ? (unsigned long long)s->st[ST_CS].sh->hash : 0ull);
    else
        snprintf(b, sizeof b, "%s VS %016llx PS %016llx %s VP %s", kind,
                 s->st[ST_VS].sh ? (unsigned long long)s->st[ST_VS].sh->hash : 0ull,
                 s->st[ST_PS].sh ? (unsigned long long)s->st[ST_PS].sh->hash : 0ull, s->rt.c_str(), s->vp.c_str());
    if (s->pendSig != b) {
        flushPending(s);
        s->pendSig = b;
    }
    ++s->pendCount;
    // camera dumps precede the "<signature> : <count>" line of the draws they belong to
    if (compute) {
        dumpCamera(s, ST_CS);
    } else {
        dumpCamera(s, ST_VS);
        dumpCamera(s, ST_PS);
    }
}

void snapshot(void *res, const void *src, UINT dstOff, UINT bytes) {
    UINT sz = cbSize(res);
    if (!sz || !src) return;
    std::lock_guard<std::mutex> lk(g_snapMx);
    auto &v = g_snap[res];
    if (v.size() < sz) v.resize(sz);
    if (dstOff >= sz) return;
    if (bytes == 0 || dstOff + bytes > sz) bytes = sz - dstOff;
    memcpy(v.data() + dstOff, src, bytes);
    ++g_ver[res];
}

// ---------------------------------------------------------------------------------------------
// the renderer's constant buffer 'View' (928 bytes) holds g_vWorldToClip at byte 480: the projection's scales and
// near plane and the camera's axes are read from it (eyeLearnLayout), and the camera hook is calibrated with it.
// (Until v11.58 the eye could also be moved without the camera hook, by changing every camera matrix the renderer
// writes into its buffers - the first way this mod worked. It went with the fallback for other builds of mw4hook.)

int g_xIdx = -1;    // float index of the clip.x translation term (12 column-major, 3 row-major)
float g_P00, g_P11; // projection scales, from g_vWorldToClip
int g_layoutLogs;

int eyeKind(UINT sz) { return sz == 928 ? EK_VIEW : EK_NONE; }

float len3(float a, float b, float c) { return sqrtf(a * a + b * b + c * c); }
