// ---------------------------------------------------------------------------------------------
// The player's own mech beside the cockpit: its arms and weapon pods. (A trial.)
// Seen from the seat the game draws nothing of the player's mech - no part, no shadow; the arm weapons fire out
// of the air. Seen from outside it draws the mech as rigid parts, each with a matrix into the world. So the parts
// are read there once for each mech - the vertices, the indices and the picture of every part near the place the
// camera had in the seat - and put into the seat camera's space; a file keeps them (mw4vr_arms\<mech>.mwa).
// From then on those of them that lie beside the seat are drawn with our own cockpit (mw5.cpp), at the world's
// scale and depth. They do not move against the torso: what the game does with them it does not tell from the seat.

#include "mod.h"

ArmPart g_arm[ARMS_MAX];
int g_armN;
volatile bool g_armsOn, g_armsWant;
static float g_armsSide = 1.2f;   // a part is drawn when its middle lies this far (m) to the side of the eye
static float g_armsLow = -2.5f;   // and not lower than this (m): the legs move, and that is not known here
static float g_armsDeep = 0.45f;  // lower than that, down to this share of the way to the feet, a part is an arm
static float g_armsWide = 2.4f;   // only if it is this far out (m) - or long and slim like a barrel; the rest are legs
static float g_armsAbove = 0.8f;  // or its middle lies this far (m) above the eye: a weapon on the mech's back
static float g_armsNear = 1.3f;   // not a part nearer to the eye than this (m): it would stand inside the cockpit
static bool g_armsAll;            // (diagnosis) every part that does not hold the eye
static float g_armsSwing = 0.25f; // an arm's way forth and back with the steps (m, each way; 0: the arms stay)
static float g_armsLong = 2.5f;   // a part this long (m) from back to front is an arm
static float g_armsBase[12];      // the game's camera -> world while the camera sat in the mech
static unsigned g_armsBaseFrame = ~0u;
static wchar_t g_armsMech[32]; // the mech the parts are of
static int g_armsOutFrames;    // frames in a row seen from outside

enum { MLR_CB_BYTES = 496, MLR_UV_AT = 80, MLR_TO_WORLD_AT = 368, MLR_TEX_SLOT = 77 }; // mw4hook's 'Mlr' buffer
enum { ARM_TEX_MAX = 2048, ARM_REACH = 14 };

// ---- which vertex layout has what where (told when the layout is made: it cannot be asked later) ----
struct ArmLayout {
    int pos = -1, nrm = -1, uv = -1;
    DXGI_FORMAT nrmFmt = DXGI_FORMAT_UNKNOWN, uvFmt = DXGI_FORMAT_UNKNOWN;
};
static std::mutex g_armIlMx;
static std::unordered_map<void *, ArmLayout> g_armIl;

static UINT armFmtBytes(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return 16;
    case DXGI_FORMAT_R32G32B32_FLOAT:
        return 12;
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
        return 8;
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return 4;
    default:
        return 0;
    }
}

HRESULT STDMETHODCALLTYPE H_CreateInputLayout(ID3D11Device *d, const D3D11_INPUT_ELEMENT_DESC *el, UINT n,
                                              const void *code, SIZE_T len, ID3D11InputLayout **out) {
    HRESULT hr = ORIG(PFN_CreateIL, d, VT_Dev_CreateInputLayout)(d, el, n, code, len, out);
    if (d == g_foreignDev || FAILED(hr) || !out || !*out || !el) return hr;
    ArmLayout L;
    UINT at = 0;
    bool known = true; // the place after the last element is known (for elements that only say "after it")
    for (UINT i = 0; i < n; ++i) {
        const D3D11_INPUT_ELEMENT_DESC &e = el[i];
        if (e.InputSlot != 0 || e.InputSlotClass != D3D11_INPUT_PER_VERTEX_DATA) continue;
        const bool append = e.AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT;
        const UINT off = append ? at : e.AlignedByteOffset;
        const bool offOk = !append || known;
        const UINT sz = armFmtBytes(e.Format);
        known = sz != 0 && offOk;
        at = off + sz;
        if (!offOk || !e.SemanticName || e.SemanticIndex != 0) continue;
        if (!_stricmp(e.SemanticName, "POSITION") && e.Format == DXGI_FORMAT_R32G32B32_FLOAT) L.pos = (int)off;
        if (!_stricmp(e.SemanticName, "NORMAL") &&
            (e.Format == DXGI_FORMAT_R32G32B32_FLOAT || e.Format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
             e.Format == DXGI_FORMAT_R8G8B8A8_UNORM || e.Format == DXGI_FORMAT_R10G10B10A2_UNORM))
            L.nrm = (int)off, L.nrmFmt = e.Format;
        if (!_stricmp(e.SemanticName, "TEXCOORD") &&
            (e.Format == DXGI_FORMAT_R32G32_FLOAT || e.Format == DXGI_FORMAT_R16G16_FLOAT))
            L.uv = (int)off, L.uvFmt = e.Format;
    }
    std::lock_guard<std::mutex> lk(g_armIlMx);
    g_armIl[*out] = L;
    return hr;
}

