// MW4VR - VR prototype, stage 1: a stereo screen in the headset.
//
// The game renders the eyes alternately (eye offset mode 3); at every Present the back buffer is
// copied into the swap chain of the eye it was rendered for, and two quad layers - one visible to
// each eye - show the latest image of each eye as a large screen in front of the player.
//
// OpenXR is used without the loader: the active 32-bit runtime is read from the registry, loaded,
// and asked for xrGetInstanceProcAddr through the loader <-> runtime negotiation interface.
// Included into main.cpp (one translation unit), after the device and logging helpers.

#define XR_NO_PROTOTYPES
#include "openxr/openxr.h"
#include "openxr/openxr_loader_negotiation.h"

// from openxr_platform.h (XR_KHR_D3D11_enable), declared here to avoid the platform header
typedef struct XrGraphicsBindingD3D11KHR {
    XrStructureType type;
    const void *next;
    ID3D11Device *device;
} XrGraphicsBindingD3D11KHR;
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
    XrSwapchain hudSc = XR_NULL_HANDLE;   // the HUD layer's image
    std::vector<XrSwapchainImageD3D11KHR> hudImg;
    bool hudHave = false, hudLayer = true;
    float hudNear = 1.5f, hudTop = 0.12f, hudBottom = 0.78f; // the edge strips' distance; where they end / begin
    float hudLeft = 0.25f, hudRight = 0.72f; // the far middle's sides; beside it the near side columns
    float hudPanelScale = 0.6f;    // the edge strips' angular size against the game's image (drawn towards the middle)
    float cockpitScale = 1.0f;     // the cockpit's size: eye movement for the near CPU-path geometry, times this
    bool mechReproject = true;     // the older eye image turned by what the mech turned since
    float panelX = 0, panelY = 0;  // the near HUD frame moved right / up, metres
    // with the own cockpit's monitors: the torso mark (below the cross-hair, in the HUD image) is shown this much higher,
    // clear of the dashboard
    float twist[4] = {0.42f, 0.585f, 0.58f, 0.665f}, twistDv = -0.045f;
    float seatFwd = 0, seatUp = 0; // the eyes moved forward / up in the cockpit, game metres
    float zoomWinScale = 0.45f;    // the zoom window's size against the game's picture (smaller = weaker zoom, less in the way)
    float zoomWinMargin = 0.02f;   // the HUD cut away around the zoom window (the game's frame of it), part of the picture
    float zoomScale = 0.9f;        // zoomed: the image shown at this share of the full view (smaller = weaker zoom)
    float baseP00 = 0, baseP11 = 0; // the game's projection at its widest (not zoomed): what the layers span
    UINT w = 0, h = 0;
    bool have[2] = {false, false}; // an image of this eye was delivered
    float distance = 2.5f, width = 4.0f, eyeOffset = 0.032f;
    float ipd = 0.064f, hudDistance = 5.0f; // the quads sit in front of each eye; the HUD's apparent depth
    unsigned frames = 0;
    // head tracking
    bool tracking = true;          // [vr] tracking: projection layers following the head (0 = the screen)
    float worldScale = 1.0f;       // head and eye movement in the game, times this
    float fovScale = 1.0f;         // the rendered field of view: the headset's, its tangents times this
    bool haveRef = false, recenter = false;
    XrPosef ref = {{0, 0, 0, 1}, {0, 0, 0}};   // the head pose the game camera stands for
    XrPosef next[2], used[2];      // located for the next frame; the pose each eye's image was rendered with
    bool nextOk[2] = {}, usedOk[2] = {};
    XrFovf usedFov[2];
    XrPosef layerPose[2];          // the pose each eye's latest image belongs to
    XrFovf layerFov[2];            // and its field of view
    bool layerOk[2] = {};
    // functions
    PFN_xrCreateInstance CreateInstance;
    PFN_xrGetSystem GetSystem;
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
static VrState g_vr;

static bool vrCheck(XrResult r, const char *what) {
    if (XR_SUCCEEDED(r)) return true;
    logf("vr: %s failed (XrResult %d)", what, (int)r);
    return false;
}

template <class F> static bool vrGet(F &fn, const char *name) {
    PFN_xrVoidFunction f = nullptr;
    if (XR_FAILED(g_vr.gipa(g_vr.instance, name, &f)) || !f) {
        logf("vr: %s not available", name);
        return false;
    }
    fn = (F)f;
    return true;
}

// the active runtime for this (32-bit) process: HKLM\SOFTWARE\Khronos\OpenXR\1 seen through WOW64
static bool vrLoadRuntime() {
    wchar_t json[MAX_PATH] = {};
    DWORD sz = sizeof json;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, json, &sz) != ERROR_SUCCESS) {
        logf("vr: no active 32-bit OpenXR runtime in the registry");
        return false;
    }
    FILE *f = _wfsopen(json, L"rb", _SH_DENYNO);
    if (!f) {
        logf("vr: runtime manifest %s not readable", narrow(json).c_str());
        return false;
    }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    fclose(f);
    size_t k = text.find("\"library_path\"");
    size_t q1 = k == std::string::npos ? k : text.find('"', text.find(':', k) + 1);
    size_t q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
    if (q2 == std::string::npos) {
        logf("vr: no library_path in %s", narrow(json).c_str());
        return false;
    }
    std::string lib = text.substr(q1 + 1, q2 - q1 - 1);
    std::string clean; // JSON escapes: "\\" -> "\"
    for (size_t i = 0; i < lib.size(); ++i) {
        if (lib[i] == '\\' && i + 1 < lib.size() && lib[i + 1] == '\\') ++i;
        clean += lib[i];
    }
    std::wstring path(clean.begin(), clean.end());
    if (!(path.size() > 2 && (path[1] == L':' || (path[0] == L'\\' && path[1] == L'\\')))) {
        std::wstring dir = json;
        dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
        path = dir + path;
    }
    g_vr.runtime = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_vr.runtime) {
        logf("vr: runtime %s not loaded (%lu)", narrow(path.c_str()).c_str(), GetLastError());
        return false;
    }
    auto negotiate = (PFN_xrNegotiateLoaderRuntimeInterface)GetProcAddress(g_vr.runtime,
                                                                           "xrNegotiateLoaderRuntimeInterface");
    if (!negotiate) {
        logf("vr: runtime has no xrNegotiateLoaderRuntimeInterface");
        return false;
    }
    XrNegotiateLoaderInfo li = {};
    li.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
    li.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
    li.structSize = sizeof li;
    li.minInterfaceVersion = 1;
    li.maxInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    li.minApiVersion = XR_MAKE_VERSION(1, 0, 0);
    li.maxApiVersion = XR_MAKE_VERSION(1, 0x3ff, 0xfff);
    XrNegotiateRuntimeRequest rr = {};
    rr.structType = XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST;
    rr.structVersion = XR_RUNTIME_INFO_STRUCT_VERSION;
    rr.structSize = sizeof rr;
    if (XR_FAILED(negotiate(&li, &rr)) || !rr.getInstanceProcAddr) {
        logf("vr: negotiation with the runtime failed");
        return false;
    }
    g_vr.gipa = rr.getInstanceProcAddr;
    logf("vr: runtime %s loaded (interface %u, API %u.%u)", narrow(path.c_str()).c_str(),
         rr.runtimeInterfaceVersion, XR_VERSION_MAJOR(rr.runtimeApiVersion), XR_VERSION_MINOR(rr.runtimeApiVersion));
    return true;
}

