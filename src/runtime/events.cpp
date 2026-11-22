// Guest time base, decrementer and timed device events.
//
// Device state is only touched on the guest thread. A host timer thread sleeps (on a
// high-resolution waitable timer) until the earliest scheduled event and then raises
// g_irqPending; the guest polls it at loop
// back-edges (safepoint), runs due events there and takes interrupts if enabled.
#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#else
#include <sys/prctl.h>
#include <time.h>
#include <condition_variable>
#endif

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include "runtime.h"
#include "settings.h"

namespace rt {

void (*g_eventHandlers[EV_COUNT])() = {};
u64 g_timerFlags = 0;  // times the timer thread raised g_irqPending (R2_GX_LOG)
volatile u32 g_irqLine = 0;

namespace {
#ifdef _WIN32
LARGE_INTEGER s_qpcFreq, s_qpcStart;
#else
timespec s_start{};
#endif
u64 s_due[EV_COUNT];  // 0 = not scheduled
std::mutex s_mu;
std::atomic<u64> s_earliest{~0ull};
// Idle skip (idleWait): time the guest clock was moved ahead of the host's
std::atomic<u64> s_warp{0};

u64 s_decBaseTb = 0;
u32 s_decValue = 0;
bool s_decPending = false;

void recomputeEarliest() {
    u64 e = ~0ull;
    for (u64 d : s_due)
        if (d && d < e) e = d;
    s_earliest = e;
}

// The timer thread sleeps on a high-resolution waitable timer. Ordinary sleeps (condition
// variables, Sleep) follow the system tick, and Windows 11 ignores timeBeginPeriod(1) for
// processes without a visible window (headless runs, windows behind others): retraces then
// fired a 15.6 ms tick late while the game idled waiting for them (the intro ran at ~1/3 speed).
#ifdef _WIN32
HANDLE s_timer = nullptr;   // high-resolution waitable timer
HANDLE s_wakeEv = nullptr;  // schedule() changed the earliest event
HANDLE s_guestEv = nullptr; // g_irqPending was raised (wakes an idle guest)

void wakeTimer() {
    if (s_wakeEv) SetEvent(s_wakeEv);
}
void signalGuest() { SetEvent(s_guestEv); }
void waitGuest(u32 ms) { WaitForSingleObject(s_guestEv, ms); }
void cpuRelax() { YieldProcessor(); }

void sleepUs(u64 us, bool wakeable) {
    LARGE_INTEGER due;
    due.QuadPart = -LONGLONG(us * 10);  // relative, 100 ns units
    SetWaitableTimer(s_timer, &due, 0, nullptr, nullptr, FALSE);
    HANDLE hs[2] = {s_timer, s_wakeEv};
    WaitForMultipleObjects(wakeable ? 2 : 1, hs, FALSE, INFINITE);
}
#else
// Linux: auto-reset events from a mutex and a condition variable; the timer thread asks for a
// 1 us timer slack (the default 50 us would make every retrace late by that much).
struct Event {
    std::mutex m;
    std::condition_variable cv;
    bool set = false;
    void signal() {
        {
            std::lock_guard lk(m);
            set = true;
        }
        cv.notify_one();
    }
    void wait(u64 us) {
        std::unique_lock lk(m);
        cv.wait_for(lk, std::chrono::microseconds(us), [this] { return set; });
        set = false;
    }
};
Event s_wakeEv, s_guestEv;
void wakeTimer() { s_wakeEv.signal(); }
void signalGuest() { s_guestEv.signal(); }
void waitGuest(u32 ms) { s_guestEv.wait(u64(ms) * 1000); }
#if defined(__aarch64__)
void cpuRelax() { asm volatile("yield"); }
#else
void cpuRelax() { __builtin_ia32_pause(); }
#endif

void sleepUs(u64 us, bool wakeable) {
    if (wakeable) {
        s_wakeEv.wait(us);
        return;
    }
    timespec ts{time_t(us / 1000000), long(us % 1000000) * 1000};
    while (nanosleep(&ts, &ts) != 0) {
    }
}
#endif

void raisePending() {
    g_irqPending = 1;
    signalGuest();
}

void timerThread() {
    for (;;) {
        const u64 e = s_earliest.load();
        const u64 now = tbNow();
        if (e <= now) {
            raisePending();
            g_timerFlags++;
            // the guest runs the event at its next safepoint (and reschedules); check again soon,
            // or as soon as it schedules something earlier. (Not wakeable, this sleep - about
            // half a millisecond on Windows - came after every event: Rebel Strike streams its
            // texture pool in thousands of 4 KiB disc reads, each waiting for the last one's
            // completion event, and the picture held still for half a second at a time.)
            sleepUs(200, true);
            continue;
        }
        if (e == ~0ull) {
            sleepUs(50000, true);
            continue;
        }
        const u64 waitUs = (e - now) * 1000000 / kTbClock;
        if (waitUs > 300) sleepUs(waitUs - 150, true);  // wake a little early, then spin
        else
            while (tbNow() < e && s_earliest.load() == e) cpuRelax();
    }
}
}  // namespace

#ifdef _WIN32
u64 tbNow() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    const u64 d = u64(t.QuadPart - s_qpcStart.QuadPart);
    const u64 f = u64(s_qpcFreq.QuadPart);
    return (d / f) * kTbClock + (d % f) * kTbClock / f + s_warp.load(std::memory_order_relaxed);
}

