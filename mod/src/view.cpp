// ---------------------------------------------------------------------------------------------
// The renderer's constant buffer 'View': where the camera's matrix lies in it, the projection read from it, which
// eye a frame is for - and the capture of one frame for diagnosis (Ctrl+Shift+F11).

#include "mod.h"

static void eyeLearnLayout(const float *m) {
    // in camera mode only the main camera's projection counts: the zoom window's and the target view's
    // cameras have their own, and the cockpit's transform and the layers' size are the main one's
    if (g_srcCamActive && !g_camInMain) return;
    // the w row of a world->clip matrix is the view's unit z axis (plus a translation)
    float cm = len3(m[3], m[7], m[11]), rm = len3(m[12], m[13], m[14]);
    int idx = -1;
    float p00 = 0;
    if (fabsf(cm - 1) < 0.02f && fabsf(rm - 1) > 0.05f) {
        idx = 12;
        p00 = len3(m[0], m[4], m[8]);
    } else if (fabsf(rm - 1) < 0.02f && fabsf(cm - 1) > 0.05f) {
        idx = 3;
        p00 = len3(m[0], m[1], m[2]);
        if (g_camInMain) rpLearnProj(m);
        if (p00 > 0) {
            g_right[0] = m[0] / p00;
            g_right[1] = m[1] / p00;
            g_right[2] = m[2] / p00;
            g_haveRight = true;
        }
        g_P11 = len3(m[4], m[5], m[6]);
        if (g_P11 > 0) {
            g_near = m[11];
            for (int i = 0; i < 3; ++i) {
                g_up[i] = m[4 + i] / g_P11;
                g_fwd[i] = m[12 + i];
            }
        }
    }
    if (g_layoutLogs < 3) {
        ++g_layoutLogs;
        logf("eye: g_vWorldToClip = [%.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f "
             "%.4f]"
             " -> |w row| column-major %.4f, row-major %.4f => x term at float %d, P00 %.4f",
             m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15], cm,
             rm, idx, p00);
    }
    if (idx >= 0) {
        g_xIdx = idx;
        g_P00 = p00;
        camCalibrate();
    }
}

// data holds the buffer bytes [base, base + sz) - a whole buffer (base 0) or the box of a partial update
void eyeLayoutFrom(int kind, const uint8_t *data, UINT sz, UINT base) {
    if (kind == EK_VIEW && base <= 480 && base + sz >= 480 + 64) eyeLearnLayout((const float *)(data + 480 - base));
}

static void eyeReadIni() {
    std::wstring p = g_dir + L"mw4vr.ini";
    if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
        FILE *f = _wfsopen(p.c_str(), L"w", _SH_DENYNO);
        if (f) {
            fputs("; MW4VR - no settings were found. Copy mw4vr.ini from the mod's package (docs/mw4vr.ini.example) "
                  "over this file:\n"
                  "; it has every key with a comment. Until then the mod stays switched off.\n"
                  "[vr]\n"
                  "enabled=0\n",
                  f);
            fclose(f);
        }
    }
}

static void hideOnPresent() {
    {
        std::lock_guard<std::mutex> lk(g_hideMx);
        if (!g_vsFrame.empty()) g_vsList.swap(g_vsFrame);
        g_vsFrame.clear();
    }
    static bool keyWas;
    static int idx;
    bool key = g_dbg && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
               (GetAsyncKeyState(VK_F8) & 0x8000); // (diagnosis: [debug] level=1 only)
    if (key && !keyWas) {
        std::lock_guard<std::mutex> lk(g_hideMx);
        int n = (int)g_vsList.size();
        idx = (idx + 1) % (n + 1);
        if (idx == 0) {
            g_hideHash = 0;
            logf("hide: nothing hidden");
            Beep(400, 80);
        } else {
            g_hideHash = g_vsList[idx - 1];
            logf("hide: VS %016llx hidden (%d of %d)", (unsigned long long)g_hideHash, idx, n);
            Beep(700 + 60 * idx, 80);
        }
    }
    keyWas = key;
}

