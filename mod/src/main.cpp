// MW4VR - a d3d11.dll proxy behind MW4Engine (mw4hook.dll): VR for MechWarrior 4: Mercenaries.
//
// Goal: find out, without touching the engine, where the camera lives (constant buffers,
// reflected variable names), how a frame is structured (annotation labels, render targets,
// passes, deferred contexts) and where the swap chain is presented. Nothing is modified.
//
// Output (next to the DLL, i.e. the game folder; opened shared, readable while the game runs):
//   mw4vr.log              - device/swap chain/context creation, what was found and switched on, periodic fps
//   ([debug] level=1:)
//   mw4vr_shaders.txt      - every created shader with its constant buffers and variables
//   mw4vr_frame_NNN.txt    - one fully traced frame (Ctrl+Shift+F11, or create mw4vr_capture.txt)
//
// v2: originals are kept per vtable (deferred contexts have their own), deferred contexts and
// command lists are hooked and counted, the immediate context is re-patched if D3D swaps its
// vtable, state is tracked per context.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11_3.h>
#include <dxgi1_2.h>
#include <d3d11shader.h>

#include <share.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <intrin.h>
#include <algorithm>

#include "eye_tl_vs.h"
#include "core.h"

extern "C" {
extern const int VT_Dev_CreateBuffer, VT_Dev_CreateVertexShader, VT_Dev_CreatePixelShader,
    VT_Dev_CreateComputeShader, VT_Dev_CreateGeometryShader, VT_Dev_CreateHullShader,
    VT_Dev_CreateDomainShader, VT_Dev_CreateDeferredContext, VT_Dev_CreateDeferredContext1,
    VT_Dev_CreateDeferredContext2, VT_Dev_CreateDeferredContext3;
extern const int VT_Ctx_VSSetConstantBuffers, VT_Ctx_PSSetConstantBuffers, VT_Ctx_CSSetConstantBuffers,
    VT_Ctx_VSSetConstantBuffers1, VT_Ctx_PSSetConstantBuffers1, VT_Ctx_CSSetConstantBuffers1,
    VT_Ctx_VSSetShader, VT_Ctx_VSSetShaderResources, VT_Ctx_PSSetShader, VT_Ctx_CSSetShader, VT_Ctx_Draw, VT_Ctx_DrawIndexed,
    VT_Ctx_DrawInstanced, VT_Ctx_DrawIndexedInstanced, VT_Ctx_DrawInstancedIndirect,
    VT_Ctx_DrawIndexedInstancedIndirect, VT_Ctx_Dispatch, VT_Ctx_DispatchIndirect, VT_Ctx_Map,
    VT_Ctx_Unmap, VT_Ctx_UpdateSubresource, VT_Ctx_UpdateSubresource1, VT_Ctx_OMSetRenderTargets,
    VT_Ctx_OMSetRenderTargetsAndUAVs, VT_Ctx_RSSetViewports, VT_Ctx_ExecuteCommandList,
    VT_Ctx_FinishCommandList;
extern const int VT_Ann_BeginEvent, VT_Ann_EndEvent, VT_Ann_SetMarker, VT_Ann_GetStatus;
extern const int VT_Fac_CreateSwapChain, VT_Fac_CreateSwapChainForHwnd;
extern const int VT_Sc_Present, VT_Sc_Present1, VT_Sc_ResizeBuffers;
}

// ---------------------------------------------------------------------------------------------
// logging

static std::wstring g_dir;
static FILE *g_log, *g_shlog;
static int g_dbg; // [debug] level in mw4vr.ini: 0 = the log alone, short; 1 = the diagnosis files and all lines
static std::mutex g_logMx;

static void logf(const char *fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMx);
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d  ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    vfprintf(g_log, fmt, a);
    va_end(a);
    fputc('\n', g_log);
    fflush(g_log);
}

static std::string narrow(const wchar_t *w) {
    if (!w) return "(null)";
    char buf[512];
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof buf, nullptr, nullptr);
    return n > 0 ? std::string(buf) : std::string("?");
}

// ---------------------------------------------------------------------------------------------
// the real d3d11.dll and the exports we pass through

static HMODULE g_real;
typedef HRESULT(WINAPI *PFN_CreateDevice)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                          const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
                                          D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT(WINAPI *PFN_CreateDeviceAndSwapChain)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                                      const D3D_FEATURE_LEVEL *, UINT, UINT,
                                                      const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **,
                                                      ID3D11Device **, D3D_FEATURE_LEVEL *,
                                                      ID3D11DeviceContext **);
static PFN_CreateDevice r_CreateDevice;
static PFN_CreateDeviceAndSwapChain r_CreateDeviceAndSwapChain;

#define FWD_LIST(X)                                                                            \
    X(D3D11CoreCreateDevice) X(D3D11CoreCreateLayeredDevice) X(D3D11CoreGetLayeredDeviceSize)    \
    X(D3D11CoreRegisterLayers) X(D3D11CreateDeviceForD3D12) X(D3D11On12CreateDevice)             \
    X(CreateDirect3D11DeviceFromDXGIDevice) X(CreateDirect3D11SurfaceFromDXGISurface)            \
    X(D3DPerformance_BeginEvent) X(D3DPerformance_EndEvent) X(D3DPerformance_GetStatus)          \
    X(D3DPerformance_SetMarker) X(EnableFeatureLevelUpgrade) X(OpenAdapter10) X(OpenAdapter10_2)

#define DECL_FWD(n)                                                                            \
    extern "C" FARPROC r_##n;                                                                  \
    FARPROC r_##n;                                                                             \
    extern "C" __declspec(naked) void P_##n() { __asm { jmp dword ptr [r_##n] } }
FWD_LIST(DECL_FWD)

#pragma comment(linker, "/EXPORT:D3D11CoreCreateDevice=_P_D3D11CoreCreateDevice")
#pragma comment(linker, "/EXPORT:D3D11CoreCreateLayeredDevice=_P_D3D11CoreCreateLayeredDevice")
#pragma comment(linker, "/EXPORT:D3D11CoreGetLayeredDeviceSize=_P_D3D11CoreGetLayeredDeviceSize")
#pragma comment(linker, "/EXPORT:D3D11CoreRegisterLayers=_P_D3D11CoreRegisterLayers")
#pragma comment(linker, "/EXPORT:D3D11CreateDeviceForD3D12=_P_D3D11CreateDeviceForD3D12")
#pragma comment(linker, "/EXPORT:D3D11On12CreateDevice=_P_D3D11On12CreateDevice")
#pragma comment(linker, "/EXPORT:CreateDirect3D11DeviceFromDXGIDevice=_P_CreateDirect3D11DeviceFromDXGIDevice")
#pragma comment(linker, "/EXPORT:CreateDirect3D11SurfaceFromDXGISurface=_P_CreateDirect3D11SurfaceFromDXGISurface")
#pragma comment(linker, "/EXPORT:D3DPerformance_BeginEvent=_P_D3DPerformance_BeginEvent")
#pragma comment(linker, "/EXPORT:D3DPerformance_EndEvent=_P_D3DPerformance_EndEvent")
#pragma comment(linker, "/EXPORT:D3DPerformance_GetStatus=_P_D3DPerformance_GetStatus")
#pragma comment(linker, "/EXPORT:D3DPerformance_SetMarker=_P_D3DPerformance_SetMarker")
#pragma comment(linker, "/EXPORT:EnableFeatureLevelUpgrade=_P_EnableFeatureLevelUpgrade")
#pragma comment(linker, "/EXPORT:OpenAdapter10=_P_OpenAdapter10")
#pragma comment(linker, "/EXPORT:OpenAdapter10_2=_P_OpenAdapter10_2")
#pragma comment(linker, "/EXPORT:D3D11CreateDevice=_Hook_D3D11CreateDevice@40")
#pragma comment(linker, "/EXPORT:D3D11CreateDeviceAndSwapChain=_Hook_D3D11CreateDeviceAndSwapChain@48")

static bool loadReal() {
    if (g_real) return true;
    wchar_t p[MAX_PATH];
    GetSystemDirectoryW(p, MAX_PATH);
    wcscat_s(p, L"\\d3d11.dll");
    g_real = LoadLibraryW(p);
    if (!g_real) return false;
    r_CreateDevice = (PFN_CreateDevice)GetProcAddress(g_real, "D3D11CreateDevice");
    r_CreateDeviceAndSwapChain =
        (PFN_CreateDeviceAndSwapChain)GetProcAddress(g_real, "D3D11CreateDeviceAndSwapChain");
#define RES_FWD(n) r_##n = GetProcAddress(g_real, #n);
    FWD_LIST(RES_FWD)
    return true;
}

