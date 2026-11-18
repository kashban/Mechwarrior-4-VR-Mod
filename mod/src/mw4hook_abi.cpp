// ---------------------------------------------------------------------------------------------
// Finding the two programs the mod reaches into, and checking that they are the builds it knows
// (mw4hook_abi.h has what is known of them).

#include "mod.h"

// the mech the player sits in: its folder's name in small letters ("catapult"); false: not a mech,
// or not in a battle
bool gmPlayerMech(char *out, int n) {
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
        if ((uintptr_t)path < 0x10000 || _strnicmp(path, "mechs\\", 6) != 0) return false;
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
uint8_t *hkBase() {
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
void gmCheck() {
    uint32_t stamp, image;
    if (abiBuildIs(GetModuleHandleW(nullptr), GM_KNOWN, &stamp, &image))
        logf("abi: the game is the known build (%s)", GM_KNOWN.name);
    else
        logf("abi: the game is NOT the build this was made for (%s): timestamp %08x, image %x - the camera's addresses "
             "may be wrong",
             GM_KNOWN.name, stamp, image);
}