void eventsInit() {
    QueryPerformanceFrequency(&s_qpcFreq);
    QueryPerformanceCounter(&s_qpcStart);
    timeBeginPeriod(1);
    s_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!s_timer) s_timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);  // before Windows 10 1803
    s_wakeEv = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    s_guestEv = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::thread(timerThread).detach();
}
#else
u64 tbNow() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    const u64 ns = u64(t.tv_sec - s_start.tv_sec) * 1000000000ull + u64(t.tv_nsec) - u64(s_start.tv_nsec);
    return (ns / 1000000000ull) * kTbClock + (ns % 1000000000ull) * kTbClock / 1000000000ull +
           s_warp.load(std::memory_order_relaxed);
}

void eventsInit() {
    clock_gettime(CLOCK_MONOTONIC, &s_start);
    std::thread([] {
        prctl(PR_SET_TIMERSLACK, 1UL, 0, 0, 0);
        timerThread();
    }).detach();
}
#endif

void schedule(int id, u64 at) {
    if (!at) at = 1;
    std::lock_guard lk(s_mu);
    s_due[id] = at;
    const u64 before = s_earliest.load();
    recomputeEarliest();
    if (s_earliest.load() < before) wakeTimer();
}

void unschedule(int id) {
    std::lock_guard lk(s_mu);
    s_due[id] = 0;
    recomputeEarliest();
}

bool scheduled(int id) { return s_due[id] != 0; }

void wakeGuest() { raisePending(); }

u64 g_idleWaits = 0;  // R2_GX_LOG statistics

u64 tbHostNow() { return tbNow() - s_warp.load(std::memory_order_relaxed); }

u64 g_warpTb = 0;  // total idle skip (R2_GX_LOG / the session log)

