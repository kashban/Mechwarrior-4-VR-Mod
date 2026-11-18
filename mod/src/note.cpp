// ---------------------------------------------------------------------------------------------
// A line of text in the headset for a few seconds (which cockpit was just chosen): drawn with GDI
// into a small bitmap and put into the HUD layer's picture above the cross-hair, over what the game
// drew there.

#include "mod.h"

#define NOTE_W 960
#define NOTE_H 64
static uint32_t g_noteBits[NOTE_W * NOTE_H]; // BGRA, top row first
static ULONGLONG g_noteUntil;

void noteShow(const wchar_t *text, int seconds) {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = NOTE_W, bi.bmiHeader.biHeight = -NOTE_H; // top-down
    bi.bmiHeader.biPlanes = 1, bi.bmiHeader.biBitCount = 32, bi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bm = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!bm || !bits) {
        if (dc) DeleteDC(dc);
        return;
    }
    HFONT font =
        CreateFontW(-44, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HGDIOBJ oldBm = SelectObject(dc, bm), oldFont = font ? SelectObject(dc, font) : nullptr;
    RECT r = {0, 0, NOTE_W, NOTE_H};
    HBRUSH back = CreateSolidBrush(RGB(16, 24, 16));
    FillRect(dc, &r, back);
    DeleteObject(back);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(120, 255, 120));
    DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    GdiFlush();
    const uint32_t *src = (const uint32_t *)bits;
    for (int i = 0; i < NOTE_W * NOTE_H; ++i) g_noteBits[i] = src[i] | 0xff000000u; // GDI leaves the alpha at 0
    if (oldFont) SelectObject(dc, oldFont);
    SelectObject(dc, oldBm);
    if (font) DeleteObject(font);
    DeleteObject(bm);
    DeleteDC(dc);
    g_noteUntil = GetTickCount64() + (ULONGLONG)seconds * 1000;
}

// into the picture tex (w x h used of it, 8-bit BGRA), while a note is due
void noteDraw(ID3D11DeviceContext *ctx, ID3D11Texture2D *tex, UINT w, UINT h) {
    if (!g_noteUntil || !ctx || !tex) return;
    if (GetTickCount64() > g_noteUntil) {
        g_noteUntil = 0;
        return;
    }
    if (w < NOTE_W || h < NOTE_H * 8) return;
    UINT x = (w - NOTE_W) / 2, y = h / 5; // above the cross-hair, in the far middle of the HUD
    D3D11_BOX box = {x, y, 0, x + NOTE_W, y + NOTE_H, 1};
    ctx->UpdateSubresource(tex, 0, &box, g_noteBits, NOTE_W * 4, 0);
}