static void vrReadIni() {
    std::wstring p = g_dir + L"mw4vr.ini";
    wchar_t b[64];
    GetPrivateProfileStringW(L"vr", L"screen_distance", L"2.5", b, 64, p.c_str());
    g_vr.distance = (float)_wtof(b);
    GetPrivateProfileStringW(L"vr", L"screen_width", L"4.0", b, 64, p.c_str());
    g_vr.width = (float)_wtof(b);
    GetPrivateProfileStringW(L"vr", L"eye_offset_m", L"0.032", b, 64, p.c_str());
    g_vr.eyeOffset = (float)_wtof(b);
    GetPrivateProfileStringW(L"vr", L"ipd_m", L"0.064", b, 64, p.c_str());
    g_vr.ipd = (float)_wtof(b);
    g_vr.tracking = GetPrivateProfileIntW(L"vr", L"tracking", 1, p.c_str()) != 0;
    g_srcCam = g_vr.tracking && GetPrivateProfileIntW(L"vr", L"source_camera", 1, p.c_str()) != 0;
    if (g_srcCam && !g_camHooked) camInstall();
    GetPrivateProfileStringW(L"vr", L"cockpit_scale", L"1.0", b, 64, p.c_str());
    g_vr.cockpitScale = (float)_wtof(b);
    if (g_vr.cockpitScale <= 0) g_vr.cockpitScale = 1;
    GetPrivateProfileStringW(L"vr", L"cockpit_range_m", L"8.0", b, 64, p.c_str());
    g_cockpitRange = (float)_wtof(b);
    g_vr.mechReproject = GetPrivateProfileIntW(L"vr", L"mech_reproject", 1, p.c_str()) != 0;
    {
        static int last = -1;
        int dd = GetPrivateProfileIntW(L"vr", L"double_draw", 1, p.c_str()) != 0;
        if (dd != last) g_doubleDraw = dd != 0, last = dd; // (a failed try switches it off until the ini changes)
    }
    GetPrivateProfileStringW(L"vr", L"hud_panel_x_m", L"0", b, 64, p.c_str());
    g_vr.panelX = (float)_wtof(b);
    GetPrivateProfileStringW(L"vr", L"hud_panel_y_m", L"0", b, 64, p.c_str());
    g_vr.panelY = (float)_wtof(b);
    GetPrivateProfileStringW(L"vr", L"world_scale", L"1.0", b, 64, p.c_str());
    g_vr.worldScale = (float)_wtof(b);
    if (g_vr.worldScale <= 0) g_vr.worldScale = 1;
    g_cockpitDelta = g_vr.cockpitScale - g_vr.worldScale;
    GetPrivateProfileStringW(L"vr", L"seat_forward_m", L"0", b, 64, p.c_str());
    g_vr.seatFwd = (float)_wtof(b);
    GetPrivateProfileStringW(L"vr", L"seat_up_m", L"0", b, 64, p.c_str());
    g_vr.seatUp = (float)_wtof(b);
    GetPrivateProfileStringW(L"vr", L"zoom_window_scale", L"0.45", b, 64, p.c_str());
    g_vr.zoomWinScale = (float)_wtof(b);
    if (g_vr.zoomWinScale < 0.1f || g_vr.zoomWinScale > 1.0f) g_vr.zoomWinScale = 0.45f;
    GetPrivateProfileStringW(L"vr", L"zoom_window_margin", L"0.02", b, 64, p.c_str());
    g_vr.zoomWinMargin = (float)_wtof(b);
    if (g_vr.zoomWinMargin < 0 || g_vr.zoomWinMargin > 0.2f) g_vr.zoomWinMargin = 0.02f;
    GetPrivateProfileStringW(L"vr", L"zoom_scale", L"0.9", b, 64, p.c_str());
    g_vr.zoomScale = (float)_wtof(b);
    if (g_vr.zoomScale < 0.1f || g_vr.zoomScale > 1.0f) g_vr.zoomScale = 0.9f;
    GetPrivateProfileStringW(L"vr", L"fov_scale", L"1.0", b, 64, p.c_str());
    g_vr.fovScale = (float)_wtof(b);
    if (g_vr.fovScale <= 0.2f || g_vr.fovScale > 1.5f) g_vr.fovScale = 1;
    GetPrivateProfileStringW(L"vr", L"hud_distance_m", L"5.0", b, 64, p.c_str());
    g_vr.hudDistance = (float)_wtof(b);
    g_vr.hudLayer = GetPrivateProfileIntW(L"vr", L"hud_layer", 1, p.c_str()) != 0;
    g_swayOn = g_vr.hudLayer && GetPrivateProfileIntW(L"vr", L"hud_sway", 1, p.c_str()) != 0;
    GetPrivateProfileStringW(L"vr", L"hud_sway_gain", L"1.0", b, 64, p.c_str());
    g_swayGain = (float)_wtof(b);

    GetPrivateProfileStringW(L"vr", L"hud_panel_distance_m", L"1.5", b, 64, p.c_str());
    g_vr.hudNear = (float)_wtof(b);
    if (g_vr.hudNear < 0.3f) g_vr.hudNear = 0.3f;
    GetPrivateProfileStringW(L"vr", L"hud_panel_scale", L"0.6", b, 64, p.c_str());
    g_vr.hudPanelScale = (float)_wtof(b);
    if (g_vr.hudPanelScale < 0.2f || g_vr.hudPanelScale > 1.0f) g_vr.hudPanelScale = 0.6f;
    GetPrivateProfileStringW(L"vr", L"hud_top", L"0.12", b, 64, p.c_str());
    g_vr.hudTop = (float)_wtof(b);
    GetPrivateProfileStringW(L"vr", L"hud_bottom", L"0.78", b, 64, p.c_str());
    g_vr.hudBottom = (float)_wtof(b);
    if (!(g_vr.hudTop >= 0 && g_vr.hudTop < g_vr.hudBottom && g_vr.hudBottom <= 1)) g_vr.hudTop = 0.12f, g_vr.hudBottom = 0.78f;
    GetPrivateProfileStringW(L"vr", L"hud_left", L"0.25", b, 64, p.c_str());
    g_vr.hudLeft = (float)_wtof(b);
    GetPrivateProfileStringW(L"vr", L"hud_right", L"0.72", b, 64, p.c_str());
    g_vr.hudRight = (float)_wtof(b);
    if (!(g_vr.hudLeft >= 0 && g_vr.hudLeft < g_vr.hudRight && g_vr.hudRight <= 1)) g_vr.hudLeft = 0.25f, g_vr.hudRight = 0.72f;
    GetPrivateProfileStringW(L"vr", L"hud_twist_rect", L"0.42 0.585 0.58 0.665", b, 64, p.c_str());
    if (swscanf(b, L"%f %f %f %f", g_vr.twist, g_vr.twist + 1, g_vr.twist + 2, g_vr.twist + 3) != 4) g_vr.twist[0] = g_vr.twist[2] = 0;
    GetPrivateProfileStringW(L"vr", L"hud_twist_shift", L"-0.045", b, 64, p.c_str());
    g_vr.twistDv = (float)_wtof(b);
    // this cockpit's own values (mech.inc) over the [vr] ones
    g_mechOn = GetPrivateProfileIntW(L"vr", L"mech_profiles", 1, p.c_str()) != 0;
    g_mechAuto = GetPrivateProfileIntW(L"vr", L"mech_auto_fit", 1, p.c_str()) != 0;
    {
        MechValues gv = {g_vr.cockpitScale, g_vr.seatFwd, g_vr.seatUp, g_vr.panelX, g_vr.panelY, g_vr.hudNear, g_vr.hudPanelScale}, mv = gv;
        const char *how = "the [vr] values";
        if (g_mechCur >= 0 && g_mechCur < g_mechN) mechReload(g_mechCur);
        if (mechValues(gv, &mv, &how)) {
            g_vr.cockpitScale = mv.cockpitScale > 0 ? mv.cockpitScale : gv.cockpitScale;
            g_vr.seatFwd = mv.seatFwd, g_vr.seatUp = mv.seatUp;
            g_vr.panelX = mv.panelX, g_vr.panelY = mv.panelY;
            g_vr.hudNear = mv.panelDist >= 0.3f ? mv.panelDist : gv.panelDist;
            g_vr.hudPanelScale = (mv.panelScale >= 0.2f && mv.panelScale <= 1.0f) ? mv.panelScale : gv.panelScale;
            g_cockpitDelta = g_vr.cockpitScale - g_vr.worldScale;
        }
        static int lastCur = -2;
        static float last[7];
        float now[7] = {g_vr.cockpitScale, g_vr.seatFwd, g_vr.seatUp, g_vr.panelX, g_vr.panelY, g_vr.hudNear, g_vr.hudPanelScale};
        if (g_mechOn && (lastCur != g_mechCur || memcmp(last, now, sizeof now))) {
            lastCur = g_mechCur;
            memcpy(last, now, sizeof now);
            logf("mech: profile %d in use (%s) - cockpit scale %.3f, seat %.2f m forward %.2f m up, panels %.2f m right %.2f m up at %.2f m, scale %.2f",
                 g_mechCur + 1, how, now[0], now[1], now[2], now[3], now[4], now[5], now[6]);
        }
    }
    // HUD shift per eye: with each eye's quad centred in front of it, zero disparity lies at infinity;
    // a point at distance D appears ipd/2 * Q/D towards the nose on a quad at distance Q
    if (g_vr.hudDistance > 0.1f && g_srcCam && g_P00 > 0 && g_vr.w) // the game's own projection
        g_hudPx = 0.5f * g_vr.ipd / g_vr.hudDistance * g_P00 * 0.5f * (float)g_vr.w;
    else if (g_vr.hudDistance > 0.1f && g_vr.tracking && g_vrFovOk && g_vr.w) // projection layer: the rendered fov
        g_hudPx = 0.5f * g_vr.ipd / g_vr.hudDistance / (g_vrFov[0][1] - g_vrFov[0][0]) * (float)g_vr.w;
    else if (g_vr.hudDistance > 0.1f && g_vr.width > 0 && g_vr.w)
        g_hudPx = 0.5f * g_vr.ipd * g_vr.distance / g_vr.hudDistance / g_vr.width * (float)g_vr.w;
    else
        g_hudPx = 0;
    if (g_hudLayerOn) g_hudPx = 0; // the HUD is its own layer: no shift inside the eye images
}

