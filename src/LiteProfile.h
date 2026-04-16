/*
    Lightweight frame breakdown profiler for debug builds.
*/

#pragma once

#include "Platform.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cinttypes>

namespace melonDS::LiteProfile
{
using Clock = std::chrono::steady_clock;

struct FrameCounters
{
    std::atomic<uint64_t> SchedulerIterations{0};
    std::atomic<uint64_t> SchedulerCappedIterations{0};
    std::atomic<uint64_t> SchedulerEventIterations{0};
    std::atomic<uint64_t> RunSystemFastSkips{0};

    std::atomic<uint64_t> ARM9ExecNs{0};
    std::atomic<uint64_t> ARM7ExecNs{0};
    std::atomic<uint64_t> ARM7WaitNs{0};
    std::atomic<uint64_t> GPU3DRunNs{0};
    std::atomic<uint64_t> RunSystemNs{0};

    std::atomic<uint64_t> DrawScanlineNs{0};
    std::atomic<uint64_t> DrawSpritesNs{0};
    std::atomic<uint64_t> DrawScanlineCalls{0};
    std::atomic<uint64_t> DrawSpritesCalls{0};

    std::atomic<uint64_t> ComposedEligibleLines{0};
    std::atomic<uint64_t> ComposedReplayHits{0};
    std::atomic<uint64_t> ComposedReplayMisses{0};
    std::atomic<uint64_t> ComposedIneligibleLines{0};
    std::atomic<uint64_t> SpriteLineSkips{0};

    std::atomic<uint64_t> TextBGLineCacheHits{0};
    std::atomic<uint64_t> TextBGLineCacheMisses{0};
    std::atomic<uint64_t> SpriteBinHits{0};
    std::atomic<uint64_t> SpriteBinMisses{0};
};

inline FrameCounters gFrame;
inline FrameCounters gWindow;
inline uint64_t gWindowFrames = 0;
inline constexpr uint64_t kLogEveryFrames = 30;
inline constexpr bool kEnabled = false;

inline uint64_t NowNs()
{
    if (!kEnabled)
        return 0;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()).count();
}

inline void ResetFrame()
{
    if (!kEnabled)
        return;
    gFrame.SchedulerIterations.store(0, std::memory_order_relaxed);
    gFrame.SchedulerCappedIterations.store(0, std::memory_order_relaxed);
    gFrame.SchedulerEventIterations.store(0, std::memory_order_relaxed);
    gFrame.RunSystemFastSkips.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecNs.store(0, std::memory_order_relaxed);
    gFrame.ARM7ExecNs.store(0, std::memory_order_relaxed);
    gFrame.ARM7WaitNs.store(0, std::memory_order_relaxed);
    gFrame.GPU3DRunNs.store(0, std::memory_order_relaxed);
    gFrame.RunSystemNs.store(0, std::memory_order_relaxed);
    gFrame.DrawScanlineNs.store(0, std::memory_order_relaxed);
    gFrame.DrawSpritesNs.store(0, std::memory_order_relaxed);
    gFrame.DrawScanlineCalls.store(0, std::memory_order_relaxed);
    gFrame.DrawSpritesCalls.store(0, std::memory_order_relaxed);
    gFrame.ComposedEligibleLines.store(0, std::memory_order_relaxed);
    gFrame.ComposedReplayHits.store(0, std::memory_order_relaxed);
    gFrame.ComposedReplayMisses.store(0, std::memory_order_relaxed);
    gFrame.ComposedIneligibleLines.store(0, std::memory_order_relaxed);
    gFrame.SpriteLineSkips.store(0, std::memory_order_relaxed);
    gFrame.TextBGLineCacheHits.store(0, std::memory_order_relaxed);
    gFrame.TextBGLineCacheMisses.store(0, std::memory_order_relaxed);
    gFrame.SpriteBinHits.store(0, std::memory_order_relaxed);
    gFrame.SpriteBinMisses.store(0, std::memory_order_relaxed);
}

