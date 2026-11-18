// What base.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_BASE_H
#define MW4VR_BASE_H

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
extern const int VT_Dev_CreateBuffer, VT_Dev_CreateVertexShader, VT_Dev_CreatePixelShader, VT_Dev_CreateComputeShader,
    VT_Dev_CreateGeometryShader, VT_Dev_CreateHullShader, VT_Dev_CreateDomainShader, VT_Dev_CreateDeferredContext,
    VT_Dev_CreateDeferredContext1, VT_Dev_CreateDeferredContext2, VT_Dev_CreateDeferredContext3;
extern const int VT_Ctx_VSSetConstantBuffers, VT_Ctx_PSSetConstantBuffers, VT_Ctx_CSSetConstantBuffers,
    VT_Ctx_VSSetConstantBuffers1, VT_Ctx_PSSetConstantBuffers1, VT_Ctx_CSSetConstantBuffers1, VT_Ctx_VSSetShader,
    VT_Ctx_VSSetShaderResources, VT_Ctx_PSSetShader, VT_Ctx_CSSetShader, VT_Ctx_Draw, VT_Ctx_DrawIndexed,
    VT_Ctx_DrawInstanced, VT_Ctx_DrawIndexedInstanced, VT_Ctx_DrawInstancedIndirect,
    VT_Ctx_DrawIndexedInstancedIndirect, VT_Ctx_Dispatch, VT_Ctx_DispatchIndirect, VT_Ctx_Map, VT_Ctx_Unmap,
    VT_Ctx_UpdateSubresource, VT_Ctx_UpdateSubresource1, VT_Ctx_OMSetRenderTargets, VT_Ctx_OMSetRenderTargetsAndUAVs,
    VT_Ctx_RSSetViewports, VT_Ctx_ExecuteCommandList, VT_Ctx_FinishCommandList;
extern const int VT_Ann_BeginEvent, VT_Ann_EndEvent, VT_Ann_SetMarker, VT_Ann_GetStatus;
extern const int VT_Fac_CreateSwapChain, VT_Fac_CreateSwapChainForHwnd;
extern const int VT_Sc_Present, VT_Sc_Present1, VT_Sc_ResizeBuffers;
}
extern std::wstring g_dir;
extern FILE *g_log, *g_shlog;
extern int g_dbg; // [debug] level in mw4vr.ini: 0 = the log alone, short; 1 = the diagnosis files and all lines
extern std::mutex g_logMx;
void logf(const char *fmt, ...);
std::string narrow(const wchar_t *w);
extern HMODULE g_real;
typedef HRESULT(WINAPI *PFN_CreateDevice)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                          UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT(WINAPI *PFN_CreateDeviceAndSwapChain)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                                      const D3D_FEATURE_LEVEL *, UINT, UINT,
                                                      const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D11Device **,
                                                      D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
extern PFN_CreateDevice r_CreateDevice;
extern PFN_CreateDeviceAndSwapChain r_CreateDeviceAndSwapChain;
bool loadReal();
void *origOf(void *self, int idx);
extern void *g_lastReplaced;
bool patch(void *obj, int idx, void *hook);
#define ORIG(T, self, idx) ((T)origOf((void *)(self), (idx)))
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
    std::string res;           // non-cbuffer bindings, for the audit
    std::vector<int> srvSlots; // shader-resource (t) slots it reads
};
// camera positions a shader gets (camera-relative rendering): moved by the eye offset as well
bool isCamPos(const std::string &n);
extern std::mutex g_shMx;
extern std::unordered_map<void *, ShaderInfo *> g_shaders;
extern std::unordered_map<void *, UINT> g_cbSize;
extern unsigned g_shaderCount;
typedef HRESULT(WINAPI *PFN_D3DReflect)(LPCVOID, SIZE_T, REFIID, void **);
extern PFN_D3DReflect g_reflect;
typedef HRESULT(WINAPI *PFN_D3DDisassemble)(LPCVOID, SIZE_T, UINT, LPCSTR, ID3DBlob **);
typedef HRESULT(WINAPI *PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT,
                                        ID3DBlob **, ID3DBlob **);
extern PFN_D3DCompile g_compile;
extern ID3D11Device *g_dev;
// a file opened (the checked form of the C library's function; null: it could not be)
FILE *fileOpen(const wchar_t *path, const wchar_t *mode);

#endif // MW4VR_BASE_H
