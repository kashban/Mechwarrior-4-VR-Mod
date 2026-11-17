// ---------------------------------------------------------------------------------------------
// A profile for every cockpit. The cockpit is drawn on the game's CPU path (pre-transformed
// vertices, VS ad8cee09): in a measured frame the triangles of those draws nearer than the
// cockpit's range are read back (left eye's pass), taken to the game camera's own space (the head's
// movement undone) and entered into a coarse grid of directions (4 degrees a cell) as the distance
// of the nearest surface. The median of some such frames is the cockpit's signature: it tells the
// cockpits apart (the mech is recognised by it at the start of a battle) and gives three
// measures - how far the console below the window is, how high it reaches, and where the window's
// middle lies. Profiles are kept in mw4vr_mechs.ini: the cockpit's scale, the seat and the near HUD
// panels' place. A cockpit set by hand (a hot key, or Ctrl+F10 "this one fits") is "tuned"; one
// that is not gets its values from the tuned ones by its measures (the console at the same
// apparent distance, the panels as high above it and as far from the window's middle).
// Included into main.cpp before the draw hooks; vr.inc applies and stores the values.

static volatile bool g_mechOn;            // [vr] mech_profiles=1
static volatile bool g_mechAuto = true;   // [vr] mech_auto_fit=1
enum { MG_AZ = 20, MG_EL = 18, MG_N = MG_AZ * MG_EL, MG_SAMPLES = 15, MECH_MAX = 64 };
static const float MG_AZ0 = -40.0f, MG_EL0 = -48.0f, MG_STEP = 4.0f;
static float g_mgFrame[MG_N];             // this frame's grid (metres, 0 = nothing)
static int g_mgTris, g_mgBad, g_mgDraws;
static float g_mgSamples[MG_SAMPLES][MG_N];
static int g_mgSampleN;
static bool g_mechSearching;              // samples are being collected
static bool g_mechSeated;                 // the game draws its cockpit: the camera sits in the mech (not a mission's intro)
static unsigned g_mechBattleFrame;        // the battle's first frame
static unsigned g_mechNextFrame;          // the next frame to measure
static unsigned g_mechLastCamFrame = ~0u;

struct MechProfile {
    wchar_t name[48];
    wchar_t model[32]; // the mech's folder in the game's files ("catapult"); empty: not known yet
    float sig[MG_N];
    bool tuned;
    float cockpitScale, seatFwd, seatUp, panelX, panelY, panelDist, panelScale;
    float dash, top, centre; // console distance (m), its top (degrees), the window's middle (degrees)
};
static MechProfile g_mechs[MECH_MAX];
static int g_mechN, g_mechCur = -1;
static bool g_mechLoaded;

static std::wstring mechIni() { return g_dir + L"mw4vr_mechs.ini"; }

static float mechGetF(const wchar_t *sec, const wchar_t *key, float def) {
    wchar_t b[64];
    GetPrivateProfileStringW(sec, key, L"", b, 64, mechIni().c_str());
    return b[0] ? (float)_wtof(b) : def;
}
static void mechPutF(const wchar_t *sec, const wchar_t *key, float v) {
    wchar_t b[64];
    swprintf(b, 64, L"%.3f", v);
    WritePrivateProfileStringW(sec, key, b, mechIni().c_str());
}

static void mechMeasures(MechProfile &p) {
    // the console: the median distance below the window, straight ahead
    float v[MG_N];
    int n = 0;
    for (int r = 0; r < MG_EL; ++r) {
        float el = MG_EL0 + (r + 0.5f) * MG_STEP;
        if (el < -40 || el > -16) continue;
        for (int c = 0; c < MG_AZ; ++c) {
            float az = MG_AZ0 + (c + 0.5f) * MG_STEP;
            if (az < -16 || az > 16) continue;
            if (p.sig[r * MG_AZ + c] > 0) v[n++] = p.sig[r * MG_AZ + c];
        }
    }
    p.dash = 0, p.top = 0, p.centre = 0;
    if (n < 6) return;
    std::sort(v, v + n);
    p.dash = v[n / 2];
    // its top: in the columns straight ahead, the highest cell below the horizon that belongs to it
    float tops[MG_AZ];
    int nt = 0;
    for (int c = 0; c < MG_AZ; ++c) {
        float az = MG_AZ0 + (c + 0.5f) * MG_STEP;
        if (az < -12 || az > 12) continue;
        for (int r = MG_EL - 1; r >= 0; --r) {
            float el = MG_EL0 + (r + 0.5f) * MG_STEP;
            if (el > 6) continue;
            float d = p.sig[r * MG_AZ + c];
            if (d > 0 && d < 1.6f * p.dash) {
                tops[nt++] = el + 0.5f * MG_STEP;
                break;
            }
        }
    }
    if (nt >= 3) {
        std::sort(tops, tops + nt);
        p.top = tops[nt / 2];
    }
    // the window's middle: the open cells around the horizon
    float sum = 0;
    int open = 0;
    for (int r = 0; r < MG_EL; ++r) {
        float el = MG_EL0 + (r + 0.5f) * MG_STEP;
        if (el < -4 || el > 12) continue;
        for (int c = 0; c < MG_AZ; ++c)
            if (p.sig[r * MG_AZ + c] <= 0) sum += MG_AZ0 + (c + 0.5f) * MG_STEP, ++open;
    }
    if (open >= 4) p.centre = sum / open;
}