static bool vrInit(IDXGISwapChain *swap) {
    if (!g_dev) return false;
    vrReadIni();
    if (!vrLoadRuntime()) return false;
    PFN_xrEnumerateInstanceExtensionProperties enumExt = nullptr;
    if (!vrGet(g_vr.CreateInstance, "xrCreateInstance")) return false;
    vrGet(enumExt, "xrEnumerateInstanceExtensionProperties");
    const char *exts[] = {"XR_KHR_D3D11_enable"};
    XrInstanceCreateInfo ici = {XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(ici.applicationInfo.applicationName, "MW4VR probe");
    ici.applicationInfo.applicationVersion = 1;
    strcpy_s(ici.applicationInfo.engineName, "MW4Engine");
    ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    ici.enabledExtensionCount = 1;
    ici.enabledExtensionNames = exts;
    if (!vrCheck(g_vr.CreateInstance(&ici, &g_vr.instance), "xrCreateInstance")) return false;
    if (!(vrGet(g_vr.GetSystem, "xrGetSystem") && vrGet(g_vr.CreateSession, "xrCreateSession") &&
          vrGet(g_vr.CreateReferenceSpace, "xrCreateReferenceSpace") &&
          vrGet(g_vr.EnumerateSwapchainFormats, "xrEnumerateSwapchainFormats") &&
          vrGet(g_vr.CreateSwapchain, "xrCreateSwapchain") &&
          vrGet(g_vr.EnumerateSwapchainImages, "xrEnumerateSwapchainImages") &&
          vrGet(g_vr.AcquireSwapchainImage, "xrAcquireSwapchainImage") &&
          vrGet(g_vr.WaitSwapchainImage, "xrWaitSwapchainImage") &&
          vrGet(g_vr.ReleaseSwapchainImage, "xrReleaseSwapchainImage") && vrGet(g_vr.PollEvent, "xrPollEvent") &&
          vrGet(g_vr.BeginSession, "xrBeginSession") && vrGet(g_vr.EndSession, "xrEndSession") &&
          vrGet(g_vr.WaitFrame, "xrWaitFrame") && vrGet(g_vr.BeginFrame, "xrBeginFrame") &&
          vrGet(g_vr.EndFrame, "xrEndFrame") && vrGet(g_vr.LocateViews, "xrLocateViews") && vrGet(g_vr.RequestExitSession, "xrRequestExitSession") &&
          vrGet(g_vr.GetD3D11GraphicsRequirementsKHR, "xrGetD3D11GraphicsRequirementsKHR")))
        return false;

    XrSystemGetInfo sgi = {XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!vrCheck(g_vr.GetSystem(g_vr.instance, &sgi, &g_vr.system), "xrGetSystem (is the headset connected?)"))
        return false;
    XrGraphicsRequirementsD3D11KHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    if (!vrCheck(g_vr.GetD3D11GraphicsRequirementsKHR(g_vr.instance, g_vr.system, &req),
                 "xrGetD3D11GraphicsRequirementsKHR"))
        return false;

    XrGraphicsBindingD3D11KHR gb = {XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    gb.device = g_dev;
    XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &gb;
    sci.systemId = g_vr.system;
    if (!vrCheck(g_vr.CreateSession(g_vr.instance, &sci, &g_vr.session), "xrCreateSession")) return false;

    XrReferenceSpaceCreateInfo rsci = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace.orientation.w = 1;
    if (!vrCheck(g_vr.CreateReferenceSpace(g_vr.session, &rsci, &g_vr.space), "xrCreateReferenceSpace")) return false;

    // swap chains at the game's back buffer size; sRGB if offered (the game's output is gamma-encoded)
    DXGI_SWAP_CHAIN_DESC d;
    swap->GetDesc(&d);
    g_vr.w = d.BufferDesc.Width;
    g_vr.h = d.BufferDesc.Height;
    uint32_t nf = 0;
    g_vr.EnumerateSwapchainFormats(g_vr.session, 0, &nf, nullptr);
    std::vector<int64_t> fmts(nf);
    g_vr.EnumerateSwapchainFormats(g_vr.session, nf, &nf, fmts.data());
    int64_t fmt = 0;
    const int64_t want[] = {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM};
    for (int64_t w : want)
        for (int64_t f2 : fmts)
            if (!fmt && f2 == w) fmt = w;
    if (!fmt) {
        logf("vr: the runtime offers no B8G8R8A8 swap chain format");
        return false;
    }
    for (int e = 0; e < 2; ++e) {
        XrSwapchainCreateInfo ci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        ci.format = fmt;
        ci.sampleCount = 1;
        ci.width = g_vr.w;
        ci.height = g_vr.h;
        ci.faceCount = 1;
        ci.arraySize = 1;
        ci.mipCount = 1;
        if (!vrCheck(g_vr.CreateSwapchain(g_vr.session, &ci, &g_vr.sc[e]), "xrCreateSwapchain")) return false;
        uint32_t ni = 0;
        g_vr.EnumerateSwapchainImages(g_vr.sc[e], 0, &ni, nullptr);
        g_vr.img[e].assign(ni, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        if (!vrCheck(g_vr.EnumerateSwapchainImages(g_vr.sc[e], ni, &ni,
                                                    (XrSwapchainImageBaseHeader *)g_vr.img[e].data()),
                     "xrEnumerateSwapchainImages"))
            return false;
    }
    {   // the HUD layer
        XrSwapchainCreateInfo ci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        ci.format = fmt;
        ci.sampleCount = 1;
        ci.width = hudWideW(g_vr.w); // wider than the eyes' pictures: the HUD beyond the picture's right edge (hud.inc)
        ci.height = g_vr.h;
        ci.faceCount = 1;
        ci.arraySize = 1;
        ci.mipCount = 1;
        if (XR_SUCCEEDED(g_vr.CreateSwapchain(g_vr.session, &ci, &g_vr.hudSc))) {
            uint32_t ni = 0;
            g_vr.EnumerateSwapchainImages(g_vr.hudSc, 0, &ni, nullptr);
            g_vr.hudImg.assign(ni, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            if (XR_FAILED(g_vr.EnumerateSwapchainImages(g_vr.hudSc, ni, &ni,
                                                        (XrSwapchainImageBaseHeader *)g_vr.hudImg.data())))
                g_vr.hudSc = XR_NULL_HANDLE;
        } else {
            g_vr.hudSc = XR_NULL_HANDLE;
            logf("vr: no swap chain for the HUD layer");
        }
    }
    logf("vr: session ready - swap chains %ux%u format %d, screen %.1f m wide at %.1f m, eye offset %.3f m",
         g_vr.w, g_vr.h, (int)fmt, g_vr.width, g_vr.distance, g_vr.eyeOffset);
    return true;
}

static void vrPollEvents() {
    XrEventDataBuffer ev = {XR_TYPE_EVENT_DATA_BUFFER};
    while (g_vr.PollEvent(g_vr.instance, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto *sc = (XrEventDataSessionStateChanged *)&ev;
            logf("vr: session state %d", (int)sc->state);
            if (sc->state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                g_vr.running = vrCheck(g_vr.BeginSession(g_vr.session, &bi), "xrBeginSession");
            } else if (sc->state == XR_SESSION_STATE_STOPPING) {
                g_vr.EndSession(g_vr.session);
                g_vr.running = false;
            } else if (sc->state == XR_SESSION_STATE_EXITING || sc->state == XR_SESSION_STATE_LOSS_PENDING) {
                g_vr.running = false;
            }
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

static XrQuaternionf qmul(XrQuaternionf a, XrQuaternionf b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
static XrQuaternionf qconj(XrQuaternionf q) { return {-q.x, -q.y, -q.z, q.w}; }
static XrVector3f qrot(XrQuaternionf q, XrVector3f v) {
    XrQuaternionf r = qmul(qmul(q, {v.x, v.y, v.z, 0}), qconj(q));
    return {r.x, r.y, r.z};
}

// rotation matrix (row-major, column vectors) -> quaternion
static XrQuaternionf qfromm(const float *m) {
    float tr = m[0] + m[4] + m[8];
    XrQuaternionf q;
    if (tr > 0) {
        float s = sqrtf(tr + 1) * 2;
        q = {(m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s, 0.25f * s};
    } else if (m[0] > m[4] && m[0] > m[8]) {
        float s = sqrtf(1 + m[0] - m[4] - m[8]) * 2;
        q = {0.25f * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s, (m[7] - m[5]) / s};
    } else if (m[4] > m[8]) {
        float s = sqrtf(1 + m[4] - m[0] - m[8]) * 2;
        q = {(m[1] + m[3]) / s, 0.25f * s, (m[5] + m[7]) / s, (m[2] - m[6]) / s};
    } else {
        float s = sqrtf(1 + m[8] - m[0] - m[4]) * 2;
        q = {(m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25f * s, (m[3] - m[1]) / s};
    }
    float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l > 1e-6f) q = {q.x / l, q.y / l, q.z / l, q.w / l};
    else q = {0, 0, 0, 1};
    return q;
}

// a pose in the runtime's space -> rotation and position for the game camera (view coordinates:
// x right, y up, z forward; OpenXR looks along -z), relative to the reference pose
static void vrPoseToGame(const XrPosef &pose, float *R, float *t, float *raw) {
    XrQuaternionf ri = qconj(g_vr.ref.orientation);
    XrQuaternionf q = qmul(ri, pose.orientation);
    XrVector3f d = {pose.position.x - g_vr.ref.position.x, pose.position.y - g_vr.ref.position.y,
                    pose.position.z - g_vr.ref.position.z};
    XrVector3f pr = qrot(ri, d);
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float M[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                  2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                  2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)};
    // S M S with S = diag(1, 1, -1)
    const float sg[3] = {1, 1, -1};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) R[i * 3 + j] = sg[i] * M[i * 3 + j] * sg[j];
    raw[0] = pr.x;
    raw[1] = pr.y;
    raw[2] = -pr.z;
    t[0] = pr.x * g_vr.worldScale;
    t[1] = pr.y * g_vr.worldScale + g_vr.seatUp;
    t[2] = -pr.z * g_vr.worldScale + g_vr.seatFwd;
}

// the eye poses for the frame after this one; the reference is the head's (between the eyes)
static void vrLocate(XrTime when) {
    XrViewLocateInfo li = {XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = when;
    li.space = g_vr.space;
    XrViewState vs = {XR_TYPE_VIEW_STATE};
    XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    uint32_t n = 0;
    if (XR_FAILED(g_vr.LocateViews(g_vr.session, &li, &vs, 2, &n, views)) || n != 2 ||
        !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        g_vrPoseOk[0] = g_vrPoseOk[1] = false;
        return;
    }
    if (!g_vr.haveRef || g_vr.recenter) {
        // the head between the eyes, looking where the left eye looks
        g_vr.ref.orientation = views[0].pose.orientation;
        g_vr.ref.position = {(views[0].pose.position.x + views[1].pose.position.x) / 2,
                             (views[0].pose.position.y + views[1].pose.position.y) / 2,
                             (views[0].pose.position.z + views[1].pose.position.z) / 2};
        logf("vr: %s - the game camera is the head's current pose; eye fov tangents L %.3f %.3f %.3f %.3f",
             g_vr.haveRef ? "recentred" : "reference set", tanf(views[0].fov.angleLeft), tanf(views[0].fov.angleRight),
             tanf(views[0].fov.angleUp), tanf(views[0].fov.angleDown));
        g_vr.haveRef = true;
        g_vr.recenter = false;
    }
    for (int e = 0; e < 2; ++e) {
        // symmetric per eye (the wider side of each axis): a screen-space HUD then lies in the same
        // direction for both eyes; the headset's own frusta are canted outwards and would double it
        float k = g_vr.fovScale;
        float tx = fmaxf(-tanf(views[e].fov.angleLeft), tanf(views[e].fov.angleRight)) * k;
        float ty = fmaxf(tanf(views[e].fov.angleUp), -tanf(views[e].fov.angleDown)) * k;
        g_vrFov[e][0] = -tx;
        g_vrFov[e][1] = tx;
        g_vrFov[e][2] = ty;
        g_vrFov[e][3] = -ty;
        g_vr.next[e] = views[e].pose;
        g_vr.nextOk[e] = true;
        vrPoseToGame(views[e].pose, g_vrR[e], g_vrT[e], g_vrRaw[e]);
        g_vrPoseOk[e] = true;
    }
    g_vrFovOk = true;
}

// the cockpit's values as changed by a hot key: into this cockpit's profile when it is known
// (it is "set by hand" from then on), else into [vr]
static void vrStoreValues(const MechValues &v) {
    if (!mechStore(v)) {
        std::wstring ini = g_dir + L"mw4vr.ini";
        wchar_t b[32];
        swprintf(b, 32, L"%.3f", v.cockpitScale);
        WritePrivateProfileStringW(L"vr", L"cockpit_scale", b, ini.c_str());
        swprintf(b, 32, L"%.2f", v.seatFwd);
        WritePrivateProfileStringW(L"vr", L"seat_forward_m", b, ini.c_str());
        swprintf(b, 32, L"%.2f", v.seatUp);
        WritePrivateProfileStringW(L"vr", L"seat_up_m", b, ini.c_str());
        swprintf(b, 32, L"%.2f", v.panelX);
        WritePrivateProfileStringW(L"vr", L"hud_panel_x_m", b, ini.c_str());
        swprintf(b, 32, L"%.2f", v.panelY);
        WritePrivateProfileStringW(L"vr", L"hud_panel_y_m", b, ini.c_str());
    }
    vrReadIni();
}
static inline MechValues vrValues() {
    return {g_vr.cockpitScale, g_vr.seatFwd, g_vr.seatUp, g_vr.panelX, g_vr.panelY, g_vr.hudNear, g_vr.hudPanelScale};
}

// eye: 0 left, 1 right, -1 the image is for both (no eye offset this frame)
// The game fades a scene in and out with a black sheet over its whole picture, drawn with the HUD: in the
// HUD layer that would darken the HUD's quads alone (dark rectangles in the view). A few points of the HUD
// image that are empty otherwise are read back (the frame before's); if none is clear the sheet is there,
// and one quad far larger than the view shows it in place of the HUD. Black sheets only: a coloured one (light
// amplification) stays as it is, with the HUD.
static const float g_fadePts[6][2] = {{0.50f, 0.30f}, {0.36f, 0.40f}, {0.64f, 0.40f}, {0.36f, 0.70f}, {0.64f, 0.70f}, {0.30f, 0.55f}};
static ID3D11Texture2D *g_fadeStage;
static bool g_fadePending;
static unsigned g_fadeCopyFrame = ~0u; // the frame whose HUD image waits to be read
static unsigned g_fadeKnownFrame = ~0u; // the frame the last answer came in; none lately: the HUD is not shown yet
static float g_fadeA;  // the sheet's alpha, 0: none
static int g_fadePt;   // the point showing the sheet alone
static void fadeProbe(ID3D11DeviceContext *ctx, ID3D11Texture2D *hud) {
    D3D11_TEXTURE2D_DESC d;
    hud->GetDesc(&d);
    int f = (int)d.Format;
    if (d.SampleDesc.Count != 1 || !((f >= 27 && f <= 31) || (f >= 87 && f <= 88) || (f >= 90 && f <= 93))) return;
    if (!g_fadeStage) {
        D3D11_TEXTURE2D_DESC n = d;
        n.Width = 8, n.Height = 1, n.MipLevels = 1, n.ArraySize = 1;
        n.Usage = D3D11_USAGE_STAGING, n.BindFlags = 0, n.CPUAccessFlags = D3D11_CPU_ACCESS_READ, n.MiscFlags = 0;
        if (FAILED(g_dev->CreateTexture2D(&n, nullptr, &g_fadeStage)) || !g_fadeStage) return;
    }
    if (g_fadePending) { // the frame before's
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ORIG(MechPFN_Map, ctx, VT_Ctx_Map)(ctx, g_fadeStage, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m)) && m.pData) {
            int lo = 255, at = 0;
            for (int i = 0; i < 6; ++i) {
                int a = ((const uint8_t *)m.pData)[i * 4 + 3];
                if (a < lo) lo = a, at = i;
            }
            const uint8_t *px = (const uint8_t *)m.pData + at * 4;
            if (px[0] > 12 || px[1] > 12 || px[2] > 12) lo = 0;
            ORIG(MechPFN_Unmap, ctx, VT_Ctx_Unmap)(ctx, g_fadeStage, 0);
            float a = lo >= 2 ? lo / 255.0f : 0;
            if (g_frame - g_fadeCopyFrame <= 3 && (a > 0) != (g_fadeA > 0)) logf("hud: the game's fade sheet %s (alpha %.2f, frame %u)", a > 0 ? "begins" : "is gone", a, g_frame);
            if (g_frame - g_fadeCopyFrame <= 3) { // not an old battle's last picture
                g_fadeA = a, g_fadePt = at;
                g_fadeKnownFrame = g_frame;
            }
            g_fadePending = false;
        }
    }
    if (!g_fadePending) {
        for (int i = 0; i < 6; ++i) {
            UINT x = (UINT)(g_fadePts[i][0] * g_hudW), y = (UINT)(g_fadePts[i][1] * d.Height);
            D3D11_BOX b = {x, y, 0, x + 1, y + 1, 1};
            ctx->CopySubresourceRegion(g_fadeStage, 0, i, 0, 0, hud, 0, &b);
        }
        g_fadePending = true;
        g_fadeCopyFrame = g_frame;
    }
}

static void vrOnPresent(IDXGISwapChain *swap, int eye) {
    if (!g_vr.tried) {
        g_vr.tried = true;
        std::wstring p = g_dir + L"mw4vr.ini";
        if (!GetPrivateProfileIntW(L"vr", L"enabled", 0, p.c_str())) return;
        g_vrInitRunning = true; // the runtime's own D3D11 device must not be hooked
        g_vr.ok = vrInit(swap);
        g_vrInitRunning = false;
        if (g_vr.ok) {
            g_vrActive = true;
            g_eyeMode = 3;                // alternate the eyes from now on
            g_eyeOffsetM = g_vr.eyeOffset;
        } else {
            logf("vr: not started");
        }
    }
    if (!g_vr.ok) return;
    static DWORD lastIni;
    if (GetTickCount() - lastIni >= 2000) { // the [vr] values can be tuned while playing
        lastIni = GetTickCount();
        vrReadIni();
        g_eyeOffsetM = g_vr.eyeOffset;
    }
    {   // Ctrl+Home: the game camera becomes the head's current pose
        static bool was;
        bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_HOME) & 0x8000);
        if (down && !was) {
            g_vr.baseP00 = g_vr.baseP11 = 0;
            g_vr.recenter = true;
            Beep(900, 60);
        }
        was = down;
    }
    {   // Ctrl + Up / Down: seat forward / back, Ctrl + Right / Left: seat up / down, 5 cm a press; saved at once
        static bool was[4];
        // with Shift: the near HUD panels up / down / right / left, 2 cm a press
        bool mod = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && !(GetAsyncKeyState(VK_MENU) & 0x8000), shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        const int keys[4] = {VK_UP, VK_DOWN, VK_RIGHT, VK_LEFT};
        for (int k = 0; k < 4; ++k) {
            bool down = mod && (GetAsyncKeyState(keys[k]) & 0x8000);
            if (down && !was[k]) {
                MechValues mv = vrValues();
                if (shift) {
                    if (k == 0) mv.panelY += 0.02f;
                    if (k == 1) mv.panelY -= 0.02f;
                    if (k == 2) mv.panelX += 0.02f;
                    if (k == 3) mv.panelX -= 0.02f;
                } else {
                    if (k == 0) mv.seatFwd += 0.05f;
                    if (k == 1) mv.seatFwd -= 0.05f;
                    if (k == 2) mv.seatUp += 0.05f;
                    if (k == 3) mv.seatUp -= 0.05f;
                }
                vrStoreValues(mv);
                if (shift) logf("vr: panels %.2f m right, %.2f m up (saved)", mv.panelX, mv.panelY);
                else logf("vr: seat %.2f m forward, %.2f m up (saved)", mv.seatFwd, mv.seatUp);
                Beep((k < 2 ? 700 : 1000) + (shift ? 300 : 0), 40);
            }
            was[k] = down;
        }
    }
    {   // Ctrl+F10: this cockpit fits as it is - its values become its own profile, and the measure for the others
        static bool was;
        bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_F10) & 0x8000);
        if (down && !was) {
            bool ok = mechStore(vrValues());
            logf(ok ? "mech: profile %d set by hand (the values in use)" : "mech: no cockpit recognised now - nothing stored", g_mechCur + 1);
            Beep(ok ? 1200 : 300, ok ? 60 : 200);
            if (ok) vrReadIni();
        }
        was = down;
    }
    {   // Ctrl+Ins / Ctrl+Del: cockpit scale up / down by 5 % (larger = the cockpit looks smaller);
        // with Shift: the world's scale (larger = the world looks smaller, the ground nearer). Saved at once
        static bool was[2];
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && !(GetAsyncKeyState(VK_MENU) & 0x8000), shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        const int keys[2] = {VK_INSERT, VK_DELETE};
        for (int k = 0; k < 2; ++k) {
            bool down = ctrl && (GetAsyncKeyState(keys[k]) & 0x8000);
            if (down && !was[k]) {
                float w = (shift ? g_vr.worldScale : g_vr.cockpitScale) * (k == 0 ? 1.05f : 1 / 1.05f);
                if (w < 0.2f) w = 0.2f;
                if (w > 10.0f) w = 10.0f;
                if (shift) {
                    wchar_t b[32];
                    swprintf(b, 32, L"%.3f", w);
                    WritePrivateProfileStringW(L"vr", L"world_scale", b, (g_dir + L"mw4vr.ini").c_str());
                    vrReadIni();
                } else {
                    MechValues mv = vrValues();
                    mv.cockpitScale = w;
                    vrStoreValues(mv);
                }
                logf("vr: %s scale %.3f (saved)", shift ? "world" : "cockpit", w);
                Beep((shift ? 300 : 600) + (int)(w * 200 > 1400 ? 1400 : w * 200), 50);
            }
            was[k] = down;
        }
    }
    // Ctrl+PgUp: HUD farther, Ctrl+PgDn: nearer, Ctrl+End: at infinity; saved to mw4vr.ini at once
    {
        static bool was[3];
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && !(GetAsyncKeyState(VK_MENU) & 0x8000); // (with Alt: the cockpit mesh, mw5.inc)
        const int keys[3] = {VK_PRIOR, VK_NEXT, VK_END};
        for (int k = 0; k < 3; ++k) {
            bool down = ctrl && (GetAsyncKeyState(keys[k]) & 0x8000);
            if (down && !was[k]) {
                float d = g_vr.hudDistance;
                if (k == 2) d = 0;
                else if (d <= 0.1f) d = k == 0 ? 0 : 200.0f; // from infinity, nearer starts at 200 m
                else d = k == 0 ? d * 1.25f : d / 1.25f;
                if (d > 250.0f) d = 0; // beyond 250 m: infinity
                if (d != 0 && d < 1.0f) d = 1.0f;
                wchar_t b[32];
                swprintf(b, 32, L"%.2f", d);
                WritePrivateProfileStringW(L"vr", L"hud_distance_m", b, (g_dir + L"mw4vr.ini").c_str());
                vrReadIni(); // recomputes the shift from the saved value
                if (d == 0) logf("vr: HUD at infinity (saved)");
                else logf("vr: HUD at %.2f m (saved)", d);
                Beep(d == 0 ? 400 : 500 + (int)(d * 4 > 800 ? 800 : d * 4), 60);
            }
            was[k] = down;
        }
    }
    {   // Ctrl+F9: the labels of both eyes' passes of the next frame -> mw4vr_labels.txt (diagnosis)
        static bool was;
        bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_F9) & 0x8000);
        if (down && !was) {
            g_lblDump = true;
            Beep(900, 40);
        }
        was = down;
    }
    vrPollEvents();
    if (!g_vr.running) return;

    // the pose the finished frame was rendered with (chosen by buildK from the poses located last time)
    g_srcCamActive = g_srcCam && g_camHooked && g_camCal;
    int renderedEye = g_srcCamActive ? (g_camEyeFrame == g_frame ? g_camEye : -1)
                                     : (g_kOn && g_kFrame == g_frame ? g_kEye : -1);
    XrPosef renderedPose = renderedEye >= 0 ? g_vr.used[renderedEye] : XrPosef{};
    bool renderedOk = renderedEye >= 0 && g_vr.usedOk[renderedEye];

    XrFrameState fs = {XR_TYPE_FRAME_STATE};
    if (!vrCheck(g_vr.WaitFrame(g_vr.session, nullptr, &fs), "xrWaitFrame")) return;
    if (!vrCheck(g_vr.BeginFrame(g_vr.session, nullptr), "xrBeginFrame")) return;
    bool track = g_vr.tracking && g_P00 > 0 && g_P11 > 0;
    g_vrTracking = track;
    if (g_P00 > 0 && g_P11 > 0 && g_in3DSeen && (g_vr.baseP00 <= 0 || g_P00 < g_vr.baseP00 * 0.995f)) {
        g_vr.baseP00 = g_P00;
        g_vr.baseP11 = g_P11;
        logf("vr: layers span %.1f x %.1f degrees (the game's widest view)", 2 * atanf(1 / g_P00) * 57.29578f,
             2 * atanf(1 / g_P11) * 57.29578f);
    }
    g_hudLayerOn = track && g_srcCamActive && g_vr.hudLayer && g_vr.hudSc != XR_NULL_HANDLE && !g_menuMode;
    if (g_hudLayerOn) g_hudPx = 0;

    // the frame just finished goes to its eye (to both when it was rendered without an offset)
    ID3D11Texture2D *bb = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    g_dev->GetImmediateContext(&ctx);
    if (fs.shouldRender && SUCCEEDED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb)) && bb && ctx) {
        D3D11_TEXTURE2D_DESC bd;
        bb->GetDesc(&bd);
        D3D11_BOX box = {0, 0, 0, min(bd.Width, g_vr.w), min(bd.Height, g_vr.h), 1};
        bool bothNow = g_camBothFrame == g_frame && g_eyeTexFrame[0] == g_frame && g_eyeTexFrame[1] == g_frame;
        for (int e = 0; e < 2; ++e) {
            if (!bothNow && eye >= 0 && e != eye) continue;
            uint32_t idx = 0;
            XrSwapchainImageAcquireInfo ai = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if (XR_FAILED(g_vr.AcquireSwapchainImage(g_vr.sc[e], &ai, &idx))) continue;
            XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wi.timeout = XR_INFINITE_DURATION;
            g_vr.WaitSwapchainImage(g_vr.sc[e], &wi);
            if (bothNow) {
                D3D11_BOX eb = {0, 0, 0, min(g_eyeTexW, g_vr.w), min(g_eyeTexH, g_vr.h), 1};
                ctx->CopySubresourceRegion(g_vr.img[e][idx].texture, 0, 0, 0, 0, g_eyeTex[e], 0, &eb);
                if (track && g_vr.usedOk[e]) {
                    g_vr.layerPose[e] = g_vr.used[e];
                    g_vr.layerFov[e] = g_vr.usedFov[e];
                    g_vr.layerOk[e] = true;
                }
            } else
                ctx->CopySubresourceRegion(g_vr.img[e][idx].texture, 0, 0, 0, 0, bb, 0, &box);
            if (!bothNow && track && renderedOk && e == renderedEye) {
                g_vr.layerPose[e] = renderedPose;
                g_vr.layerFov[e] = g_vr.usedFov[e];
                g_vr.layerOk[e] = true;
            }
            XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            g_vr.ReleaseSwapchainImage(g_vr.sc[e], &ri);
            g_vr.have[e] = true;
        }
        // diagnosis pictures (shot.inc): what went to the eyes just now, and the HUD layer's image
        shotFrame(ctx, bothNow ? g_eyeTex[0] : bb, bothNow ? g_eyeTex[1] : nullptr,
                  (g_hudLayerOn && g_hudTex && g_hudDrawnFrame == g_frame) ? g_hudTex : nullptr,
                  bothNow ? "both eyes of this frame" : "one picture, no eye pair in this frame");
    }
    // the HUD drawn in this frame
    if (ctx && g_hudLayerOn && g_hudTex && g_hudDrawnFrame == g_frame) mw5HudCopy(ctx, g_hudTex); // for the own cockpit's monitors
    if (ctx && g_hudLayerOn && g_hudTex && g_hudDrawnFrame == g_frame) fadeProbe(ctx, g_hudTex);
    if (ctx && fs.shouldRender && g_hudLayerOn && g_hudTex && g_hudDrawnFrame == g_frame) {
        uint32_t idx = 0;
        XrSwapchainImageAcquireInfo ai = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if (XR_SUCCEEDED(g_vr.AcquireSwapchainImage(g_vr.hudSc, &ai, &idx))) {
            XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wi.timeout = XR_INFINITE_DURATION;
            g_vr.WaitSwapchainImage(g_vr.hudSc, &wi);
            D3D11_BOX hb = {0, 0, 0, min(g_hudTexW, hudWideW(g_vr.w)), min(g_hudH, g_vr.h), 1};
            ctx->CopySubresourceRegion(g_vr.hudImg[idx].texture, 0, 0, 0, 0, g_hudTex, 0, &hb);
            noteDraw(ctx, g_vr.hudImg[idx].texture, min(g_hudW, g_vr.w), hb.bottom); // a note of ours (note.inc)
            XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            g_vr.ReleaseSwapchainImage(g_vr.hudSc, &ri);
            if (!g_vr.hudHave) logf("vr: HUD layer running (edge strips at %.1f m, the middle at %.1f m)", g_vr.hudNear, g_vr.hudDistance);
            g_vr.hudHave = true;
        }
    }
    if (bb) bb->Release();
    if (ctx) ctx->Release();

    XrCompositionLayerQuad quads[2] = {};
    XrCompositionLayerQuad hq[32] = {};
    const XrCompositionLayerBaseHeader *layers[40];
    uint32_t nl = 0;
    XrCompositionLayerProjectionView pv[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                              {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    XrCompositionLayerProjection proj = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    bool battle = !g_srcCamActive || (g_camEyeFrame != ~0u && g_frame - g_camEyeFrame < 30 && !g_menuMode);
    mechOnPresent(battle && track && g_srcCamActive, g_vr.baseP00 > 0 && g_P00 > g_vr.baseP00 * 1.05f);
    XrCompositionLayerQuad menuQ = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    if (track && !battle && fs.shouldRender && g_vr.have[0]) {
        // menus and films: the frame went to both eyes' images; one of them as a screen 3 m away
        float D = 3.0f, W = 3.6f;
        menuQ.space = g_vr.space;
        menuQ.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        menuQ.subImage.swapchain = g_vr.sc[0];
        menuQ.subImage.imageRect.extent = {(int32_t)g_vr.w, (int32_t)g_vr.h};
        XrQuaternionf rq = g_vr.haveRef ? g_vr.ref.orientation : XrQuaternionf{0, 0, 0, 1};
        XrVector3f rp = g_vr.haveRef ? g_vr.ref.position : XrVector3f{0, 0, 0};
        XrVector3f o = qrot(rq, {0, 0, -D});
        menuQ.pose.orientation = rq;
        menuQ.pose.position = {rp.x + o.x, rp.y + o.y, rp.z + o.z};
        menuQ.size = {W, W * g_vr.h / (float)g_vr.w};
        layers[nl++] = (const XrCompositionLayerBaseHeader *)&menuQ;
    }
    if (track && battle && fs.shouldRender && g_vr.layerOk[0] && g_vr.layerOk[1] && g_vr.have[0] && g_vr.have[1]) {
        for (int e = 0; e < 2; ++e) {
            pv[e].pose = g_vr.layerPose[e];
            // the mech turned since this eye's image was drawn: the image turns with it (about the reference),
            // so both eyes show the world in one place again
            if (g_vr.mechReproject && g_camBothFrame != g_frame && g_srcCamActive && renderedEye >= 0 && e != renderedEye && g_camEyeBaseOk[0] &&
                g_camEyeBaseOk[1] && g_vr.haveRef) {
                const float *Bn = g_camEyeBase[renderedEye], *Be = g_camEyeBase[e], *A = g_camA;
                float Dl[9], T1[9], Dv[9];
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) Dl[i * 3 + j] = Bn[0 * 4 + i] * Be[0 * 4 + j] + Bn[1 * 4 + i] * Be[1 * 4 + j] + Bn[2 * 4 + i] * Be[2 * 4 + j];
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) T1[i * 3 + j] = A[0 * 3 + i] * Dl[0 * 3 + j] + A[1 * 3 + i] * Dl[1 * 3 + j] + A[2 * 3 + i] * Dl[2 * 3 + j];
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) Dv[i * 3 + j] = T1[i * 3 + 0] * A[0 * 3 + j] + T1[i * 3 + 1] * A[1 * 3 + j] + T1[i * 3 + 2] * A[2 * 3 + j];
                const float sg[3] = {1, 1, -1};
                float Dx[9];
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) Dx[i * 3 + j] = sg[i] * Dv[i * 3 + j] * sg[j];
                XrQuaternionf dq = qfromm(Dx);
                if (dq.w > 0.94f || dq.w < -0.94f) { // less than 40 degrees: a turn, not a cut
                    XrQuaternionf rq = g_vr.ref.orientation;
                    XrQuaternionf wq = qmul(qmul(rq, dq), qconj(rq));
                    XrVector3f dp = {pv[e].pose.position.x - g_vr.ref.position.x, pv[e].pose.position.y - g_vr.ref.position.y,
                                     pv[e].pose.position.z - g_vr.ref.position.z};
                    XrVector3f np = qrot(wq, dp);
                    pv[e].pose.orientation = qmul(wq, pv[e].pose.orientation);
                    pv[e].pose.position = {g_vr.ref.position.x + np.x, g_vr.ref.position.y + np.y, g_vr.ref.position.z + np.z};
                }
            }
            pv[e].fov = g_vr.layerFov[e];
            pv[e].subImage.swapchain = g_vr.sc[e];
            pv[e].subImage.imageRect.extent = {(int32_t)g_vr.w, (int32_t)g_vr.h};
        }
        proj.space = g_vr.space;
        proj.viewCount = 2;
        proj.views = pv;
        layers[nl++] = (const XrCompositionLayerBaseHeader *)&proj;
    }
    for (int e = 0; e < 2 && !track; ++e) {
        if (!g_vr.have[e] || !fs.shouldRender) continue;
        XrCompositionLayerQuad &q = quads[e];
        q.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
        q.space = g_vr.space;
        q.eyeVisibility = e == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
        q.subImage.swapchain = g_vr.sc[e];
        q.subImage.imageRect.extent = {(int32_t)g_vr.w, (int32_t)g_vr.h};
        q.pose.orientation.w = 1;
        // each eye's quad centred in front of that eye: what both images show at the same place lies at infinity
        q.pose.position = {(e == 0 ? -0.5f : 0.5f) * g_vr.ipd, 0.0f, -g_vr.distance};
        q.size = {g_vr.width, g_vr.width * g_vr.h / (float)g_vr.w};
        layers[nl++] = (const XrCompositionLayerBaseHeader *)&q;
    }
    // the HUD: three strips of its image as quads fixed to the mech (the reference pose), each spanning
    // the game's field of view from there - the top and bottom strips (panels, radar) near, in the
    // cockpit; the middle (cross-hair, target marks) far ahead
    if (nl && battle && g_hudLayerOn && g_vr.hudHave && g_vr.haveRef && fs.shouldRender && g_hudDrawnFrame + 3 >= g_frame) {
        // rectangles of the HUD image (u0 v0 u1 v1): the far middle first, then the near frame around it -
        // top and bottom strips over the whole width, the side columns between them
        float T = g_vr.hudTop, B = g_vr.hudBottom, L = g_vr.hudLeft, R = g_vr.hudRight;
        float far_ = g_vr.hudDistance > 0.1f ? g_vr.hudDistance : 200.0f;
        // the cockpit's sway for the near frame: view space (z forward, game metres) -> the runtime's (z back, metres)
        swayOnPresent();
        XrQuaternionf swq = {0, 0, 0, 1};
        XrVector3f swt = {0, 0, 0};
        if (g_swayValid) {
            const float sg[3] = {1, 1, -1};
            float Rx[9];
            for (int a = 0; a < 3; ++a)
                for (int c = 0; c < 3; ++c) Rx[a * 3 + c] = sg[a] * g_swayR[a * 3 + c] * sg[c];
            swq = qfromm(Rx);
            float k = g_swayGain / g_vr.cockpitScale;
            swt = {g_swayT[0] * k, g_swayT[1] * k, -g_swayT[2] * k};
        }
        float p0 = g_vr.baseP00 > 0 ? g_vr.baseP00 : g_P00, p1 = g_vr.baseP11 > 0 ? g_vr.baseP11 : g_P11;
        // which parts of the HUD image are shown where (core.h)
        McHud hd = {};
        hd.T = T, hd.B = B, hd.L = L, hd.R = R;
        // the HUD image's right edge: beyond 1 where the layer texture is wider than the picture (hud.inc)
        hd.UR = hudWide() ? (float)min(g_hudTexW, hudWideW(g_vr.w)) / (float)g_hudW : 1.0f;
        if (g_hudCamFrame == g_frame && g_hudW && g_hudH)
            for (int i = 0; i < g_hudCamN && i < MC_CAMS; ++i) { // the small cameras' windows
                const HudCamRect &r = g_hudCams[i];
                float *o = hd.cam[hd.nCam++];
                o[0] = (float)r.x / g_hudW, o[1] = (float)r.y / g_hudH, o[2] = (float)(r.x + r.w) / g_hudW, o[3] = (float)(r.y + r.h) / g_hudH;
            }
        hd.margin = g_vr.zoomWinMargin;
        // the own cockpit's monitors show the near frame, its glass what no monitor shows; the rest stays, far
        static GlassItem gi[MC_GLASSES];
        int ng = mw5GlassItems(gi);
        hd.screens = mw5ScreensOn() || ng > 0;
        if (hd.screens) {
            hd.nSpan = mw5ScreensOn() ? mw5TopSpans(T, hd.span) : 0;
            hd.leftAlt = mw5ScreensOn() ? mw5LeftAltBottom(L) : 0; // the lance orders' menu is on a monitor
            for (int g = 0; g < ng; ++g) {
                float *r = gi[g].rc;
                if (r[2] >= 1.0f && hd.UR > r[2]) r[2] = hd.UR; // at the picture's right edge: with what the wide HUD adds there
                memcpy(hd.glass[hd.nGlass++], r, sizeof hd.glass[0]);
                mcTopSpan(hd.span, hd.nSpan, r, T);
                if (r[1] < T) { // the lists with what hangs below them (the lance menu, the weapons): not the far columns' any more
                    // one that reaches below the strip and into the middle (the weapon list's frame begins left of
                    // hud_right): the middle ends where it begins - middle and columns are shown alike here
                    if (r[3] > T + 0.01f && r[0] > 0.5f && r[0] < hd.R) hd.R = r[0];
                    if (r[3] > T + 0.01f && r[2] < 0.5f && r[2] > hd.L) hd.L = r[2];
                    hd.leftAlt = mcLeftAlt(r, hd.L, hd.leftAlt);
                    hd.rightAlt = mcRightAlt(r, hd.R, hd.rightAlt);
                }
            }
        }
        memcpy(hd.twist, g_vr.twist, sizeof hd.twist);
        hd.twistDv = g_vr.twistDv;
        static McRects hq_;
        mcHudRects(hd, hq_);
        float (*rc)[5] = hq_.rc;
        int nr = hq_.n;
        // a battle's first pictures (the answer comes a frame late, and the first frame can last long): no HUD yet
        if (g_fadeStage && (g_fadeKnownFrame == ~0u || g_frame - g_fadeKnownFrame > 4)) nr = 0;
        else if (g_fadeA > 0) { // the game's fade sheet: over the whole view, the HUD not
            nr = 0;
            float du = 3.0f / g_vr.w, dv = 3.0f / g_vr.h;
            rc[0][0] = g_fadePts[g_fadePt][0] - du, rc[0][1] = g_fadePts[g_fadePt][1] - dv, rc[0][2] = g_fadePts[g_fadePt][0] + du,
            rc[0][3] = g_fadePts[g_fadePt][1] + dv, rc[0][4] = 5;
            nr = 1;
        }
        for (int i = 0; i < nr; ++i) {
            float u0 = rc[i][0], v0 = rc[i][1], u1 = rc[i][2], v1 = rc[i][3];
            bool isFar = rc[i][4] == 1 || rc[i][4] >= 3, opaque = rc[i][4] == 2 || rc[i][4] == 3;

            if (rc[i][4] != 5 && (u1 - u0 < 0.005f || v1 - v0 < 0.005f)) continue;
            if (nl >= 16) { // what every runtime shows at least; with more, xrEndFrame fails and the picture stands still
                static bool told;
                if (!told) logf("vr: more than 16 layers - %d parts of the HUD left out", nr - i);
                told = true;
                break;
            }
            if (rc[i][4] >= MC_GLASS) { // on the cockpit's glass: where its part's middle is seen from the seat, facing the seat
                bool solid = rc[i][4] >= MC_GLASS_OPAQUE;
                const GlassItem &it = gi[(int)rc[i][4] - (solid ? MC_GLASS_OPAQUE : MC_GLASS)];
                float gp[3], gq[4], gd = it.dist;
                mcGlassPose(it.right, it.up, gd, gp, gq);
                float mu = mcGlassScale(it.width, gd, it.base[2] - it.base[0]), mv = mu * (float)g_vr.h / (float)g_vr.w;
                XrQuaternionf lq = {gq[0], gq[1], gq[2], gq[3]};
                XrVector3f off = qrot(lq, XrVector3f{mu * 0.5f * (u0 + u1 - it.base[0] - it.base[2]), -mv * 0.5f * (v0 + v1 - it.base[1] - it.base[3]), 0});
                XrVector3f local = {gp[0] + off.x, gp[1] + off.y, gp[2] + off.z};
                XrQuaternionf qo = qmul(g_vr.ref.orientation, lq);
                if (g_swayValid) { // it moves with the cockpit
                    XrVector3f l2 = qrot(swq, local);
                    local = {l2.x + swt.x, l2.y + swt.y, l2.z + swt.z};
                    qo = qmul(g_vr.ref.orientation, qmul(swq, lq));
                }
                XrVector3f wpos = qrot(g_vr.ref.orientation, local);
                XrCompositionLayerQuad &q = hq[i];
                q.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
                q.layerFlags = solid ? 0 : XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                q.space = g_vr.space;
                q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                q.subImage.swapchain = g_vr.hudSc;
                q.subImage.imageRect.offset = {(int32_t)(u0 * g_vr.w), (int32_t)(v0 * g_vr.h)};
                q.subImage.imageRect.extent = {(int32_t)((u1 - u0) * g_vr.w), (int32_t)((v1 - v0) * g_vr.h)};
                q.pose.orientation = qo;
                q.pose.position = {g_vr.ref.position.x + wpos.x, g_vr.ref.position.y + wpos.y, g_vr.ref.position.z + wpos.z};
                q.size = {mu * (u1 - u0), mv * (v1 - v0)};
                layers[nl++] = (const XrCompositionLayerBaseHeader *)&q;
                continue;
            }
            float D = isFar ? far_ : g_vr.hudNear;
            float sc = isFar ? 1.0f : g_vr.hudPanelScale; // the middle spans the game's view: marks stay on their targets
            XrCompositionLayerQuad &q = hq[i];
            q.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
            q.layerFlags = opaque ? 0 : XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            q.space = g_vr.space;
            q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            q.subImage.swapchain = g_vr.hudSc;
            q.subImage.imageRect.offset = {(int32_t)(u0 * g_vr.w), (int32_t)(v0 * g_vr.h)};
            q.subImage.imageRect.extent = {(int32_t)((u1 - u0) * g_vr.w), (int32_t)((v1 - v0) * g_vr.h)};
            float W = sc * 2 * D / p0, H = sc * 2 * D / p1;
            XrVector3f local = {0.5f * W * ((u0 + u1) - 1), 0.5f * H * (1 - (v0 + v1)), -D};
            XrQuaternionf qo = g_vr.ref.orientation;
            if (!isFar) local.x += g_vr.panelX, local.y += g_vr.panelY;
            if (rc[i][4] == 4) local.y -= g_vr.twistDv * H;
            if (!isFar && g_swayValid) { // the near frame moves with the cockpit
                XrVector3f l2 = qrot(swq, local);
                local = {l2.x + swt.x, l2.y + swt.y, l2.z + swt.z};
                qo = qmul(g_vr.ref.orientation, swq);
            }
            XrVector3f wpos = qrot(g_vr.ref.orientation, local);
            q.pose.orientation = qo;
            q.pose.position = {g_vr.ref.position.x + wpos.x, g_vr.ref.position.y + wpos.y, g_vr.ref.position.z + wpos.z};
            float ws = rc[i][4] == 3 ? g_vr.zoomWinScale : 1.0f; // the zoom window smaller, about its centre
            q.size = {W * (u1 - u0) * ws, H * (v1 - v0) * ws};
            if (rc[i][4] == 5) { // straight ahead of the mech, many times the view
                XrVector3f c = qrot(g_vr.ref.orientation, XrVector3f{0, 0, -D});
                q.pose.position = {g_vr.ref.position.x + c.x, g_vr.ref.position.y + c.y, g_vr.ref.position.z + c.z};
                q.size = {40 * D, 40 * D};
            }
            layers[nl++] = (const XrCompositionLayerBaseHeader *)&q;
        }
    }
    XrFrameEndInfo ei = {XR_TYPE_FRAME_END_INFO};
    ei.displayTime = fs.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = nl;
    ei.layers = layers;
    vrCheck(g_vr.EndFrame(g_vr.session, &ei), "xrEndFrame");
    // poses for the next frame (rendered now, shown one period later)
    if (track) vrLocate(fs.predictedDisplayTime + fs.predictedDisplayPeriod);
    else g_vrPoseOk[0] = g_vrPoseOk[1] = false;
    if (++g_vr.frames == 1) logf("vr: first frame submitted");
}

