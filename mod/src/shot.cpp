// ---------------------------------------------------------------------------------------------
// Pictures of what goes to the headset, for diagnosis: both eyes side by side (left | right) as
// they are handed to the runtime, and the HUD layer's image, as .bmp into mw4vr_shots\.
// Ctrl+F12: one picture at full size. Ctrl+Shift+F12: a series, every 4th frame for about 5 s,
// each eye at most 720 pixels wide (again to stop). The file names carry the frame number the
// log counts with.

#include "mod.h"

static ID3D11Texture2D *g_shotStage;
static D3D11_TEXTURE2D_DESC g_shotStageD;
int g_shotOne;           // a single picture is due
static int g_shotSeries; // frames of the series left
static unsigned g_shotCount;

static float shotHalf(uint16_t h) {
    int e = (h >> 10) & 31, m = h & 1023;
    float v = e == 0 ? ldexpf((float)m, -24) : e == 31 ? 65504.0f : ldexpf((float)(m + 1024), e - 25);
    return (h & 0x8000) ? -v : v;
}
// unsigned small floats of R11G11B10_FLOAT: mb mantissa bits, 5 exponent bits
static float shotUFloat(uint32_t v, int mb) {
    int e = (v >> mb) & 31, m = v & ((1 << mb) - 1);
    float f = (float)m / (float)(1 << mb);
    return e == 0 ? ldexpf(f, -14) : ldexpf(1 + f, e - 15);
}
static inline uint8_t shotLin(float v) { // linear light -> display
    if (!(v > 0)) return 0;
    if (v > 1) v = 1;
    return (uint8_t)(sqrtf(v) * 255.0f + 0.5f);
}

// the texture's pixels, every step-th of them, as BGR rows top-down into out (appended at column x0 of a
// picture ow wide: out must hold ow * (height / step) * 3 bytes); false: this format is not read
bool shotRead(ID3D11DeviceContext *ctx, ID3D11Texture2D *src, UINT step, uint8_t *out, UINT ow, UINT x0, UINT w,
              UINT h) {
    D3D11_TEXTURE2D_DESC d;
    src->GetDesc(&d);
    if (d.SampleDesc.Count != 1) return false;
    if (!g_shotStage || g_shotStageD.Width != d.Width || g_shotStageD.Height != d.Height ||
        g_shotStageD.Format != d.Format) {
        if (g_shotStage) g_shotStage->Release();
        g_shotStage = nullptr;
        D3D11_TEXTURE2D_DESC n = d;
        n.MipLevels = 1, n.ArraySize = 1;
        n.Usage = D3D11_USAGE_STAGING, n.BindFlags = 0, n.CPUAccessFlags = D3D11_CPU_ACCESS_READ, n.MiscFlags = 0;
        if (FAILED(g_dev->CreateTexture2D(&n, nullptr, &g_shotStage)) || !g_shotStage) return false;
        g_shotStageD = d;
    }
    ctx->CopySubresourceRegion(g_shotStage, 0, 0, 0, 0, src, 0, nullptr);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ORIG(MechPFN_Map, ctx, VT_Ctx_Map)(ctx, g_shotStage, 0, D3D11_MAP_READ, 0, &m)) || !m.pData)
        return false;
    bool ok = true;
    int f = (int)d.Format;
    for (UINT y = 0; y < h && ok; ++y) {
        const uint8_t *row = (const uint8_t *)m.pData + (size_t)(y * step) * m.RowPitch;
        uint8_t *o = out + ((size_t)y * ow + x0) * 3;
        for (UINT x = 0; x < w; ++x, o += 3) {
            UINT sx = x * step;
            if (f >= 27 && f <= 31) { // R8G8B8A8
                const uint8_t *p = row + sx * 4;
                o[0] = p[2], o[1] = p[1], o[2] = p[0];
            } else if ((f >= 87 && f <= 88) || (f >= 90 && f <= 93)) { // B8G8R8A8 / X8
                const uint8_t *p = row + sx * 4;
                o[0] = p[0], o[1] = p[1], o[2] = p[2];
            } else if (f >= 23 && f <= 25) { // R10G10B10A2
                uint32_t v = *(const uint32_t *)(row + sx * 4);
                o[2] = (uint8_t)((v & 1023) >> 2), o[1] = (uint8_t)(((v >> 10) & 1023) >> 2),
                o[0] = (uint8_t)(((v >> 20) & 1023) >> 2);
            } else if (f == 9 || f == 10) { // R16G16B16A16 float
                const uint16_t *p = (const uint16_t *)(row + sx * 8);
                o[2] = shotLin(shotHalf(p[0])), o[1] = shotLin(shotHalf(p[1])), o[0] = shotLin(shotHalf(p[2]));
            } else if (f == 11) { // R16G16B16A16 unorm
                const uint16_t *p = (const uint16_t *)(row + sx * 8);
                o[2] = (uint8_t)(p[0] >> 8), o[1] = (uint8_t)(p[1] >> 8), o[0] = (uint8_t)(p[2] >> 8);
            } else if (f == 26) { // R11G11B10 float
                uint32_t v = *(const uint32_t *)(row + sx * 4);
                o[2] = shotLin(shotUFloat(v & 2047, 6)), o[1] = shotLin(shotUFloat((v >> 11) & 2047, 6)),
                o[0] = shotLin(shotUFloat(v >> 22, 5));
            } else {
                ok = false;
                break;
            }
        }
    }
    ORIG(MechPFN_Unmap, ctx, VT_Ctx_Unmap)(ctx, g_shotStage, 0);
    return ok;
}

