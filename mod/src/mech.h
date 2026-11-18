// What mech.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_MECH_H
#define MW4VR_MECH_H

extern volatile bool g_mechOn;   // [vr] mech_profiles=1
extern volatile bool g_mechAuto; // [vr] mech_auto_fit=1
enum { MG_AZ = 20, MG_EL = 18, MG_N = MG_AZ * MG_EL, MG_SAMPLES = 15, MECH_MAX = 64 };
extern bool g_mechSearching;       // samples are being collected
extern McSeat g_mechSeat;          // seen from outside (third person)? (core.h)
extern bool g_mechMw5;             // a cockpit of our own is in use (mw5.cpp tells): its seat counts
extern bool g_mechSeated;          // the game draws its cockpit: the camera sits in the mech (not a mission's intro)
extern unsigned g_mechBattleFrame; // the battle's first frame
struct MechProfile {
    wchar_t name[48];
    wchar_t model[32]; // the mech's folder in the game's files ("catapult"); empty: not known yet
    float sig[MG_N];
    bool tuned;
    float cockpitScale, seatFwd, seatUp, panelX, panelY, panelDist, panelScale;
    bool seat5;              // the seat was set in a cockpit of our own
    float seat5Fwd, seat5Up; // that seat
    float dash, top, centre; // console distance (m), its top (degrees), the window's middle (degrees)
};
extern MechProfile g_mechs[MECH_MAX];
extern int g_mechN, g_mechCur;
std::wstring mechIni();
// the values of a profile set by hand are read again (they can be edited while playing)
void mechReload(int i);
typedef HRESULT(STDMETHODCALLTYPE *MechPFN_Map)(ID3D11DeviceContext *, ID3D11Resource *, UINT, D3D11_MAP, UINT,
                                                D3D11_MAPPED_SUBRESOURCE *);
typedef void(STDMETHODCALLTYPE *MechPFN_Unmap)(ID3D11DeviceContext *, ID3D11Resource *, UINT);
// a CPU-path draw of an eye's pass of the main camera. Returns how many triangles of the game's cockpit it draws.
int mechDraw(ID3D11DeviceContext *c, bool indexed, UINT count, UINT start, INT baseV, bool collect);
// (in every frame: the sway is measured from the cockpit's vertices, and they tell whether the camera sits in the mech)
static inline bool mechMeasuring() { return g_camInMain && (g_camPassEye == 0 || g_camPassEye == 1); }
// inBattle: the main camera is drawn with head tracking; zoomed: its projection is not the wide one
void mechOnPresent(bool inBattle, bool zoomed);
struct MechValues {
    float cockpitScale, seatFwd, seatUp, panelX, panelY, panelDist, panelScale;
};
// g: the [vr] values; out: what this cockpit gets. false: the [vr] values stay
bool mechValues(const MechValues &g, MechValues *out, const char **how);
// the values in use become this cockpit's own ("set by hand"); false: no cockpit is known now
bool mechStore(const MechValues &v);

#endif // MW4VR_MECH_H
