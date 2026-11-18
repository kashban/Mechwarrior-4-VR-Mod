// What hud.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_HUD_H
#define MW4VR_HUD_H

extern volatile bool g_hudLayerOn; // set by vr.cpp: tracking at the camera and [vr] hud_layer=1
extern ID3D11Texture2D *g_hudTex;
extern ID3D11RenderTargetView *g_hudRtv;
extern ID3D11RenderTargetView *g_hudFrameRtv; // the game's target, replaced while the HUD pass runs (a reference held)
extern UINT g_hudW, g_hudH;
extern unsigned g_hudClearedFrame, g_hudDrawnFrame;
extern bool g_hudInternal;          // our own OMSetRenderTargets calls pass the hook untouched
extern UINT g_hudTexW;              // the layer texture's width (>= g_hudW, the frame's)
extern volatile bool g_hudWideLive; // the frame constants are known: the part beyond the edge is drawn
UINT hudWideW(UINT w);
static inline bool hudWide() { return g_hudTexW > g_hudW && g_hudW; }
// the HUD pass draws into the wider layer texture: its full viewports are as wide
static inline bool hudWideNow() { return g_inHud && g_hudFrameRtv && hudWide() && g_hudWideLive; }
void hudKeepDsv(ID3D11DepthStencilView *dsv);
// the 'Frame' constants (g_screen = 2 / width, 2 / height, ...) as last written, per buffer
struct FrameCb {
    float v[12];
};
extern std::mutex g_frameCbMx;
extern std::unordered_map<void *, FrameCb> g_frameCb;
void frameCbSeen(void *r, const void *p);
// the small cameras drawn into the frame this frame (pixels): a window in the middle of the picture is
// the zoom window, shown at the cross-hair's depth
struct HudCamRect {
    int x, y, w, h;
};
extern HudCamRect g_hudCams[4];
extern int g_hudCamN;
extern unsigned g_hudCamFrame;
void hudCamRect(int x, int y, int w, int h);
void hudBegin(ID3D11DeviceContext *c);
// a small camera drawn into the frame (the zoom window, the target's view): from now until hudEnd the
// frame target is ours wherever the game sets it
void hudBeginKnown(ID3D11DeviceContext *c);
void hudEnd(ID3D11DeviceContext *c);
// the game sets its frame target again inside the HUD pass: ours instead
static inline ID3D11RenderTargetView *hudSwap(ID3D11RenderTargetView *v) {
    return (g_hudFrameRtv && !g_hudInternal && v == g_hudFrameRtv && g_hudRtv) ? g_hudRtv : v;
}
extern ID3D11Texture2D *g_eyeTex[2];
extern unsigned g_eyeTexFrame[2];
extern UINT g_eyeTexW, g_eyeTexH;
bool eyeGrab(int e);

#endif // MW4VR_HUD_H
