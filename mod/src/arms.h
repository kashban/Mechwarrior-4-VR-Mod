// What arms.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_ARMS_H
#define MW4VR_ARMS_H

// A part of the player's own mech that is seen from the seat (an arm, a weapon pod): its shape in the game
// camera's space (x right, y up, z forward; game metres), 32 bytes a vertex as the cockpit's parts have them.
struct ArmPart {
    ID3D11Buffer *vb, *ib;
    UINT nIdx;
    ID3D11ShaderResourceView *clr;
    int swing; // an arm: it goes forth and back with the mech's steps (+1: the right arm, -1: the left; 0: not)
};
enum { ARMS_MAX = 48 };
extern ArmPart g_arm[ARMS_MAX];
extern int g_armN;               // parts to draw (0: none - no file for this mech yet, or [vr] arms=0)
extern volatile bool g_armsOn;   // [vr] arms=1
extern volatile bool g_armsWant; // the mech's parts are to be read at its next picture from outside
typedef HRESULT(STDMETHODCALLTYPE *PFN_CreateIL)(ID3D11Device *, const D3D11_INPUT_ELEMENT_DESC *, UINT, const void *,
                                                 SIZE_T, ID3D11InputLayout **);
HRESULT STDMETHODCALLTYPE H_CreateInputLayout(ID3D11Device *d, const D3D11_INPUT_ELEMENT_DESC *el, UINT n,
                                              const void *code, SIZE_T len, ID3D11InputLayout **out);
// mw5.cpp: the game drew its cockpit for the left eye in this frame - the camera sits in the mech
void armsSeatFrame();
// draws.cpp: a DrawIndexed of the immediate context, before it is passed on
void armsOnDraw(ID3D11DeviceContext *c, UINT count, UINT start, INT baseV);
// once a frame
void armsOnPresent();
// How far (m) the right arm is ahead of its place in this frame, with the mech's steps; the left arm is as far
// behind. 0 while the mech stands.
float armsSwing();

#endif // MW4VR_ARMS_H