void armsSeatFrame() {
    if (!g_armsOn || !g_camEyeBaseOk[0]) return;
    memcpy(g_armsBase, g_camEyeBase[0], sizeof g_armsBase);
    g_armsBaseFrame = g_frame;
}

// ---- reading the parts, seen from outside ----
struct CapPart {
    std::vector<uint8_t> v; // 32 bytes a vertex: place, normal, uv
    std::vector<uint32_t> ix;
    int tex = -1;
    float lo[3], hi[3];
};
struct CapTex {
    UINT size = 0;
    std::vector<uint8_t> px; // size x size, RGBA
};
static std::vector<CapPart> g_capParts;
static std::vector<CapTex> g_capTex;
static std::unordered_map<void *, std::vector<uint8_t>> g_capBuf;
static std::unordered_map<void *, int> g_capTexOf;
static std::unordered_set<std::string> g_capSeen;
static unsigned g_capFrame = ~0u;
static int g_capTries;

static bool armRead(ID3D11DeviceContext *c, ID3D11Buffer *b, std::vector<uint8_t> &out) {
    D3D11_BUFFER_DESC bd;
    b->GetDesc(&bd);
    if (!bd.ByteWidth || bd.ByteWidth > (64u << 20)) return false;
    D3D11_BUFFER_DESC sd = {bd.ByteWidth, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
    ID3D11Buffer *st = nullptr;
    if (FAILED(ORIG(PFN_CreateBuffer, g_dev, VT_Dev_CreateBuffer)(g_dev, &sd, nullptr, &st)) || !st) return false;
    c->CopyResource(st, b);
    D3D11_MAPPED_SUBRESOURCE m;
    bool ok = SUCCEEDED(ORIG(PFN_Map, c, VT_Ctx_Map)(c, st, 0, D3D11_MAP_READ, 0, &m)) && m.pData;
    if (ok) {
        out.assign((const uint8_t *)m.pData, (const uint8_t *)m.pData + bd.ByteWidth);
        ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, st, 0);
    }
    st->Release();
    return ok;
}
static const std::vector<uint8_t> *armBuf(ID3D11DeviceContext *c, ID3D11Buffer *b) {
    auto it = g_capBuf.find(b);
    if (it != g_capBuf.end()) return it->second.empty() ? nullptr : &it->second;
    std::vector<uint8_t> v;
    if (!armRead(c, b, v)) v.clear();
    auto &slot = g_capBuf[b];
    slot = std::move(v);
    return slot.empty() ? nullptr : &slot;
}