// ---------------------------------------------------------------------------------------------
// vtable patching, originals kept per vtable

enum { MAX_VT = 48, MAX_SLOT = 256 };
struct VtEntry {
    void **vt;
    void *o[MAX_SLOT];
};
static VtEntry g_vts[MAX_VT];
static volatile LONG g_nvts;
static std::mutex g_vtMx;

static void *origOf(void *self, int idx) {
    void **vt = *(void ***)self;
    LONG n = g_nvts;
    for (LONG i = 0; i < n; ++i)
        if (g_vts[i].vt == vt) return g_vts[i].o[idx];
    for (LONG i = 0; i < n; ++i) // vtable copied by the runtime with our pointer in it
        if (g_vts[i].o[idx]) return g_vts[i].o[idx];
    return nullptr;
}

static void *g_lastReplaced;
static std::string moduleOf(void *addr);
static bool patch(void *obj, int idx, void *hook) {
    if (!obj || idx < 0 || idx >= MAX_SLOT) return false;
    std::lock_guard<std::mutex> lk(g_vtMx);
    void **vt = *(void ***)obj;
    VtEntry *e = nullptr;
    for (LONG i = 0; i < g_nvts; ++i)
        if (g_vts[i].vt == vt) e = &g_vts[i];
    if (!e) {
        if (g_nvts >= MAX_VT) return false;
        e = &g_vts[g_nvts];
        e->vt = vt;
        InterlockedIncrement(&g_nvts);
    }
    if (vt[idx] == hook) return false;
    // the runtime may rewrite entries of its per-object vtable (e.g. a thread-safe variant):
    // whatever stands there now is the function to call through
    e->o[idx] = vt[idx];
    g_lastReplaced = vt[idx];
    DWORD old;
    VirtualProtect(&vt[idx], sizeof(void *), PAGE_EXECUTE_READWRITE, &old);
    vt[idx] = hook;
    VirtualProtect(&vt[idx], sizeof(void *), old, &old);
    FlushInstructionCache(GetCurrentProcess(), &vt[idx], sizeof(void *));
    return true;
}

#define ORIG(T, self, idx) ((T)origOf((void *)(self), (idx)))

// ---------------------------------------------------------------------------------------------
// shader reflection

struct VarInfo {
    std::string name;
    UINT offset, size, rows, cols, elems;
};
struct CBInfo {
    std::string name;
    UINT slot, size;
    std::vector<VarInfo> vars;
    std::vector<int> camVars;
};
struct ShaderInfo {
    char stage;
    uint64_t hash;
    std::vector<CBInfo> cbs;
    bool camera = false;
    std::vector<std::pair<UINT, UINT>> camPos; // (cb slot, byte offset) of camera-position float3/float4s
    std::vector<std::string> camPosNames;
    std::string res; // non-cbuffer bindings, for the audit
    std::vector<int> srvSlots; // shader-resource (t) slots it reads
};

// camera positions a shader gets (camera-relative rendering): moved by the eye offset as well
static bool isCamPos(const std::string &n) {
    std::string l = n;
    for (auto &c : l) c = (char)tolower((unsigned char)c);
    auto ends = [&](const char *t) {
        size_t k = strlen(t);
        return l.size() >= k && l.compare(l.size() - k, k, t) == 0;
    };
    return ends("cam") || ends("campos") || ends("eye");
}

static std::mutex g_shMx;
static std::unordered_map<void *, ShaderInfo *> g_shaders;
static std::unordered_map<void *, UINT> g_cbSize;
static unsigned g_shaderCount;

typedef HRESULT(WINAPI *PFN_D3DReflect)(LPCVOID, SIZE_T, REFIID, void **);
static PFN_D3DReflect g_reflect;
typedef HRESULT(WINAPI *PFN_D3DDisassemble)(LPCVOID, SIZE_T, UINT, LPCSTR, ID3DBlob **);
typedef HRESULT(WINAPI *PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT,
                                        ID3DBlob **, ID3DBlob **);
static PFN_D3DCompile g_compile;
static ID3D11Device *g_dev;
// a file opened (the checked form of the C library's function; null: it could not be)
static FILE *fileOpen(const wchar_t *path, const wchar_t *mode) {
    FILE *f = nullptr;
    return _wfopen_s(&f, path, mode) == 0 ? f : nullptr;
}

#include "mw4hook_abi.inc"

static ID3D11VertexShader *g_eyeVS; // replacement for the CPU path's screen-space vertex shader
static void createEyeVS();
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