static void mechLoad() {
    g_mechLoaded = true;
    g_mechN = 0;
    int n = GetPrivateProfileIntW(L"mechs", L"count", 0, mechIni().c_str());
    if (n > MECH_MAX) n = MECH_MAX;
    static wchar_t buf[8192];
    for (int i = 0; i < n; ++i) {
        wchar_t sec[32];
        swprintf(sec, 32, L"mech_%d", i + 1);
        MechProfile &p = g_mechs[g_mechN];
        memset(&p, 0, sizeof p);
        GetPrivateProfileStringW(sec, L"name", sec, p.name, 48, mechIni().c_str());
        GetPrivateProfileStringW(sec, L"model", L"", p.model, 32, mechIni().c_str());
        GetPrivateProfileStringW(sec, L"signature", L"", buf, 8192, mechIni().c_str());
        int k = 0;
        for (wchar_t *q = buf; *q && k < MG_N;) {
            p.sig[k++] = (float)wcstol(q, &q, 10) * 0.01f;
            while (*q == L',' || *q == L' ') ++q;
        }
        if (k != MG_N) {
            logf("mech: profile %d in mw4vr_mechs.ini has no usable signature (%d of %d values) - left out", i + 1, k, (int)MG_N);
            // keeps its number: an empty signature matches nothing
            for (int j = 0; j < MG_N; ++j) p.sig[j] = 0;
        }
        p.tuned = GetPrivateProfileIntW(sec, L"tuned", 0, mechIni().c_str()) != 0;
        p.cockpitScale = mechGetF(sec, L"cockpit_scale", 1);
        p.seatFwd = mechGetF(sec, L"seat_forward_m", 0);
        p.seatUp = mechGetF(sec, L"seat_up_m", 0);
        p.panelX = mechGetF(sec, L"hud_panel_x_m", 0);
        p.panelY = mechGetF(sec, L"hud_panel_y_m", 0);
        p.panelDist = mechGetF(sec, L"hud_panel_distance_m", 1.5f);
        p.panelScale = mechGetF(sec, L"hud_panel_scale", 0.6f);
        mechMeasures(p);
        ++g_mechN;
    }
    logf("mech: %d cockpit profiles in mw4vr_mechs.ini", g_mechN);
}

// the values of a profile set by hand are read again (they can be edited while playing)
static void mechReload(int i) {
    if (i < 0 || i >= g_mechN) return;
    wchar_t sec[32];
    swprintf(sec, 32, L"mech_%d", i + 1);
    MechProfile &p = g_mechs[i];
    p.tuned = GetPrivateProfileIntW(sec, L"tuned", 0, mechIni().c_str()) != 0;
    p.cockpitScale = mechGetF(sec, L"cockpit_scale", p.cockpitScale);
    p.seatFwd = mechGetF(sec, L"seat_forward_m", p.seatFwd);
    p.seatUp = mechGetF(sec, L"seat_up_m", p.seatUp);
    p.panelX = mechGetF(sec, L"hud_panel_x_m", p.panelX);
    p.panelY = mechGetF(sec, L"hud_panel_y_m", p.panelY);
    p.panelDist = mechGetF(sec, L"hud_panel_distance_m", p.panelDist);
    p.panelScale = mechGetF(sec, L"hud_panel_scale", p.panelScale);
}

