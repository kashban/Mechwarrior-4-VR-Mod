// What vr.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_VR_H
#define MW4VR_VR_H

#define XR_NO_PROTOTYPES
#include "openxr/openxr.h"
#include "openxr/openxr_loader_negotiation.h"
typedef struct XrSwapchainImageD3D11KHR {
    XrStructureType type;
    void *next;
    ID3D11Texture2D *texture;
} XrSwapchainImageD3D11KHR;
typedef struct XrGraphicsRequirementsD3D11KHR {
    XrStructureType type;
    void *next;
    LUID adapterLuid;
    D3D_FEATURE_LEVEL minFeatureLevel;
} XrGraphicsRequirementsD3D11KHR;
typedef XrResult(XRAPI_PTR *PFN_xrGetD3D11GraphicsRequirementsKHR)(XrInstance, XrSystemId,
                                                                   XrGraphicsRequirementsD3D11KHR *);
struct VrState {
    bool tried = false, ok = false, running = false;
    HMODULE runtime = nullptr;
    PFN_xrGetInstanceProcAddr gipa = nullptr;
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    XrSwapchain sc[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    std::vector<XrSwapchainImageD3D11KHR> img[2];
    XrSwapchain hudSc = XR_NULL_HANDLE; // the HUD layer's image
    int64_t hudFormat = 0;
    std::vector<XrSwapchainImageD3D11KHR> hudImg;
    bool hudHave = false, hudLayer = true;
    float hudNear = 1.5f, hudTop = 0.12f, hudBottom = 0.77f; // the edge strips' distance; where they end / begin
    float hudLeft = 0.25f, hudRight = 0.72f;                 // the far middle's sides; beside it the near side columns
    float hudPanelScale = 0.6f;   // the edge strips' angular size against the game's image (drawn towards the middle)
    float cockpitScale = 1.0f;    // the cockpit's size: eye movement for the near CPU-path geometry, times this
    bool mechReproject = true;    // the older eye image turned by what the mech turned since
    float panelX = 0, panelY = 0; // the near HUD frame moved right / up, metres
    // with the own cockpit's monitors: the torso mark (below the cross-hair, in the HUD image) is shown this much
    // higher, clear of the dashboard
    float twist[4] = {0.42f, 0.585f, 0.58f, 0.665f}, twistDv = -0.045f;
    float seatFwd = 0, seatUp = 0; // the eyes moved forward / up in the cockpit, game metres
    float zoomWinScale =
        0.45f; // the zoom window's size against the game's picture (smaller = weaker zoom, less in the way)
    float cross[4] = {0.39f, 0.415f, 0.61f, 0.585f},
          crossScale = 0.25f; // the cross-hair's part of the HUD image, and its size
    float menuScale = 1.5f; // a paused battle's menu: the HUD image as one screen, at this size against the game's view
                            // (0: as it comes)
    float zoomWinMargin =
        0.02f;              // the HUD cut away around the zoom window (the game's frame of it), part of the picture
    float zoomScale = 0.9f; // zoomed: the image shown at this share of the full view (smaller = weaker zoom)
    float baseP00 = 0, baseP11 = 0; // the game's projection at its widest (not zoomed): what the layers span
    UINT w = 0, h = 0;
    bool have[2] = {false, false}; // an image of this eye was delivered
    float distance = 2.5f, width = 4.0f, eyeOffset = 0.032f;
    float ipd = 0.064f, hudDistance = 5.0f; // the quads sit in front of each eye; the HUD's apparent depth
    unsigned frames = 0;
    // head tracking
    bool tracking = true;    // [vr] tracking: projection layers following the head (0 = the screen)
    float worldScale = 1.0f; // head and eye movement in the game, times this
    float fovScale = 1.0f;   // the rendered field of view: the headset's, its tangents times this
    bool haveRef = false, recenter = false;
    // the runtime moves its space (the headset's own "recentre"): from this time on our reference is taken anew
    bool spaceChange = false;
    XrTime spaceChangeAt = 0;
    XrPosef ref = {{0, 0, 0, 1}, {0, 0, 0}}; // the head pose the game camera stands for
    XrPosef next[2], used[2];                // located for the next frame; the pose each eye's image was rendered with
    bool nextOk[2] = {}, usedOk[2] = {};
    XrFovf usedFov[2];
    XrPosef layerPose[2]; // the pose each eye's latest image belongs to
    XrFovf layerFov[2];   // and its field of view
    bool layerOk[2] = {};
    // functions
    PFN_xrCreateInstance CreateInstance;
    PFN_xrGetSystem GetSystem;
    PFN_xrDestroySwapchain DestroySwapchain;
    PFN_xrCreateSession CreateSession;
    PFN_xrCreateReferenceSpace CreateReferenceSpace;
    PFN_xrEnumerateSwapchainFormats EnumerateSwapchainFormats;
    PFN_xrCreateSwapchain CreateSwapchain;
    PFN_xrEnumerateSwapchainImages EnumerateSwapchainImages;
    PFN_xrAcquireSwapchainImage AcquireSwapchainImage;
    PFN_xrWaitSwapchainImage WaitSwapchainImage;
    PFN_xrReleaseSwapchainImage ReleaseSwapchainImage;
    PFN_xrPollEvent PollEvent;
    PFN_xrBeginSession BeginSession;
    PFN_xrEndSession EndSession;
    PFN_xrWaitFrame WaitFrame;
    PFN_xrBeginFrame BeginFrame;
    PFN_xrEndFrame EndFrame;
    PFN_xrLocateViews LocateViews;
    PFN_xrRequestExitSession RequestExitSession;
    PFN_xrGetD3D11GraphicsRequirementsKHR GetD3D11GraphicsRequirementsKHR;
};
extern VrState g_vr;
XrQuaternionf qconj(XrQuaternionf q);
XrVector3f qrot(XrQuaternionf q, XrVector3f v);
// eye: 0 left, 1 right, -1 the image is for both (no eye offset this frame)
// The game fades a scene in and out with a black sheet over its whole picture, drawn with the HUD: in the
// HUD layer that would darken the HUD's quads alone (dark rectangles in the view). A few points of the HUD
// image that are empty otherwise are read back (the frame before's); if none is clear the sheet is there,
// and one quad far larger than the view shows it in place of the HUD. Black sheets only: a coloured one (light
// amplification) stays as it is, with the HUD.
// A blue sheet is the one behind the menu of a paused battle (Esc): the HUD's parts - on monitors, on the glass, the
// cross-hair made small and far - cut that menu to pieces. The whole HUD image is one flat screen then, straight
// ahead ([vr] menu_scale; 0: off).
static const float g_fadePts[6][2] = {{0.50f, 0.30f}, {0.36f, 0.40f}, {0.64f, 0.40f},
                                      {0.36f, 0.70f}, {0.64f, 0.70f}, {0.30f, 0.55f}};
extern unsigned g_vrWorldFrame; // the frame mw4hook last drew its complete world in (main.cpp: its label)
extern bool g_fadeMenu;         // the sheet is a paused battle's (blue): its menu is up
extern unsigned
    g_sheetCleanFrame; // the frame whose HUD image holds the sheet and the menu alone (draws.cpp: hudSheetPost)
void vrOnPresent(IDXGISwapChain *swap, int eye);
// after buildK: the pose this frame is rendered with, for its layer at the next Present
void vrAfterBuildK();

#endif // MW4VR_VR_H