// a block of 4 x 4 pixels of a BC1 / BC2 / BC3 picture's colours
static void armBcColours(const uint8_t *b, bool punch, uint8_t (*o)[4]) {
    const unsigned c0 = b[0] | (b[1] << 8), c1 = b[2] | (b[3] << 8);
    uint8_t p[4][4];
    const unsigned cs[2] = {c0, c1};
    for (int i = 0; i < 2; ++i) {
        const unsigned r = (cs[i] >> 11) & 31, g = (cs[i] >> 5) & 63, bl = cs[i] & 31;
        p[i][0] = (uint8_t)((r << 3) | (r >> 2)), p[i][1] = (uint8_t)((g << 2) | (g >> 4));
        p[i][2] = (uint8_t)((bl << 3) | (bl >> 2)), p[i][3] = 255;
    }
    for (int k = 0; k < 3; ++k) {
        if (c0 > c1 || !punch) {
            p[2][k] = (uint8_t)((2 * p[0][k] + p[1][k]) / 3), p[3][k] = (uint8_t)((p[0][k] + 2 * p[1][k]) / 3);
        } else {
            p[2][k] = (uint8_t)((p[0][k] + p[1][k]) / 2), p[3][k] = 0;
        }
    }
    p[2][3] = 255, p[3][3] = (c0 > c1 || !punch) ? 255 : 0;
    const uint32_t bits = b[4] | (b[5] << 8) | (b[6] << 16) | ((uint32_t)b[7] << 24);
    for (int i = 0; i < 16; ++i) memcpy(o[i], p[(bits >> (2 * i)) & 3], 4);
}
// 0: plain 32 bit, 1..3: BC1..BC3; bgr: blue comes first. false: a format not read here
static bool armTexKind(DXGI_FORMAT f, int *bc, bool *bgr) {
    *bc = 0, *bgr = false;
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return true;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        *bgr = true;
        return true;
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        *bc = 1;
        return true;
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
        *bc = 2;
        return true;
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
        *bc = 3;
        return true;
    default:
        return false;
    }
}
// the mapped picture's largest level -> w x h RGBA
static void armTexDecode(const uint8_t *src, UINT pitch, UINT w, UINT h, int bc, bool bgr, std::vector<uint8_t> &px) {
    px.assign((size_t)w * h * 4, 255);
    if (!bc) {
        for (UINT y = 0; y < h; ++y) {
            const uint8_t *s = src + (size_t)y * pitch;
            uint8_t *d = px.data() + (size_t)y * w * 4;
            for (UINT x = 0; x < w; ++x, s += 4, d += 4)
                d[0] = bgr ? s[2] : s[0], d[1] = s[1], d[2] = bgr ? s[0] : s[2], d[3] = 255;
        }
        return;
    }
    const UINT blk = bc == 1 ? 8 : 16;
    for (UINT by = 0; by < (h + 3) / 4; ++by) {
        for (UINT bx = 0; bx < (w + 3) / 4; ++bx) {
            const uint8_t *b = src + (size_t)by * pitch + (size_t)bx * blk;
            uint8_t o[16][4];
            armBcColours(bc == 1 ? b : b + 8, bc == 1, o);
            for (UINT i = 0; i < 16; ++i) {
                const UINT x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x < w && y < h) memcpy(px.data() + ((size_t)y * w + x) * 4, o[i], 3);
            }
        }
    }
}
// w x h -> a square whose side is a power of two (the cockpit's shader takes such pictures)
static void armTexSquare(const std::vector<uint8_t> &px, UINT w, UINT h, CapTex &t) {
    UINT s = 1;
    while (s < max(w, h) && s < ARM_TEX_MAX) s <<= 1;
    t.size = s;
    t.px.resize((size_t)s * s * 4);
    for (UINT y = 0; y < s; ++y) {
        const UINT y0 = (UINT)((uint64_t)y * h / s), y1 = max(y0 + 1, (UINT)((uint64_t)(y + 1) * h / s));
        for (UINT x = 0; x < s; ++x) {
            const UINT x0 = (UINT)((uint64_t)x * w / s), x1 = max(x0 + 1, (UINT)((uint64_t)(x + 1) * w / s));
            unsigned sum[4] = {}, n = 0;
            for (UINT yy = y0; yy < y1 && yy < h; ++yy)
                for (UINT xx = x0; xx < x1 && xx < w; ++xx, ++n)
                    for (int k = 0; k < 4; ++k) sum[k] += px[((size_t)yy * w + xx) * 4 + k];
            for (int k = 0; k < 4; ++k) t.px[((size_t)y * s + x) * 4 + k] = (uint8_t)(n ? sum[k] / n : 128);
        }
    }
}
// the picture a part is drawn with; -1: none that can be read (the part is then grey)
static int armTex(ID3D11DeviceContext *c, ID3D11ShaderResourceView *srv) {
    ID3D11Resource *res = nullptr;
    srv->GetResource(&res);
    if (!res) return -1;
    auto it = g_capTexOf.find(res);
    if (it != g_capTexOf.end()) {
        res->Release();
        return it->second;
    }
    int idx = -1;
    ID3D11Texture2D *tex = nullptr, *st = nullptr;
    D3D11_TEXTURE2D_DESC td = {};
    int bc = 0;
    bool bgr = false;
    if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&tex)) && tex) {
        tex->GetDesc(&td);
        if (td.SampleDesc.Count == 1 && td.Width && td.Height && td.Width <= 8192 && td.Height <= 8192 &&
            armTexKind(td.Format, &bc, &bgr)) {
            D3D11_TEXTURE2D_DESC sd = td;
            sd.MipLevels = sd.ArraySize = 1;
            sd.Usage = D3D11_USAGE_STAGING, sd.BindFlags = 0, sd.MiscFlags = 0;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (SUCCEEDED(g_dev->CreateTexture2D(&sd, nullptr, &st)) && st) {
                c->CopySubresourceRegion(st, 0, 0, 0, 0, tex, 0, nullptr);
                D3D11_MAPPED_SUBRESOURCE m;
                if (SUCCEEDED(ORIG(PFN_Map, c, VT_Ctx_Map)(c, st, 0, D3D11_MAP_READ, 0, &m)) && m.pData) {
                    std::vector<uint8_t> px;
                    armTexDecode((const uint8_t *)m.pData, m.RowPitch, td.Width, td.Height, bc, bgr, px);
                    ORIG(PFN_Unmap, c, VT_Ctx_Unmap)(c, st, 0);
                    g_capTex.emplace_back();
                    armTexSquare(px, td.Width, td.Height, g_capTex.back());
                    idx = (int)g_capTex.size() - 1;
                }
                st->Release();
            }
        }
        if (idx < 0)
            logf("arms:   a picture of %u x %u, format %d, is not read - its part stays grey", td.Width, td.Height,
                 (int)td.Format);
        tex->Release();
    }
    g_capTexOf[res] = idx;
    res->Release();
    return idx;
}

