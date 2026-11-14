// MW4VR probe - a d3d11.dll proxy that observes how MW4Engine (mw4hook.dll) renders.
//
// Goal: find out, without touching the engine, where the camera lives (constant buffers,
// reflected variable names), how a frame is structured (annotation labels, render targets,
// passes, deferred contexts) and where the swap chain is presented. Nothing is modified.
//
// Output (next to the DLL, i.e. the game folder; opened shared, readable while the game runs):
//   mw4vr_probe.log        - device/swap chain/context creation, periodic stats, label changes
//   mw4vr_shaders.txt      - every created shader with its constant buffers and variables
//   mw4vr_frame_NNN.txt    - one fully traced frame (Ctrl+F11, or create mw4vr_capture.txt)
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
static std::unordered_map<void *, UINT> g_dynSize; // small dynamic non-constant buffers
static unsigned g_shaderCount;

typedef HRESULT(WINAPI *PFN_D3DReflect)(LPCVOID, SIZE_T, REFIID, void **);
static PFN_D3DReflect g_reflect;
typedef HRESULT(WINAPI *PFN_D3DDisassemble)(LPCVOID, SIZE_T, UINT, LPCSTR, ID3DBlob **);
typedef HRESULT(WINAPI *PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT,
                                        ID3DBlob **, ID3DBlob **);
