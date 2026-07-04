/*
    LiteProfile - lightweight frame-breakdown profiler for melonDS (liteDS-v2).

    This is a TRIMMED port of the v1 fork's LiteProfile.h. Only the macro
    skeleton and a small per-frame counter struct are kept; the fork-specific
    hundreds-of-counters taxonomy is intentionally NOT ported.

    Everything compiles to nothing unless the build sets LITEV_PROFILE=1.
    Core call sites are deferred to later liteDS-v2 units; for Unit 0 the
    headless runner uses the FrameCounters struct + macros directly.
*/

#pragma once

#ifndef LITEV_PROFILE
#define LITEV_PROFILE 0
#endif

#if LITEV_PROFILE

#include <atomic>
#include <chrono>
#include <cstdint>

namespace melonDS::LiteProfile
{
using Clock = std::chrono::steady_clock;

// Per-frame counters. Deliberately small: the categories the v2 dispatch-core
// work (Milestones 1-2) needs from day one. Extend as call sites land.
struct FrameCounters
{
    // Scheduler
    std::atomic<uint64_t> SchedulerIterations{0};
    std::atomic<uint64_t> SchedulerEventsFired{0};

    // CPU execution time (nanoseconds within the frame)
    std::atomic<uint64_t> ARM9ExecNs{0};
    std::atomic<uint64_t> ARM7ExecNs{0};
    std::atomic<uint64_t> ARM7WaitNs{0};

    // Frame-decomposition timers (Unit 6 measurement): GPU3D geometry engine
    // run + the per-slice RunSystem event drain, so a frame decomposes into
    // ARM9 / ARM7 / GPU3D / system.
    std::atomic<uint64_t> GPU3DNs{0};
    std::atomic<uint64_t> RunSystemNs{0};

    // Idle-loop fast-forward hits (Unit 6): how often the existing branch-to-self
    // IdleLoop detection (ARM.cpp Execute) fast-forwards each CPU to its slice
    // target. ARM7IdleSkips additionally splits out hits attributable to the
    // LITEV_ARM7_IDLE IPC/SPI detector (only counted when that flag is built).
    std::atomic<uint64_t> ARM9IdleHits{0};
    std::atomic<uint64_t> ARM7IdleHits{0};
    std::atomic<uint64_t> ARM7IdleSkips{0};

    // Block-transition taxonomy (populated once M1 dispatcher/linking lands)
    std::atomic<uint64_t> LinkedTransitions{0};
    std::atomic<uint64_t> DispatcherHits{0};
    std::atomic<uint64_t> DispatcherMisses{0};
    std::atomic<uint64_t> CppReentries{0};

    // Unit 4 direct-linking bookkeeping counters (C++-side; the per-hop asm
    // LinkedHops counter is intentionally not implemented — see the unit report).
    std::atomic<uint64_t> LinksPatched{0};   // outgoing sites patched site->target
    std::atomic<uint64_t> LinksUnlinked{0};  // sites rewritten target->dispatcher
    std::atomic<uint64_t> PendingPeak{0};    // high-water mark of pending-link maps
    std::atomic<uint64_t> LinkSitesEmitted{0};   // eligible exit sites (got a link slot)
    std::atomic<uint64_t> DispatchOnlyExits{0};  // ineligible exit sites (plain dispatcher)

    // Time-in-JIT vs time-in-C++ (nanoseconds within the frame)
    std::atomic<uint64_t> TimeInJitNs{0};
    std::atomic<uint64_t> TimeInCppNs{0};

    // M3 memory-fast-path accounting: how many times the ARM9 C++ memory helpers
    // are actually entered. With the LITEV_MEM_FAST tiers off these count every
    // block transfer / u32 load; with them on they count only guard-miss
    // fallbacks, so the OFF-vs-ON delta is the number of helper calls eliminated.
    std::atomic<uint64_t> MemBlock9HelperCalls{0};   // SlowBlockTransfer9 entries
    std::atomic<uint64_t> MemRead9U32HelperCalls{0}; // SlowRead9<u32> entries

    void Reset()
    {
        SchedulerIterations.store(0, std::memory_order_relaxed);
        SchedulerEventsFired.store(0, std::memory_order_relaxed);
        ARM9ExecNs.store(0, std::memory_order_relaxed);
        ARM7ExecNs.store(0, std::memory_order_relaxed);
        ARM7WaitNs.store(0, std::memory_order_relaxed);
        GPU3DNs.store(0, std::memory_order_relaxed);
        RunSystemNs.store(0, std::memory_order_relaxed);
        ARM9IdleHits.store(0, std::memory_order_relaxed);
        ARM7IdleHits.store(0, std::memory_order_relaxed);
        ARM7IdleSkips.store(0, std::memory_order_relaxed);
        LinkedTransitions.store(0, std::memory_order_relaxed);
        DispatcherHits.store(0, std::memory_order_relaxed);
        DispatcherMisses.store(0, std::memory_order_relaxed);
        CppReentries.store(0, std::memory_order_relaxed);
        LinksPatched.store(0, std::memory_order_relaxed);
        LinksUnlinked.store(0, std::memory_order_relaxed);
        PendingPeak.store(0, std::memory_order_relaxed);
        LinkSitesEmitted.store(0, std::memory_order_relaxed);
        DispatchOnlyExits.store(0, std::memory_order_relaxed);
        TimeInJitNs.store(0, std::memory_order_relaxed);
        TimeInCppNs.store(0, std::memory_order_relaxed);
        MemBlock9HelperCalls.store(0, std::memory_order_relaxed);
        MemRead9U32HelperCalls.store(0, std::memory_order_relaxed);
    }
};

// Global frame counters (one active frame at a time). The headless runner and
// later core call sites both reference this instance.
inline FrameCounters g_Frame;

inline uint64_t NowNs()
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()).count();
}

inline void AddAtomic(std::atomic<uint64_t>& counter, uint64_t value = 1)
{
    counter.fetch_add(value, std::memory_order_relaxed);
}

// RAII scope timer: accumulates elapsed nanoseconds into an atomic counter.
struct ScopeTimer
{
    std::atomic<uint64_t>& Counter;
    uint64_t Start;
    explicit ScopeTimer(std::atomic<uint64_t>& counter)
        : Counter(counter), Start(NowNs()) {}
    ~ScopeTimer() { Counter.fetch_add(NowNs() - Start, std::memory_order_relaxed); }
};

inline void ResetFrame() { g_Frame.Reset(); }

} // namespace melonDS::LiteProfile

#define LITE_PROFILE_SCOPE(var_name, counter) melonDS::LiteProfile::ScopeTimer var_name(counter)
#define LITE_PROFILE_ADD(counter) melonDS::LiteProfile::AddAtomic(counter)
#define LITE_PROFILE_ADD_VALUE(counter, value) melonDS::LiteProfile::AddAtomic((counter), (value))
#define LITE_PROFILE_RESET_FRAME() melonDS::LiteProfile::ResetFrame()
#define LITE_PROFILE_NOW_NS() melonDS::LiteProfile::NowNs()

#else // !LITEV_PROFILE

#define LITE_PROFILE_SCOPE(var_name, counter) do { } while (0)
#define LITE_PROFILE_ADD(counter) do { } while (0)
#define LITE_PROFILE_ADD_VALUE(counter, value) do { } while (0)
#define LITE_PROFILE_RESET_FRAME() do { } while (0)
#define LITE_PROFILE_NOW_NS() 0ULL

#endif // LITEV_PROFILE
