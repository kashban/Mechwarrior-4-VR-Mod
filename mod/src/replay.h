// What replay.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_REPLAY_H
#define MW4VR_REPLAY_H

// [vr] replay_right: 0 = the game draws both eyes; 1 = the right eye's picture is made from the left eye's
// draws; 2, 3 = as 0, with a file for diagnosis (replay.cpp)
extern int g_rpMode;
extern volatile bool g_rpLive; // the left eye's pass runs, and each draw of it is made for the right eye too
extern volatile LONG g_rpOwn;  // > 0: the calls that follow are the mod's own (not the game's)
// the mod's own work inside a hook: what it calls on the context is not the game's
struct RpOwn {
    RpOwn() { ++g_rpOwn; }
    ~RpOwn() { --g_rpOwn; }
    RpOwn(const RpOwn &) = delete;
    RpOwn &operator=(const RpOwn &) = delete;
};
void rpReadIni();
// the context's methods no other unit hooks
int rpHookContext(ID3D11DeviceContext *ctx);

// what the game binds (hooks.cpp, draws.cpp follow the calls)
void rpSrvSet(int stage, UINT start, UINT n, ID3D11ShaderResourceView *const *v);
void rpOmSet(UINT n, ID3D11RenderTargetView *const *rtv, ID3D11DepthStencilView *dsv, bool uavs, UINT us, UINT un,
             ID3D11UnorderedAccessView *const *uav);
// what the game writes
void rpCbData(ID3D11Resource *r, const void *p, UINT sz);
void rpMapped(ID3D11Resource *r, UINT sub, D3D11_MAP t, const D3D11_MAPPED_SUBRESOURCE *m);
void rpUnmapping(ID3D11Resource *r, UINT sub);
void rpUpdated(ID3D11Resource *r, UINT sub, const D3D11_BOX *box, const void *src);
void rpWork(const char *kind);
// the instances' buffer the game writes into memory of ours (replay.cpp): hooks.cpp at the game's Map and Unmap,
// draws.cpp before every draw
bool rpRingMap(ID3D11DeviceContext *c, ID3D11Resource *r, D3D11_MAP t, D3D11_MAPPED_SUBRESOURCE *m);
bool rpRingUnmap(ID3D11Resource *r);
void rpRingFeed(ID3D11DeviceContext *c, UINT instances);
// main.cpp: an annotation label of the immediate context opens (depth: before it) or has closed (depth: after)
void rpLabel(const wchar_t *name, int depth, bool open);
// view.cpp: the main camera's world -> clip matrix of this frame
void rpLearnProj(const float *w);

// cam.cpp: around each eye's pass of the main camera
void rpPassBegin(int eye);
void rpPassEnd(int eye);
bool rpWanted();
bool rpBegin(const float *c0, const float *c1);
bool rpEnd();
void rpIdle();
void rpCheckK(const float *c0, const float *c1);

// draws.cpp: a draw or dispatch of the left eye's pass is made again for the right eye
bool rpStereo(void *c);
// instances: how many the draw has (0: not known - the graphics card is told)
bool rpRightBegin(ID3D11DeviceContext *c, UINT instances);
void rpRightEnd(ID3D11DeviceContext *c);
bool rpRightBeginCs(ID3D11DeviceContext *c);
void rpRightEndCs(ID3D11DeviceContext *c);
void rpTlMatrices(const float *kNear, bool nearOn, float *out32);
// draws.cpp: the game clears or copies - the right eye's call (null, false: there is none)
ID3D11RenderTargetView *rpClearTwin(void *c, ID3D11RenderTargetView *v);
ID3D11DepthStencilView *rpClearTwin(void *c, ID3D11DepthStencilView *v);
ID3D11UnorderedAccessView *rpClearTwin(void *c, ID3D11UnorderedAccessView *v);
ID3D11ShaderResourceView *rpMipsTwin(void *c, ID3D11ShaderResourceView *v);
bool rpCopyTwins(void *c, ID3D11Resource *d, ID3D11Resource *sr, ID3D11Resource **td, ID3D11Resource **ts);
// hud.cpp: the right eye's picture is in the twin of the frame's texture (null: there is none)
ID3D11Texture2D *rpTwinOf(ID3D11Texture2D *t);

#endif // MW4VR_REPLAY_H