static void onShader(char stage, const void *code, SIZE_T len, void *obj) {
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

static ShaderInfo *lookupShader(void *s) {
    if (!s) return nullptr;
    std::lock_guard<std::mutex> lk(g_shMx);
    auto it = g_shaders.find(s);
    return it == g_shaders.end() ? nullptr : it->second;
}

static UINT cbSize(void *b) {
    std::lock_guard<std::mutex> lk(g_shMx);
    auto it = g_cbSize.find(b);
    return it == g_cbSize.end() ? 0 : it->second;
}

// ---------------------------------------------------------------------------------------------
// per-context state

enum { ST_VS, ST_PS, ST_CS, ST_N };
enum { CB_SLOTS = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT };
struct StageState {
    ShaderInfo *sh = nullptr;
    void *srvBuf[64] = {}; // buffer behind each shader-resource slot (VS only), null for textures
    void *obj = nullptr;
    void *cb[CB_SLOTS] = {};
    UINT cbOff[CB_SLOTS] = {};
};
struct CtxState {
    int id = 0;
    void *self = nullptr;
    UINT vpN = 0;
    D3D11_VIEWPORT vp0[4] = {}; // the viewports as the game set them
    bool foreign = false; // a context of the VR runtime's own device
    bool deferred = false;
    StageState st[ST_N];
    std::string rt = "?", vp = "?", pendSig;
    std::vector<std::string> labels; // open annotation labels
    unsigned pendCount = 0;
    int depth = 0;
    std::unordered_map<void *, void *> mapped;
    std::unordered_map<void *, int> mapType;
};

static std::mutex g_ctxMx;
static std::unordered_map<void *, CtxState *> g_ctx;      // context -> state
static std::unordered_map<void *, void *> g_annCtx;       // annotation interface -> its context

static CtxState *ctxOf(void *c) {
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
static CtxState *ctxOfAnn(void *a) {
    void *c;
    {
        std::lock_guard<std::mutex> lk(g_ctxMx);
        auto it = g_annCtx.find(a);
        c = it == g_annCtx.end() ? a : it->second;
    }
    return ctxOf(c);
}

// statistics (all contexts)
static volatile LONG g_statDrawsImm, g_statDrawsDef, g_statDisp, g_statMaps, g_statLabels, g_statExec,
    g_statFinish;
static unsigned g_statFrames, g_frame;
static DWORD g_statT0;
static std::string g_frameLabels, g_lastFrameLabels;
static std::mutex g_labelMx;
static bool g_labelsSeen, g_firstPresentLogged;
static ID3D11DeviceContext *g_imm;
static void **g_immVt;

// capture
static volatile bool g_cap;
static FILE *g_capf;
static std::mutex g_capMx;
static unsigned g_capIndex;
static std::mutex g_snapMx;
static std::unordered_map<void *, std::vector<uint8_t>> g_snap;
static std::unordered_map<void *, unsigned> g_ver;
static std::unordered_set<std::string> g_dumped;

static void capLine(CtxState *s, const char *fmt, ...) {
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

static void flushPending(CtxState *s) {
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
        snprintf(b, sizeof b, "%ux%u f%u%s%s", d.Width, d.Height, (unsigned)d.Format,
                 d.ArraySize > 1 ? " arr" : "", d.SampleDesc.Count > 1 ? " msaa" : "");
        s = b;
        t->Release();
    }
    r->Release();
    return s;
}

static void setRTs(CtxState *s, UINT n, ID3D11RenderTargetView *const *rtv, ID3D11DepthStencilView *dsv) {
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
        capLine(s, "  cam %cS %016llx cb b%u '%s' buf %p +%u v%u:", sh->stage, (unsigned long long)sh->hash,
                cb.slot, cb.name.c_str(), buf, off, ver);
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

static float g_right[3];
static volatile bool g_haveRight;

static volatile LONG g_statTlSwaps;
static std::mutex g_hideMx;
static std::vector<uint64_t> g_vsFrame, g_vsList; // vertex shaders drawn in "3D": this frame, last frame
static volatile uint64_t g_hideHash;
static volatile int g_eyeMode; // 0 off, 1 right eye, 2 left eye, 3 alternating every frame
static volatile bool g_vrActive;
static volatile bool g_vrInitRunning; // devices created now belong to the VR runtime
static void mouseInstall();
// a menu with a 3D picture in it (the MechLab's mech): the game's screen mode is 4:3 there while the
// output is not. It is shown as a menu (one flat screen), the camera is left alone
static volatile bool g_menuMode;
static void menuModeUpdate();
static void mouseCapture();
static void camKeepKey();
static void camAtPresent();
static void mw5OnPresent();
static bool mw5ScreensOn();
static int mw5TopSpans(float T, float (*iv)[2]);
static float mw5LeftAltBottom(float L);
static void mw5MonitorLists(McHud &hd, float T);
static void mw5HighlightDraw(ID3D11DeviceContext *c);
// a part of the HUD on the cockpit's glass (mw5.inc): what is shown (rc), the part its place is given for (base),
// that place as seen from the seat (degrees right and up of base's middle, base's width in degrees, metres away)
struct GlassItem {
    float rc[4], base[4];
    float right, up, width, dist;
};
static int mw5GlassItems(GlassItem *out);
static void mw5HudCopy(ID3D11DeviceContext *, ID3D11Texture2D *);
static bool g_renderOverride;          // the game renders larger than its window ([vr] render_height)
static void *volatile g_foreignDev;    // the VR runtime's device (shares the vtable with the game's)
static volatile bool g_inHud;          // inside the top-level "after 3D: HUD, 2D" label
static volatile bool g_smallCamSide;   // the small camera being drawn is a window at the picture's side (the target's view), not the zoom window
static volatile float g_hudPx;         // HUD shift per eye in back-buffer pixels (VR), 0 = none // VR session running: the eyes alternate, offset from [vr]
static volatile bool g_in3D; // inside the top-level "3D" label: the world, not HUD or menus
static volatile bool g_in3DSeen; // a "3D" label was seen (a battle is running)

static void onWork(void *ctx, const char *kind, bool compute) {
    if (ctxOf(ctx)->foreign) return;
    CtxState *s = ctxOf(ctx);
    if (!compute && g_in3D && s->st[ST_VS].sh) {
        uint64_t h = s->st[ST_VS].sh->hash;
        std::lock_guard<std::mutex> lk(g_hideMx);
        bool have = false;
        for (uint64_t x : g_vsFrame) have |= x == h;
        if (!have) g_vsFrame.push_back(h);
    }
    if (compute) InterlockedIncrement(&g_statDisp);
    else if (s->deferred) InterlockedIncrement(&g_statDrawsDef);
    else InterlockedIncrement(&g_statDrawsImm);
    if (!g_cap) return;
    char b[512];
    if (compute)
        snprintf(b, sizeof b, "%s CS %016llx", kind,
                 s->st[ST_CS].sh ? (unsigned long long)s->st[ST_CS].sh->hash : 0ull);
    else
        snprintf(b, sizeof b, "%s VS %016llx PS %016llx %s VP %s", kind,
                 s->st[ST_VS].sh ? (unsigned long long)s->st[ST_VS].sh->hash : 0ull,
                 s->st[ST_PS].sh ? (unsigned long long)s->st[ST_PS].sh->hash : 0ull, s->rt.c_str(),
                 s->vp.c_str());
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

static void snapshot(void *res, const void *src, UINT dstOff, UINT bytes) {
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

enum { EK_NONE, EK_VIEW };
static int g_xIdx = -1;               // float index of the clip.x translation term (12 column-major, 3 row-major)
static float g_P00, g_P11;            // projection scales, from g_vWorldToClip
static int g_layoutLogs;
#include "head.inc"

static int eyeKind(UINT sz) { return sz == 928 ? EK_VIEW : EK_NONE; }

static float len3(float a, float b, float c) { return sqrtf(a * a + b * b + c * c); }

#include "cam.inc"
#include "sway.inc"

static void eyeLearnLayout(const float *m) {
    // in camera mode only the main camera's projection counts: the zoom window's and the target view's
    // cameras have their own, and the cockpit's transform and the layers' size are the main one's
    if (g_srcCamActive && !g_camInMain) return;
    // the w row of a world->clip matrix is the view's unit z axis (plus a translation)
    float cm = len3(m[3], m[7], m[11]), rm = len3(m[12], m[13], m[14]);
    int idx = -1;
    float p00 = 0;
    if (fabsf(cm - 1) < 0.02f && fabsf(rm - 1) > 0.05f) {
        idx = 12;
        p00 = len3(m[0], m[4], m[8]);
    } else if (fabsf(rm - 1) < 0.02f && fabsf(cm - 1) > 0.05f) {
        idx = 3;
        p00 = len3(m[0], m[1], m[2]);
        if (p00 > 0) {
            g_right[0] = m[0] / p00;
            g_right[1] = m[1] / p00;
            g_right[2] = m[2] / p00;
            g_haveRight = true;
        }
        g_P11 = len3(m[4], m[5], m[6]);
        if (g_P11 > 0) {
            g_near = m[11];
            for (int i = 0; i < 3; ++i) {
                g_up[i] = m[4 + i] / g_P11;
                g_fwd[i] = m[12 + i];
            }
        }
    }
    if (g_layoutLogs < 3) {
        ++g_layoutLogs;
        logf("eye: g_vWorldToClip = [%.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f]"
             " -> |w row| column-major %.4f, row-major %.4f => x term at float %d, P00 %.4f",
             m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15], cm,
             rm, idx, p00);
    }
    if (idx >= 0) {
        g_xIdx = idx;
        g_P00 = p00;
        camCalibrate();
    }
}

// data holds the buffer bytes [base, base + sz) - a whole buffer (base 0) or the box of a partial update
static void eyeLayoutFrom(int kind, const uint8_t *data, UINT sz, UINT base = 0) {
    if (kind == EK_VIEW && base <= 480 && base + sz >= 480 + 64) eyeLearnLayout((const float *)(data + 480 - base));
}

static void eyeReadIni() {
    std::wstring p = g_dir + L"mw4vr.ini";
    if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
        FILE *f = _wfsopen(p.c_str(), L"w", _SH_DENYNO);
        if (f) {
            fputs("; MW4VR - no settings were found. Copy mw4vr.ini from the mod's package (docs/mw4vr.ini.example) over this file:\n"
                  "; it has every key with a comment. Until then the mod stays switched off.\n"
                  "[vr]\n"
                  "enabled=0\n",
                  f);
            fclose(f);
        }
    }
}

static void hideOnPresent() {
    {
        std::lock_guard<std::mutex> lk(g_hideMx);
        if (!g_vsFrame.empty()) g_vsList.swap(g_vsFrame);
        g_vsFrame.clear();
    }
    static bool keyWas;
    static int idx;
    bool key = g_dbg && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_F8) & 0x8000); // (diagnosis: [debug] level=1 only)
    if (key && !keyWas) {
        std::lock_guard<std::mutex> lk(g_hideMx);
        int n = (int)g_vsList.size();
        idx = (idx + 1) % (n + 1);
        if (idx == 0) {
            g_hideHash = 0;
            logf("hide: nothing hidden");
            Beep(400, 80);
        } else {
            g_hideHash = g_vsList[idx - 1];
            logf("hide: VS %016llx hidden (%d of %d)", (unsigned long long)g_hideHash, idx, n);
            Beep(700 + 60 * idx, 80);
        }
    }
    keyWas = key;
}

static void eyeOnPresent() {
    hideOnPresent();
    static bool first = true;
    if (first) {
        first = false;
        eyeReadIni();
    }
    static bool keyWas;
    // (diagnosis, [debug] level=1 only: without it a slip beside Ctrl+F11 switched the eyes off - one flat picture,
    // no HUD layer, no menu)
    bool key = g_dbg && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_F7) & 0x8000);
    if (key && !keyWas) {
        g_eyeMode = (g_eyeMode + 1) & 3;
        static const char *names[] = {"off", "right eye", "left eye", "alternating"};
        logf("eye: mode %d (%s), P00 %.4f", g_eyeMode, names[g_eyeMode], g_P00);
        Beep(g_eyeMode ? 600 + 200 * g_eyeMode : 400, 80);
    }
    keyWas = key;
}