static void mechWriteNew(int i) {
    wchar_t sec[32];
    swprintf(sec, 32, L"mech_%d", i + 1);
    const MechProfile &p = g_mechs[i];
    std::wstring s;
    for (int k = 0; k < MG_N; ++k) {
        wchar_t b[16];
        swprintf(b, 16, k ? L",%d" : L"%d", (int)(p.sig[k] * 100 + 0.5f));
        s += b;
    }
    wchar_t b[16];
    swprintf(b, 16, L"%d", g_mechN);
    WritePrivateProfileStringW(L"mechs", L"count", b, mechIni().c_str());
    WritePrivateProfileStringW(sec, L"name", p.name, mechIni().c_str());
    if (p.model[0]) WritePrivateProfileStringW(sec, L"model", p.model, mechIni().c_str());
    WritePrivateProfileStringW(sec, L"tuned", L"0", mechIni().c_str());
    mechPutF(sec, L"console_m", p.dash);
    mechPutF(sec, L"console_top_deg", p.top);
    mechPutF(sec, L"window_middle_deg", p.centre);
    WritePrivateProfileStringW(sec, L"signature", s.c_str(), mechIni().c_str());
}

// how alike two signatures are: the mean difference of the distances' logarithms over the cells
// both have; 1e9 when they share too little
static float mechDiff(const float *a, const float *b) {
    int na = 0, nb = 0, both = 0;
    double sum = 0;
    for (int k = 0; k < MG_N; ++k) {
        if (a[k] > 0) ++na;
        if (b[k] > 0) ++nb;
        if (a[k] > 0 && b[k] > 0) sum += fabs(log((double)a[k] / b[k])), ++both;
    }
    if (both < 40 || both * 10 < na * 6 || both * 10 < nb * 6) return 1e9f;
    return (float)(sum / both);
}

// ---- measuring -------------------------------------------------------------------------------

struct MgVtx {
    float az, el, d;
};

static void mgTriangle(const MgVtx &a, const MgVtx &b, const MgVtx &c) {
    float az0 = a.az < b.az ? a.az : b.az, az1 = a.az > b.az ? a.az : b.az;
    float el0 = a.el < b.el ? a.el : b.el, el1 = a.el > b.el ? a.el : b.el;
    if (c.az < az0) az0 = c.az;
    if (c.az > az1) az1 = c.az;
    if (c.el < el0) el0 = c.el;
    if (c.el > el1) el1 = c.el;
    if (az1 - az0 > 120) return; // around the back: not a cell's worth
    int c0 = (int)floorf((az0 - MG_AZ0) / MG_STEP), c1 = (int)floorf((az1 - MG_AZ0) / MG_STEP);
    int r0 = (int)floorf((el0 - MG_EL0) / MG_STEP), r1 = (int)floorf((el1 - MG_EL0) / MG_STEP);
    if (c1 < 0 || r1 < 0 || c0 >= MG_AZ || r0 >= MG_EL) return;
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 >= MG_AZ) c1 = MG_AZ - 1;
    if (r1 >= MG_EL) r1 = MG_EL - 1;
    float den = (b.el - c.el) * (a.az - c.az) + (c.az - b.az) * (a.el - c.el);
    bool any = false;
    if (fabsf(den) > 1e-6f)
        for (int r = r0; r <= r1; ++r)
            for (int cc = c0; cc <= c1; ++cc) {
                float pa = MG_AZ0 + (cc + 0.5f) * MG_STEP, pe = MG_EL0 + (r + 0.5f) * MG_STEP;
                float l0 = ((b.el - c.el) * (pa - c.az) + (c.az - b.az) * (pe - c.el)) / den;
                float l1 = ((c.el - a.el) * (pa - c.az) + (a.az - c.az) * (pe - c.el)) / den;
                float l2 = 1 - l0 - l1;
                if (l0 < 0 || l1 < 0 || l2 < 0) continue;
                float d = l0 * a.d + l1 * b.d + l2 * c.d;
                float &g = g_mgFrame[r * MG_AZ + cc];
                if (g <= 0 || d < g) g = d;
                any = true;
            }
    if (!any) { // smaller than a cell: where its middle is
        float pa = (a.az + b.az + c.az) / 3, pe = (a.el + b.el + c.el) / 3, d = (a.d + b.d + c.d) / 3;
        int cc = (int)floorf((pa - MG_AZ0) / MG_STEP), r = (int)floorf((pe - MG_EL0) / MG_STEP);
        if (cc >= 0 && cc < MG_AZ && r >= 0 && r < MG_EL) {
            float &g = g_mgFrame[r * MG_AZ + cc];
            if (g <= 0 || d < g) g = d;
        }
    }
}

