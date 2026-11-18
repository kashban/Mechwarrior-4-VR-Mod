// What perf.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_PERF_H
#define MW4VR_PERF_H

// the parts of a frame whose time is taken
enum {
    PF_EYE0, // the game and mw4hook drawing the left eye's pass of the main camera (our hooks in it included)
    PF_EYE1, // the right eye's
    PF_OURS, // our work at Present (xrWaitFrame included)
    PF_WAIT, // xrWaitFrame
    PF_SYS,  // the system's Present
    PF_GRAB, // an eye's picture copied out of the frame
    PF_HOOK, // our hooks of the draw and dispatch calls, Direct3D's own call in them included
    PF_D3D,  // Direct3D's own draw and dispatch calls
    PF_MAP,  // Direct3D's Map
    PF_EXEC, // ExecuteCommandList
    PF_N
};
extern bool g_perfOn;
extern LONGLONG g_perfAcc[PF_N]; // ticks of QueryPerformanceCounter, since the last line

static inline LONGLONG perfNow() {
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}
// the time between its making and its end goes to one part (nothing is done while no record is written)
struct PerfScope {
    int part;
    LONGLONG t0;
    explicit PerfScope(int p) : part(p), t0(g_perfOn ? perfNow() : 0) {}
    ~PerfScope() {
        if (g_perfOn) g_perfAcc[part] += perfNow() - t0;
    }
    PerfScope(const PerfScope &) = delete;
    PerfScope &operator=(const PerfScope &) = delete;
};
// a test run asks for the record (mw4vr_perf.csv beside the DLL); sampling: also where the render thread's
// instruction pointer is, about every millisecond (mw4vr_perf_samples.txt)
void perfStart(bool sampling);
// after every Present: a line for the frame that ended with it; t: the test plan's clock
void perfFrame(float t);

#endif // MW4VR_PERF_H
