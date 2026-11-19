// ---------------------------------------------------------------------------------------------
// Everything this mod knows about the insides of the two programs it reaches into:
//
//   mw4hook.dll   the MW4Engine DX11 renderer mod (not ours), build 20261107_ea16ef2
//   MW4Mercs.exe  MechWarrior 4: Mercenaries, MekTek MekPak 3.1c
//
// Addresses, offsets, shader hashes and the names of mw4hook's annotation labels live here and
// nowhere else, so a new build of either program means changes in this one file - and a renderer
// other than mw4hook would be a second file like it. A build that is not the known one: what reads
// or writes mw4hook's memory is switched off (hkBase() gives nullptr; [vr] mw4hook_any_build=1
// tries anyway - every place still checks the code it relies on), the rest goes on.
// The facts are in this header; the functions that use them to find the programs in mw4hook_abi.cpp.
#ifndef MW4VR_MW4HOOK_ABI_H
#define MW4VR_MW4HOOK_ABI_H

// --- mw4hook.dll: data (offsets from its base) ---
enum : uint32_t {
    HK_SCREEN_MODE = 0x43e1c0,   // LONG[2]: the game's screen mode (what the cursor is mapped to)
    HK_WATER_REFLECT = 0x43d3e0, // byte: the water's planar reflection is wanted in this pass
    HK_PICTURE = 0x4931c8,       // LONG[2]: the size the battles are drawn at
    HK_PICTURE_KNOWN = 0x4931d9, // byte: its picture's rectangle is worked out
    HK_PICTURE_RECT = 0x4931dc,  // LONG[4]: its picture's rectangle in render pixels (what the cursor is mapped with)
    HK_OUT_WINDOW = 0x4b90cc,    // HWND: its output window
    HK_OUT_RECT = 0x4b90d0,      // RECT: its output size
    HK_OUT_WIDTH = 0x4b90d8,     // (the RECT's right)
    HK_PTR_GETCURSORPOS = 0x4c7310, // the real functions its cursor mapping calls through
    HK_PTR_SETCURSORPOS = 0x4c7314,
    HK_PTR_CLIPCURSOR = 0x4c7318,
    HK_IAT_GETCLIENTRECT = 0x210308, // import slot
};
// --- mw4hook.dll: code that is checked before the data above is trusted ---
enum : uint32_t {
    HK_AT_PICTURE_READ = 0x14710, // mov eax, [HK_PICTURE]
    HK_AT_WIDTH_STORE = 0x4e60a,  // mov [HK_OUT_WIDTH], eax
    HK_AT_SIZE_CALL = 0x571a9,    // call [HK_IAT_GETCLIENTRECT]
    HK_AT_WATER_CHECK = 0x9e5f0,  // cmp byte ptr [HK_WATER_REFLECT], 0
    HK_AT_GETCURSORPOS = 0xbb953, // mov eax, [HK_PTR_GETCURSORPOS]
    HK_AT_CLIPCURSOR = 0xbc0fd,   // mov eax, [HK_PTR_CLIPCURSOR]
};
// --- mw4hook.dll: shaders (hashes of their bytecode) ---
static const uint64_t HK_VS_TL = 0xad8cee09478ddb12ull;       // the CPU path's screen-space vertex shader
static const uint64_t HK_PS_BACKDROP = 0x3c49bfee1754587bull; // a small camera's backdrop: the frame behind its window
// --- mw4hook.dll: annotation labels (their beginnings) ---
#define HK_LBL_3D L"3D"                          // top level: the world
#define HK_LBL_HUD L"after 3D"                   // top level: "after 3D: HUD, 2D"
#define HK_LBL_TONEMAP L"tonemap into the frame" // the camera's picture is in the frame
#define HK_LBL_WATER L"water (RTIN"              // the water pass of a camera
#define HK_LBL_SSAA L"SSAA 2x2"                  // a small camera ("... a camera at twice the resolution, WxH at X,Y")
#define HK_LBL_SSAA_SIZE L"resolution, "
#define HK_LBL_CAMERA L"camera:" // "camera: perspective, viewport X,Y WxH (...)"
#define HK_LBL_VIEWPORT L"viewport "