static HMODULE g_self;
static std::string moduleOf(void *addr) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)addr, &m) || !m)
        return "?";
    char o[64];
    snprintf(o, sizeof o, "+%lx", (unsigned long)((char *)addr - (char *)m));
    if (m == g_self) return std::string("PROBE") + o;
    wchar_t n[MAX_PATH];
    GetModuleFileNameW(m, n, MAX_PATH);
    const wchar_t *b = wcsrchr(n, L'\\');
    return narrow(b ? b + 1 : n) + o;
}

static void dumpSlots(void *obj, const char *what) {
    if (!obj) return;
    void **vt = *(void ***)obj;
    struct { const char *n; int i; } slots[] = {
        {"Draw", VT_Ctx_Draw}, {"DrawIndexed", VT_Ctx_DrawIndexed},
        {"DrawIndexedInstanced", VT_Ctx_DrawIndexedInstanced}, {"DrawInstanced", VT_Ctx_DrawInstanced},
        {"Dispatch", VT_Ctx_Dispatch}, {"VSSetShader", VT_Ctx_VSSetShader}, {"PSSetShader", VT_Ctx_PSSetShader},
        {"PSSetConstantBuffers", VT_Ctx_PSSetConstantBuffers}, {"OMSetRenderTargets", VT_Ctx_OMSetRenderTargets},
        {"RSSetViewports", VT_Ctx_RSSetViewports}, {"Map", VT_Ctx_Map}, {"Unmap", VT_Ctx_Unmap}};
    logf("%s %p vtable %p (%s):", what, obj, vt, moduleOf(vt).c_str());
    for (auto &s : slots) logf("   [%3d] %-22s -> %p %s", s.i, s.n, vt[s.i], moduleOf(vt[s.i]).c_str());
}

static void startCapture() {
    dumpSlots(g_imm, "immediate context");
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%smw4vr_frame_%03u.txt", g_dir.c_str(), g_capIndex++);
    {
        std::lock_guard<std::mutex> lk(g_capMx);
        g_capf = _wfsopen(p, L"w", _SH_DENYNO);
        if (!g_capf) return;
        fprintf(g_capf, "frame %u  (c<N> = context, d = deferred; indentation = label depth)\n", g_frame);
    }
    {
        std::lock_guard<std::mutex> lk(g_snapMx);
        g_snap.clear();
        g_ver.clear();
        g_dumped.clear();
    }
    {
        std::lock_guard<std::mutex> lk(g_ctxMx);
        for (auto &kv : g_ctx) {
            kv.second->pendCount = 0;
            kv.second->pendSig.clear();
            kv.second->mapped.clear();
            kv.second->mapType.clear();
        }
    }
    g_cap = true;
    logf("capture started: %s", narrow(p).c_str());
}

static void endCapture() {
    g_cap = false;
    {
        std::lock_guard<std::mutex> lk(g_ctxMx);
        for (auto &kv : g_ctx)
            if (kv.second->pendCount) {
                std::lock_guard<std::mutex> lk2(g_capMx);
                if (g_capf)
                    fprintf(g_capf, "[c%d%s] %s : %u\n", kv.second->id, kv.second->deferred ? "d" : "",
                            kv.second->pendSig.c_str(), kv.second->pendCount);
                kv.second->pendCount = 0;
            }
    }
    std::lock_guard<std::mutex> lk(g_capMx);
    if (g_capf) {
        fprintf(g_capf, "end of frame\n");
        fclose(g_capf);
        g_capf = nullptr;
    }
    logf("capture written");
}

static int hookContext(ID3D11DeviceContext *ctx, bool deferred, bool repair);
static void hotpatchScan(ID3D11DeviceContext *ctx);

#include "mech.inc"
#include "note.inc"
#include "help.inc"
#include "hud.inc"
#include "shot.inc"
#include "e2e.inc"
// a picture that stands still: who held the frame up - the game, the system's Present, or our own work in it?
static DWORD g_presOurs, g_presReal, g_presWait; // ms: our part of the last Present, the system's, xrWaitFrame in ours
#include "vr.inc"

// The keys' page is up (help.inc): the frame just handed over is handed over again and again, until Ctrl+F1 is
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
        if (GetTickCount() - a < 3) Sleep(8);                // no frame of the runtime's to wait for: not a busy loop
        if (GetTickCount() - t0 > 15 * 60 * 1000) g_helpOn = false; // a quarter of an hour: back to the game
    }
    g_helpOn = false;
    for (int i = 0; i < 100 && (GetAsyncKeyState(VK_ESCAPE) & 0x8000); ++i) Sleep(10); // Esc let go: its release is not the game's either
    helpForgetKeys();
    QueryPerformanceCounter(&q1);
    DWORD ms = GetTickCount() - t0;
    if (g_helpClock) g_helpOffQpc += q1.QuadPart - q0.QuadPart, g_helpOffMs += ms;
    logf("help: closed after %.1f s%s", ms / 1000.0f, g_helpClock ? "" : " (the clocks are left alone: the game sees one long frame)");
}

static void presentGap() {
    static DWORD last;
    DWORD now = GetTickCount();
    if (last && now - last >= 3000)
        logf("present: no frame for %.1f s (frame %u; of the last Present our part took %.1f s - xrWaitFrame %.1f s -, the system's %.1f s; "
             "the rest was the game's)", (now - last) / 1000.0f, g_frame, g_presOurs / 1000.0f, g_presWait / 1000.0f, g_presReal / 1000.0f);
    last = now;
}