inline void ResetWindow()
{
    if (!kEnabled)
        return;
    gWindow.SchedulerIterations.store(0, std::memory_order_relaxed);
    gWindow.SchedulerCappedIterations.store(0, std::memory_order_relaxed);
    gWindow.SchedulerEventIterations.store(0, std::memory_order_relaxed);
    gWindow.RunSystemFastSkips.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecNs.store(0, std::memory_order_relaxed);
    gWindow.ARM7ExecNs.store(0, std::memory_order_relaxed);
    gWindow.ARM7WaitNs.store(0, std::memory_order_relaxed);
    gWindow.GPU3DRunNs.store(0, std::memory_order_relaxed);
    gWindow.RunSystemNs.store(0, std::memory_order_relaxed);
    gWindow.DrawScanlineNs.store(0, std::memory_order_relaxed);
    gWindow.DrawSpritesNs.store(0, std::memory_order_relaxed);
    gWindow.DrawScanlineCalls.store(0, std::memory_order_relaxed);
    gWindow.DrawSpritesCalls.store(0, std::memory_order_relaxed);
    gWindow.ComposedEligibleLines.store(0, std::memory_order_relaxed);
    gWindow.ComposedReplayHits.store(0, std::memory_order_relaxed);
    gWindow.ComposedReplayMisses.store(0, std::memory_order_relaxed);
    gWindow.ComposedIneligibleLines.store(0, std::memory_order_relaxed);
    gWindow.SpriteLineSkips.store(0, std::memory_order_relaxed);
    gWindow.TextBGLineCacheHits.store(0, std::memory_order_relaxed);
    gWindow.TextBGLineCacheMisses.store(0, std::memory_order_relaxed);
    gWindow.SpriteBinHits.store(0, std::memory_order_relaxed);
    gWindow.SpriteBinMisses.store(0, std::memory_order_relaxed);
}

inline void AddAtomic(std::atomic<uint64_t>& counter, uint64_t value = 1)
{
    if (!kEnabled)
        return;
    counter.fetch_add(value, std::memory_order_relaxed);
}

