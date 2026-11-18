// What draws.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_DRAWS_H
#define MW4VR_DRAWS_H

int e2eCockpit(wchar_t *name, int n);
void e2ePass(int e, int *v);
void createEyeVS();
void e2eState(int *v);
void hotpatchScan(ID3D11DeviceContext *ctx);
typedef void(STDMETHODCALLTYPE *PFN_OMSet)(ID3D11DeviceContext *, UINT, ID3D11RenderTargetView *const *,
                                           ID3D11DepthStencilView *);
void STDMETHODCALLTYPE H_OMSetRenderTargets(ID3D11DeviceContext *c, UINT n, ID3D11RenderTargetView *const *rtv,
                                            ID3D11DepthStencilView *dsv);
void STDMETHODCALLTYPE H_OMSetRenderTargetsAndUAVs(ID3D11DeviceContext *c, UINT n, ID3D11RenderTargetView *const *rtv,
                                                   ID3D11DepthStencilView *dsv, UINT us, UINT un,
                                                   ID3D11UnorderedAccessView *const *uav, const UINT *ic);
float hudShift();
void STDMETHODCALLTYPE H_RSSetViewports(ID3D11DeviceContext *c, UINT n, const D3D11_VIEWPORT *v);
void applyViewports(ID3D11DeviceContext *c, CtxState *s, bool shifted);
typedef void(STDMETHODCALLTYPE *PFN_Exec)(ID3D11DeviceContext *, ID3D11CommandList *, BOOL);

#endif // MW4VR_DRAWS_H
