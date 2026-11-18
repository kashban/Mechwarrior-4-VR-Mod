// What state.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_STATE_H
#define MW4VR_STATE_H

extern ID3D11VertexShader *g_eyeVS; // replacement for the CPU path's screen-space vertex shader
void onShader(char stage, const void *code, SIZE_T len, void *obj);
ShaderInfo *lookupShader(void *s);
UINT cbSize(void *b);
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
    bool foreign = false;       // a context of the VR runtime's own device
    bool deferred = false;
    StageState st[ST_N];
    std::string rt = "?", vp = "?", pendSig;
    std::vector<std::string> labels; // open annotation labels
    unsigned pendCount = 0;
    int depth = 0;
    std::unordered_map<void *, void *> mapped;
    std::unordered_map<void *, int> mapType;
};
extern std::mutex g_ctxMx;
extern std::unordered_map<void *, CtxState *> g_ctx; // context -> state
extern std::unordered_map<void *, void *> g_annCtx;  // annotation interface -> its context
CtxState *ctxOf(void *c);
CtxState *ctxOfAnn(void *a);
extern volatile LONG g_statDrawsImm, g_statDrawsDef, g_statDisp, g_statMaps, g_statLabels, g_statExec, g_statFinish;
extern unsigned g_statFrames, g_frame;
extern DWORD g_statT0;
extern std::string g_frameLabels, g_lastFrameLabels;
extern std::mutex g_labelMx;
extern bool g_labelsSeen, g_firstPresentLogged;
extern ID3D11DeviceContext *g_imm;
extern void **g_immVt;
extern volatile bool g_cap;
extern FILE *g_capf;
extern std::mutex g_capMx;
extern unsigned g_capIndex;
extern std::mutex g_snapMx;
extern std::unordered_map<void *, std::vector<uint8_t>> g_snap;
extern std::unordered_map<void *, unsigned> g_ver;
extern std::unordered_set<std::string> g_dumped;
void capLine(CtxState *s, const char *fmt, ...);
void flushPending(CtxState *s);
void setRTs(CtxState *s, UINT n, ID3D11RenderTargetView *const *rtv, ID3D11DepthStencilView *dsv);
extern float g_right[3];
extern volatile bool g_haveRight;
extern volatile LONG g_statTlSwaps;
extern std::mutex g_hideMx;
extern std::vector<uint64_t> g_vsFrame, g_vsList; // vertex shaders drawn in "3D": this frame, last frame
extern volatile uint64_t g_hideHash;
extern volatile int g_eyeMode; // 0 off, 1 right eye, 2 left eye, 3 alternating every frame
extern volatile bool g_vrActive;
extern volatile bool g_vrInitRunning; // devices created now belong to the VR runtime
extern volatile bool g_menuMode;
// a part of the HUD on the cockpit's glass (mw5.cpp): what is shown (rc), the part its place is given for (base),
// that place as seen from the seat (degrees right and up of base's middle, base's width in degrees, metres away)
struct GlassItem {
    float rc[4], base[4];
    float right, up, width, dist;
};
extern bool g_renderOverride;        // the game renders larger than its window ([vr] render_height)
extern void *volatile g_foreignDev;  // the VR runtime's device (shares the vtable with the game's)
extern volatile bool g_inHud;        // inside the top-level "after 3D: HUD, 2D" label
extern volatile bool g_smallCamSide; // the small camera being drawn is a window at the picture's side (the target's
extern volatile float g_hudPx; // HUD shift per eye in back-buffer pixels (VR), 0 = none // VR session running: the eyes
extern volatile bool g_in3D;   // inside the top-level "3D" label: the world, not HUD or menus
extern volatile bool g_in3DSeen; // a "3D" label was seen (a battle is running)
void onWork(void *ctx, const char *kind, bool compute);
void snapshot(void *res, const void *src, UINT dstOff, UINT bytes);
enum { EK_NONE, EK_VIEW };
extern int g_xIdx;         // float index of the clip.x translation term (12 column-major, 3 row-major)
extern float g_P00, g_P11; // projection scales, from g_vWorldToClip
extern int g_layoutLogs;
int eyeKind(UINT sz);
float len3(float a, float b, float c);

#endif // MW4VR_STATE_H