// ---- the right eye's picture from the left eye's draws (replay.cpp) ----
// The shaders' constants that hold the eye's view: name, kind, bytes into the variable. Kinds: 1 = a matrix
// into clip space, 2 = a matrix out of clip space, 3 = a position in the world, 4 = an object's matrix into
// clip space (beside the object's matrix into the world, HK_RP_OBJ_VAR). Found by comparing what mw4hook writes
// for the two eyes ([vr] replay_right=2). Left as the left eye's, though they differ: the camera's place in
// g_mlrFog and g_rtin (fog and the terrain's levels of detail 3 cm off), and the counters of the passes in
// g_vAo, g_vAo2, g_vOrigin, g_vSsgi2.
#define HK_RP_VARS                                                                                                     \
    {"g_mlrToClip", 4, 0}, {"g_mlrPrevToClip", 4, 0}, {"g_vWorldToClip", 1, 0}, {"g_vSsgiPrevClip", 1, 0},             \
        {"g_vClipToWorld", 2, 0}, {"g_vCam", 3, 0}, {"g_vFog", 3, 32}, {"g_fClipToWorld", 2, 0}, {"g_fFog", 3, 32},    \
        {"g_camPos", 3, 0}, {"g_cultCam", 3, 0}, {"g_cardToClip", 1, 0}, {"g_cardCam", 3, 0},                          \
        {"g_wClipToWorld", 2, 0}, {"g_wCam", 3, 0}, {"g_rToClip", 1, 0}, {"g_rEye", 3, 0}
#define HK_RP_OBJ_VAR "g_mlrToWorld"
// an instance's record in the structured buffer the instanced shaders read: 288 bytes, the matrix into clip
// space first, last frame's last
#define HK_RP_INST_BYTES 288
#define HK_RP_INST_PREV 224 // where last frame's matrix lies in it
// the constant that tells a vertex shader its first instance's record
#define HK_RP_INST_BASE_VAR "g_mlrInstBase"
// labels inside the main camera's pass under which things are made once a frame, not once an eye
#define HK_RP_ONCE_LABELS L"RTIN tree of sun shadow", L"sun shadows:", L"terrain VT:"
// --- MW4Mercs.exe ---
static const uintptr_t GM_CAMERA_DRAW = 0x4b6ec0; // CameraElement::Draw (mw4hook puts a jmp there)
static const uintptr_t GM_HWND = 0x1241050;       // HWND of the game's window
// a scene element (the camera is one)
enum : uint32_t {
    EL_PARENT = 0x10,     // the element above it
    EL_LOCAL = 0x20,      // 3x4 float: its own matrix (rows: axis x, y, z, translation)
    EL_LOCAL_COPY = 0x50, // the same again
    EL_WORLD = 0x80,      // 3x4 float: element -> world, made by the game from the parent's and EL_LOCAL
    CAM_PLANES = 0x1ac,   // 6 x (normal, d): far, near, right, left, bottom, top; inside is n.p <= d
    CAM_TANGENTS = 0x20c, // 4 float: the frustum's half-angle tangents
    CAM_VIEWPORT = 0x2c8, // 4 float: the normalised viewport
};
struct AbiBuild {
    uint32_t stamp; // the PE header's TimeDateStamp
    uint32_t image; // SizeOfImage
    const char *name;
};
static const AbiBuild HK_KNOWN = {0x6aef4435, 0x58a000, "20261107_ea16ef2"};
static const AbiBuild GM_KNOWN = {0x4ddc5ee1, 0xf40000, "MekPak 3.1c"};

// the player's vehicle, found the way mw4hook finds it: node = [GM_PLAYER_KEY] ^ [[GM_PLAYER_REF]],
// holder = [node + PL_HOLDER] ^ GM_PLAYER_XOR, vehicle = [holder + PL_VEHICLE]
static const uintptr_t GM_PLAYER_KEY = 0x103b0b8, GM_PLAYER_REF = 0x103b0bc;
static const uint32_t GM_PLAYER_XOR = 0x85739531;
enum : uint32_t {
    PL_HOLDER = 0x18,
    PL_VEHICLE = 0x174,
    VEH_MODEL = 0x5c, // char *: the vehicle's model file, "mechs\<mech>\<mech>.data"
};

// the mech the player sits in: its folder's name in small letters ("catapult"); false: not a mech,
// or not in a battle
bool gmPlayerMech(char *out, int n);
// mw4hook.dll's base - nullptr while it is not loaded, and for good when it is not the known build
uint8_t *hkBase();
// the game itself: a warning only (the camera hook checks its own place)
void gmCheck();

#endif // MW4VR_MW4HOOK_ABI_H
