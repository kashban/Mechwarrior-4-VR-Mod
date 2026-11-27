// What mw5.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_MW5_H
#define MW4VR_MW5_H

typedef void(STDMETHODCALLTYPE *Mw5PFN_DrawIdx)(ID3D11DeviceContext *, UINT, UINT, INT);
extern int g_mw5State; // 0: not loaded yet, 1: ready, -1: failed (stays off)
extern volatile bool g_mw5On;
extern wchar_t g_mw5Name[64];
// Not while the game draws no cockpit (a mission's intro, seen from outside). Which cockpit: by the mech's name as
// the game tells it, from the battle's first frame (mw5ForMech) - not only once the mech's profile is recognised
// by its cockpit's shape, which takes seconds and showed the cockpit chosen for all mechs until then.
// Nor while the camera is outside the mech in a battle (the third-person view, g_mechSeat).
static inline bool mw5Waiting() { return g_mechOn && g_doubleDraw && (!g_mechSeated || g_mechSeat.outside); }
static inline bool mw5Active() { return g_mw5On && g_mw5State == 1 && !mw5Waiting(); }
// the monitors show the HUD (the near HUD quads are then left out, vr.cpp)
bool mw5ScreensOn();
// a monitor's piece of the HUD image that reaches below the top strip (a list set longer by hand): the far HUD
// does not show that again
void mw5MonitorLists(McHud &hd, float T);
// The parts of the HUD shown on the glass: those no monitor shows, with a width. 0: none (no cockpit of our own).
int mw5GlassItems(GlassItem *out);
// the parts of the HUD image's top strip (above v = T) the monitors show, as spans of u in rising order
int mw5TopSpans(float T, float (*iv)[2]);
// the left side column of the HUD image (u < L): how far down the monitors' second parts reach (0: none)
float mw5LeftAltBottom(float L);
// The lance list on the glass or on a monitor, with its open orders menu: the column of the HUD image (u) that tells
// how far down the menu reaches, and the row (v) the part ends at without being told. mw5LanceDown: it reaches down to
// v now (0: no further than that row); other: something else stands below it, above the row mw5LanceHoldTo.
// mw5LanceHold: the far HUD's left column begins below this row (0: where it would anyway) - the answer about the
// menu comes pictures late, and until then the far HUD would show the menu's end.
void mw5LanceColumn(float *u, float *v);
void mw5LanceDown(float v, bool other);
float mw5LanceHoldTo();
float mw5LanceHold();
// at Present, with the HUD image of this frame complete
void mw5HudCopy(ID3D11DeviceContext *c, ID3D11Texture2D *hud);
extern unsigned g_m5PassFrame[2];
extern int g_m5PassDraws[2], g_m5PassOrd[2], g_m5PassCk[2];
extern unsigned g_m5AllDraws; // every Draw / DrawIndexed of the immediate context, counted on (which draw of the frame
void mw5Draw(ID3D11DeviceContext *c, Mw5PFN_DrawIdx draw, int ckTris);
// The part the keys change, for some seconds after it was chosen, placed or changed: a yellow frame around it in
// the HUD picture of this frame - seen wherever the part is shown, on a monitor, on the glass or in the row.
void mw5HighlightDraw(ID3D11DeviceContext *c);
// hot keys, once a frame
void mw5OnPresent();

#endif // MW4VR_MW5_H