// Idle skip: while the game shows nothing new - the VI is off (a video mode switch) or no frame
// has come for a while (loading at a cut into a cutscene) - and it only waits for the next timed
// event (retraces, disc and ARAM completions), the guest clock jumps to that event. On Rebel
// Strike's cuts the game thread idled ~85% of the 0.13 s pause, most of it in the frame flip's
// VIWaitForRetrace loop (0x80009F68) across the mode switch. Frames keep their own pace: only
// time nobody sees is skipped. Part of "Fast loading"; R2_NO_IDLE_SKIP=1 turns it off.
static bool idleSkip(u64 now) {
    static const bool off = getenv("R2_NO_IDLE_SKIP") && atoi(getenv("R2_NO_IDLE_SKIP"));
    if (off || !settings::get().fastDisc) return false;
    const u64 e = s_earliest.load();
    if (e == ~0ull || e <= now) return false;
    const u64 ms = kTbClock / 1000;
    if (e - now > 50 * ms) return false;  // (a long wait is the game's own, not a load)
    const u64 last = diagLastPresentTb(), host = tbHostNow();
    const bool viOff = viXfbLines() == 0;
    if (!viOff && (!last || host - last < 40 * ms)) return false;
    // at most 150 ms per stretch without frames (the sound queue takes the burst)
    static u64 stretchLast = 0, stretchWarp = 0;
    if (stretchLast != last) stretchLast = last, stretchWarp = 0;
    if (stretchWarp + (e - now) > 150 * ms) return false;
    stretchWarp += e - now;
    g_warpTb += e - now;
    s_warp.fetch_add(e - now, std::memory_order_relaxed);
    return true;
}

void idleWait(Ctx& c, u32 pc) {
    if (!g_irqPending && !g_irqLine && idleSkip(tbNow())) {
        g_irqPending = 1;  // the event is due now: run it at this poll
    }
    if (!g_irqPending && !g_irqLine) {
        g_idleWaits++;
        // The timer thread signals when an event is due; the timeout only bounds a missed wake.
        const u64 t0 = tbNow();
        waitGuest(4);
        g_idleTb += tbNow() - t0;  // (the session log's "game thread busy")
    }
    poll(c, pc);
}

void runDueEvents() {
    const u64 now = tbNow();
    for (int id = 0; id < EV_COUNT; id++) {
        bool fire = false;
        {
            std::lock_guard lk(s_mu);
            if (s_due[id] && s_due[id] <= now) {
                s_due[id] = 0;
                recomputeEarliest();
                fire = true;
            }
        }
        if (fire && g_eventHandlers[id]) g_eventHandlers[id]();
    }
}

// --- decrementer ----------------------------------------------------------------------
static void onDec() {
    s_decPending = true;
    g_irqLine |= 2;
    wakeGuest();
}

void decWrite(u32 v) {
    s_decBaseTb = tbNow();
    s_decValue = v;
    if (!g_eventHandlers[EV_DEC]) g_eventHandlers[EV_DEC] = onDec;
    if (v & 0x80000000u) onDec();
    else schedule(EV_DEC, s_decBaseTb + v + 1);
}

u32 decRead() { return s_decValue - u32(tbNow() - s_decBaseTb); }
bool decPending() { return s_decPending; }
void decAck() {
    s_decPending = false;
    g_irqLine &= ~2u;
}

}  // namespace rt

// --- save states ----------------------------------------------------------------------------
#include "savestate.h"
namespace rt {
void eventsSaveState(state::Writer& w) {
    w.section("EVNT");
    {
        std::lock_guard lk(s_mu);
        w.pod(s_due);
    }
    w.pod(s_decBaseTb);
    w.pod(s_decValue);
    w.pod(s_decPending);
}

void eventsLoadState(state::Reader& r) {
    r.section("EVNT");
    u64 due[EV_COUNT];
    r.pod(due);
    r.pod(s_decBaseTb);
    r.pod(s_decValue);
    r.pod(s_decPending);
    s_decBaseTb = r.time(s_decBaseTb);
    {
        std::lock_guard lk(s_mu);
        for (int i = 0; i < EV_COUNT; i++) s_due[i] = r.time(due[i]);
        recomputeEarliest();
    }
    wakeTimer();
    if (!g_eventHandlers[EV_DEC]) g_eventHandlers[EV_DEC] = onDec;
    if (s_decPending) g_irqLine |= 2;
    else g_irqLine &= ~2u;
    g_irqPending = 1;
}
}  // namespace rt
