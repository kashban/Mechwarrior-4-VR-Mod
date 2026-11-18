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
// Included into main.cpp after the logging.

struct AbiBuild {
    uint32_t stamp; // the PE header's TimeDateStamp
    uint32_t image; // SizeOfImage
    const char *name;
};
static const AbiBuild HK_KNOWN = {0x6aef4435, 0x58a000, "20261107_ea16ef2"};
static const AbiBuild GM_KNOWN = {0x4ddc5ee1, 0xf40000, "MekPak 3.1c"};

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
static bool gmPlayerMech(char *out, int n) {
    out[0] = 0;
    __try {
        const uint32_t *ref = *(const uint32_t **)GM_PLAYER_REF;
        if (!ref) return false;
        uintptr_t node = *(const uint32_t *)GM_PLAYER_KEY ^ *ref;
        if (!node) return false;
        uintptr_t holder = *(const uint32_t *)(node + PL_HOLDER) ^ GM_PLAYER_XOR;
        if (holder < 0x10000) return false;
        uintptr_t veh = *(const uint32_t *)(holder + PL_VEHICLE);
        if (veh < 0x10000) return false;
        const char *path = *(const char **)(veh + VEH_MODEL);
        if ((uintptr_t)path < 0x10000 || _strnicmp(path, "mechs\\", 6)) return false;
        int k = 0;
        for (const char *p = path + 6; *p && *p != '\\' && k < n - 1; ++p) {
            char c = *p;
            bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (c >= 'A' && c <= 'Z') c += 32, ok = true;
            if (!ok) return out[0] = 0, false;
            out[k++] = c;
        }
        out[k] = 0;
        return k > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = 0;
        return false;
    }
}

static bool abiBuildIs(HMODULE m, const AbiBuild &k, uint32_t *stamp, uint32_t *image) {
    *stamp = *image = 0;
    __try {
        const IMAGE_DOS_HEADER *d = (const IMAGE_DOS_HEADER *)m;
        const IMAGE_NT_HEADERS32 *n = (const IMAGE_NT_HEADERS32 *)((const uint8_t *)m + d->e_lfanew);
        *stamp = n->FileHeader.TimeDateStamp;
        *image = n->OptionalHeader.SizeOfImage;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return *stamp == k.stamp && *image == k.image;
}
static int abiIni(const wchar_t *key) { return GetPrivateProfileIntW(L"vr", key, 0, (g_dir + L"mw4vr.ini").c_str()); }

// mw4hook.dll's base - nullptr while it is not loaded, and for good when it is not the known build
static uint8_t *hkBase() {
    static int state = -1; // -1 not looked at yet, 0 not usable, 1 usable
    static uint8_t *base;
    if (state >= 0) return state ? base : nullptr;
    HMODULE m = GetModuleHandleW(L"mw4hook.dll");
    if (!m) return nullptr; // later
    uint32_t stamp, image;
    bool known = abiBuildIs(m, HK_KNOWN, &stamp, &image);
    bool any = !known && abiIni(L"mw4hook_any_build") != 0;
    base = (uint8_t *)m;
    state = (known || any) ? 1 : 0;
    if (known)
        logf("abi: mw4hook.dll is the known build (%s)", HK_KNOWN.name);
    else
        logf("abi: mw4hook.dll is NOT the build this was made for (%s): timestamp %08x, image %x - %s", HK_KNOWN.name,
             stamp, image,
             any ? "trying anyway (mw4hook_any_build=1)"
                 : "the mouse mapping, render_height, menu detection and the water's reflection for both eyes are off");
    return state ? base : nullptr;
}

// the game itself: a warning only (the camera hook checks its own place)
static void gmCheck() {
    uint32_t stamp, image;
    if (abiBuildIs(GetModuleHandleW(nullptr), GM_KNOWN, &stamp, &image))
        logf("abi: the game is the known build (%s)", GM_KNOWN.name);
    else
        logf("abi: the game is NOT the build this was made for (%s): timestamp %08x, image %x - the camera's addresses "
             "may be wrong",
             GM_KNOWN.name, stamp, image);
}