// a 16-bit float
static float armHalf(uint16_t h) {
    const int e = (h >> 10) & 31, m = h & 1023;
    const float v = e == 0 ? ldexpf((float)m, -24) : e == 31 ? 65504.0f : ldexpf((float)(m + 1024), e - 25);
    return (h & 0x8000) ? -v : v;
}

static void armCapture(ID3D11DeviceContext *c, UINT count, UINT start, INT baseV, const uint8_t *cb) {
    float W[12], rel[12];
    memcpy(W, cb + MLR_TO_WORLD_AT, sizeof W);
    for (int i = 0; i < 3; ++i) { // a rigid part's matrix: three axes of length 1 (an instanced draw's is empty)
        const float l = len3(W[i * 4], W[i * 4 + 1], W[i * 4 + 2]);
        if (!(l > 0.9f && l < 1.1f)) return;
    }
    const float *b = g_armsBase;
    const float dx = W[3] - b[3], dy = W[7] - b[7], dz = W[11] - b[11];
    if (!(dx * dx + dy * dy + dz * dz < (float)(ARM_REACH * ARM_REACH))) return;

    ID3D11InputLayout *il = nullptr;
    ID3D11Buffer *vb = nullptr, *ib = nullptr;
    UINT stride = 0, vOff = 0, iOff = 0;
    DXGI_FORMAT iFmt = DXGI_FORMAT_UNKNOWN;
    c->IAGetInputLayout(&il);
    c->IAGetVertexBuffers(0, 1, &vb, &stride, &vOff);
    c->IAGetIndexBuffer(&ib, &iFmt, &iOff);
    ArmLayout L;
    {
        std::lock_guard<std::mutex> lk(g_armIlMx);
        auto it = g_armIl.find(il);
        if (it != g_armIl.end()) L = it->second;
    }
    const UINT isz = iFmt == DXGI_FORMAT_R16_UINT ? 2u : iFmt == DXGI_FORMAT_R32_UINT ? 4u : 0u;
    const std::vector<uint8_t> *vd = vb ? armBuf(c, vb) : nullptr, *id = ib ? armBuf(c, ib) : nullptr;
    std::string key((const char *)W, sizeof W);
    key.append((const char *)&vb, sizeof vb).append((const char *)&ib, sizeof ib);
    key.append((const char *)&start, sizeof start).append((const char *)&count, sizeof count);
    key.append((const char *)&baseV, sizeof baseV);
    const bool usable = L.pos >= 0 && isz && vd && id && stride >= 12 && count >= 3 && count <= 600000 &&
                        (size_t)iOff + ((size_t)start + count) * isz <= id->size();
    static int told;
    if (!usable && told < 12) {
        ++told;
        logf("arms:   a part near the seat is not read: layout %p (place at %d), stride %u, index format %d, "
             "%u indices from %u, buffers %s / %s",
             il, L.pos, stride, (int)iFmt, count, start, vd ? "read" : "NOT READ", id ? "read" : "NOT READ");
    }
    if (usable && g_capSeen.insert(key).second && g_capParts.size() < 256) {
        mcShapeInView(b, g_camA, W, rel);
        CapPart p;
        std::unordered_map<uint32_t, uint32_t> map;
        const float uo[2] = {*(const float *)(cb + MLR_UV_AT), *(const float *)(cb + MLR_UV_AT + 4)};
        bool ok = true;
        for (UINT k = 0; k < count && ok; ++k) {
            const uint8_t *ip = id->data() + iOff + ((size_t)start + k) * isz;
            const int64_t vi = (int64_t)(isz == 2 ? *(const uint16_t *)ip : *(const uint32_t *)ip) + baseV;
            const size_t at = (size_t)vOff + (size_t)(vi < 0 ? 0 : vi) * stride;
            if (vi < 0 || at + stride > vd->size()) {
                ok = false;
                break;
            }
            auto f = map.find((uint32_t)vi);
            if (f != map.end()) {
                p.ix.push_back(f->second);
                continue;
            }
            const uint8_t *s = vd->data() + at;
            float o[8] = {0, 0, 0, 0, 1, 0, 0, 0}, q[3], nm[3] = {0, 1, 0};
            memcpy(q, s + L.pos, 12);
            if (L.nrm >= 0 && (UINT)L.nrm + 4 <= stride) {
                if (L.nrmFmt == DXGI_FORMAT_R8G8B8A8_UNORM) {
                    for (int a = 0; a < 3; ++a) nm[a] = s[L.nrm + a] / 255.0f * 2.001957f - 1;
                } else if (L.nrmFmt == DXGI_FORMAT_R10G10B10A2_UNORM) { // (the scale is the game's shader's)
                    uint32_t pk;
                    memcpy(&pk, s + L.nrm, 4);
                    for (int a = 0; a < 3; ++a) nm[a] = ((pk >> (10 * a)) & 1023) / 1023.0f * 2.001957f - 1;
                } else if ((UINT)L.nrm + 12 <= stride) {
                    memcpy(nm, s + L.nrm, 12);
                }
            }
            for (int a = 0; a < 3; ++a) {
                o[a] = rel[a * 4] * q[0] + rel[a * 4 + 1] * q[1] + rel[a * 4 + 2] * q[2] + rel[a * 4 + 3];
                o[3 + a] = rel[a * 4] * nm[0] + rel[a * 4 + 1] * nm[1] + rel[a * 4 + 2] * nm[2];
            }
            const float nl = len3(o[3], o[4], o[5]);
            if (nl > 1e-4f)
                for (int a = 0; a < 3; ++a) o[3 + a] /= nl;
            else
                o[3] = 0, o[4] = 1, o[5] = 0;
            if (L.uv >= 0 && L.uvFmt == DXGI_FORMAT_R16G16_FLOAT && (UINT)L.uv + 4 <= stride) {
                uint16_t hf[2];
                memcpy(hf, s + L.uv, 4);
                o[6] = armHalf(hf[0]) + uo[0], o[7] = armHalf(hf[1]) + uo[1];
            } else if (L.uv >= 0 && L.uvFmt == DXGI_FORMAT_R32G32_FLOAT && (UINT)L.uv + 8 <= stride) {
                memcpy(o + 6, s + L.uv, 8);
                o[6] += uo[0], o[7] += uo[1];
            }
            const uint32_t ni = (uint32_t)(p.v.size() / 32);
            if (!ni)
                for (int a = 0; a < 3; ++a) p.lo[a] = p.hi[a] = o[a];
            for (int a = 0; a < 3; ++a) p.lo[a] = min(p.lo[a], o[a]), p.hi[a] = max(p.hi[a], o[a]);
            p.v.insert(p.v.end(), (const uint8_t *)o, (const uint8_t *)o + 32);
            map[(uint32_t)vi] = ni;
            p.ix.push_back(ni);
        }
        if (ok && !p.v.empty()) {
            ID3D11ShaderResourceView *srv = nullptr;
            c->PSGetShaderResources(MLR_TEX_SLOT, 1, &srv);
            if (srv) {
                p.tex = armTex(c, srv);
                srv->Release();
            }
            g_capParts.push_back(std::move(p));
            g_capFrame = g_frame;
        }
    }
    if (il) il->Release();
    if (vb) vb->Release();
    if (ib) ib->Release();
}

