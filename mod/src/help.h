// What help.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_HELP_H
#define MW4VR_HELP_H

extern volatile bool g_helpOn;
extern bool g_helpPause, g_helpClock;
// into the HUD layer's picture tex (w x h used of it), while the page is up: its far middle
void helpDraw(ID3D11DeviceContext *ctx, ID3D11Texture2D *tex, UINT w, UINT h, float L, float R, float T, float B);
// while the page is up and has been drawn: where it lies in that picture, in pixels (left, top, right, bottom)
bool helpBox(UINT *box);
extern volatile LONGLONG g_helpOffQpc; // what the page's time adds up to, in the counter's units
extern volatile DWORD g_helpOffMs;     // and in milliseconds
// once, in the first battle frame (the game is unpacked and mw4hook loaded by then)
void helpClocks();
// What was typed while the game waited is thrown away, so the game does not act on it afterwards (Esc: its own
// pause menu): the key-down messages in the window's queue, and the keyboard's buffer in DirectInput (through
// our dinput.dll, if it is the one loaded). The releases stay, so no key the game knew as held stays held -
// only in DirectInput's buffer they go too: a key held before the page and let go while it was up counts as
// held until it is pressed again.
void helpForgetKeys();
// Ctrl+F1, once a frame: the page on / off (on only in a battle, where it can be seen); Esc closes it too
void helpKey(bool battle);

#endif // MW4VR_HELP_H