static PFN_D3DCompile g_compile;
static ID3D11Device *g_dev;
static ID3D11VertexShader *g_eyeVS; // replacement for the CPU path's screen-space vertex shader
static const uint64_t TL_HASH = 0xad8cee09478ddb12ull;
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
        CreateDirectoryW((g_dir + L"mw4vr_shaders").c_str(), nullptr);
        if (!g_reflect) logf("reflection unavailable (d3dcompiler_47.dll / D3DReflect missing)");
    }
    auto *si = new ShaderInfo();
    si->stage = stage;
    si->hash = fnv(code, len);
    {   // keep every shader: bytecode and disassembly, once per hash
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
    if (stage == 'V' && si->hash == TL_HASH && !g_eyeVS) createEyeVS();
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

static std::unordered_map<void *, UINT> g_bufSize; // small non-constant, non-vertex/index buffers
static UINT bufSize(void *b) {
    std::lock_guard<std::mutex> lk(g_shMx);
    auto it = g_bufSize.find(b);
    return it == g_bufSize.end() ? 0 : it->second;
}

static UINT dynSize(void *b) {
    std::lock_guard<std::mutex> lk(g_shMx);
    auto it = g_dynSize.find(b);
    return it == g_dynSize.end() ? 0 : it->second;
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

static std::mutex g_cpMx;
static std::unordered_map<void *, std::vector<UINT>> g_camPosBufs;
static float g_right[3];
static volatile bool g_haveRight, g_applyCamPos = true, g_scanOn = true;
static volatile LONG g_statCamPos;

static void registerCamPos(CtxState *s, int st) {
    ShaderInfo *sh = s->st[st].sh;
    if (!sh || sh->camPos.empty()) return;
    for (size_t i = 0; i < sh->camPos.size(); ++i) {
        UINT slot = sh->camPos[i].first, off = sh->camPos[i].second;
        if (slot >= CB_SLOTS) continue;
        void *buf = s->st[st].cb[slot];
        if (!buf) continue;
        off += s->st[st].cbOff[slot];
        thread_local std::unordered_set<uint64_t> known;
        uint64_t k = ((uint64_t)(uintptr_t)buf << 16) ^ off;
        if (!known.insert(k).second) continue;
        std::lock_guard<std::mutex> lk(g_cpMx);
        auto &v = g_camPosBufs[buf];
        bool have = false;
        for (UINT o : v) have |= o == off;
        if (!have) {
            v.push_back(off);
            logf("eye: camera position %s (cb b%u, byte %u) in buffer %p", sh->camPosNames[i].c_str(), slot, off, buf);
        }
    }
}

static bool camPosOffsets(void *buf, std::vector<UINT> &out) {
    std::lock_guard<std::mutex> lk(g_cpMx);
    auto it = g_camPosBufs.find(buf);
    if (it == g_camPosBufs.end()) return false;
    out = it->second;
    return true;
}

static volatile LONG g_statTlSwaps;
static std::mutex g_hideMx;
static std::vector<uint64_t> g_vsFrame, g_vsList; // vertex shaders drawn in "3D": this frame, last frame
static volatile uint64_t g_hideHash;
static volatile int g_eyeMode; // 0 off, 1 right eye, 2 left eye, 3 alternating every frame
static thread_local unsigned t_patches; // data changes made by the eye patches on this thread
static std::mutex g_patchedMx;
static std::unordered_map<void *, unsigned> g_patchedAt; // buffer -> frame of its last eye patch
static std::unordered_map<void *, int> g_lastWrite;      // buffer -> 1..5 map type, 10 update whole, 11 update box
static void markPatched(void *buf);
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
static void mw5HudCopy(ID3D11DeviceContext *, ID3D11Texture2D *);
static bool g_renderOverride;          // the game renders larger than its window ([vr] render_height)
static void *volatile g_foreignDev;    // the VR runtime's device (shares the vtable with the game's)
static volatile bool g_inHud;          // inside the top-level "after 3D: HUD, 2D" label
static volatile bool g_smallCamSide;   // the small camera being drawn is a window at the picture's side (the target's view), not the zoom window
static volatile float g_hudPx;         // HUD shift per eye in back-buffer pixels (VR), 0 = none // VR session running: the eyes alternate, offset from [vr]
static volatile bool g_in3D; // inside the top-level "3D" label: the world, not HUD or menus
static volatile bool g_in3DSeen; // a "3D" label was seen (a battle is running)

struct AuditEntry {
    unsigned draws = 0, uncovered = 0;
    std::unordered_map<std::string, unsigned> where; // label path -> uncovered draws
};
static std::mutex g_auditMx;
static std::unordered_map<uint64_t, AuditEntry> g_audit;
static DWORD g_auditT0;
static unsigned g_auditFrames;

static void markPatched(void *buf) {
    std::lock_guard<std::mutex> lk(g_patchedMx);
    g_patchedAt[buf] = g_frame;
}
static bool recentlyPatched(void *buf) {
    if (!buf) return false;
    std::lock_guard<std::mutex> lk(g_patchedMx);
    auto it = g_patchedAt.find(buf);
    return it != g_patchedAt.end() && g_frame - it->second <= 2;
}

// a draw in "3D" with an eye active whose vertex shader reads nothing the eye patches touched
static void auditDraw(CtxState *s) {
    ShaderInfo *sh = s->st[ST_VS].sh;
    if (!sh) return;
    bool covered = sh->hash == TL_HASH; // the CPU path runs the replacement shader
    std::string detail;
    bool camCb = false;
    if (!covered) {
        bool all = true;
        for (auto &cb : sh->cbs) {
            bool clipMat = false; // a 4x4 world/object -> clip matrix (not a position or jitter)
            for (int vi : cb.camVars) {
                std::string l = cb.vars[vi].name;
                for (auto &ch : l) ch = (char)tolower((unsigned char)ch);
                if (cb.vars[vi].rows == 4 && cb.vars[vi].cols == 4 && l.find("toclip") != std::string::npos &&
                    l.find("prev") == std::string::npos) clipMat = true;
            }
            if (!clipMat || cb.slot >= CB_SLOTS) continue;
            camCb = true;
            void *b = s->st[ST_VS].cb[cb.slot];
            bool ok = recentlyPatched(b);
            if (!ok) {
                all = false;
                char t[160];
                int lw = -1;
                {
                    std::lock_guard<std::mutex> lk(g_patchedMx);
                    auto it = g_lastWrite.find(b);
                    if (it != g_lastWrite.end()) lw = it->second;
                }
                D3D11_BUFFER_DESC bd = {};
                if (b) ((ID3D11Buffer *)b)->GetDesc(&bd);
                snprintf(t, sizeof t, " %s(b%u): buffer %u B usage %d, bound at +%u, last write %s", cb.name.c_str(),
                         cb.slot, bd.ByteWidth, (int)bd.Usage, s->st[ST_VS].cbOff[cb.slot],
                         lw < 0 ? "never seen" : lw == 10 ? "update" : lw == 11 ? "update box" :
                         lw == 4 ? "map discard" : lw == 5 ? "map no-overwrite" : "map other");
                detail += t;
            }
        }
        if (camCb) covered = all;
        for (size_t i = 0; i < sh->srvSlots.size() && !covered && !camCb; ++i)
            if (sh->srvSlots[i] < 64) covered = recentlyPatched(s->st[ST_VS].srvBuf[sh->srvSlots[i]]);
    }
    std::lock_guard<std::mutex> lk(g_auditMx);
    AuditEntry &e = g_audit[sh->hash];
    ++e.draws;
    if (covered) return;
    ++e.uncovered;
    std::string path;
    for (size_t i = 1; i < s->labels.size() && i < 4; ++i) {
        if (!path.empty()) path += " > ";
        path += s->labels[i];
    }
    if (!detail.empty()) path += "  |" + detail;
    ++e.where[path];
}

static void auditOnPresent() {
    if (!g_eyeMode) {
        g_auditT0 = 0;
        return;
    }
    DWORD now = GetTickCount();
    if (!g_auditT0) g_auditT0 = now;
    ++g_auditFrames;
    if (now - g_auditT0 < 10000) return;
    // every 10 s with an eye active: the report, rewritten
    std::vector<std::pair<uint64_t, AuditEntry>> rows;
    {
        std::lock_guard<std::mutex> lk(g_auditMx);
        for (auto &kv : g_audit) rows.push_back(kv);
        g_audit.clear();
    }
    std::sort(rows.begin(), rows.end(), [](auto &a, auto &b) { return a.second.uncovered > b.second.uncovered; });
    FILE *f = _wfsopen((g_dir + L"mw4vr_audit.txt").c_str(), L"w", _SH_DENYNO);
    if (f) {
        fprintf(f, "MW4VR audit over %u frames: vertex shaders drawn in \"3D\" and how many of their draws read nothing the eye patches moved\n\n", g_auditFrames);
        for (auto &r : rows) {
            ShaderInfo *sh = nullptr;
            {
                std::lock_guard<std::mutex> lk(g_shMx);
                for (auto &kv : g_shaders)
                    if (kv.second->hash == r.first && kv.second->stage == 'V') sh = kv.second;
            }
            fprintf(f, "%s VS %016llx  draws %u, uncovered %u\n", r.second.uncovered ? "GAP" : "ok ",
                    (unsigned long long)r.first, r.second.draws, r.second.uncovered);
            if (sh) {
                std::string cbs;
                for (auto &cb : sh->cbs) cbs += " " + cb.name + "(b" + std::to_string(cb.slot) + ")";
                fprintf(f, "    cbuffers:%s\n    resources:%s\n", cbs.empty() ? " -" : cbs.c_str(),
                        sh->res.empty() ? " -" : sh->res.c_str());
            }
            for (auto &w : r.second.where) fprintf(f, "    %5u x  %s\n", w.second, w.first.c_str());
        }
        fclose(f);
    }
    logf("audit: mw4vr_audit.txt written (%u frames, %u vertex shaders)", g_auditFrames, (unsigned)rows.size());
    g_auditT0 = now;
    g_auditFrames = 0;
}

static void onWork(void *ctx, const char *kind, bool compute) {
    if (ctxOf(ctx)->foreign) return;
    CtxState *s = ctxOf(ctx);
    extern volatile bool g_srcCamActive;
    if (!compute && g_in3D && g_eyeMode && !g_srcCamActive) auditDraw(s);
    if (!compute && g_in3D && s->st[ST_VS].sh) {
        uint64_t h = s->st[ST_VS].sh->hash;
        std::lock_guard<std::mutex> lk(g_hideMx);
        bool have = false;
        for (uint64_t x : g_vsFrame) have |= x == h;
        if (!have) g_vsFrame.push_back(h);
    }
    if (compute) {
        registerCamPos(s, ST_CS);
    } else {
        registerCamPos(s, ST_VS);
        registerCamPos(s, ST_PS);
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
// eye offset experiment: shift the camera sideways by moving the forward (world/mesh -> clip)
// matrices in clip space. For a camera moved by e along its own x axis, clip.x' = clip.x - P00*e
// for every point (a constant added to the x row's translation), so one float per matrix changes.
// Inverse matrices (lighting reconstruction) stay as they are - slightly wrong light, fine for a test.

enum { EK_NONE, EK_MLR, EK_VIEW, EK_CARDS, EK_REFLECT, EK_SCAN, EK_FOG, EK_WATER, EK_CLUSTERS, EK_N };
static const char *g_ekName[EK_N] = {"-", "Mlr", "View", "Cards", "Reflect", "scan", "Fog", "Water", "Clusters"};
static volatile LONG g_statKindMaps[EK_N], g_statKindPatched[EK_N];
static volatile LONG g_mlrPath[4]; // Mlr updates: map discard, map other, update whole, update box
static float g_eyeOffsetM = 1.0f;     // metres from the centre (exaggerated by default to be visible)
static bool g_eyeApply[EK_N] = {false, true, true, true, false, true, true, true, true};
static volatile bool g_eyeInverse = true; // also correct the clip->world matrices
static int g_xIdx = -1;               // float index of the clip.x translation term (12 column-major, 3 row-major)
static float g_P00, g_P11;            // projection scales, from g_vWorldToClip
static int g_layoutLogs;
#include "head.inc"

static int eyeKind(UINT sz) {
    switch (sz) {
    case 496: return EK_MLR;     // cb 'Mlr': g_mlrToClip@0, g_mlrPrevToClip@432
    case 928: return EK_VIEW;    // cb 'View': g_vWorldToClip@480, g_vSsgiPrevClip@864
    case 2144: return EK_CARDS;  // cb 'Cards': g_cardToClip@0
    case 416: return EK_REFLECT; // cb 'Reflect': g_rToClip@0
    case 272: return EK_FOG;     // cb 'Fog': g_fClipToWorld@176
    case 2016: return EK_WATER;  // cb 'Water': g_wClipToWorld@160
    case 336: return EK_CLUSTERS; // cb 'Clusters': g_clView@0 (3x4), g_clToWorld@48 (3x4), g_clWorldToMlr@224
    }
    return EK_NONE;
}

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
static void eyePatch(int kind, uint8_t *data, UINT sz, UINT base = 0) {
    if (kind == EK_VIEW && base <= 480 && base + sz >= 480 + 64) eyeLearnLayout((const float *)(data + 480 - base));
    if (!kReadyWorld() || !g_eyeApply[kind]) return;
    // forward (world/mesh -> clip) matrices
    static const int fwd[EK_N][2] = {{-1, -1}, {0, 432}, {480, 864}, {0, -1}, {0, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};
    for (int i = 0; i < 2; ++i) {
        int off = fwd[kind][i];
        if (off < 0 || (UINT)off < base || off + 64 > base + sz) continue;
        kFwd((float *)(data + off - base));
        ++t_patches;
    }
    // inverse (clip -> world) matrices used to rebuild positions from depth (lighting, shadows, fog, water)
    static const int inv[EK_N] = {-1, -1, 0, -1, -1, -1, 176, 160, -1};
    int io = inv[kind];
    if (g_eyeInverse && io >= 0 && (UINT)io >= base && io + 64 <= base + sz) kInv((float *)(data + io - base));
    // the light clusters (and the effects drawn with them): V = g_clView (3x4 world -> view, rows may be
    // scaled: V = S Vu), W = g_clToWorld (3x4, V^-1), C = g_clWorldToMlr (4x4 world -> cluster projection).
    // The new view Vu' = Hv Vu: V' = (S Hv S^-1) V, W' = W (S Hv^-1 S^-1), C' = C W (S Hv S^-1) V
    if (kind == EK_CLUSTERS && base == 0 && sz >= 288) {
        float *v = (float *)data, *w = (float *)(data + 48), *c = (float *)(data + 224);
        float V[16], W[16];
        memcpy(V, v, 48);
        memcpy(W, w, 48);
        V[12] = V[13] = V[14] = W[12] = W[13] = W[14] = 0;
        V[15] = W[15] = 1;
        float sc[3];
        for (int i = 0; i < 3; ++i) sc[i] = len3(V[i * 4], V[i * 4 + 1], V[i * 4 + 2]);
        float Hs[16], His[16];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float si = i < 3 ? sc[i] : 1, sj = j < 3 ? sc[j] : 1;
                Hs[i * 4 + j] = g_Hv[i * 4 + j] * si / (j < 3 ? sj : 1);
                His[i * 4 + j] = g_HvInv[i * 4 + j] * si / (j < 3 ? sj : 1);
            }
        float Vn[16], Wn[16], G[16];
        m4mul(Hs, V, Vn);
        m4mul(W, His, Wn);
        m4mul(W, Vn, G);
        float Cn[16];
        m4mul(c, G, Cn);
        memcpy(v, Vn, 48);
        memcpy(w, Wn, 48);
        memcpy(c, Cn, 64);
        ++t_patches;
    }
    InterlockedIncrement(&g_statKindPatched[kind]);
}

// other buffers (e.g. the clutter's structured g_ctView) may carry a copy of the camera's
// world->clip matrix: find row-major 4x4s with the same projection (|row0| = P00, |row1| = P11,
// |row3| = 1) anywhere in a rewritten dynamic buffer and move them as well
static std::mutex g_scanMx;
static std::unordered_set<std::string> g_scanSeen;
// is m a world/object -> clip matrix of this camera at some uniform object scale k?
// |row0| = P00 k, |row1| = P11 k, |row3| = k, row2 (reversed infinite depth) nearly 0 - squared, no roots
static inline bool camMat(const float *m, float &k2) {
    float r3 = m[12] * m[12] + m[13] * m[13] + m[14] * m[14];
    if (!(r3 > 1e-4f && r3 < 1e10f)) return false; // object scale 0.01 .. 1e5
    float p0 = g_P00 * g_P00, p1 = g_P11 * g_P11;
    float r0 = m[0] * m[0] + m[1] * m[1] + m[2] * m[2];
    if (fabsf(r0 - p0 * r3) > 0.02f * p0 * r3) return false;
    float r1 = m[4] * m[4] + m[5] * m[5] + m[6] * m[6];
    if (fabsf(r1 - p1 * r3) > 0.02f * p1 * r3) return false;
    float r2 = m[8] * m[8] + m[9] * m[9] + m[10] * m[10];
    if (r2 > 1e-4f * r3) return false;
    k2 = r3;
    return true;
}

// per buffer: where its matrices were (first + k * stride), or that it has none (skipped a while)
struct ScanCache {
    UINT phase = 0, stride = 0;   // instance records: matrices at phase + k * stride
    std::vector<UINT> offs;       // irregular: the places of the last full scan
    unsigned fullAt = 0, skipUntil = 0;
    bool valid = false;
};
static std::unordered_map<void *, ScanCache> g_scanCache;
static volatile LONG g_statFullScans;

static UINT gcdU(UINT a, UINT b) {
    while (b) {
        UINT t = a % b;
        a = b;
        b = t;
    }
    return a;
}

// finds the camera matrices. Regular buffers (instance records) are checked on their grid every
// time, so a changing number of instances is followed; irregular ones at the places found by the
// last full scan; a full scan when nothing is found there, or every 120 frames.
static void eyeFind(const uint8_t *data, UINT sz, void *key, std::vector<UINT> &out, float &k2first) {
    out.clear();
    if (g_xIdx != 3 || g_P00 <= 0 || g_P11 <= 0 || sz < 64) return;
    const float *f = (const float *)data;
    std::lock_guard<std::mutex> lk(g_scanMx);
    ScanCache &e = g_scanCache[key];
    if (e.skipUntil > g_frame) return;
    float k2 = 0;
    auto hit = [&](UINT o) {
        if (o + 64 <= sz && camMat(f + o / 4, k2)) {
            if (out.empty()) k2first = k2;
            out.push_back(o);
        }
    };
    if (e.valid && g_frame - e.fullAt < 120) {
        if (e.stride)
            for (UINT o = e.phase; o + 64 <= sz; o += e.stride) hit(o);
        else
            for (UINT o : e.offs) hit(o);
        if (!out.empty()) return;
    }
    InterlockedIncrement(&g_statFullScans);
    UINT n = sz / 4;
    for (UINT i = 0; i + 16 <= n; ++i)
        if (camMat(f + i, k2)) {
            if (out.empty()) k2first = k2;
            out.push_back(i * 4);
            i += 15;
        }
    e.fullAt = g_frame;
    e.valid = !out.empty();
    e.offs = out;
    e.stride = 0;
    if (out.size() >= 3) {
        UINT g = 0;
        for (size_t j = 1; j < out.size(); ++j) g = gcdU(g, out[j] - out[0]);
        if (g >= 64) {
            e.stride = g;
            e.phase = out[0] % g;
        }
    }
    if (out.empty()) e.skipUntil = g_frame + 120;
}

// data is patched in place (mayPatch) where camera matrices are found; returns how many
static int eyeScan(uint8_t *data, UINT sz, void *key, const char *how = "?", bool mayPatch = true) {
    thread_local std::vector<UINT> offs;
    float k2 = 0;
    eyeFind(data, sz, key, offs, k2);
    InterlockedIncrement(&g_statKindMaps[EK_SCAN]);
    if (offs.empty()) return 0;
    if (kReadyWorld() && mayPatch && g_eyeApply[EK_SCAN]) {
        for (UINT o : offs) kFwd((float *)(data + o));
        ++t_patches;
        InterlockedIncrement(&g_statKindPatched[EK_SCAN]);
    }
    char key2[64];
    snprintf(key2, sizeof key2, "%u/%s", sz, how);
    bool fresh;
    {
        std::lock_guard<std::mutex> lk(g_scanMx);
        fresh = g_scanSeen.insert(key2).second;
    }
    if (fresh)
        logf("eye: %u camera matrices in a %u-byte buffer (%s), first at byte %u (object scale %.3f)",
             (unsigned)offs.size(), sz, how, offs[0], sqrtf(k2));
    return (int)offs.size();
}

// data holds the buffer bytes [base, base + sz)
static void camPosPatch(uint8_t *data, UINT sz, UINT base, const std::vector<UINT> &offs) {
    if (!kReadyWorld() || !g_applyCamPos || !g_haveRight) return;
    const float *t = g_headT;
    for (UINT o : offs) {
        if (o < base || o + 12 > base + sz) continue;
        float *f = (float *)(data + o - base);
        ++t_patches;
        for (int i = 0; i < 3; ++i) f[i] += t[0] * g_right[i] + t[1] * g_up[i] + t[2] * g_fwd[i];
    }
    InterlockedIncrement(&g_statCamPos);
}

// constant buffers without a known layout (e.g. 'Sun' with g_worldToShadow holding the camera's
// matrix for effects): every 16-byte position checked for a camera matrix, moved where found
static volatile LONG g_statCbScanHits;
static std::unordered_set<std::string> g_cbScanSeen;
static int eyeScanCB(const uint8_t *data, UINT sz, UINT base, float *patchOut) {
    if (g_xIdx != 3 || g_P00 <= 0 || g_P11 <= 0 || sz < 64) return 0;
    bool on = kReadyWorld();
    int found = 0;
    float k2;
    for (UINT o = (16 - base % 16) % 16; o + 64 <= sz; o += 16) {
        if (!camMat((const float *)(data + o), k2)) continue;
        ++found;
        if (patchOut && on) {
            kFwd((float *)((uint8_t *)patchOut + o));
            ++t_patches;
        }
        char key[48];
        snprintf(key, sizeof key, "%u@%u", sz, base + o);
        bool fresh;
        {
            std::lock_guard<std::mutex> lk(g_scanMx);
            fresh = g_cbScanSeen.insert(key).second;
        }
        if (fresh) logf("eye: camera matrix in a %u-byte constant buffer at byte %u (object scale %.3f)", sz, base + o, sqrtf(k2));
        o += 48;
    }
    if (found) InterlockedIncrement(&g_statCbScanHits);
    return found;
}

static void eyeReadIni() {
    std::wstring p = g_dir + L"mw4vr.ini";
    if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
        FILE *f = _wfsopen(p.c_str(), L"w", _SH_DENYNO);
        if (f) {
            fputs("; MW4VR probe - eye offset experiment. Ctrl+F7 cycles: off -> right eye -> left eye -> alternating.\n"
                  "; Read again every 2 seconds while the game runs.\n"
                  "[eye]\n"
                  "; distance of the eye from the centre in metres (real: 0.032; large values make the shift obvious)\n"
                  "offset_m=1.0\n"
                  "; which matrices are moved: meshes and terrain (Mlr), the deferred view (View), particles (Cards),\n"
                  "; water reflections (Reflect)\n"
                  "apply_mlr=1\napply_view=1\napply_cards=1\napply_reflect=0\n"
                  "; start mode: 0 off, 1 right, 2 left, 3 alternating\n"
                  "mode=0\n",
                  f);
            fclose(f);
        }
    }
    wchar_t b[64];
    GetPrivateProfileStringW(L"eye", L"offset_m", L"1.0", b, 64, p.c_str());
    float off = (float)_wtof(b);
    g_eyeApply[EK_MLR] = GetPrivateProfileIntW(L"eye", L"apply_mlr", 1, p.c_str()) != 0;
    g_eyeApply[EK_VIEW] = GetPrivateProfileIntW(L"eye", L"apply_view", 1, p.c_str()) != 0;
    g_eyeApply[EK_CARDS] = GetPrivateProfileIntW(L"eye", L"apply_cards", 1, p.c_str()) != 0;
    g_eyeApply[EK_REFLECT] = GetPrivateProfileIntW(L"eye", L"apply_reflect", 0, p.c_str()) != 0;
    g_eyeApply[EK_SCAN] = GetPrivateProfileIntW(L"eye", L"apply_scan", 1, p.c_str()) != 0;
    g_applyCamPos = GetPrivateProfileIntW(L"eye", L"apply_campos", 1, p.c_str()) != 0;
    g_eyeInverse = GetPrivateProfileIntW(L"eye", L"apply_inverse", 1, p.c_str()) != 0;
    g_scanOn = GetPrivateProfileIntW(L"eye", L"scan", 1, p.c_str()) != 0;
    if (g_srcCamActive) g_scanOn = false; // the camera itself is moved: nothing to look for in the buffers
    if (g_vrActive) return; // in VR the eye offset comes from [vr] eye_offset_m
    if (off != g_eyeOffsetM) logf("eye: offset %.3f m", off);
    g_eyeOffsetM = off;
}

static void hideOnPresent() {
    {
        std::lock_guard<std::mutex> lk(g_hideMx);
        if (!g_vsFrame.empty()) g_vsList.swap(g_vsFrame);
        g_vsFrame.clear();
    }
    static bool keyWas;
    static int idx;
    bool key = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_F8) & 0x8000);
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
    auditOnPresent();
    static bool first = true;
    if (first) {
        first = false;
        eyeReadIni();
        if (!g_vrActive) g_eyeMode = GetPrivateProfileIntW(L"eye", L"mode", 0, (g_dir + L"mw4vr.ini").c_str()) & 3;
        logf("eye: experiment ready, mode %d, offset %.3f m (Ctrl+F7 cycles the mode)", g_eyeMode, g_eyeOffsetM);
    }
    static DWORD lastIni;
    if (GetTickCount() - lastIni >= 2000) { // every 2 s, whatever the frame rate
        lastIni = GetTickCount();
        eyeReadIni();
    }
    static bool keyWas;
    bool key = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_F7) & 0x8000);
    if (key && !keyWas) {
        g_eyeMode = (g_eyeMode + 1) & 3;
        static const char *names[] = {"off", "right eye", "left eye", "alternating"};
        logf("eye: mode %d (%s), offset %.3f m, x term at float %d, P00 %.4f", g_eyeMode, names[g_eyeMode],
             g_eyeOffsetM, g_xIdx, g_P00);
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
#include "hud.inc"
#include "shot.inc"
#include "vr.inc"

static void onPresent(IDXGISwapChain *sc) {
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
        vrOnPresent(sc, eye);
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
            logf("frame %u top-level labels: %s", g_frame, g_frameLabels.empty() ? "(none)" : g_frameLabels.c_str());
            g_lastFrameLabels = g_frameLabels;
        }
        g_frameLabels.clear();
    }

    DWORD now = GetTickCount();
    if (!g_statT0) g_statT0 = now;
    if (now - g_statT0 >= 5000) {
        float s = (now - g_statT0) / 1000.0f, f = g_statFrames ? (float)g_statFrames : 1.0f;
        logf("%.1f fps | per frame: %.0f draws imm, %.0f draws deferred, %.0f dispatches, %.0f maps, "
             "%.0f labels, %.1f FinishCommandList, %.1f ExecuteCommandList | shaders %u, contexts %u",
             g_statFrames / s, g_statDrawsImm / f, g_statDrawsDef / f, g_statDisp / f, g_statMaps / f,
             g_statLabels / f, g_statFinish / f, g_statExec / f, g_shaderCount, (unsigned)g_ctx.size());
        logf("   eye mode %d: per frame maps / patched - Mlr %.0f/%.0f, View %.1f/%.1f, Cards %.1f/%.1f, Reflect %.1f/%.1f",
             g_eyeMode, g_statKindMaps[EK_MLR] / f, g_statKindPatched[EK_MLR] / f, g_statKindMaps[EK_VIEW] / f,
             g_statKindPatched[EK_VIEW] / f, g_statKindMaps[EK_CARDS] / f, g_statKindPatched[EK_CARDS] / f,
             g_statKindMaps[EK_REFLECT] / f, g_statKindPatched[EK_REFLECT] / f);
        logf("   Mlr updates per frame: map discard %.0f, map other %.0f, update whole %.0f, update box %.0f",
             g_mlrPath[0] / f, g_mlrPath[1] / f, g_mlrPath[2] / f, g_mlrPath[3] / f);
        g_mlrPath[0] = g_mlrPath[1] = g_mlrPath[2] = g_mlrPath[3] = 0;
        logf("   camera-position updates moved per frame %.1f (buffers known %u), full scans %.2f per frame",
             g_statCamPos / f, (unsigned)g_camPosBufs.size(), g_statFullScans / f);
        g_statFullScans = 0;
        logf("   CPU-path draws with the replacement shader per frame %.1f, constant buffers with a camera matrix %.1f",
             g_statTlSwaps / f, g_statCbScanHits / f);
        g_statCbScanHits = 0;
        g_statTlSwaps = 0;
        g_statCamPos = 0;
        logf("   scanned dynamic buffers per frame %.1f, with a camera matrix %.1f", g_statKindMaps[EK_SCAN] / f,
             g_statKindPatched[EK_SCAN] / f);
        for (int k = 0; k < EK_N; ++k) g_statKindMaps[k] = g_statKindPatched[k] = 0;
        g_statT0 = now;
        g_statFrames = 0;
        g_statDrawsImm = g_statDrawsDef = g_statDisp = g_statMaps = g_statLabels = g_statExec = g_statFinish = 0;
    }

    static bool keyWas;
    bool key = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_F11) & 0x8000);
    bool trig = key && !keyWas;
    keyWas = key;
    if (g_frame % 30 == 0) {
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
    } else if (SUCCEEDED(hr) && out && *out && desc && desc->Usage == D3D11_USAGE_DYNAMIC &&
               desc->ByteWidth <= (4u << 20) && !(desc->BindFlags & (D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER))) {
        std::lock_guard<std::mutex> lk(g_shMx);
        g_dynSize[*out] = desc->ByteWidth;
    }
    if (SUCCEEDED(hr) && out && *out && desc && !(desc->BindFlags & D3D11_BIND_CONSTANT_BUFFER) &&
        desc->ByteWidth <= (4u << 20) && !(desc->BindFlags & (D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER))) {
        std::lock_guard<std::mutex> lk(g_shMx);
        g_bufSize[*out] = desc->ByteWidth;
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
static bool isCamPosBuf(void *b) {
    std::lock_guard<std::mutex> lk(g_cpMx);
    return g_camPosBufs.count(b) != 0;
}
static HRESULT STDMETHODCALLTYPE H_Map(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub, D3D11_MAP t,
                                       UINT fl, D3D11_MAPPED_SUBRESOURCE *m) {
    HRESULT hr = ORIG(PFN_Map, c, VT_Ctx_Map)(c, r, sub, t, fl, m);
    InterlockedIncrement(&g_statMaps);
    if (SUCCEEDED(hr) && t != D3D11_MAP_READ) {
        std::lock_guard<std::mutex> lk(g_patchedMx);
        g_lastWrite[r] = (int)t;
    }
    if (SUCCEEDED(hr) && m && t != D3D11_MAP_READ && !ctxOf(c)->foreign) {
        UINT sz = cbSize(r);
        int kind = eyeKind(sz);
        if (kind) InterlockedIncrement(&g_statKindMaps[kind]);
        if (!sz && t == D3D11_MAP_WRITE_DISCARD && dynSize(r)) {
            CtxState *s = ctxOf(c);
            s->mapped[r] = m->pData;
            s->mapType[r] = -1; // a dynamic buffer to scan for camera matrices
        } else if (!sz && t == D3D11_MAP_WRITE_NO_OVERWRITE && dynSize(r)) {
            static volatile LONG probes;
            if (InterlockedIncrement(&probes) <= 400) { // detection only: which part was written is unknown
                CtxState *s = ctxOf(c);
                s->mapped[r] = m->pData;
                s->mapType[r] = -2;
            }
        } else if (sz && (g_cap || kind || isCamPosBuf(r) || (g_scanOn && sz <= 4096) || sz == 48)) {
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
    unsigned patchesBefore = t_patches;
    auto it = s->mapped.find(r);
    if (it != s->mapped.end()) {
        int t = s->mapType[r];
        if (t == -1 || t == -2) {
            if (t == -1 && g_scanOn) eyeScan((uint8_t *)it->second, dynSize(r), r, "map discard");
            if (t_patches != patchesBefore) markPatched(r);
            else if (t == -2 && g_scanOn) eyeScan((uint8_t *)it->second, dynSize(r), r, "map no-overwrite, NOT patched", false);
            s->mapped.erase(it);
            s->mapType.erase(r);
            ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, r, sub);
            return;
        }
        UINT sz = cbSize(r);
        int kind = eyeKind(sz);
        // only whole-buffer rewrites: a NO_OVERWRITE map may only fill a part we know nothing about
        if (kind == EK_MLR) InterlockedIncrement(&g_mlrPath[t == D3D11_MAP_WRITE_DISCARD ? 0 : 1]);
        if (kind && t == D3D11_MAP_WRITE_DISCARD) eyePatch(kind, (uint8_t *)it->second, sz);
        if (!kind && t == D3D11_MAP_WRITE_DISCARD && g_scanOn && sz <= 4096)
            eyeScanCB((const uint8_t *)it->second, sz, 0, (float *)it->second);
        if (t == D3D11_MAP_WRITE_DISCARD) {
            thread_local std::vector<UINT> cpo;
            if (camPosOffsets(r, cpo)) camPosPatch((uint8_t *)it->second, sz, 0, cpo);
        }
        if (sz == 48 && t == D3D11_MAP_WRITE_DISCARD) frameCbSeen(r, it->second);
        if (g_cap) snapshot(r, it->second, 0, 0);
        if (t_patches != patchesBefore) markPatched(r);
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

static void updateCommon2(ID3D11Resource *r, const D3D11_BOX *box, const void *&src);
static void updateCommon(ID3D11Resource *r, const D3D11_BOX *box, const void *&src) {
    {
        std::lock_guard<std::mutex> lk(g_patchedMx);
        g_lastWrite[r] = box ? 11 : 10;
    }
    unsigned before = t_patches;
    updateCommon2(r, box, src);
    if (t_patches != before) markPatched(r);
}
static void updateCommon2(ID3D11Resource *r, const D3D11_BOX *box, const void *&src) {
    if (!src) return;
    thread_local std::vector<uint8_t> tmp;
    UINT csz = cbSize(r);
    if (csz == 48 && !box) frameCbSeen(r, src);
    if (csz) {
        int kind = eyeKind(csz);
        if (kind && !box) {
            InterlockedIncrement(&g_statKindMaps[kind]);
            if (kind == EK_MLR) InterlockedIncrement(&g_mlrPath[2]);
            tmp.assign((const uint8_t *)src, (const uint8_t *)src + csz);
            eyePatch(kind, tmp.data(), csz);
            src = tmp.data();
        } else if (kind && box && box->right > box->left && box->right <= csz) {
            InterlockedIncrement(&g_statKindMaps[kind]);
            if (kind == EK_MLR) InterlockedIncrement(&g_mlrPath[3]);
            UINT n = box->right - box->left;
            tmp.assign((const uint8_t *)src, (const uint8_t *)src + n);
            eyePatch(kind, tmp.data(), n, box->left);
            src = tmp.data();
        }
        if (!kind && g_scanOn && csz <= 4096) {
            UINT base = box ? box->left : 0, n = box ? box->right - box->left : csz;
            if (n && base + n <= csz && eyeScanCB((const uint8_t *)src, n, base, nullptr) && g_eyeMode) {
                tmp.assign((const uint8_t *)src, (const uint8_t *)src + n);
                eyeScanCB(tmp.data(), n, base, (float *)tmp.data());
                src = tmp.data();
            }
        }
        thread_local std::vector<UINT> cpo;
        if (g_eyeMode && camPosOffsets(r, cpo)) {
            UINT base = box ? box->left : 0, n = box ? box->right - box->left : csz;
            if (n && base + n <= csz) {
                if (src != tmp.data()) {
                    tmp.assign((const uint8_t *)src, (const uint8_t *)src + n);
                    src = tmp.data();
                }
                camPosPatch(tmp.data(), n, base, cpo);
            }
        }
        return;
    }
    UINT bsz = bufSize(r);
    if (!bsz) return;
    if (box || !g_scanOn) return; // partial updates: not scanned (each box would be a full scan)
    UINT n = bsz;
    // look first (read only), copy only when there is something to move
    thread_local std::vector<UINT> offs;
    float k2 = 0;
    eyeFind((const uint8_t *)src, n, r, offs, k2);
    if (offs.empty() || !g_eyeMode) return;
    tmp.assign((const uint8_t *)src, (const uint8_t *)src + n);
    if (eyeScan(tmp.data(), n, r, "update whole"))
        src = tmp.data();
}

// ---------------------------------------------------------------------------------------------
#include "mw5.inc"

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
    if (!sh || sh->hash != TL_HASH) return false;
    static unsigned cbFrame = ~0u;
    if (cbFrame != g_tlEpoch) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ORIG(PFN_Map, c, VT_Ctx_Map)(c, g_eyeCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            float v[20] = {};
            memcpy(v, g_K, 64);
            v[16] = g_near;
            v[17] = g_kTlOnly ? g_cockpitRange : 1e30f; // camera mode: the cockpit alone
            v[18] = (g_kTlOnly && mw5Active()) ? 1.0f : 0.0f; // our own cockpit instead of the game's
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
static const uint64_t BACKDROP_PS = 0x3c49bfee1754587bull;
static bool smallCamBackdrop(ID3D11DeviceContext *c) {
    if (!g_hudLayerOn || !g_smallCamSide) return false; // the zoom window keeps its backdrop
    CtxState *s = ctxOf(c);
    if (s->foreign || s->deferred) return false;
    ShaderInfo *sh = s->st[ST_PS].sh;
    if (!sh || sh->hash != BACKDROP_PS) return false;
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

template <int V> static void STDMETHODCALLTYPE HP_Draw(ID3D11DeviceContext *c, UINT a, UINT b) {
    onWork(c, "Draw", false);
    if (eyeHidden(c)) return;
    if (smallCamBackdrop(c)) return;
    bool sw = eyeTlPre(c);
    if (sw && mechMeasuring()) mechDraw(c, false, a, b, 0);
    if (sw && g_mw5On) mw5Draw(c, (Mw5PFN_DrawIdx)g_tr[SL_DRAWIDX][V]);
    bool hw = hudWidePre(c);
    ((PFN_U2)g_tr[SL_DRAW][V])(c, a, b);
    if (hw) hudWidePost(c);
    if (sw) eyeTlPost(c);
}
template <int V> static void STDMETHODCALLTYPE HP_DrawIndexed(ID3D11DeviceContext *c, UINT a, UINT b, INT d) {
    onWork(c, "DrawIndexed", false);
    if (eyeHidden(c)) return;
    bool sw = eyeTlPre(c);
    if (sw && mechMeasuring()) mechDraw(c, true, a, b, d);
    if (sw && g_mw5On) mw5Draw(c, (Mw5PFN_DrawIdx)g_tr[SL_DRAWIDX][V]);
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
    bool hw = hudWidePre(c);
    ((PFN_BU)g_tr[SL_DRAWINSTIND][V])(c, b, o);
    if (hw) hudWidePost(c);
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
// Diagnosis (the water mirrors the scene in the left eye's pass only): mw4hook's variables the planar
// reflection asks before it is drawn, as they stand when a pass comes to the water (build 20261107_ea16ef2).
static std::string waterState() {
    uint8_t *h = (uint8_t *)GetModuleHandleW(L"mw4hook.dll");
    if (!h) return "  [water state: mw4hook.dll not found]\n";
    static const struct {
        uint32_t off;
        int size;
        const char *what;
    } v[] = {{0x4b89e4, 4, "frame"},        {0x4bf3e8, 4, "casters stamp"}, {0x43d550, 4, "view stamp"}, {0x4bf3c4, 1, "failed"},
             {0x4bf2f0, 1, "2f0"},          {0x4bf43c, 4, "43c"},           {0x4bf36c, 4, "36c"},        {0x43d3e0, 1, "3e0"},
             {0x4bf1e4, 1, "1e4"},          {0x4bf128, 1, "128"},           {0x4beebc, 1, "eebc"},       {0x4b8fbc, 4, "8fbc"},
             {0x4bf3d0, 4, "casters list"}, {0x4bf3e0, 4, "3e0q"},          {0x4bf3c8, 4, "3c8"},        {0x4bf3cc, 4, "3cc"},
             {0x43d3ec, 4, "3ec"},          {0x43d3f0, 4, "3f0"},           {0x4b8a10, 4, "8a10"},       {0x4b8a14, 4, "8a14"}};
    std::string s = "  [water state:";
    for (auto &e : v) {
        uint32_t x = 0;
        char b[64];
        if (camRead(h + e.off, &x, e.size)) snprintf(b, sizeof b, " %s=%x", e.what, x);
        else snprintf(b, sizeof b, " %s=?", e.what);
        s += b;
    }
    return s + "]\n";
}

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
        h = (uint8_t *)GetModuleHandleW(L"mw4hook.dll");
        uint8_t code[7] = {};
        uint32_t at = h ? (uint32_t)(uintptr_t)(h + 0x43d3e0) : 0;
        ok = h && camRead(h + 0x9e5f0, code, 7) && code[0] == 0x80 && code[1] == 0x3d && memcmp(code + 2, &at, 4) == 0 && code[6] == 0;
        logf("water: reflection for both eyes %s", ok ? "on (mw4hook's code as known)" : "off - mw4hook's code is not the known one");
        if (!ok) return;
    }
    uint8_t *flag = h + 0x43d3e0;
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
        g_in3D = name && wcscmp(name, L"3D") == 0;
        if (g_in3D) g_in3DSeen = true;
    }
    if (name && wcsstr(name, L"tonemap into the frame")) InterlockedIncrement(&g_tonemapCount);
    if (g_lblDump && g_camInMain && s->depth <= 6) { // the labels of both eyes' passes, once (cam.inc)
        std::lock_guard<std::mutex> lk(g_labelMx);
        std::string &b = g_lblBuf[g_lblEye & 1];
        if (name && wcsncmp(name, L"water (RTIN", 11) == 0) b += waterState(); // diagnosis: the planar reflection's conditions
    }
    if (g_camInMain && s->depth <= 6 && !s->deferred && name && wcsncmp(name, L"water (RTIN", 11) == 0) waterBothEyes();
    if (g_lblDump && g_camInMain && s->depth <= 6) {
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
        g_inHud = name && wcsncmp(name, L"after 3D", 8) == 0;
        if (g_inHud) hudBegin((ID3D11DeviceContext *)s->self);
        if (g_inHud && (hudShift() != 0 || hudWideNow())) applyViewports((ID3D11DeviceContext *)s->self, s, true);
    }
    if (s->depth == 1 && !s->deferred && g_in3D && name && g_hudW) {
        // inside "3D": a camera smaller than the frame (drawn with "SSAA 2x2") is a window of the HUD
        bool smallCam = wcsncmp(name, L"SSAA 2x2", 8) == 0;
        if (smallCam) { // "... a camera at twice the resolution, 225x170 at 0,1188": where its window lies
            const wchar_t *v = wcsstr(name, L"resolution, ");
            int x = 0, y = 0, w = 0, hh = 0;
            if (v && swscanf(v + 12, L"%dx%d at %d,%d", &w, &hh, &x, &y) == 4 && w > 0) {
                float cu = (x + 0.5f * w) / g_hudW;
                g_smallCamSide = cu < 0.3f || cu > 0.7f;
            }
        }
        if (!smallCam && wcsncmp(name, L"camera:", 7) == 0) {
            const wchar_t *v = wcsstr(name, L"viewport ");
            int x = 0, y = 0, w = 0, hh = 0;
            if (v && swscanf(v, L"viewport %d,%d %dx%d", &x, &y, &w, &hh) == 4 && w > 0 && (UINT)w * 10 < g_hudW * 7) {
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
    if (!(fl & DXGI_PRESENT_TEST)) onPresent(sc);
    return ORIG(PFN_Present, sc, VT_Sc_Present)(sc, si, fl);
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
    HWND w = *(HWND *)(uintptr_t)0x1241050; // the game's window (MW4Mercs.exe)
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
            const LONG *pic = (const LONG *)(g_mouseBase + 0x4931dc);
            logf("mouse: cursor scaled by %.3f x %.3f about %ld,%ld (mw4hook's picture %ld,%ld - %ld,%ld, known %d)", kx, ky, o.x, o.y,
                 pic[0], pic[1], pic[2], pic[3], (int)*(g_mouseBase + 0x4931d9));
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
    uint8_t *base = (uint8_t *)GetModuleHandleW(L"mw4hook.dll");
    if (!base) return;
    __try {
        void **pg = (void **)(base + 0x4c7310), **ps = (void **)(base + 0x4c7314), **pc = (void **)(base + 0x4c7318);
        // this build? "mov eax, [mw4hook+0x4c7310]" in its GetCursorPos, "mov eax, [mw4hook+0x4c7318]" in its clip
        const uint8_t *i1 = base + 0xbb953, *i2 = base + 0xbc0fd;
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
    HWND w = *(HWND *)(uintptr_t)0x1241050, fg = GetForegroundWindow();
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
    if (!base) base = (uint8_t *)GetModuleHandleW(L"mw4hook.dll");
    if (!base) return;
    __try {
        const LONG *mode = (const LONG *)(base + 0x43e1c0); // the game's screen mode (what the cursor is mapped to)
        const RECT *out = (const RECT *)(base + 0x4b90d0);  // mw4hook's output size
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
    uint8_t *base = (uint8_t *)GetModuleHandleW(L"mw4hook.dll");
    if (!base) return;
    __try {
        // this build of mw4hook? "mov [mw4hook+0x4b90d8], eax" where the width is stored
        const uint8_t *ins = base + 0x4e60a;
        if (ins[0] != 0xA3 || *(uint32_t *)(ins + 1) != (uint32_t)(uintptr_t)(base + 0x4b90d8)) {
            logf("render: mw4hook.dll is not the build this was made for - render_height ignored");
            return;
        }
        HWND child = *(HWND *)(base + 0x4b90cc);
        RECT *rc = (RECT *)(base + 0x4b90d0), real;
        if (!child || !GetClientRect(child, &real) || real.right <= 0 || real.bottom <= 0 || rc->right != real.right ||
            rc->bottom != real.bottom) {
            logf("render: mw4hook's output size (%ld x %ld) is not its window's - render_height ignored", rc->right, rc->bottom);
            return;
        }
        if (want > 8192) want = 8192;
        LONG w = (LONG)((double)real.right * want / real.bottom + 0.5), h = want;
        if (h == real.bottom) return;
        // the import slot of GetClientRect (mw4hook.dll+0x210308), checked by the call that uses it
        void **slot = (void **)(base + 0x210308);
        const uint8_t *call = base + 0x571a9;
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
        LONG *pic = (LONG *)(base + 0x4931c8);
        const uint8_t *rd = base + 0x14710; // "mov eax, [mw4hook+0x4931c8]"
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
        MoveFileExW((g_dir + L"mw4vr_probe.log").c_str(), (g_dir + L"mw4vr_probe_prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
        g_log = _wfsopen((g_dir + L"mw4vr_probe.log").c_str(), L"w", _SH_DENYNO);
        g_shlog = _wfsopen((g_dir + L"mw4vr_shaders.txt").c_str(), L"w", _SH_DENYNO);
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        logf("mw4vr probe v11.15 (VR stage 5, both eyes per frame, camera head tracking, HUD layer, cockpit profiles, own cockpit mesh with HUD monitors, pictures) loaded into %s", narrow(exe).c_str());
        if (!loadReal()) logf("ERROR: the system d3d11.dll could not be loaded");
    }
    return TRUE;
}