void armsOnDraw(ID3D11DeviceContext *c, UINT count, UINT start, INT baseV) {
    if (!g_armsWant || (void *)c != (void *)g_imm) return;
    if (!g_camInMain) return;
    if (g_camPassEye != 0 || !g_camCal) return;
    // Reading costs: every draw's constants come back from the graphics card. So it is tried in single frames only -
    // the 5th from outside, and twice more a second apart - not in every frame until parts are found (a camera far
    // from the mech finds none: the game then ran at 2 pictures a second).
    if (g_armsBaseFrame == ~0u || !g_armsMech[0]) return;
    if (g_armsOutFrames != 5 && g_armsOutFrames != 95 && g_armsOutFrames != 185) return;
    if (g_capFrame != ~0u && g_capFrame != g_frame) return; // one frame's picture, no more
    ID3D11Buffer *cb = nullptr;
    c->VSGetConstantBuffers(4, 1, &cb);
    if (!cb) return;
    D3D11_BUFFER_DESC bd;
    cb->GetDesc(&bd);
    try {
        std::vector<uint8_t> data;
        if (bd.ByteWidth == MLR_CB_BYTES && armRead(c, cb, data)) armCapture(c, count, start, baseV, data.data());
    } catch (const std::bad_alloc &) {
        g_armsWant = false;
        logf("arms: no memory to read the mech's parts");
    }
    cb->Release();
}

// ---- the arms' way with the steps ----
// From the seat the game tells nothing of its mech's limbs. What it does tell is the cockpit's sway (sway.cpp): to
// the left and right once for every two steps - the weight on one leg, then on the other. The arms go with that:
// one ahead while the other is behind, as far as the sway is of its own largest in the last seconds. Standing,
// the sway is small and the arms come to rest.
float armsSwing() {
    static unsigned at = ~0u;
    static float mean, sq, out;
    if (at == g_frame) return out;
    at = g_frame;
    const float sx = g_swayValid && g_swayOn ? g_swayT[0] : 0;
    // the sway about its own middle (it leans to one side on some mechs), against its usual size: both taken over
    // the last seconds
    mean += (sx - mean) * 0.01f;
    const float x = sx - mean;
    sq += (x * x - sq) * 0.01f;
    const float size = 1.5f * sqrtf(max(sq, 0.0f));                  // (a sine's peak is 1.41 of its root mean square)
    const float walk = min(1.0f, max(0.0f, (size - 0.01f) / 0.02f)); // (1 cm: standing; 3 cm and more: walking)
    // -1 .. 1, and eased at both ends: an arm slows down before it turns, it does not stop at a wall
    const float u = size > 1e-4f ? max(-1.0f, min(1.0f, x / size)) : 0;
    const float want = g_armsSwing * walk * sinf(1.5707963f * u);
    out += (want - out) * 0.25f; // (the measure has a little noise in it)
    return out;
}