// the vertices are read from the buffers' own memory; nothing here has a destructor (SEH)
static void mgRead(const uint8_t *vbase, UINT vbytes, UINT stride, UINT voff, INT baseV, const uint8_t *ibase, UINT ibytes,
                   UINT ioff, bool idx32, UINT start, UINT count, float W, float H) {
    const float *R = g_vrR[0], *T = g_vrT[0];
    __try {
        for (UINT t = 0; t + 2 < count; t += 3) {
            MgVtx v[3];
            bool ok = true;
            for (int k = 0; k < 3 && ok; ++k) {
                long long vi;
                if (ibase) {
                    UINT io = ioff + (start + t + k) * (idx32 ? 4u : 2u);
                    if (io + 4 > ibytes) {
                        ok = false;
                        break;
                    }
                    vi = idx32 ? *(const uint32_t *)(ibase + io) : *(const uint16_t *)(ibase + io);
                    vi += baseV;
                } else {
                    vi = (long long)start + t + k;
                }
                long long vo = (long long)voff + vi * stride;
                if (vi < 0 || vo + 16 > (long long)vbytes) {
                    ok = false;
                    break;
                }
                const float *p = (const float *)(vbase + vo);
                float rhw = p[3];
                if (!(rhw > 1e-4f) || !(rhw < 100.0f) || !(fabsf(p[0]) < 1e6f) || !(fabsf(p[1]) < 1e6f)) {
                    ok = false;
                    break;
                }
                float w = 1.0f / rhw;
                if (w >= g_cockpitRange) { // not the cockpit
                    ok = false;
                    break;
                }
                float nx = (p[0] + 0.5f) * 2.0f / W - 1.0f, ny = 1.0f - (p[1] + 0.5f) * 2.0f / H;
                if (fabsf(nx) > 3 || fabsf(ny) > 3) {
                    ++g_mgBad;
                    ok = false;
                    break;
                }
                float e[3] = {nx * w / g_P00, ny * w / g_P11, w}; // the eye's view space
                float x = R[0] * e[0] + R[1] * e[1] + R[2] * e[2] + T[0];
                float y = R[3] * e[0] + R[4] * e[1] + R[5] * e[2] + T[1];
                float z = R[6] * e[0] + R[7] * e[1] + R[8] * e[2] + T[2];
                float h = sqrtf(x * x + z * z);
                v[k].az = atan2f(x, z) * 57.29578f;
                v[k].el = atan2f(y, h) * 57.29578f;
                v[k].d = sqrtf(x * x + y * y + z * z);
                if (!(v[k].d > 0.02f)) ok = false;
            }
            if (!ok) continue;
            mgTriangle(v[0], v[1], v[2]);
            ++g_mgTris;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ++g_mgBad;
    }
}

typedef HRESULT(STDMETHODCALLTYPE *MechPFN_Map)(ID3D11DeviceContext *, ID3D11Resource *, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE *);
typedef void(STDMETHODCALLTYPE *MechPFN_Unmap)(ID3D11DeviceContext *, ID3D11Resource *, UINT);

// a CPU-path draw of the measured pass
static void mechDraw(ID3D11DeviceContext *c, bool indexed, UINT count, UINT start, INT baseV) {
    if (count < 3 || count > 200000 || !(g_P00 > 0) || !(g_P11 > 0)) return;
    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    c->IAGetPrimitiveTopology(&topo);
    if (topo != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) return;
    D3D11_VIEWPORT vp = {};
    UINT nvp = 1;
    c->RSGetViewports(&nvp, &vp);
    if (!nvp || vp.Width < 16 || vp.Height < 16) return;
    ID3D11Buffer *vb = nullptr, *ib = nullptr;
    UINT stride = 0, voff = 0, ioff = 0;
    DXGI_FORMAT ifmt = DXGI_FORMAT_UNKNOWN;
    c->IAGetVertexBuffers(0, 1, &vb, &stride, &voff);
    if (indexed) c->IAGetIndexBuffer(&ib, &ifmt, &ioff);
    D3D11_BUFFER_DESC vd = {}, id = {};
    if (vb) vb->GetDesc(&vd);
    if (ib) ib->GetDesc(&id);
    bool ok = vb && stride >= 16 && vd.Usage == D3D11_USAGE_DYNAMIC && (vd.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) &&
              (!indexed || (ib && id.Usage == D3D11_USAGE_DYNAMIC && (id.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) &&
                            (ifmt == DXGI_FORMAT_R16_UINT || ifmt == DXGI_FORMAT_R32_UINT)));
    if (ok) {
        // the buffers' memory as it is (nothing is written)
        D3D11_MAPPED_SUBRESOURCE mv = {}, mi = {};
        bool mvOk = SUCCEEDED(ORIG(MechPFN_Map, c, VT_Ctx_Map)(c, vb, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mv)) && mv.pData;
        bool miOk = !indexed || (SUCCEEDED(ORIG(MechPFN_Map, c, VT_Ctx_Map)(c, ib, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mi)) && mi.pData);
        if (mvOk && miOk) {
            mgRead((const uint8_t *)mv.pData, vd.ByteWidth, stride, voff, baseV, indexed ? (const uint8_t *)mi.pData : nullptr,
                   id.ByteWidth, ioff, ifmt == DXGI_FORMAT_R32_UINT, start, count, vp.Width, vp.Height);
            ++g_mgDraws;
        }
        if (indexed && miOk && mi.pData) ORIG(MechPFN_Unmap, c, VT_Ctx_Unmap)(c, ib, 0);
        if (mvOk) ORIG(MechPFN_Unmap, c, VT_Ctx_Unmap)(c, vb, 0);
    }
    if (vb) vb->Release();
    if (ib) ib->Release();
}

static inline bool mechMeasuring() { return g_mechMeasure && g_camInMain && g_camPassEye == 0; }

// ---- once per frame (from vrOnPresent) -------------------------------------------------------

static void mechIdentify() {
    // the median over the samples, cell by cell
    static float sig[MG_N];
    int valid = 0;
    for (int k = 0; k < MG_N; ++k) {
        float v[MG_SAMPLES];
        int n = 0;
        for (int s = 0; s < g_mgSampleN; ++s)
            if (g_mgSamples[s][k] > 0) v[n++] = g_mgSamples[s][k];
        if (n * 2 > g_mgSampleN) {
            std::sort(v, v + n);
            sig[k] = v[n / 2];
            ++valid;
        } else {
            sig[k] = 0;
        }
    }
    if (valid < 50) {
        logf("mech: the cockpit covers only %d cells - not recognised (outside view?), trying again", valid);
        g_mgSampleN = 0;
        return;
    }
    // the mech's name, when the game tells it, decides; the signature only among profiles without a name
    char mech[32];
    wchar_t model[32] = L"";
    if (gmPlayerMech(mech, 32)) swprintf(model, 32, L"%hs", mech);
    int best = -1;
    float bd = 1e9f;
    bool byName = false;
    for (int i = 0; i < g_mechN && !byName; ++i) {
        const MechProfile &q = g_mechs[i];
        if (model[0] && q.model[0]) {
            if (!_wcsicmp(model, q.model)) best = i, bd = mechDiff(sig, q.sig), byName = true;
            continue; // another mech's profile
        }
        float d = mechDiff(sig, q.sig);
        if (d < bd) bd = d, best = i;
    }
    g_mechSearching = false;
    if (best >= 0 && (byName || bd < 0.06f)) {
        g_mechCur = best;
        mechReload(best);
        MechProfile &p = g_mechs[best];
        if (model[0] && !p.model[0]) { // a profile from before the names: it is this mech's now
            wchar_t sec[32];
            swprintf(sec, 32, L"mech_%d", best + 1);
            wcscpy_s(p.model, model);
            WritePrivateProfileStringW(sec, L"model", model, mechIni().c_str());
            if (!wcsncmp(p.name, L"cockpit ", 8)) {
                wcscpy_s(p.name, model);
                WritePrivateProfileStringW(sec, L"name", model, mechIni().c_str());
            }
        }
        logf("mech: cockpit recognised - profile %d '%s' (%s, difference %.3f), %s; console %.2f m, top %.0f deg, window middle %.0f deg",
             best + 1, narrow(p.name).c_str(), byName ? "by the mech's name" : "by its shape", bd < 1e8f ? bd : -1.0f,
             p.tuned ? "set by hand" : "fitted from the others", p.dash, p.top, p.centre);
        return;
    }
    if (g_mechN >= MECH_MAX) {
        logf("mech: a cockpit not known, and no room for another profile (%d)", (int)MECH_MAX);
        g_mechCur = -1;
        return;
    }
    MechProfile &p = g_mechs[g_mechN];
    memset(&p, 0, sizeof p);
    memcpy(p.sig, sig, sizeof sig);
    if (model[0]) wcscpy_s(p.name, model), wcscpy_s(p.model, model);
    else swprintf(p.name, 48, L"cockpit %d", g_mechN + 1);
    p.cockpitScale = 1, p.panelDist = 1.5f, p.panelScale = 0.6f;
    mechMeasures(p);
    g_mechCur = g_mechN++;
    mechWriteNew(g_mechCur);
    logf("mech: a new cockpit - profile %d (%d cells; nearest known one differs by %.3f); console %.2f m, top %.0f deg, window middle %.0f deg",
         g_mechCur + 1, valid, bd < 1e8f ? bd : -1.0f, p.dash, p.top, p.centre);
    Beep(500, 40);
}

// inBattle: the main camera is drawn with head tracking; zoomed: its projection is not the wide one
static void mechOnPresent(bool inBattle, bool zoomed) {
    bool measured = g_mechMeasure;
    g_mechMeasure = false;
    if (!g_mechOn) {
        g_mechCur = -1;
        return;
    }
    if (!g_mechLoaded) mechLoad();
    // a battle begins (the camera was not drawn for a while): which cockpit is it?
    if (inBattle && (g_mechLastCamFrame == ~0u || g_frame - g_mechLastCamFrame > 30)) {
        g_mechSearching = true;
        g_mechSeated = false;
        g_mechBattleFrame = g_frame;
        g_mgSampleN = 0;
        g_mechNextFrame = g_frame + 60; // (the cockpit known so far stays in use until this one is recognised)
    }
    if (inBattle) g_mechLastCamFrame = g_frame;
    // the game tells the mech's name and there is a profile of it: no need to look at the cockpit's shape,
    // so its values and its MechWarrior 5 cockpit are there from the battle's first picture
    if (g_mechSearching && inBattle) {
        char mech[32];
        wchar_t model[32];
        if (gmPlayerMech(mech, 32)) {
            swprintf(model, 32, L"%hs", mech);
            for (int i = 0; i < g_mechN; ++i) {
                if (_wcsicmp(model, g_mechs[i].model)) continue;
                g_mechSearching = false;
                g_mgSampleN = 0;
                g_mechCur = i;
                g_mechNextFrame = g_frame + 2; // (is the camera in the cockpit? - looked at now)
                mechReload(i);
                logf("mech: profile %d '%s' by the mech's name, at once (%s)", i + 1, narrow(g_mechs[i].name).c_str(),
                     g_mechs[i].tuned ? "set by hand" : "fitted from the others");
                break;
            }
        }
    }
    if (measured && g_mgTris >= 30 && !g_mechSeated) {
        g_mechSeated = true;
        logf("mech: frame %u - the game draws its cockpit (%d triangles): the camera sits in the mech", g_frame, g_mgTris);
    }
    if (inBattle && !g_mechSeated && !g_mechSearching && g_frame - g_mechBattleFrame > 2700) { // never seen for half a minute: not held back for ever
        g_mechSeated = true;
        logf("mech: frame %u - no cockpit of the game's seen in this battle; ours is shown all the same", g_frame);
    }
    if (measured && g_mechSearching) {
        static int logs;
        if (logs < 6) {
            ++logs;
            int cells = 0;
            for (int k = 0; k < MG_N; ++k)
                if (g_mgFrame[k] > 0) ++cells;
            logf("mech: measured frame %u - %d draws, %d cockpit triangles, %d cells, %d unreadable", g_frame, g_mgDraws, g_mgTris, cells, g_mgBad);
        }
        if (g_mgTris >= 30 && g_mgSampleN < MG_SAMPLES) memcpy(g_mgSamples[g_mgSampleN++], g_mgFrame, sizeof g_mgFrame);
        if (g_mgSampleN >= MG_SAMPLES) mechIdentify();
    }
    if ((g_mechSearching || !g_mechSeated) && inBattle && !zoomed && g_doubleDraw && g_frame >= g_mechNextFrame) {
        memset(g_mgFrame, 0, sizeof g_mgFrame);
        g_mgTris = g_mgBad = g_mgDraws = 0;
        g_mechMeasure = true; // the coming frame
        // outside the cockpit nothing is found: then only now and then
        g_mechNextFrame = g_frame + (!g_mechSearching ? 10 : g_mgSampleN ? 12 : 45);
    }
}

// ---- the values in use -----------------------------------------------------------------------

struct MechValues {
    float cockpitScale, seatFwd, seatUp, panelX, panelY, panelDist, panelScale;
};
static inline float mechClamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// g: the [vr] values; out: what this cockpit gets. false: the [vr] values stay
static bool mechValues(const MechValues &g, MechValues *out, const char **how) {
    if (!g_mechOn || g_mechCur < 0 || g_mechCur >= g_mechN) return false;
    const MechProfile &p = g_mechs[g_mechCur];
    if (p.tuned) {
        *out = {p.cockpitScale, p.seatFwd, p.seatUp, p.panelX, p.panelY, p.panelDist, p.panelScale};
        *how = "set by hand";
        return true;
    }
    if (!g_mechAuto || !(p.dash > 0.05f)) return false;
    // from the tuned cockpits: the one most alike in its console's distance counts most
    double wsum = 0, k = 0, sf = 0, su = 0, px = 0, py = 0, pd = 0, ps = 0;
    for (int i = 0; i < g_mechN; ++i) {
        const MechProfile &r = g_mechs[i];
        if (!r.tuned || !(r.dash > 0.05f)) continue;
        double w = 1.0 / (0.1 + fabs(log((double)p.dash / r.dash)));
        double ratio = (double)p.dash / r.dash;
        float D = r.panelDist;
        k += w * r.cockpitScale * ratio;        // the console as far away as there
        sf += w * r.seatFwd * ratio;            // the seat moved in proportion
        su += w * r.seatUp * ratio;
        px += w * (r.panelX + (tanf(p.centre * 0.0174533f) - tanf(r.centre * 0.0174533f)) * D);
        py += w * (r.panelY + (tanf(p.top * 0.0174533f) - tanf(r.top * 0.0174533f)) * D);
        pd += w * D;
        ps += w * r.panelScale;
        wsum += w;
    }
    if (wsum <= 0) return false;
    out->cockpitScale = mechClamp((float)(k / wsum), 0.5f, 8.0f);
    out->seatFwd = mechClamp((float)(sf / wsum), -3.0f, 3.0f);
    out->seatUp = mechClamp((float)(su / wsum), -3.0f, 3.0f);
    out->panelX = mechClamp((float)(px / wsum), g.panelX - 0.6f, g.panelX + 0.6f);
    out->panelY = mechClamp((float)(py / wsum), g.panelY - 0.6f, g.panelY + 0.6f);
    out->panelDist = (float)(pd / wsum);
    out->panelScale = (float)(ps / wsum);
    *how = "fitted from the cockpits set by hand";
    return true;
}

// the values in use become this cockpit's own ("set by hand"); false: no cockpit is known now
static bool mechStore(const MechValues &v) {
    if (!g_mechOn || g_mechCur < 0 || g_mechCur >= g_mechN) return false;
    wchar_t sec[32];
    swprintf(sec, 32, L"mech_%d", g_mechCur + 1);
    MechProfile &p = g_mechs[g_mechCur];
    p.tuned = true;
    p.cockpitScale = v.cockpitScale, p.seatFwd = v.seatFwd, p.seatUp = v.seatUp;
    p.panelX = v.panelX, p.panelY = v.panelY, p.panelDist = v.panelDist, p.panelScale = v.panelScale;
    WritePrivateProfileStringW(sec, L"tuned", L"1", mechIni().c_str());
    mechPutF(sec, L"cockpit_scale", v.cockpitScale);
    mechPutF(sec, L"seat_forward_m", v.seatFwd);
    mechPutF(sec, L"seat_up_m", v.seatUp);
    mechPutF(sec, L"hud_panel_x_m", v.panelX);
    mechPutF(sec, L"hud_panel_y_m", v.panelY);
    mechPutF(sec, L"hud_panel_distance_m", v.panelDist);
    mechPutF(sec, L"hud_panel_scale", v.panelScale);
    return true;
}
