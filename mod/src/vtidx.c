/* vtable indices taken from the C-style interface definitions of the Windows SDK,
   so the probe never relies on hand-counted method positions. */
#define CINTERFACE
#define COBJMACROS
#include <d3d11_3.h>
#include <dxgi1_2.h>
#include <stddef.h>

#define IDX(T, m) (int)(offsetof(T##Vtbl, m) / sizeof(void *))

const int VT_Dev_CreateBuffer = IDX(ID3D11Device, CreateBuffer);
const int VT_Dev_CreateVertexShader = IDX(ID3D11Device, CreateVertexShader);
const int VT_Dev_CreatePixelShader = IDX(ID3D11Device, CreatePixelShader);
const int VT_Dev_CreateComputeShader = IDX(ID3D11Device, CreateComputeShader);
const int VT_Dev_CreateGeometryShader = IDX(ID3D11Device, CreateGeometryShader);
const int VT_Dev_CreateHullShader = IDX(ID3D11Device, CreateHullShader);
const int VT_Dev_CreateDomainShader = IDX(ID3D11Device, CreateDomainShader);

const int VT_Dev_CreateDeferredContext = IDX(ID3D11Device3, CreateDeferredContext);
const int VT_Dev_CreateDeferredContext1 = IDX(ID3D11Device3, CreateDeferredContext1);
const int VT_Dev_CreateDeferredContext2 = IDX(ID3D11Device3, CreateDeferredContext2);
const int VT_Dev_CreateDeferredContext3 = IDX(ID3D11Device3, CreateDeferredContext3);
const int VT_Ctx_ExecuteCommandList = IDX(ID3D11DeviceContext1, ExecuteCommandList);
const int VT_Ctx_FinishCommandList = IDX(ID3D11DeviceContext1, FinishCommandList);

const int VT_Ctx_VSSetConstantBuffers = IDX(ID3D11DeviceContext1, VSSetConstantBuffers);
const int VT_Ctx_PSSetConstantBuffers = IDX(ID3D11DeviceContext1, PSSetConstantBuffers);
const int VT_Ctx_CSSetConstantBuffers = IDX(ID3D11DeviceContext1, CSSetConstantBuffers);
const int VT_Ctx_VSSetConstantBuffers1 = IDX(ID3D11DeviceContext1, VSSetConstantBuffers1);
const int VT_Ctx_PSSetConstantBuffers1 = IDX(ID3D11DeviceContext1, PSSetConstantBuffers1);
const int VT_Ctx_CSSetConstantBuffers1 = IDX(ID3D11DeviceContext1, CSSetConstantBuffers1);
const int VT_Ctx_VSSetShader = IDX(ID3D11DeviceContext1, VSSetShader);
const int VT_Ctx_VSSetShaderResources = IDX(ID3D11DeviceContext1, VSSetShaderResources);
const int VT_Ctx_PSSetShader = IDX(ID3D11DeviceContext1, PSSetShader);
const int VT_Ctx_CSSetShader = IDX(ID3D11DeviceContext1, CSSetShader);
const int VT_Ctx_Draw = IDX(ID3D11DeviceContext1, Draw);
const int VT_Ctx_DrawIndexed = IDX(ID3D11DeviceContext1, DrawIndexed);
const int VT_Ctx_DrawInstanced = IDX(ID3D11DeviceContext1, DrawInstanced);
const int VT_Ctx_DrawIndexedInstanced = IDX(ID3D11DeviceContext1, DrawIndexedInstanced);
const int VT_Ctx_DrawInstancedIndirect = IDX(ID3D11DeviceContext1, DrawInstancedIndirect);
const int VT_Ctx_DrawIndexedInstancedIndirect = IDX(ID3D11DeviceContext1, DrawIndexedInstancedIndirect);
const int VT_Ctx_Dispatch = IDX(ID3D11DeviceContext1, Dispatch);
const int VT_Ctx_DispatchIndirect = IDX(ID3D11DeviceContext1, DispatchIndirect);
const int VT_Ctx_Map = IDX(ID3D11DeviceContext1, Map);
const int VT_Ctx_Unmap = IDX(ID3D11DeviceContext1, Unmap);
const int VT_Ctx_UpdateSubresource = IDX(ID3D11DeviceContext1, UpdateSubresource);
const int VT_Ctx_UpdateSubresource1 = IDX(ID3D11DeviceContext1, UpdateSubresource1);
const int VT_Ctx_OMSetRenderTargets = IDX(ID3D11DeviceContext1, OMSetRenderTargets);
const int VT_Ctx_OMSetRenderTargetsAndUAVs = IDX(ID3D11DeviceContext1, OMSetRenderTargetsAndUnorderedAccessViews);
const int VT_Ctx_RSSetViewports = IDX(ID3D11DeviceContext1, RSSetViewports);

const int VT_Ann_BeginEvent = IDX(ID3DUserDefinedAnnotation, BeginEvent);
const int VT_Ann_EndEvent = IDX(ID3DUserDefinedAnnotation, EndEvent);
const int VT_Ann_SetMarker = IDX(ID3DUserDefinedAnnotation, SetMarker);
const int VT_Ann_GetStatus = IDX(ID3DUserDefinedAnnotation, GetStatus);

const int VT_Fac_CreateSwapChain = IDX(IDXGIFactory2, CreateSwapChain);
const int VT_Fac_CreateSwapChainForHwnd = IDX(IDXGIFactory2, CreateSwapChainForHwnd);

const int VT_Sc_Present = IDX(IDXGISwapChain1, Present);
const int VT_Sc_Present1 = IDX(IDXGISwapChain1, Present1);
const int VT_Sc_ResizeBuffers = IDX(IDXGISwapChain1, ResizeBuffers);