static void onPresent(IDXGISwapChain *sc) {
    presentGap();
    if (!g_firstPresentLogged) {
        g_firstPresentLogged = true;
        DXGI_SWAP_CHAIN_DESC d;
        if (SUCCEEDED(sc->GetDesc(&d)))
            logf("first Present: %ux%u fmt %u buffers %u swap effect %u", d.BufferDesc.Width,
                 d.BufferDesc.Height, (unsigned)d.BufferDesc.Format, d.BufferCount, (unsigned)d.SwapEffect);
    }
    if (g_cap) endCapture();
    {   // the eye this frame was rendered for (as eyePatch chose it during the frame)
        int mode = g_eyeMode, eye = -1;
        if (mode == 1) eye = 1;
        else if (mode == 2) eye = 0;
        else if (mode == 3) eye = (g_frame & 1) ? 0 : 1;
        mouseInstall();
        menuModeUpdate();
        mouseCapture();
        camKeepKey();
        mw5OnPresent();
        shotKeys();
        bool inBattle = g_vr.ok && g_hudLayerOn && g_srcCamActive && g_camEyeFrame != ~0u && g_frame - g_camEyeFrame < 30 && !g_menuMode;
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
            if (g_dbg) logf("frame %u top-level labels: %s", g_frame, g_frameLabels.empty() ? "(none)" : g_frameLabels.c_str());
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
        if (all || reports++ % 6 == 0) logf("%.1f fps | per frame: %.0f draws imm, %.0f draws deferred, %.0f dispatches, %.0f maps, "
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
    bool key = g_dbg && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) && (GetAsyncKeyState(VK_F11) & 0x8000);
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

typedef HRESULT(STDMETHODCALLTYPE *PFN_CreateBuffer)(ID3D11Device *, const D3D11_BUFFER_DESC *,
                                                     const D3D11_SUBRESOURCE_DATA *, ID3D11Buffer **);
static HRESULT STDMETHODCALLTYPE H_CreateBuffer(ID3D11Device *d, const D3D11_BUFFER_DESC *desc,
                                                const D3D11_SUBRESOURCE_DATA *init, ID3D11Buffer **out) {
    HRESULT hr = ORIG(PFN_CreateBuffer, d, VT_Dev_CreateBuffer)(d, desc, init, out);
    if (d == g_foreignDev) return hr;
    if (SUCCEEDED(hr) && out && *out && desc && (desc->BindFlags & D3D11_BIND_CONSTANT_BUFFER)) {
        std::lock_guard<std::mutex> lk(g_shMx);
        g_cbSize[*out] = desc->ByteWidth;
    }
    return hr;
}

typedef HRESULT(STDMETHODCALLTYPE *PFN_CreateShader)(ID3D11Device *, const void *, SIZE_T, ID3D11ClassLinkage *,
                                                     void **);
#define SHADER_HOOK(Kind, letter)                                                                  \
    static HRESULT STDMETHODCALLTYPE H_Create##Kind(ID3D11Device *d, const void *code, SIZE_T len, \
                                                    ID3D11ClassLinkage *cl, void **out) {          \
        HRESULT hr = ORIG(PFN_CreateShader, d, VT_Dev_Create##Kind)(d, code, len, cl, out);        \
        if (SUCCEEDED(hr) && out && *out && d != g_foreignDev) onShader(letter, code, len, *out);  \
        return hr;                                                                                 \
    }
SHADER_HOOK(VertexShader, 'V')
SHADER_HOOK(PixelShader, 'P')
SHADER_HOOK(ComputeShader, 'C')
SHADER_HOOK(GeometryShader, 'G')
SHADER_HOOK(HullShader, 'H')
SHADER_HOOK(DomainShader, 'D')

typedef HRESULT(STDMETHODCALLTYPE *PFN_CreateDeferred)(void *, UINT, void **);
#define DEFERRED_HOOK(Sfx)                                                                         \
    static HRESULT STDMETHODCALLTYPE H_CreateDeferredContext##Sfx(void *d, UINT fl, void **out) { \
        HRESULT hr = ORIG(PFN_CreateDeferred, d, VT_Dev_CreateDeferredContext##Sfx)(d, fl, out);   \
        logf("CreateDeferredContext" #Sfx " on thread %lu -> 0x%08lx ctx %p", GetCurrentThreadId(), hr, \
             out ? *out : nullptr);                                                                \
        if (SUCCEEDED(hr) && out && *out) hookContext((ID3D11DeviceContext *)*out, true, false);          \
        return hr;                                                                                 \
    }
DEFERRED_HOOK()
DEFERRED_HOOK(1)
DEFERRED_HOOK(2)
DEFERRED_HOOK(3)

// ---------------------------------------------------------------------------------------------
// hooks: context

typedef void(STDMETHODCALLTYPE *PFN_SetShader)(ID3D11DeviceContext *, void *, ID3D11ClassInstance *const *, UINT);
#define SETSHADER_HOOK(St, idx)                                                                    \
    static void STDMETHODCALLTYPE H_##St##SetShader(ID3D11DeviceContext *c, void *s,               \
                                                    ID3D11ClassInstance *const *ci, UINT n) {      \
        ctxOf(c)->st[idx].sh = lookupShader(s);                                                    \
        ctxOf(c)->st[idx].obj = s;                                                                 \
        ORIG(PFN_SetShader, c, VT_Ctx_##St##SetShader)(c, s, ci, n);                               \
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
typedef void(STDMETHODCALLTYPE *PFN_SetCB)(ID3D11DeviceContext *, UINT, UINT, ID3D11Buffer *const *);
typedef void(STDMETHODCALLTYPE *PFN_SetCB1)(ID3D11DeviceContext *, UINT, UINT, ID3D11Buffer *const *,
                                            const UINT *, const UINT *);
#define SETCB_HOOK(St, idx)                                                                        \
    static void STDMETHODCALLTYPE H_##St##SetConstantBuffers(ID3D11DeviceContext *c, UINT s, UINT n, \
                                                             ID3D11Buffer *const *b) {             \
        trackCBs(c, idx, s, n, b, nullptr);                                                        \
        ORIG(PFN_SetCB, c, VT_Ctx_##St##SetConstantBuffers)(c, s, n, b);                           \
    }                                                                                              \
    static void STDMETHODCALLTYPE H_##St##SetConstantBuffers1(ID3D11DeviceContext *c, UINT s, UINT n, \
                                                              ID3D11Buffer *const *b, const UINT *f, \
                                                              const UINT *nc) {                    \
        trackCBs(c, idx, s, n, b, f);                                                              \
        ORIG(PFN_SetCB1, c, VT_Ctx_##St##SetConstantBuffers1)(c, s, n, b, f, nc);                  \
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
static void STDMETHODCALLTYPE H_VSSetShaderResources(ID3D11DeviceContext *c, UINT start, UINT n,
                                                     ID3D11ShaderResourceView *const *v) {
    CtxState *s = ctxOf(c);
    for (UINT i = 0; i < n && start + i < 64; ++i) s->st[ST_VS].srvBuf[start + i] = viewBuffer(v ? v[i] : nullptr);
    ORIG(PFN_SetSRV, c, VT_Ctx_VSSetShaderResources)(c, start, n, v);
}

typedef void(STDMETHODCALLTYPE *PFN_U2)(ID3D11DeviceContext *, UINT, UINT);
typedef void(STDMETHODCALLTYPE *PFN_U2I)(ID3D11DeviceContext *, UINT, UINT, INT);
typedef void(STDMETHODCALLTYPE *PFN_U4)(ID3D11DeviceContext *, UINT, UINT, UINT, UINT);
typedef void(STDMETHODCALLTYPE *PFN_U3IU)(ID3D11DeviceContext *, UINT, UINT, UINT, INT, UINT);
typedef void(STDMETHODCALLTYPE *PFN_BU)(ID3D11DeviceContext *, ID3D11Buffer *, UINT);
typedef void(STDMETHODCALLTYPE *PFN_U3)(ID3D11DeviceContext *, UINT, UINT, UINT);

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

typedef HRESULT(STDMETHODCALLTYPE *PFN_Map)(ID3D11DeviceContext *, ID3D11Resource *, UINT, D3D11_MAP, UINT,
                                            D3D11_MAPPED_SUBRESOURCE *);
static HRESULT STDMETHODCALLTYPE H_Map(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub, D3D11_MAP t,
                                       UINT fl, D3D11_MAPPED_SUBRESOURCE *m) {
    HRESULT hr = ORIG(PFN_Map, c, VT_Ctx_Map)(c, r, sub, t, fl, m);
    InterlockedIncrement(&g_statMaps);
    if (SUCCEEDED(hr) && m && t != D3D11_MAP_READ && !ctxOf(c)->foreign) {
        UINT sz = cbSize(r);
        if (sz && (g_cap || eyeKind(sz) || sz == 48)) { // what is looked at when it is unmapped
            CtxState *s = ctxOf(c);
            s->mapped[r] = m->pData;
            s->mapType[r] = (int)t;
        }
    }
    return hr;
}
typedef void(STDMETHODCALLTYPE *PFN_Unmap)(ID3D11DeviceContext *, ID3D11Resource *, UINT);
static void STDMETHODCALLTYPE H_Unmap(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub) {
    CtxState *s = ctxOf(c);
    auto it = s->mapped.find(r);
    if (it != s->mapped.end()) {
        int t = s->mapType[r];
        UINT sz = cbSize(r);
        int kind = eyeKind(sz);
        // only whole-buffer rewrites: a NO_OVERWRITE map may only fill a part we know nothing about
        if (kind && t == D3D11_MAP_WRITE_DISCARD) eyeLayoutFrom(kind, (const uint8_t *)it->second, sz);
        if (sz == 48 && t == D3D11_MAP_WRITE_DISCARD) frameCbSeen(r, it->second);
        if (g_cap) snapshot(r, it->second, 0, 0);
        s->mapped.erase(it);
        s->mapType.erase(r);
    }
    ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, r, sub);
}
typedef void(STDMETHODCALLTYPE *PFN_Update)(ID3D11DeviceContext *, ID3D11Resource *, UINT, const D3D11_BOX *,
                                            const void *, UINT, UINT);
static void STDMETHODCALLTYPE H_UpdateSubresource(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub,
                                                  const D3D11_BOX *box, const void *src, UINT rp, UINT dp) {
    if (g_cap) snapshot(r, src, box ? box->left : 0, box ? box->right - box->left : 0);
    ORIG(PFN_Update, c, VT_Ctx_UpdateSubresource)(c, r, sub, box, src, rp, dp);
}
typedef void(STDMETHODCALLTYPE *PFN_Update1)(ID3D11DeviceContext *, ID3D11Resource *, UINT, const D3D11_BOX *,
                                             const void *, UINT, UINT, UINT);
static void STDMETHODCALLTYPE H_UpdateSubresource1(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub,
                                                   const D3D11_BOX *box, const void *src, UINT rp, UINT dp,
                                                   UINT fl) {
    if (g_cap) snapshot(r, src, box ? box->left : 0, box ? box->right - box->left : 0);
    ORIG(PFN_Update1, c, VT_Ctx_UpdateSubresource1)(c, r, sub, box, src, rp, dp, fl);
}

// ---------------------------------------------------------------------------------------------
// hotpatches: the runtime rewrites the context's Draw*/Dispatch*/UpdateSubresource* slots every
// frame, so these are hooked in the code of d3d11.dll itself (each function starts with the
// hot-patch prologue "mov edi,edi" and has 5 spare bytes in front of it). The runtime may use more
// than one implementation per slot; every one met in a slot gets its own variant hook.

enum { SL_DRAW, SL_DRAWIDX, SL_DRAWINST, SL_DRAWIDXINST, SL_DRAWINSTIND, SL_DRAWIDXINSTIND, SL_DISP, SL_DISPIND,
       SL_UPD, SL_UPD1, SL_N };
enum { HP_VARS = 4 };
static void *g_tr[SL_N][HP_VARS];     // trampolines (function + 2)
static void *g_hpAddr[SL_N][HP_VARS]; // patched functions
static const char *g_slName[SL_N] = {"Draw", "DrawIndexed", "DrawInstanced", "DrawIndexedInstanced",
                                     "DrawInstancedIndirect", "DrawIndexedInstancedIndirect", "Dispatch",
                                     "DispatchIndirect", "UpdateSubresource", "UpdateSubresource1"};

// what an UpdateSubresource writes (the data itself is passed on as it is)
static void updateCommon(ID3D11Resource *r, const D3D11_BOX *box, const void *&src) {
    if (!src) return;
    UINT csz = cbSize(r);
    if (csz == 48 && !box) frameCbSeen(r, src);
    int kind = eyeKind(csz);
    if (kind && !box) eyeLayoutFrom(kind, (const uint8_t *)src, csz);
    else if (kind && box && box->right > box->left && box->right <= csz) eyeLayoutFrom(kind, (const uint8_t *)src, box->right - box->left, box->left);
}

// ---------------------------------------------------------------------------------------------
#include "mw5.inc"

static int e2eCockpit(wchar_t *name, int n) {
    wcsncpy(name, g_mw5Name, n - 1);
    name[n - 1] = 0;
    return mw5Active() ? 1 : 0;
}
static void e2ePass(int e, int *v) {
    bool now = g_m5PassFrame[e] == g_frame;
    v[0] = now ? g_m5PassDraws[e] : 0, v[1] = now ? g_m5PassOrd[e] : 0, v[2] = now ? g_m5PassCk[e] : 0;
}

// CPU path: while in "3D" and an eye is active, draws with VS ad8cee09 run our replacement shader
// (same work + clip.x offset), its offset in our own constant buffer at b12; both restored after.

static ID3D11Buffer *g_eyeCB;
static float g_eyeCBval = 1e30f;

static void createEyeVS() {
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
    D3D11_BUFFER_DESC bd = {80, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    ID3D11Buffer *cb = nullptr;
    HRESULT hr2 = ORIG(PFN_CreateBuffer, g_dev, VT_Dev_CreateBuffer)(g_dev, &bd, nullptr, &cb);
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
            float v[20] = {};
            memcpy(v, g_K, 64);
            v[16] = g_near;
            v[17] = g_kTlOnly ? g_cockpitRange : 1e30f; // camera mode: the cockpit alone
            // our own cockpit instead of the game's: that is hidden. A large one (a large cockpit scale) reaches further
            // than the range its near parts are told by
            v[18] = (g_kTlOnly && mw5Active()) ? g_cockpitRange * max(1.0f, g_vr.cockpitScale / 1.9f) : 0.0f;
            memcpy(m.pData, v, sizeof v);
            ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, g_eyeCB, 0);
            cbFrame = g_tlEpoch;
        }
    }
    ORIG(PFN_SetShader, c, VT_Ctx_VSSetShader)(c, g_eyeVS, nullptr, 0);
    ORIG(PFN_SetCB, c, VT_Ctx_VSSetConstantBuffers)(c, 12, 1, &g_eyeCB);
    InterlockedIncrement(&g_statTlSwaps);
    return true;
}

static void eyeTlPost(ID3D11DeviceContext *c) {
    CtxState *s = ctxOf(c);
    ORIG(PFN_SetShader, c, VT_Ctx_VSSetShader)(c, s->st[ST_VS].obj, nullptr, 0);
    ID3D11Buffer *prev = (ID3D11Buffer *)s->st[ST_VS].cb[12];
    ORIG(PFN_SetCB, c, VT_Ctx_VSSetConstantBuffers)(c, 12, 1, &prev);
}

// The HUD pass with the wider layer texture (hud.inc): the vertex shaders turn pixels into clip space with
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
    if (!have || memcmp(last, d, 48)) {
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
        logf("hud: wide HUD pass running (g_screen.x %.6f -> %.6f, scissor rects %u, first right edge %ld)", d[0] * g_hudTexW / g_hudW, d[0], ns,
             ns ? sr[0].right : 0L);
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
// rest then show through behind the menu. While the menu is up (vr.inc tells: g_fadeMenu) the HUD's image is
// emptied right before the sheet is drawn, so it holds the sheet and the menu alone.
// Which draw is the sheet? The HUD's quads are indirect draws (their vertices are made on the GPU, nothing of them
// can be read here). So it is found by its effect: a point of the HUD image that is empty otherwise is read back
// after a draw - the draw that fills it is the sheet. In every frame anew: how many things the HUD draws before the
// sheet changes from frame to frame (a mark that blinks), and with one number kept for all frames the cross-hair
// flickered through the menu. The image is emptied before each draw from a little before where the sheet was last
// (before every draw, the first time) until the point is filled; that is a few read-backs a frame.
static int g_sheetFrom = -1; // the indirect draw of the HUD pass (from 1) the emptying begins with; -1: not known, from the first
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
static int g_sheetN, g_sheetTries; // indirect draws into the HUD's image in this frame; searches from the first draw in this menu
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
        // not found in the frame before: the sheet came earlier than looked for (or it fades out) - from the first draw again
        fails = g_sheetLook && !g_sheetFound ? fails + 1 : 0;
        if (fails >= 2) g_sheetFrom = -1, fails = 0;
        g_sheetFrame = g_frame, g_sheetN = 0, g_sheetFound = false;
        g_sheetLook = g_sheetFrom > 0 || g_sheetTries < 8; // (a search from the first draw reads back after every draw: some a menu, not for ever)
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
        logf("hud: the paused battle's sheet is indirect draw %d of the HUD pass (frame %u) - the HUD's image is emptied before it, found anew in every frame",
             g_sheetN, g_frame);
    }
    if (g_sheetFrom < 0 || from < g_sheetFrom) g_sheetFrom = from;
}

