// What rec.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_REC_H
#define MW4VR_REC_H

extern void *volatile g_recDev; // the video's own device (shares the vtable with the game's, like the VR runtime's)
// the hot key (Ctrl+F5); once a frame, on the render thread
void recKeys();
// a video runs: the frame's layers are wanted whether the headset shows them or not
bool recRunning();
// once at the beginning of a Present's work for the headset: is this frame's picture one for the video?
bool recBegin();
// while recBegin said yes: what was put into the swap chain's image in this frame. keep: tex itself lasts until
// recFrame (a reference is held); otherwise a copy is made now.
void recImage(XrSwapchain sc, ID3D11DeviceContext *ctx, ID3D11Texture2D *tex, bool keep);
// the frame's layers as they go to xrEndFrame: drawn for both eyes, side by side, into the video's next picture
void recFrame(ID3D11DeviceContext *ctx, const XrCompositionLayerBaseHeader *const *layers, uint32_t n);
// while a video runs: a red light into the HUD layer's image (w x h used of it) - in the headset, not in the video
void recLight(ID3D11DeviceContext *ctx, ID3D11Texture2D *tex, UINT w, UINT h);

#endif // MW4VR_REC_H