// ---- the file ----
static std::wstring armsFile() { return g_dir + L"mw4vr_arms\\" + g_armsMech + L".mwa"; }

static bool armsWrite() {
    CreateDirectoryW((g_dir + L"mw4vr_arms").c_str(), nullptr);
    FILE *f = fileOpen(armsFile().c_str(), L"wb");
    if (!f) return false;
    const uint32_t head[2] = {(uint32_t)g_capParts.size(), (uint32_t)g_capTex.size()};
    bool ok = fwrite("MWA1", 1, 4, f) == 4 && fwrite(head, 4, 2, f) == 2;
    for (const CapTex &t : g_capTex)
        ok = ok && fwrite(&t.size, 4, 1, f) == 1 && fwrite(t.px.data(), 1, t.px.size(), f) == t.px.size();
    for (const CapPart &p : g_capParts) {
        const uint32_t h[3] = {(uint32_t)(p.v.size() / 32), (uint32_t)p.ix.size(), (uint32_t)p.tex};
        ok = ok && fwrite(h, 4, 3, f) == 3 && fwrite(p.lo, 4, 3, f) == 3 && fwrite(p.hi, 4, 3, f) == 3 &&
             fwrite(p.v.data(), 1, p.v.size(), f) == p.v.size() &&
             fwrite(p.ix.data(), 4, p.ix.size(), f) == p.ix.size();
    }
    fclose(f);
    return ok;
}

static void armsFree() {
    std::unordered_set<IUnknown *> seen; // (parts share their pictures)
    for (int i = 0; i < g_armN; ++i) {
        if (g_arm[i].vb) g_arm[i].vb->Release();
        if (g_arm[i].ib) g_arm[i].ib->Release();
        if (g_arm[i].clr && seen.insert(g_arm[i].clr).second) g_arm[i].clr->Release();
    }
    g_armN = 0;
    memset(g_arm, 0, sizeof g_arm);
}

static ID3D11ShaderResourceView *armsTexture(const uint8_t *top, UINT size) {
    std::vector<uint8_t> px(top, top + (size_t)size * size * 4);
    std::vector<size_t> at;
    for (UINT s = size; s; s >>= 1) {
        at.push_back(px.size() - (size_t)s * s * 4);
        if (s == 1) break;
        const size_t src = at.back();
        const UINT h = s / 2;
        px.resize(px.size() + (size_t)h * h * 4);
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < h; ++x)
                for (int k = 0; k < 4; ++k) {
                    const uint8_t *p = px.data() + src + ((size_t)(2 * y) * s + 2 * x) * 4 + k;
                    px[src + (size_t)s * s * 4 + ((size_t)y * h + x) * 4 + k] =
                        (uint8_t)((p[0] + p[4] + p[(size_t)s * 4] + p[(size_t)s * 4 + 4] + 2) / 4);
                }
    }
    std::vector<D3D11_SUBRESOURCE_DATA> sub;
    UINT s = size;
    for (size_t a : at) sub.push_back({px.data() + a, s * 4, 0}), s >>= 1;
    D3D11_TEXTURE2D_DESC td = {size,
                               size,
                               (UINT)sub.size(),
                               1,
                               DXGI_FORMAT_R8G8B8A8_UNORM,
                               {1, 0},
                               D3D11_USAGE_IMMUTABLE,
                               D3D11_BIND_SHADER_RESOURCE,
                               0,
                               0};
    ID3D11Texture2D *t = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&td, sub.data(), &t)) || !t) return nullptr;
    ID3D11ShaderResourceView *v = nullptr;
    g_dev->CreateShaderResourceView(t, nullptr, &v);
    t->Release();
    return v;
}

// How far under the eye the mech's feet are (m, negative): the lowest middle of its parts. Leaves the file where
// it was.
static float armsFeet(FILE *f, uint32_t n) {
    const long at = ftell(f);
    float feet = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t h[3] = {};
        float lo[3], hi[3];
        if (fread(h, 4, 3, f) != 3 || fread(lo, 4, 3, f) != 3 || fread(hi, 4, 3, f) != 3 || h[0] > 600000 ||
            h[1] > 600000)
            break;
        if (h[0] >= 8) feet = min(feet, 0.5f * (lo[1] + hi[1]));
        if (fseek(f, (long)(h[0] * 32 + h[1] * 4), SEEK_CUR)) break;
    }
    fseek(f, at, SEEK_SET);
    return feet;
}