static void e2eState(int *v) {
    static unsigned last, lastFrame = ~0u;
    static int hud;
    v[0] = g_fadeMenu && !g_menuMode ? 1 : 0; // (a menu's own screen draws no HUD: the sheet's state is an old one there)
    v[1] = (int)(g_m5AllDraws - last);
    last = g_m5AllDraws;
    if (g_sheetFrame != lastFrame) lastFrame = g_sheetFrame, hud = g_sheetN; // (counted in the frame before: this one's HUD is still to come)
    v[2] = g_fadeMenu ? hud : 0;
    v[3] = g_fadeMenu && !g_menuMode && g_sheetCleanFrame == g_frame ? 1 : 0;
}

template <int V> static void STDMETHODCALLTYPE HP_Draw(ID3D11DeviceContext *c, UINT a, UINT b) {
    onWork(c, "Draw", false);
    ++g_m5AllDraws;
    if (eyeHidden(c)) return;
    if (smallCamBackdrop(c)) return;
    bool sw = eyeTlPre(c);
    int ck = sw && mechMeasuring() ? mechDraw(c, false, a, b, 0, g_camPassEye == 0) : 0;
    if (sw && g_mw5On) mw5Draw(c, (Mw5PFN_DrawIdx)g_tr[SL_DRAWIDX][V], ck);
    bool hw = hudWidePre(c);
    ((PFN_U2)g_tr[SL_DRAW][V])(c, a, b);
    if (hw) hudWidePost(c);
    if (sw) eyeTlPost(c);
}
template <int V> static void STDMETHODCALLTYPE HP_DrawIndexed(ID3D11DeviceContext *c, UINT a, UINT b, INT d) {
    onWork(c, "DrawIndexed", false);
    ++g_m5AllDraws;
    if (eyeHidden(c)) return;
    bool sw = eyeTlPre(c);
    int ck = sw && mechMeasuring() ? mechDraw(c, true, a, b, d, g_camPassEye == 0) : 0;
    if (sw && g_mw5On) mw5Draw(c, (Mw5PFN_DrawIdx)g_tr[SL_DRAWIDX][V], ck);
    bool hw = hudWidePre(c);
    ((PFN_U2I)g_tr[SL_DRAWIDX][V])(c, a, b, d);
    if (hw) hudWidePost(c);
    if (sw) eyeTlPost(c);
}
template <int V> static void STDMETHODCALLTYPE HP_DrawInstanced(ID3D11DeviceContext *c, UINT a, UINT b, UINT d, UINT e) {
    onWork(c, "DrawInstanced", false);
    if (eyeHidden(c)) return;
    bool sw = eyeTlPre(c);
    bool hw = hudWidePre(c);
    ((PFN_U4)g_tr[SL_DRAWINST][V])(c, a, b, d, e);
    if (hw) hudWidePost(c);
    if (sw) eyeTlPost(c);
}
template <int V>
static void STDMETHODCALLTYPE HP_DrawIndexedInstanced(ID3D11DeviceContext *c, UINT a, UINT b, UINT d, INT e, UINT f) {
    onWork(c, "DrawIndexedInstanced", false);
    if (eyeHidden(c)) return;
    bool sw = eyeTlPre(c);
    bool hw = hudWidePre(c);
    ((PFN_U3IU)g_tr[SL_DRAWIDXINST][V])(c, a, b, d, e, f);
    if (hw) hudWidePost(c);
    if (sw) eyeTlPost(c);
}
template <int V> static void STDMETHODCALLTYPE HP_DrawInstancedIndirect(ID3D11DeviceContext *c, ID3D11Buffer *b, UINT o) {
    onWork(c, "DrawInstancedIndirect", false);
    if (eyeHidden(c)) return;
    bool sheet = hudSheetPre(c);
    bool hw = hudWidePre(c);
    ((PFN_BU)g_tr[SL_DRAWINSTIND][V])(c, b, o);
    if (hw) hudWidePost(c);
    if (sheet) hudSheetPost(c);
}
template <int V>
static void STDMETHODCALLTYPE HP_DrawIndexedInstancedIndirect(ID3D11DeviceContext *c, ID3D11Buffer *b, UINT o) {
    onWork(c, "DrawIndexedInstancedIndirect", false);
    if (eyeHidden(c)) return;
    bool hw = hudWidePre(c);
    ((PFN_BU)g_tr[SL_DRAWIDXINSTIND][V])(c, b, o);
    if (hw) hudWidePost(c);
}
template <int V> static void STDMETHODCALLTYPE HP_Dispatch(ID3D11DeviceContext *c, UINT x, UINT y, UINT z) {
    onWork(c, "Dispatch", true);
    ((PFN_U3)g_tr[SL_DISP][V])(c, x, y, z);
}
template <int V> static void STDMETHODCALLTYPE HP_DispatchIndirect(ID3D11DeviceContext *c, ID3D11Buffer *b, UINT o) {
    onWork(c, "DispatchIndirect", true);
    ((PFN_BU)g_tr[SL_DISPIND][V])(c, b, o);
}
template <int V>
static void STDMETHODCALLTYPE HP_Upd(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub, const D3D11_BOX *box,
                                     const void *src, UINT rp, UINT dp) {
    if (!ctxOf(c)->foreign) updateCommon(r, box, src);
    if (g_cap) snapshot(r, src, box ? box->left : 0, box ? box->right - box->left : 0);
    ((PFN_Update)g_tr[SL_UPD][V])(c, r, sub, box, src, rp, dp);
}
template <int V>
static void STDMETHODCALLTYPE HP_Upd1(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub, const D3D11_BOX *box,
                                      const void *src, UINT rp, UINT dp, UINT fl) {
    if (!ctxOf(c)->foreign) updateCommon(r, box, src);
    if (g_cap) snapshot(r, src, box ? box->left : 0, box ? box->right - box->left : 0);
    ((PFN_Update1)g_tr[SL_UPD1][V])(c, r, sub, box, src, rp, dp, fl);
}

