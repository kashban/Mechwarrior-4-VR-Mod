// What shot.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_SHOT_H
#define MW4VR_SHOT_H

extern int g_shotOne; // a single picture is due
// the texture's pixels, every step-th of them, as BGR rows top-down into out (appended at column x0 of a
// picture ow wide: out must hold ow * (height / step) * 3 bytes); false: this format is not read
bool shotRead(ID3D11DeviceContext *ctx, ID3D11Texture2D *src, UINT step, uint8_t *out, UINT ow, UINT x0, UINT w,
              UINT h);
// hot keys; once a frame
void shotKeys();
// with the frame's pictures at hand: l / r this frame's eyes (r null: one picture for both, in l), hud or null
void shotFrame(ID3D11DeviceContext *ctx, ID3D11Texture2D *l, ID3D11Texture2D *r, ID3D11Texture2D *hud,
               const char *what);

#endif // MW4VR_SHOT_H
