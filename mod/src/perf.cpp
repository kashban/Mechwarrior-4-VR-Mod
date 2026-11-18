// ---------------------------------------------------------------------------------------------
// Where a frame's time goes, for the performance tests (tools/e2e/run.ps1 -Perf): a test plan with perf=1 has a
// line written for every frame to mw4vr_perf.csv - the frame's length and the time of its parts, in microseconds
// (QueryPerformanceCounter), and the number of draw calls. Without such a plan nothing is measured.

#include "mod.h"
#include <tlhelp32.h>

bool g_perfOn;
LONGLONG g_perfAcc[PF_N];
static FILE *g_perfCsv;
static LONGLONG g_perfLast;
static double g_perfUs = 1.0; // microseconds a tick
static bool g_perfSampling;   // the sampler is asked for

void perfStart(bool sampling) {
    if (g_perfOn) return;
    g_perfSampling = sampling;
    g_perfCsv = _wfsopen((g_dir + L"mw4vr_perf.csv").c_str(), L"w", _SH_DENYNO);
    if (!g_perfCsv) {
        logf("perf: mw4vr_perf.csv cannot be written - no record");
        return;
    }
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_perfUs = 1e6 / (double)f.QuadPart;
    fprintf(g_perfCsv, "frame,t,us,eye0,eye1,ours,wait,sys,grab,hook,d3d,map,exec,draws,seated,outside,menu\n");
    g_perfLast = perfNow();
    g_perfOn = true;
    logf("perf: a line for every frame goes to mw4vr_perf.csv");
}

// ---- the sampler: where the render thread's instruction pointer is, about every millisecond ----
// Only when the test plan asks for it (perf=2): the thread is stopped for the moment of each reading, which
// costs time itself. Every five seconds the counts so far are written to
// mw4vr_perf_samples.txt: per module, and for each module the 256-byte stretches of code met most often
// (offsets from the module's base - to be looked up in a disassembly).
static HANDLE g_perfThread; // the render thread (the one that presents)
static std::mutex g_perfSampleMx;
static std::vector<uint32_t> g_perfSamples[3]; // by where the frame is: the left eye's pass, the right's, elsewhere
static float g_perfT = -1;

static void perfDump() {
    struct Mod {
        uintptr_t base, size;
        std::string name;
    };
    std::vector<Mod> mods;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me = {sizeof me};
        for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me))
            // (two modules are called d3d11.dll: this mod, and the system's)
            mods.push_back({(uintptr_t)me.modBaseAddr, me.modBaseSize,
                            (HMODULE)me.modBaseAddr == g_self ? "MW4VR (this mod)" : narrow(me.szModule)});
        CloseHandle(snap);
    }
    std::vector<uint32_t> s[3];
    {
        std::lock_guard<std::mutex> lk(g_perfSampleMx);
        for (int k = 0; k < 3; ++k) s[k] = g_perfSamples[k];
    }
    FILE *f = _wfsopen((g_dir + L"mw4vr_perf_samples.txt").c_str(), L"w", _SH_DENYNO);
    if (!f) return;
    static const char *where[3] = {"left eye's pass", "right eye's pass", "elsewhere in the frame"};
    for (int k = 0; k < 3; ++k) {
        std::unordered_map<std::string, unsigned> byMod;
        std::unordered_map<std::string, std::unordered_map<uint32_t, unsigned>> byPlace;
        for (uint32_t ip : s[k]) {
            std::string name = "(no module)";
            uint32_t off = ip;
            for (const Mod &m : mods)
                if (ip >= m.base && ip < m.base + m.size) {
                    name = m.name, off = (uint32_t)(ip - m.base);
                    break;
                }
            ++byMod[name];
            ++byPlace[name][off >> 8];
        }
        fprintf(f, "== %s: %u samples\n", where[k], (unsigned)s[k].size());
        std::vector<std::pair<unsigned, std::string>> order;
        order.reserve(byMod.size());
        for (auto &m : byMod) order.push_back({m.second, m.first});
        std::sort(order.rbegin(), order.rend());
        for (size_t i = 0; i < order.size() && i < 12; ++i) {
            fprintf(f, "%6.1f %%  %s\n", 100.0 * order[i].first / (double)max((size_t)1, s[k].size()),
                    order[i].second.c_str());
            if (i >= 5) continue;
            std::vector<std::pair<unsigned, uint32_t>> places;
            places.reserve(byPlace[order[i].second].size());
            for (auto &pl : byPlace[order[i].second]) places.push_back({pl.second, pl.first});
            std::sort(places.rbegin(), places.rend());
            for (size_t j = 0; j < places.size() && j < 24; ++j)
                fprintf(f, "            %5.1f %%  +0x%06x\n",
                        100.0 * places[j].first / (double)max((size_t)1, s[k].size()), places[j].second << 8);
        }
    }
    fclose(f);
}

static DWORD WINAPI perfSampler(void *) {
    ULONGLONG lastDump = GetTickCount64();
    for (;;) {
        Sleep(1);
        HANDLE th = g_perfThread;
        // in the mech, in a battle that runs: the test plan's clock is past its first two seconds
        if (th && g_perfT > 2 && g_mechSeated && !g_mechSeat.outside && !g_menuMode) {
            CONTEXT c = {};
            c.ContextFlags = CONTEXT_CONTROL;
            int k = g_camPassEye == 0 ? 0 : g_camPassEye == 1 ? 1 : 2;
            if (SuspendThread(th) != (DWORD)-1) {
                BOOL ok = GetThreadContext(th, &c);
                ResumeThread(th);
                if (ok) {
                    std::lock_guard<std::mutex> lk(g_perfSampleMx);
                    g_perfSamples[k].push_back((uint32_t)c.Eip);
                }
            }
        }
        if (GetTickCount64() - lastDump > 5000) {
            lastDump = GetTickCount64();
            perfDump();
        }
    }
}

void perfFrame(float t) {
    if (!g_perfOn) return;
    g_perfT = t;
    if (g_perfSampling && !g_perfThread) {
        HANDLE th = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &th,
                            THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0)) {
            g_perfThread = th;
            HANDLE s = CreateThread(nullptr, 0, perfSampler, nullptr, 0, nullptr);
            if (s) CloseHandle(s);
        }
    }
    static LONG lastDraws;
    LONGLONG now = perfNow();
    LONG draws = g_statDrawsImm + g_statDrawsDef, d = draws - lastDraws;
    lastDraws = draws;
    if (d < 0) d = draws; // (the log's statistics were set to zero in between)
    fprintf(g_perfCsv, "%u,%.3f,%.0f", g_frame, t, (double)(now - g_perfLast) * g_perfUs);
    for (LONGLONG &a : g_perfAcc) {
        fprintf(g_perfCsv, ",%.0f", (double)a * g_perfUs);
        a = 0;
    }
    fprintf(g_perfCsv, ",%ld,%d,%d,%d\n", d, g_mechSeated ? 1 : 0, g_mechSeat.outside ? 1 : 0, g_menuMode ? 1 : 0);
    g_perfLast = now;
    if ((g_frame & 63) == 0) fflush(g_perfCsv);
}