// false: no file for this mech (or one that cannot be read)
static bool armsLoad() {
    armsFree();
    FILE *f = fileOpen(armsFile().c_str(), L"rb");
    if (!f) return false;
    bool ok = false;
    try {
        char magic[4];
        uint32_t head[2] = {};
        ok = fread(magic, 1, 4, f) == 4 && !memcmp(magic, "MWA1", 4) && fread(head, 4, 2, f) == 2 && head[0] <= 256 &&
             head[1] <= 256;
        std::vector<ID3D11ShaderResourceView *> tex;
        const uint8_t grey[4] = {128, 128, 128, 255};
        ID3D11ShaderResourceView *plain = ok ? armsTexture(grey, 1) : nullptr;
        for (uint32_t i = 0; i < head[1] && ok; ++i) {
            uint32_t size = 0;
            ok = fread(&size, 4, 1, f) == 1 && size >= 1 && size <= ARM_TEX_MAX && !(size & (size - 1));
            if (!ok) break;
            std::vector<uint8_t> px((size_t)size * size * 4);
            ok = fread(px.data(), 1, px.size(), f) == px.size();
            tex.push_back(ok ? armsTexture(px.data(), size) : nullptr);
        }
        bool plainUsed = false;
        const float feet = ok ? armsFeet(f, head[0]) : 0;
        for (uint32_t i = 0; i < head[0] && ok; ++i) {
            uint32_t h[3] = {};
            float lo[3], hi[3];
            ok = fread(h, 4, 3, f) == 3 && fread(lo, 4, 3, f) == 3 && fread(hi, 4, 3, f) == 3 && h[0] >= 3 &&
                 h[0] <= 600000 && h[1] >= 3 && h[1] <= 600000;
            if (!ok) break;
            std::vector<uint8_t> v((size_t)h[0] * 32);
            std::vector<uint32_t> ix(h[1]);
            ok = fread(v.data(), 1, v.size(), f) == v.size() && fread(ix.data(), 4, ix.size(), f) == ix.size();
            if (!ok) break;
            for (uint32_t x : ix) ok = ok && x < h[0];
            if (!ok) break;
            const float cx = 0.5f * (lo[0] + hi[0]), cy = 0.5f * (lo[1] + hi[1]), cz = 0.5f * (lo[2] + hi[2]);
            const float m = 0.3f; // a part that holds the eye is the hull the pilot sits in
            const bool holds =
                lo[0] - m < 0 && hi[0] + m > 0 && lo[1] - m < 0 && hi[1] + m > 0 && lo[2] - m < 0 && hi[2] + m > 0;
            float near2 = 0; // the part's nearest place to the eye
            for (int k = 0; k < 3; ++k) {
                const float dk = lo[k] > 0 ? lo[k] : hi[k] < 0 ? -hi[k] : 0;
                near2 += dk * dk;
            }
            // under the usual height the legs begin: there a part is an arm only far out, or long and slim
            const float len = hi[2] - lo[2];
            const bool low = cy < g_armsLow && cy >= min(g_armsLow, g_armsDeep * feet) &&
                             (fabsf(cx) >= g_armsWide || (len >= 2.5f && len >= 2 * (hi[1] - lo[1])));
            const bool beside = fabsf(cx) >= g_armsSide && (cy >= g_armsLow || low), above = cy >= g_armsAbove;
            const bool take = !holds && g_armN < ARMS_MAX && h[0] >= 8 &&
                              (g_armsAll || ((beside || above) && near2 >= g_armsNear * g_armsNear));
            logf("arms:   part %2u - %6u vertices, %6u triangles, middle %6.2f right %6.2f up %6.2f ahead, %.1f x "
                 "%.1f x %.1f m%s%s",
                 i, h[0], h[1] / 3, cx, cy, cz, hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2],
                 holds ? " - holds the eye" : "", take ? " - DRAWN" : "");
            if (!take) continue;
            ArmPart &a = g_arm[g_armN];
            a = {};
            D3D11_BUFFER_DESC vd = {(UINT)v.size(), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
            D3D11_BUFFER_DESC id = {(UINT)(ix.size() * 4), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0};
            D3D11_SUBRESOURCE_DATA vs = {v.data(), 0, 0}, is = {ix.data(), 0, 0};
            const HRESULT h1 = ORIG(PFN_CreateBuffer, g_dev, VT_Dev_CreateBuffer)(g_dev, &vd, &vs, &a.vb);
            const HRESULT h2 = ORIG(PFN_CreateBuffer, g_dev, VT_Dev_CreateBuffer)(g_dev, &id, &is, &a.ib);
            a.nIdx = h[1];
            a.swing = beside && hi[2] - lo[2] >= g_armsLong ? (cx > 0 ? 1 : -1) : 0;
            a.clr = h[2] < tex.size() && tex[h[2]] ? tex[h[2]] : plain;
            if (a.clr) a.clr->AddRef();
            plainUsed = plainUsed || a.clr == plain;
            if (FAILED(h1) || FAILED(h2) || !a.vb || !a.ib || !a.clr) {
                if (a.vb) a.vb->Release();
                if (a.ib) a.ib->Release();
                if (a.clr) a.clr->Release();
                a = {};
                continue;
            }
            ++g_armN;
        }
        // (every part holds a reference of its own to its picture; armsFree gives each picture back once - so the
        // references beyond the first are given back here)
        std::unordered_map<ID3D11ShaderResourceView *, int> uses;
        for (int i = 0; i < g_armN; ++i) ++uses[g_arm[i].clr];
        for (auto &u : uses)
            for (int k = 1; k < u.second; ++k) u.first->Release();
        for (ID3D11ShaderResourceView *t : tex)
            if (t) t->Release();
        if (plain) plain->Release();
        (void)plainUsed;
    } catch (const std::bad_alloc &) {
        ok = false;
    }
    fclose(f);
    if (!ok) {
        armsFree();
        logf("arms: %ls cannot be read", armsFile().c_str());
        return false;
    }
    logf("arms: '%ls' - %d parts of the mech are drawn beside the cockpit (%ls)", g_armsMech, g_armN,
         armsFile().c_str());
    return true;
}

