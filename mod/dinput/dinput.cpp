// MW4VR: dinput.dll for the game folder, in front of the system's.
//
// The game asks the old DirectInput (version 7) for every input device there is. For the class "other HID devices"
// (light controllers, receivers, head sets' buttons - nothing a game steers with) the system's dinput.dll damages
// the heap on some PCs while it describes them: the game then dies at start-up or a little later, four times out
// of five. A test program of fifty lines that only lists the devices does the same, so it is not the game's fault.
// Mice, keyboards and joysticks listed on their own are safe.
//
// So: "every device" becomes mice, then keyboards, then joysticks, and the class of the other devices is empty.
// Everything else goes to the system's dinput.dll untouched. What was asked is written to mw4vr_dinput.log.
#define DIRECTINPUT_VERSION 0x0700
#define INITGUID
#include <windows.h>
#include <dinput.h>
#include <stdarg.h>
#include <stdio.h>

static HMODULE g_real;
static char g_log[MAX_PATH];
static int g_lines;

static void say(const char *fmt, ...) {
    if (!g_log[0] || g_lines >= 200) return; // a game that asks every frame must not fill the disk
    FILE *f = fopen(g_log, g_lines++ ? "a" : "w");
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "%02d:%02d:%02d.%03d  ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fputc('\n', f);
    fclose(f);
}

static FARPROC real(const char *name) {
    if (!g_real) {
        char p[MAX_PATH];
        UINT n = GetSystemDirectoryA(p, sizeof p - 16); // of a 32-bit process: SysWOW64
        if (!n || n >= sizeof p - 16) return 0;
        lstrcatA(p, "\\dinput.dll");
        g_real = LoadLibraryA(p);
        say(g_real ? "the system's DirectInput: %s" : "the system's DirectInput not loaded: %s", p);
    }
    return g_real ? GetProcAddress(g_real, name) : 0;
}

// the game's callback, and whether it has said "enough"
struct Each {
    LPDIENUMDEVICESCALLBACKA cb;
    void *ref;
    bool stopped;
    int n;
};

static BOOL CALLBACK each(const DIDEVICEINSTANCEA *d, void *p) {
    Each *e = (Each *)p;
    if (e->n++ < 24) say("  device: type %08lx '%s'", d->dwDevType, d->tszInstanceName);
    if (e->cb(d, e->ref) == DIENUM_CONTINUE) return DIENUM_CONTINUE;
    e->stopped = true;
    return DIENUM_STOP;
}

// The keyboard the game made, kept (with a reference of ours, so it stays valid) for MW4VR_FlushKeys.
static IDirectInputDeviceA *g_keys;
static void keep(REFGUID g, void *dev) {
    if (!dev || g != GUID_SysKeyboard) return;
    IDirectInputDeviceA *d = (IDirectInputDeviceA *)dev; // every version's table begins with this one's
    d->AddRef();
    if (g_keys) g_keys->Release();
    g_keys = d;
    say("the game's keyboard device is known");
}

// d3d11.dll (MW4VR) calls this when its keys' page closes: what was typed while the game waited is thrown
// away, so the game does not act on it afterwards. -> how many key events, -1 none thrown away
extern "C" int WINAPI MW4VR_FlushKeys(void) {
    if (!g_keys) return -1;
    DWORD n = INFINITE;
    HRESULT hr = g_keys->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), 0, &n, 0);
    return SUCCEEDED(hr) ? (int)n : -1; // (a keyboard read without a buffer: nothing to throw away)
}

struct Input : IDirectInput7A {
    IDirectInput7A *r;
    LONG refs;
    explicit Input(IDirectInput7A *real) : r(real), refs(1) {}