inline void MergeCounter(std::atomic<uint64_t>& dst, std::atomic<uint64_t>& src)
{
    if (!kEnabled)
        return;
    dst.fetch_add(src.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

inline double NsPerFrame(const std::atomic<uint64_t>& counter)
{
    return static_cast<double>(counter.load(std::memory_order_relaxed)) /
           static_cast<double>(kLogEveryFrames) / 1000000.0;
}

inline double CountPerFrame(const std::atomic<uint64_t>& counter)
{
    return static_cast<double>(counter.load(std::memory_order_relaxed)) /
           static_cast<double>(kLogEveryFrames);
}

inline double Percent(uint64_t num, uint64_t den)
{
    if (den == 0)
        return 0.0;
    return (100.0 * static_cast<double>(num)) / static_cast<double>(den);
}

inline void EndFrame()
{
    if (!kEnabled)
        return;
    MergeCounter(gWindow.SchedulerIterations, gFrame.SchedulerIterations);
    MergeCounter(gWindow.SchedulerCappedIterations, gFrame.SchedulerCappedIterations);
    MergeCounter(gWindow.SchedulerEventIterations, gFrame.SchedulerEventIterations);
    MergeCounter(gWindow.RunSystemFastSkips, gFrame.RunSystemFastSkips);
    MergeCounter(gWindow.ARM9ExecNs, gFrame.ARM9ExecNs);
    MergeCounter(gWindow.ARM7ExecNs, gFrame.ARM7ExecNs);
    MergeCounter(gWindow.ARM7WaitNs, gFrame.ARM7WaitNs);
    MergeCounter(gWindow.GPU3DRunNs, gFrame.GPU3DRunNs);
    MergeCounter(gWindow.RunSystemNs, gFrame.RunSystemNs);
    MergeCounter(gWindow.DrawScanlineNs, gFrame.DrawScanlineNs);
    MergeCounter(gWindow.DrawSpritesNs, gFrame.DrawSpritesNs);
    MergeCounter(gWindow.DrawScanlineCalls, gFrame.DrawScanlineCalls);
    MergeCounter(gWindow.DrawSpritesCalls, gFrame.DrawSpritesCalls);
    MergeCounter(gWindow.ComposedEligibleLines, gFrame.ComposedEligibleLines);
    MergeCounter(gWindow.ComposedReplayHits, gFrame.ComposedReplayHits);
    MergeCounter(gWindow.ComposedReplayMisses, gFrame.ComposedReplayMisses);
    MergeCounter(gWindow.ComposedIneligibleLines, gFrame.ComposedIneligibleLines);
    MergeCounter(gWindow.SpriteLineSkips, gFrame.SpriteLineSkips);
    MergeCounter(gWindow.TextBGLineCacheHits, gFrame.TextBGLineCacheHits);
    MergeCounter(gWindow.TextBGLineCacheMisses, gFrame.TextBGLineCacheMisses);
    MergeCounter(gWindow.SpriteBinHits, gFrame.SpriteBinHits);
    MergeCounter(gWindow.SpriteBinMisses, gFrame.SpriteBinMisses);

    if (++gWindowFrames < kLogEveryFrames)
        return;

    const uint64_t iterations = gWindow.SchedulerIterations.load(std::memory_order_relaxed);
    const uint64_t capped = gWindow.SchedulerCappedIterations.load(std::memory_order_relaxed);
    const uint64_t events = gWindow.SchedulerEventIterations.load(std::memory_order_relaxed);
    const uint64_t fastSkips = gWindow.RunSystemFastSkips.load(std::memory_order_relaxed);
    const uint64_t composedHits = gWindow.ComposedReplayHits.load(std::memory_order_relaxed);
    const uint64_t composedMisses = gWindow.ComposedReplayMisses.load(std::memory_order_relaxed);
    const uint64_t composedEligible = gWindow.ComposedEligibleLines.load(std::memory_order_relaxed);
    const uint64_t textHits = gWindow.TextBGLineCacheHits.load(std::memory_order_relaxed);
    const uint64_t textMisses = gWindow.TextBGLineCacheMisses.load(std::memory_order_relaxed);
    const uint64_t spriteBinHits = gWindow.SpriteBinHits.load(std::memory_order_relaxed);
    const uint64_t spriteBinMisses = gWindow.SpriteBinMisses.load(std::memory_order_relaxed);

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] frames=%" PRIu64
        " iter/frame=%.1f capped=%.1f%% event=%.1f%% fastskip/frame=%.1f arm9=%.3fms arm7exec=%.3fms arm7wait=%.3fms gpu3d=%.3fms runsys=%.3fms 2dscan=%.3fms sprites=%.3fms",
        gWindowFrames,
        CountPerFrame(gWindow.SchedulerIterations),
        Percent(capped, iterations),
        Percent(events, iterations),
        CountPerFrame(gWindow.RunSystemFastSkips),
        NsPerFrame(gWindow.ARM9ExecNs),
        NsPerFrame(gWindow.ARM7ExecNs),
        NsPerFrame(gWindow.ARM7WaitNs),
        NsPerFrame(gWindow.GPU3DRunNs),
        NsPerFrame(gWindow.RunSystemNs),
        NsPerFrame(gWindow.DrawScanlineNs),
        NsPerFrame(gWindow.DrawSpritesNs));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] caches composed_hit=%.1f%% eligible/frame=%.1f ineligible/frame=%.1f text_hit=%.1f%% spritebin_hit=%.1f%% sprite_skip/frame=%.1f scan_calls/frame=%.1f sprite_calls/frame=%.1f",
        Percent(composedHits, composedHits + composedMisses),
        CountPerFrame(gWindow.ComposedEligibleLines),
        CountPerFrame(gWindow.ComposedIneligibleLines),
        Percent(textHits, textHits + textMisses),
        Percent(spriteBinHits, spriteBinHits + spriteBinMisses),
        CountPerFrame(gWindow.SpriteLineSkips),
        CountPerFrame(gWindow.DrawScanlineCalls),
        CountPerFrame(gWindow.DrawSpritesCalls));

    gWindowFrames = 0;
    ResetWindow();
}

class ScopeTimer
{
public:
    explicit ScopeTimer(std::atomic<uint64_t>& counter) : Counter(counter), Start(kEnabled ? NowNs() : 0) {}
    ~ScopeTimer()
    {
        if (!kEnabled)
            return;
        AddAtomic(Counter, NowNs() - Start);
    }

private:
    std::atomic<uint64_t>& Counter;
    uint64_t Start;
};
}