#define HP_ROW(f) {(void *)&f<0>, (void *)&f<1>, (void *)&f<2>, (void *)&f<3>}
static void *const g_hpHook[SL_N][HP_VARS] = {
    HP_ROW(HP_Draw), HP_ROW(HP_DrawIndexed), HP_ROW(HP_DrawInstanced), HP_ROW(HP_DrawIndexedInstanced),
    HP_ROW(HP_DrawInstancedIndirect), HP_ROW(HP_DrawIndexedInstancedIndirect), HP_ROW(HP_Dispatch),
    HP_ROW(HP_DispatchIndirect), HP_ROW(HP_Upd), HP_ROW(HP_Upd1)};

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

static void hotpatchScan(ID3D11DeviceContext *ctx) {
    if (!ctx || !g_real) return;
    static std::mutex mx;
    std::lock_guard<std::mutex> lk(mx);
    const int slots[SL_N] = {VT_Ctx_Draw, VT_Ctx_DrawIndexed, VT_Ctx_DrawInstanced, VT_Ctx_DrawIndexedInstanced,
                             VT_Ctx_DrawInstancedIndirect, VT_Ctx_DrawIndexedInstancedIndirect, VT_Ctx_Dispatch,
                             VT_Ctx_DispatchIndirect, VT_Ctx_UpdateSubresource, VT_Ctx_UpdateSubresource1};
    void **vt = *(void ***)ctx;
    for (int s = 0; s < SL_N; ++s) {
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

typedef void(STDMETHODCALLTYPE *PFN_OMSet)(ID3D11DeviceContext *, UINT, ID3D11RenderTargetView *const *,
                                           ID3D11DepthStencilView *);
static void STDMETHODCALLTYPE H_OMSetRenderTargets(ID3D11DeviceContext *c, UINT n,
                                                   ID3D11RenderTargetView *const *rtv,
                                                   ID3D11DepthStencilView *dsv) {
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
                                            ID3D11DepthStencilView *, UINT, UINT,
                                            ID3D11UnorderedAccessView *const *, const UINT *);
static void STDMETHODCALLTYPE H_OMSetRenderTargetsAndUAVs(ID3D11DeviceContext *c, UINT n,
                                                          ID3D11RenderTargetView *const *rtv,
                                                          ID3D11DepthStencilView *dsv, UINT us, UINT un,
                                                          ID3D11UnorderedAccessView *const *uav,
                                                          const UINT *ic) {
    if (g_cap && n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) setRTs(ctxOf(c), n, rtv, dsv);
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
static float hudShift() {
    int mode = g_eyeMode;
    float px = g_hudPx;
    if (!mode || px == 0) return 0;
    float sign = mode == 1 ? 1.0f : mode == 2 ? -1.0f : ((g_frame & 1) ? -1.0f : 1.0f); // +1 right eye
    return -sign * px; // the right eye's HUD to the left: in front of infinity
}
static void applyViewports(ID3D11DeviceContext *c, CtxState *s, bool shifted);
static void STDMETHODCALLTYPE H_RSSetViewports(ID3D11DeviceContext *c, UINT n, const D3D11_VIEWPORT *v) {
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

static void applyViewports(ID3D11DeviceContext *c, CtxState *s, bool shifted) {
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

typedef void(STDMETHODCALLTYPE *PFN_Exec)(ID3D11DeviceContext *, ID3D11CommandList *, BOOL);
#include "cross.inc"

static void STDMETHODCALLTYPE H_ExecuteCommandList(ID3D11DeviceContext *c, ID3D11CommandList *l, BOOL keep) {
    InterlockedIncrement(&g_statExec);
    if (g_cap) {
        CtxState *s = ctxOf(c);
        flushPending(s);
        capLine(s, "ExecuteCommandList %p", l);
    }
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
        ok = h && camRead(h + HK_AT_WATER_CHECK, code, 7) && code[0] == 0x80 && code[1] == 0x3d && memcmp(code + 2, &at, 4) == 0 && code[6] == 0;
        logf("water: reflection for both eyes %s", ok ? "on (mw4hook's code as known)" : "off - mw4hook's code is not the known one");
        if (!ok) return;
    }
    uint8_t *flag = h + HK_WATER_REFLECT;
    if (g_camPassEye == 0) left = *flag;
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
    if (g_lblDump && g_camInMain && s->depth <= 6) { // the labels of both eyes' passes, once (cam.inc)
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
            if (v && swscanf_s(v, L"viewport %d,%d %dx%d", &x, &y, &w, &hh) == 4 && w > 0 && (UINT)w * 10 < g_hudW * 7) {
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
    if (!(fl & DXGI_PRESENT_TEST)) onPresent(sc);
    DWORD t1 = GetTickCount();
    HRESULT hr = ORIG(PFN_Present, sc, VT_Sc_Present)(sc, si, fl);
    g_presOurs = t1 - t0, g_presReal = GetTickCount() - t1;
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
                                                          const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fs,
                                                          IDXGIOutput *o, IDXGISwapChain1 **out) {
    DXGI_SWAP_CHAIN_DESC1 d2;
    if (d && g_renderOverride) { // larger than its window: shown stretched to it
        d2 = *d;
        d2.Scaling = DXGI_SCALING_STRETCH;
        d = &d2;
    }
    HRESULT hr = ORIG(PFN_CSCHwnd, f, VT_Fac_CreateSwapChainForHwnd)(f, dev, w, d, fs, o, out);
    if (d)
        logf("CreateSwapChainForHwnd: %ux%u fmt %u buffers %u effect %u flags 0x%x -> 0x%08lx", d->Width,
             d->Height, (unsigned)d->Format, d->BufferCount, (unsigned)d->SwapEffect, d->Flags, hr);
    if (SUCCEEDED(hr) && out && *out) hookSwapChain(*out);
    return hr;
}
typedef HRESULT(STDMETHODCALLTYPE *PFN_CSC)(IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **);
static HRESULT STDMETHODCALLTYPE H_CreateSwapChain(IDXGIFactory *f, IUnknown *dev, DXGI_SWAP_CHAIN_DESC *d,
                                                   IDXGISwapChain **out) {
    HRESULT hr = ORIG(PFN_CSC, f, VT_Fac_CreateSwapChain)(f, dev, d, out);
    if (d)
        logf("CreateSwapChain: %ux%u fmt %u buffers %u effect %u -> 0x%08lx", d->BufferDesc.Width,
             d->BufferDesc.Height, (unsigned)d->BufferDesc.Format, d->BufferCount, (unsigned)d->SwapEffect, hr);
    if (SUCCEEDED(hr) && out && *out) hookSwapChain(*out);
    return hr;
}

// ---------------------------------------------------------------------------------------------
// installing

static int hookContext(ID3D11DeviceContext *ctx, bool deferred, bool repair) {
    CtxState *s = ctxOf(ctx);
    s->deferred = deferred;
    int n = 0;
    std::string names;
#define PT(obj, idx, hook)                                                                         \
    if (patch(obj, idx, (void *)(hook))) {                                                         \
        ++n;                                                                                       \
        names += " " #idx;                                                                         \
        if (repair && g_frame < 8) names += "(was " + moduleOf(g_lastReplaced) + ")";              \
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
            logf("mouse: cursor scaled by %.3f x %.3f about %ld,%ld (mw4hook's picture %ld,%ld - %ld,%ld, known %d)", kx, ky, o.x, o.y,
                 pic[0], pic[1], pic[2], pic[3], (int)*(g_mouseBase + HK_PICTURE_KNOWN));
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

static void mouseInstall() {
    if (g_mouseDone) return;
    uint8_t *base = hkBase();
    if (!base) return;
    __try {
        void **pg = (void **)(base + HK_PTR_GETCURSORPOS), **ps = (void **)(base + HK_PTR_SETCURSORPOS), **pc = (void **)(base + HK_PTR_CLIPCURSOR);
        // this build? "mov eax, [mw4hook+0x4c7310]" in its GetCursorPos, "mov eax, [mw4hook+0x4c7318]" in its clip
        const uint8_t *i1 = base + HK_AT_GETCURSORPOS, *i2 = base + HK_AT_CLIPCURSOR;
        if (i1[0] != 0xA1 || *(uint32_t *)(i1 + 1) != (uint32_t)(uintptr_t)pg || i2[0] != 0xA1 || *(uint32_t *)(i2 + 1) != (uint32_t)(uintptr_t)pc) {
            logf("mouse: mw4hook's cursor mapping is not where it was - the cursor is not corrected for the render size");
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
static void mouseCapture() {
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
        logf("focus: in front now: %s '%ls' (%ls), cursor at %ld,%ld, frame %u", fg == w ? "the game" : "NOT the game", t, cls, cp.x, cp.y, g_frame);
    }
    static bool held;
    bool hold = want && w && fg == w && !g_menuMode && g_srcCamActive;
    if (hold) {
        RECT r;
        POINT o = {0, 0};
        if (GetClientRect(w, &r) && r.right > 64 && r.bottom > 64 && ClientToScreen(w, &o)) {
            RECT c = {o.x + 8, o.y + 8, o.x + r.right - 8, o.y + r.bottom - 8};
            ClipCursor(&c); // every frame: other programs and the game itself lift it
            if (!held) logf("mouse: cursor kept inside the game's window (%ld,%ld - %ld,%ld)", c.left, c.top, c.right, c.bottom);
            held = true;
        }
    } else if (held) {
        held = false;
        if (fg == w || !fg) ClipCursor(nullptr);
        logf("mouse: cursor free again (%s)", fg != w ? "another window in front" : "no battle");
    }
}

static void menuModeUpdate() {
    static uint8_t *base;
    if (!base) base = hkBase();
    if (!base) return;
    __try {
        const LONG *mode = (const LONG *)(base + HK_SCREEN_MODE); // the game's screen mode (what the cursor is mapped to)
        const RECT *out = (const RECT *)(base + HK_OUT_RECT);  // mw4hook's output size
        LONG mw = mode[0], mh = mode[1], ow = out->right, oh = out->bottom;
        bool menu = false;
        if (mw >= 320 && mh >= 200 && mw <= 16384 && mh <= 16384 && ow > 0 && oh > 0) {
            float ma = (float)mw / mh, oa = (float)ow / oh;
            menu = fabsf(ma - 4.0f / 3.0f) < 0.02f && fabsf(oa - 4.0f / 3.0f) > 0.05f;
        }
        static LONG lw, lh;
        if (mw != lw || mh != lh) {
            lw = mw, lh = mh;
            logf("mode: the game's screen mode is %ld x %ld (output %ld x %ld) - %s", mw, mh, ow, oh, menu ? "a menu" : "a battle");
        }
        g_menuMode = menu;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
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
            logf("render: mw4hook's output size (%ld x %ld) is not its window's - render_height ignored", rc->right, rc->bottom);
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
            logf("render: mw4hook's picture size (%ld x %ld, check %d) is not its window's - the battles may stay at the window's size",
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
                                                 const D3D_FEATURE_LEVEL *fls, UINT nfl, UINT sdk,
                                                 ID3D11Device **dev, D3D_FEATURE_LEVEL *ofl,
                                                 ID3D11DeviceContext **ctx) {
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

extern "C" HRESULT WINAPI Hook_D3D11CreateDeviceAndSwapChain(IDXGIAdapter *a, D3D_DRIVER_TYPE dt, HMODULE sw,
                                                             UINT fl, const D3D_FEATURE_LEVEL *fls, UINT nfl,
                                                             UINT sdk, const DXGI_SWAP_CHAIN_DESC *scd,
                                                             IDXGISwapChain **sc, ID3D11Device **dev,
                                                             D3D_FEATURE_LEVEL *ofl, ID3D11DeviceContext **ctx) {
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
        logf("MW4VR v11.70 (debug level %d) loaded into %s", g_dbg, narrow(exe).c_str());
        if (!loadReal()) logf("ERROR: the system d3d11.dll could not be loaded");
    }
    return TRUE;
}