    STDMETHOD(QueryInterface)(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        // the three versions' tables begin alike, so this object answers for all of them
        if (iid == IID_IUnknown || iid == IID_IDirectInputA || iid == IID_IDirectInput2A || iid == IID_IDirectInput7A) {
            *out = this;
            AddRef();
            return S_OK;
        }
        return r->QueryInterface(iid, out);
    }
    STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&refs); }
    STDMETHOD_(ULONG, Release)() override {
        LONG n = InterlockedDecrement(&refs);
        if (!n) {
            r->Release();
            delete this;
        }
        return n;
    }
    STDMETHOD(CreateDevice)(REFGUID g, IDirectInputDeviceA **dev, IUnknown *outer) override {
        HRESULT hr = r->CreateDevice(g, dev, outer);
        if (SUCCEEDED(hr) && dev && !outer) keep(g, *dev);
        return hr;
    }
    STDMETHOD(EnumDevices)(DWORD type, LPDIENUMDEVICESCALLBACKA cb, void *ref, DWORD flags) override {
        say("the game lists devices: type %lu, flags %08lx", type, flags);
        if (!cb) return DIERR_INVALIDPARAM;
        Each e = {cb, ref, false, 0};
        if (type == DIDEVTYPE_DEVICE) return DI_OK; // the class that damages the heap: none
        if (type != 0) return r->EnumDevices(type, each, &e, flags);
        static const DWORD safe[3] = {DIDEVTYPE_MOUSE, DIDEVTYPE_KEYBOARD, DIDEVTYPE_JOYSTICK}; // the order DirectInput lists in
        for (int i = 0; i < 3 && !e.stopped; ++i) {
            HRESULT hr = r->EnumDevices(safe[i], each, &e, flags);
            if (FAILED(hr)) return hr;
        }
        return DI_OK;
    }
    STDMETHOD(GetDeviceStatus)(REFGUID g) override { return r->GetDeviceStatus(g); }
    STDMETHOD(RunControlPanel)(HWND w, DWORD flags) override { return r->RunControlPanel(w, flags); }
    STDMETHOD(Initialize)(HINSTANCE inst, DWORD version) override { return r->Initialize(inst, version); }
    STDMETHOD(FindDevice)(REFGUID cls, LPCSTR name, GUID *instance) override { return r->FindDevice(cls, name, instance); }
    STDMETHOD(CreateDeviceEx)(REFGUID g, REFIID iid, void **dev, IUnknown *outer) override {
        HRESULT hr = r->CreateDeviceEx(g, iid, dev, outer);
        if (SUCCEEDED(hr) && dev && !outer && (iid == IID_IDirectInputDeviceA || iid == IID_IDirectInputDevice2A || iid == IID_IDirectInputDevice7A))
            keep(g, *dev);
        return hr;
    }
};

extern "C" HRESULT WINAPI MW4VR_DirectInputCreateEx(HINSTANCE inst, DWORD version, REFIID iid, void **out, IUnknown *outer) {
    typedef HRESULT(WINAPI * Fn)(HINSTANCE, DWORD, REFIID, void **, IUnknown *);
    Fn fn = (Fn)real("DirectInputCreateEx");
    if (!fn) return DIERR_GENERIC;
    bool ours = !outer && out && (iid == IID_IDirectInputA || iid == IID_IDirectInput2A || iid == IID_IDirectInput7A);
    if (!ours) {
        say("DirectInputCreateEx version %lx with another interface: passed on as it is", version);
        return fn(inst, version, iid, out, outer);
    }
    IDirectInput7A *r = 0;
    HRESULT hr = fn(inst, version, IID_IDirectInput7A, (void **)&r, 0);
    say("DirectInputCreateEx version %lx: %08lx", version, hr);
    if (FAILED(hr)) return hr;
    *out = new Input(r);
    return hr;
}

// the older ways in: passed on (the game does not use them)
extern "C" HRESULT WINAPI MW4VR_DirectInputCreateA(HINSTANCE inst, DWORD version, IDirectInputA **out, IUnknown *outer) {
    typedef HRESULT(WINAPI * Fn)(HINSTANCE, DWORD, IDirectInputA **, IUnknown *);
    Fn fn = (Fn)real("DirectInputCreateA");
    say("DirectInputCreateA version %lx: passed on as it is", version);
    return fn ? fn(inst, version, out, outer) : DIERR_GENERIC;
}

extern "C" HRESULT WINAPI MW4VR_DirectInputCreateW(HINSTANCE inst, DWORD version, IDirectInputW **out, IUnknown *outer) {
    typedef HRESULT(WINAPI * Fn)(HINSTANCE, DWORD, IDirectInputW **, IUnknown *);
    Fn fn = (Fn)real("DirectInputCreateW");
    say("DirectInputCreateW version %lx: passed on as it is", version);
    return fn ? fn(inst, version, out, outer) : DIERR_GENERIC;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD why, void *) {
    if (why == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        DWORD n = GetModuleFileNameA(self, g_log, sizeof g_log);
        char *slash = n && n < sizeof g_log ? strrchr(g_log, '\\') : 0;
        if (slash && slash - g_log + 20 < (int)sizeof g_log) lstrcpyA(slash + 1, "mw4vr_dinput.log");
        else g_log[0] = 0;
    }
    return TRUE;
}