// after buildK: the pose this frame is rendered with, for its layer at the next Present
static void vrAfterBuildK() {
    if (!g_vr.ok) return;
    bool src = g_srcCamActive && g_eyeMode;
    if (!src && !g_kOn) return;
    int only = src ? (eyeSign() > 0 ? 1 : 0) : g_kEye;
    for (int e = 0; e < 2; ++e) {
        if (!(src && g_doubleDraw) && e != only) continue; // both eyes may be drawn in the coming frame
        g_vr.usedOk[e] = g_vrTracking && g_vrPoseOk[e] && g_vr.nextOk[e];
        if (!g_vr.usedOk[e]) continue;
        g_vr.used[e] = g_vr.next[e];
        if (g_srcCamActive) { // drawn with the game's projection
            float p0 = g_vr.baseP00 > 0 ? g_vr.baseP00 : g_P00, p1 = g_vr.baseP11 > 0 ? g_vr.baseP11 : g_P11;
            float zs = (g_vr.baseP00 > 0 && g_P00 > g_vr.baseP00 * 1.05f) ? g_vr.zoomScale : 1.0f; // zoomed in
            float ax = atanf(zs / p0), ay = atanf(zs / p1);
            g_vr.usedFov[e] = {-ax, ax, ay, -ay};
        } else {
            g_vr.usedFov[e] = {atanf(g_vrFov[e][0]), atanf(g_vrFov[e][1]), atanf(g_vrFov[e][2]), atanf(g_vrFov[e][3])};
        }
    }
}