void eyeOnPresent() {
    hideOnPresent();
    static bool first = true;
    if (first) {
        first = false;
        eyeReadIni();
    }
    static bool keyWas;
    // (diagnosis, [debug] level=1 only: without it a slip beside Ctrl+F11 switched the eyes off - one flat picture,
    // no HUD layer, no menu)
    bool key = g_dbg && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_F7) & 0x8000);
    if (key && !keyWas) {
        g_eyeMode = (g_eyeMode + 1) & 3;
        static const char *names[] = {"off", "right eye", "left eye", "alternating"};
        logf("eye: mode %d (%s), P00 %.4f", g_eyeMode, names[g_eyeMode], g_P00);
        Beep(g_eyeMode ? 600 + 200 * g_eyeMode : 400, 80);
    }
    keyWas = key;
}

HMODULE g_self;
std::string moduleOf(void *addr) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)addr, &m) ||
        !m)
        return "?";
    char o[64];
    snprintf(o, sizeof o, "+%lx", (unsigned long)((char *)addr - (char *)m));
    if (m == g_self) return std::string("PROBE") + o;
    wchar_t n[MAX_PATH];
    GetModuleFileNameW(m, n, MAX_PATH);
    const wchar_t *b = wcsrchr(n, L'\\');
    return narrow(b ? b + 1 : n) + o;
}

void dumpSlots(void *obj, const char *what) {
    if (!obj) return;
    void **vt = *(void ***)obj;
    struct {
        const char *n;
        int i;
    } slots[] = {{"Draw", VT_Ctx_Draw},
                 {"DrawIndexed", VT_Ctx_DrawIndexed},
                 {"DrawIndexedInstanced", VT_Ctx_DrawIndexedInstanced},
                 {"DrawInstanced", VT_Ctx_DrawInstanced},
                 {"Dispatch", VT_Ctx_Dispatch},
                 {"VSSetShader", VT_Ctx_VSSetShader},
                 {"PSSetShader", VT_Ctx_PSSetShader},
                 {"PSSetConstantBuffers", VT_Ctx_PSSetConstantBuffers},
                 {"OMSetRenderTargets", VT_Ctx_OMSetRenderTargets},
                 {"RSSetViewports", VT_Ctx_RSSetViewports},
                 {"Map", VT_Ctx_Map},
                 {"Unmap", VT_Ctx_Unmap}};
    logf("%s %p vtable %p (%s):", what, obj, vt, moduleOf(vt).c_str());
    for (auto &s : slots) logf("   [%3d] %-22s -> %p %s", s.i, s.n, vt[s.i], moduleOf(vt[s.i]).c_str());
}

void startCapture() {
    dumpSlots(g_imm, "immediate context");
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%smw4vr_frame_%03u.txt", g_dir.c_str(), g_capIndex++);
    {
        std::lock_guard<std::mutex> lk(g_capMx);
        g_capf = _wfsopen(p, L"w", _SH_DENYNO);
        if (!g_capf) return;
        fprintf(g_capf, "frame %u  (c<N> = context, d = deferred; indentation = label depth)\n", g_frame);
    }
    {
        std::lock_guard<std::mutex> lk(g_snapMx);
        g_snap.clear();
        g_ver.clear();
        g_dumped.clear();
    }
    {
        std::lock_guard<std::mutex> lk(g_ctxMx);
        for (auto &kv : g_ctx) {
            kv.second->pendCount = 0;
            kv.second->pendSig.clear();
            kv.second->mapped.clear();
            kv.second->mapType.clear();
        }
    }
    g_cap = true;
    logf("capture started: %s", narrow(p).c_str());
}

void endCapture() {
    g_cap = false;
    {
        std::lock_guard<std::mutex> lk(g_ctxMx);
        for (auto &kv : g_ctx)
            if (kv.second->pendCount) {
                std::lock_guard<std::mutex> lk2(g_capMx);
                if (g_capf)
                    fprintf(g_capf, "[c%d%s] %s : %u\n", kv.second->id, kv.second->deferred ? "d" : "",
                            kv.second->pendSig.c_str(), kv.second->pendCount);
                kv.second->pendCount = 0;
            }
    }
    std::lock_guard<std::mutex> lk(g_capMx);
    if (g_capf) {
        fprintf(g_capf, "end of frame\n");
        fclose(g_capf);
        g_capf = nullptr;
    }
    logf("capture written");
}