static void armsCapClear() {
    g_capParts.clear(), g_capTex.clear(), g_capBuf.clear(), g_capTexOf.clear(), g_capSeen.clear();
    g_capFrame = ~0u;
}

void armsOnPresent() {
    static bool read;
    if (!read) {
        read = true;
        const std::wstring ini = g_dir + L"mw4vr.ini";
        wchar_t b[32];
        g_armsOn = GetPrivateProfileIntW(L"vr", L"arms", 0, ini.c_str()) != 0;
        g_armsAll = GetPrivateProfileIntW(L"vr", L"arms_all", 0, ini.c_str()) != 0;
        if (GetPrivateProfileStringW(L"vr", L"arms_side", L"", b, 32, ini.c_str())) g_armsSide = (float)_wtof(b);
        if (GetPrivateProfileStringW(L"vr", L"arms_low", L"", b, 32, ini.c_str())) g_armsLow = (float)_wtof(b);
        if (GetPrivateProfileStringW(L"vr", L"arms_deep", L"", b, 32, ini.c_str())) g_armsDeep = (float)_wtof(b);
        if (GetPrivateProfileStringW(L"vr", L"arms_wide", L"", b, 32, ini.c_str())) g_armsWide = (float)_wtof(b);
        if (GetPrivateProfileStringW(L"vr", L"arms_swing", L"", b, 32, ini.c_str())) g_armsSwing = (float)_wtof(b);
        if (GetPrivateProfileStringW(L"vr", L"arms_above", L"", b, 32, ini.c_str())) g_armsAbove = (float)_wtof(b);
        if (GetPrivateProfileStringW(L"vr", L"arms_near", L"", b, 32, ini.c_str())) g_armsNear = (float)_wtof(b);
        if (g_armsOn)
            logf("arms: the mech's own parts beside the cockpit are on (a part's middle at least %.1f m to the side, "
                 "not lower than %.1f m%s)",
                 g_armsSide, g_armsLow, g_armsAll ? "; ALL parts" : "");
    }
    if (!g_armsOn) return;
    g_armsOutFrames = g_mechSeated && g_mechSeat.outside ? g_armsOutFrames + 1 : 0;

    if (g_capFrame != ~0u) { // the picture the parts were read in is complete
        const size_t n = g_capParts.size();
        if (n < 3 && ++g_capTries < 20) {
            logf("arms: only %u parts near the seat in frame %u - read again", (unsigned)n, g_capFrame);
        } else {
            g_armsWant = false;
            const bool ok = n >= 3 && armsWrite();
            logf(ok ? "arms: %u parts and %u pictures of '%ls' read from the picture seen from outside, kept in %ls"
                    : "arms: %u parts, %u pictures of '%ls' - NOT kept (%ls)",
                 (unsigned)n, (unsigned)g_capTex.size(), g_armsMech, armsFile().c_str());
            if (ok) armsLoad();
        }
        armsCapClear();
    }

    if (g_frame % 30) return;
    char mech[32];
    wchar_t w[32];
    if (!gmPlayerMech(mech, 32)) return;
    swprintf(w, 32, L"%hs", mech);
    if (!wcscmp(w, g_armsMech) || !mcNameOk(w)) return;
    wcscpy_s(g_armsMech, w);
    armsCapClear();
    g_capTries = 0;
    g_armsWant = !armsLoad();
    if (g_armsWant)
        logf("arms: no parts of '%ls' yet - they are read at the next look at the mech from outside (stand still, "
             "torso ahead)",
             g_armsMech);
}
