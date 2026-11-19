// What hooks.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_HOOKS_H
#define MW4VR_HOOKS_H

extern DWORD g_presOurs, g_presReal, g_presWait; // ms: our part of the last Present, the system's, xrWaitFrame in ours
void onPresent(IDXGISwapChain *sc);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CreateBuffer)(ID3D11Device *, const D3D11_BUFFER_DESC *,
                                                     const D3D11_SUBRESOURCE_DATA *, ID3D11Buffer **);
HRESULT STDMETHODCALLTYPE H_CreateBuffer(ID3D11Device *d, const D3D11_BUFFER_DESC *desc,
                                         const D3D11_SUBRESOURCE_DATA *init, ID3D11Buffer **out);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CreateShader)(ID3D11Device *, const void *, SIZE_T, ID3D11ClassLinkage *,
                                                     void **);
typedef void(STDMETHODCALLTYPE *PFN_SetShader)(ID3D11DeviceContext *, void *, ID3D11ClassInstance *const *, UINT);
typedef void(STDMETHODCALLTYPE *PFN_SetCB)(ID3D11DeviceContext *, UINT, UINT, ID3D11Buffer *const *);
void STDMETHODCALLTYPE H_VSSetShaderResources(ID3D11DeviceContext *c, UINT start, UINT n,
                                              ID3D11ShaderResourceView *const *v);
typedef void(STDMETHODCALLTYPE *PFN_U2)(ID3D11DeviceContext *, UINT, UINT);
typedef void(STDMETHODCALLTYPE *PFN_U2I)(ID3D11DeviceContext *, UINT, UINT, INT);
typedef void(STDMETHODCALLTYPE *PFN_U4)(ID3D11DeviceContext *, UINT, UINT, UINT, UINT);
typedef void(STDMETHODCALLTYPE *PFN_U3IU)(ID3D11DeviceContext *, UINT, UINT, UINT, INT, UINT);
typedef void(STDMETHODCALLTYPE *PFN_BU)(ID3D11DeviceContext *, ID3D11Buffer *, UINT);
typedef void(STDMETHODCALLTYPE *PFN_U3)(ID3D11DeviceContext *, UINT, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE *PFN_Map)(ID3D11DeviceContext *, ID3D11Resource *, UINT, D3D11_MAP, UINT,
                                            D3D11_MAPPED_SUBRESOURCE *);
HRESULT STDMETHODCALLTYPE H_Map(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub, D3D11_MAP t, UINT fl,
                                D3D11_MAPPED_SUBRESOURCE *m);
typedef void(STDMETHODCALLTYPE *PFN_Unmap)(ID3D11DeviceContext *, ID3D11Resource *, UINT);
void STDMETHODCALLTYPE H_Unmap(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub);
typedef void(STDMETHODCALLTYPE *PFN_Update)(ID3D11DeviceContext *, ID3D11Resource *, UINT, const D3D11_BOX *,
                                            const void *, UINT, UINT);
typedef void(STDMETHODCALLTYPE *PFN_Update1)(ID3D11DeviceContext *, ID3D11Resource *, UINT, const D3D11_BOX *,
                                             const void *, UINT, UINT, UINT);
enum {
    SL_DRAW,
    SL_DRAWIDX,
    SL_DRAWINST,
    SL_DRAWIDXINST,
    SL_DRAWINSTIND,
    SL_DRAWIDXINSTIND,
    SL_DISP,
    SL_DISPIND,
    SL_UPD,
    SL_UPD1,
    SL_CLRRTV,
    SL_CLRDSV,
    SL_CLRUAVU,
    SL_CLRUAVF,
    SL_COPYRES,
    SL_COPYREG,
    SL_GENMIPS,
    SL_N
};
enum { HP_VARS = 4 };
extern void *g_tr[SL_N][HP_VARS];     // trampolines (function + 2)
extern void *g_hpAddr[SL_N][HP_VARS]; // patched functions
extern const char *g_slName[SL_N];
// what an UpdateSubresource writes (the data itself is passed on as it is)
void updateCommon(ID3D11Resource *r, const D3D11_BOX *box, const void *&src);

// the hooks that SHADER_HOOK, DEFERRED_HOOK, SETSHADER_HOOK and SETCB_HOOK make (hooks.cpp)
#define MW4VR_DECL_SHADER_HOOK(Kind)                                                                                   \
    HRESULT STDMETHODCALLTYPE H_Create##Kind(ID3D11Device *, const void *, SIZE_T, ID3D11ClassLinkage *, void **);
MW4VR_DECL_SHADER_HOOK(VertexShader)
MW4VR_DECL_SHADER_HOOK(PixelShader)
MW4VR_DECL_SHADER_HOOK(ComputeShader)
MW4VR_DECL_SHADER_HOOK(GeometryShader)
MW4VR_DECL_SHADER_HOOK(HullShader)
MW4VR_DECL_SHADER_HOOK(DomainShader)
#undef MW4VR_DECL_SHADER_HOOK
HRESULT STDMETHODCALLTYPE H_CreateDeferredContext(void *, UINT, void **);
HRESULT STDMETHODCALLTYPE H_CreateDeferredContext1(void *, UINT, void **);
HRESULT STDMETHODCALLTYPE H_CreateDeferredContext2(void *, UINT, void **);
HRESULT STDMETHODCALLTYPE H_CreateDeferredContext3(void *, UINT, void **);
#define MW4VR_DECL_STAGE_HOOKS(St)                                                                                     \
    void STDMETHODCALLTYPE H_##St##SetShader(ID3D11DeviceContext *, void *, ID3D11ClassInstance *const *, UINT);       \
    void STDMETHODCALLTYPE H_##St##SetConstantBuffers(ID3D11DeviceContext *, UINT, UINT, ID3D11Buffer *const *);       \
    void STDMETHODCALLTYPE H_##St##SetConstantBuffers1(ID3D11DeviceContext *, UINT, UINT, ID3D11Buffer *const *,       \
                                                       const UINT *, const UINT *);
MW4VR_DECL_STAGE_HOOKS(VS)
MW4VR_DECL_STAGE_HOOKS(PS)
MW4VR_DECL_STAGE_HOOKS(CS)
#undef MW4VR_DECL_STAGE_HOOKS

#endif // MW4VR_HOOKS_H
