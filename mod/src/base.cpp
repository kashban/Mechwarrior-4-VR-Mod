// MW4VR - a d3d11.dll proxy behind MW4Engine (mw4hook.dll): VR for MechWarrior 4: Mercenaries.
//
// How the units work together: docs/ARCHITECTURE.md. This one is the ground the others stand on: the log, the
// real d3d11.dll and the exports passed through to it, the patching of vtable slots (the originals kept), and what
// is known of every shader the game creates (its constant buffers and their variables).
//
// Output (next to the DLL, i.e. the game folder; opened shared, readable while the game runs):
//   mw4vr.log              - device/swap chain/context creation, what was found and switched on, periodic fps
//   ([debug] level=1:)
//   mw4vr_shaders.txt      - every created shader with its constant buffers and variables
//   mw4vr_frame_NNN.txt    - one fully traced frame (Ctrl+Shift+F11, or create mw4vr_capture.txt)
//
// Originals are kept per vtable (deferred contexts have their own); the immediate context is patched again if
// Direct3D swaps its vtable.

#include "mod.h"

// ---------------------------------------------------------------------------------------------
// logging

std::wstring g_dir;
FILE *g_log, *g_shlog;
int g_dbg; // [debug] level in mw4vr.ini: 0 = the log alone, short; 1 = the diagnosis files and all lines
std::mutex g_logMx;

void logf(const char *fmt, ...) {
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

std::string narrow(const wchar_t *w) {
    if (!w) return "(null)";
    char buf[512];
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof buf, nullptr, nullptr);
    return n > 0 ? std::string(buf) : std::string("?");
}

// ---------------------------------------------------------------------------------------------
// the real d3d11.dll and the exports we pass through

HMODULE g_real;
PFN_CreateDevice r_CreateDevice;
PFN_CreateDeviceAndSwapChain r_CreateDeviceAndSwapChain;

// by hand: clang-format finds no stable form for a list of macro calls
// clang-format off
#define FWD_LIST(X)                                                                                                    \
    X(D3D11CoreCreateDevice)                                                                                           \
    X(D3D11CoreCreateLayeredDevice)                                                                                    \
    X(D3D11CoreGetLayeredDeviceSize)                                                                                   \
    X(D3D11CoreRegisterLayers)                                                                                         \
    X(D3D11CreateDeviceForD3D12)                                                                                       \
    X(D3D11On12CreateDevice)                                                                                           \
    X(CreateDirect3D11DeviceFromDXGIDevice)                                                                            \
    X(CreateDirect3D11SurfaceFromDXGISurface)                                                                          \
    X(D3DPerformance_BeginEvent)                                                                                       \
    X(D3DPerformance_EndEvent)                                                                                         \
    X(D3DPerformance_GetStatus)                                                                                        \
    X(D3DPerformance_SetMarker)                                                                                        \
    X(EnableFeatureLevelUpgrade)                                                                                       \
    X(OpenAdapter10)                                                                                                   \
    X(OpenAdapter10_2)

#define DECL_FWD(n)                                                                                                    \
    extern "C" FARPROC r_##n;                                                                                          \
    FARPROC r_##n;                                                                                                     \
    extern "C" __declspec(naked) void P_##n() {                                                                        \
        __asm { jmp dword ptr [r_##n] }                                                                                    \
    }
// clang-format on
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

bool loadReal() {
    if (g_real) return true;
    wchar_t p[MAX_PATH];
    GetSystemDirectoryW(p, MAX_PATH);
    wcscat_s(p, L"\\d3d11.dll");
    g_real = LoadLibraryW(p);
    if (!g_real) return false;
    r_CreateDevice = (PFN_CreateDevice)GetProcAddress(g_real, "D3D11CreateDevice");
    r_CreateDeviceAndSwapChain = (PFN_CreateDeviceAndSwapChain)GetProcAddress(g_real, "D3D11CreateDeviceAndSwapChain");
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

void *origOf(void *self, int idx) {
    void **vt = *(void ***)self;
    LONG n = g_nvts;
    for (LONG i = 0; i < n; ++i)
        if (g_vts[i].vt == vt) return g_vts[i].o[idx];
    for (LONG i = 0; i < n; ++i) // vtable copied by the runtime with our pointer in it
        if (g_vts[i].o[idx]) return g_vts[i].o[idx];
    return nullptr;
}

void *g_lastReplaced;
bool patch(void *obj, int idx, void *hook) {
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

// ---------------------------------------------------------------------------------------------
// shader reflection

// camera positions a shader gets (camera-relative rendering): moved by the eye offset as well
bool isCamPos(const std::string &n) {
    std::string l = n;
    for (auto &c : l) c = (char)tolower((unsigned char)c);
    auto ends = [&](const char *t) {
        size_t k = strlen(t);
        return l.size() >= k && l.compare(l.size() - k, k, t) == 0;
    };
    return ends("cam") || ends("campos") || ends("eye");
}

std::mutex g_shMx;
std::unordered_map<void *, ShaderInfo *> g_shaders;
std::unordered_map<void *, UINT> g_cbSize;
unsigned g_shaderCount;

PFN_D3DReflect g_reflect;
PFN_D3DCompile g_compile;
ID3D11Device *g_dev;
// a file opened (the checked form of the C library's function; null: it could not be)
FILE *fileOpen(const wchar_t *path, const wchar_t *mode) {
    FILE *f = nullptr;
    return _wfopen_s(&f, path, mode) == 0 ? f : nullptr;
}