static bool shotWriteBmp(const wchar_t *path, const uint8_t *bgr, UINT w, UINT h) {
    FILE *f = fileOpen(path, L"wb");
    if (!f) return false;
    UINT pitch = (w * 3 + 3) & ~3u, size = pitch * h;
#pragma pack(push, 1)
    struct {
        uint16_t bm;
        uint32_t fileSize;
        uint32_t res;
        uint32_t off;
        uint32_t hdr;
        int32_t w, h;
        uint16_t planes, bpp;
        uint32_t comp, img, xp, yp, used, imp;
    } hd = {0x4d42, 54 + size, 0, 54, 40, (int32_t)w, -(int32_t)h, 1, 24, 0, size, 2835, 2835, 0, 0};
#pragma pack(pop)
    fwrite(&hd, 54, 1, f);
    const uint8_t pad[4] = {};
    for (UINT y = 0; y < h; ++y) {
        fwrite(bgr + (size_t)y * w * 3, 1, w * 3, f);
        if (pitch > w * 3) fwrite(pad, 1, pitch - w * 3, f);
    }
    fclose(f);
    return true;
}

// one texture, or two side by side (b may be null), into <dir>\<name>
static bool shotSave(ID3D11DeviceContext *ctx, ID3D11Texture2D *a, ID3D11Texture2D *b, UINT maxW, const wchar_t *name,
                     int *fmt) {
    D3D11_TEXTURE2D_DESC d;
    a->GetDesc(&d);
    *fmt = (int)d.Format;
    UINT step = 1;
    while (maxW && d.Width / step > maxW) ++step;
    UINT w = d.Width / step, h = d.Height / step, ow = b ? 2 * w : w;
    if (!w || !h) return false;
    static std::vector<uint8_t> px;
    px.resize((size_t)ow * h * 3);
    if (!shotRead(ctx, a, step, px.data(), ow, 0, w, h)) return false;
    if (b) {
        D3D11_TEXTURE2D_DESC d2;
        b->GetDesc(&d2);
        if (d2.Width != d.Width || d2.Height != d.Height || !shotRead(ctx, b, step, px.data(), ow, w, w, h))
            return false;
    }
    std::wstring dir = g_dir + L"mw4vr_shots";
    CreateDirectoryW(dir.c_str(), nullptr);
    return shotWriteBmp((dir + L"\\" + name).c_str(), px.data(), ow, h);
}

// hot keys; once a frame
void shotKeys() {
    static bool was;
    bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_F12) & 0x8000);
    if (down && !was) {
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
            g_shotSeries = g_shotSeries ? 0 : 480; // frames to go; every 4th is kept
            logf("shot: series %s at frame %u", g_shotSeries ? "started" : "stopped", g_frame);
            Beep(g_shotSeries ? 1500 : 700, 60);
        } else {
            g_shotOne = 1;
        }
    }
    was = down;
}

// with the frame's pictures at hand: l / r this frame's eyes (r null: one picture for both, in l), hud or null
void shotFrame(ID3D11DeviceContext *ctx, ID3D11Texture2D *l, ID3D11Texture2D *r, ID3D11Texture2D *hud,
               const char *what) {
    bool one = g_shotOne != 0, ser = false;
    if (g_shotSeries > 0) {
        ser = (g_shotSeries % 4) == 0;
        if (--g_shotSeries == 0) {
            logf("shot: series ended at frame %u (%u pictures so far)", g_frame, g_shotCount + (ser ? 1 : 0));
            Beep(700, 60);
        }
    }
    if (!one && !ser) return;
    if (!ctx || !l) return;
    g_shotOne = 0;
    wchar_t name[80];
    int fmt = 0, hfmt = 0;
    swprintf(name, 80, L"%s_%06u_%s.bmp", one ? L"shot" : L"seq", g_frame, r ? L"stereo" : L"single");
    bool ok = shotSave(ctx, l, r, one ? 0 : 720, name, &fmt);
    bool hok = false;
    if (hud) {
        swprintf(name, 80, L"%s_%06u_hud.bmp", one ? L"shot" : L"seq", g_frame);
        hok = shotSave(ctx, hud, nullptr, one ? 0 : 720, name, &hfmt);
    }
    ++g_shotCount;
    if (one || !ok) {
        logf("shot: frame %u %s (%s, format %d)%s", g_frame, ok ? "saved" : "NOT saved - format not read", what, fmt,
             hud ? (hok ? ", HUD layer saved" : ", HUD layer not saved") : ", no HUD layer in this frame");
        Beep(ok ? 1500 : 300, ok ? 40 : 200);
    }
    if (!one && !ok) g_shotSeries = 0;
}
