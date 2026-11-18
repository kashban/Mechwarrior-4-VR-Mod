// What cross.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_CROSS_H
#define MW4VR_CROSS_H

// part: the cross-hair's part of the HUD picture (w x h), readable by a shader. crossTex: the cross-hair's image
// (w x h). hudTex: the HUD layer's image; the part lies at x0, y0 in it. fmt: both images' format.
bool crossSplit(ID3D11DeviceContext *c, ID3D11ShaderResourceView *part, ID3D11Texture2D *crossTex,
                ID3D11Texture2D *hudTex, UINT x0, UINT y0, UINT w, UINT h, DXGI_FORMAT fmt);

#endif // MW4VR_CROSS_H
