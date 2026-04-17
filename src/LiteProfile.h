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
    std::atomic<uint64_t> EventLCDNs{0};
    std::atomic<uint64_t> EventLCDCount{0};
    std::atomic<uint64_t> EventSPUNs{0};
    std::atomic<uint64_t> EventSPUCount{0};
    std::atomic<uint64_t> EventDisplayFIFONs{0};
    std::atomic<uint64_t> EventDisplayFIFOCount{0};
    std::atomic<uint64_t> EventWifiNs{0};
    std::atomic<uint64_t> EventWifiCount{0};
    std::atomic<uint64_t> EventRTCNs{0};
    std::atomic<uint64_t> EventRTCCount{0};
    std::atomic<uint64_t> EventCartNs{0};
    std::atomic<uint64_t> EventCartCount{0};
    std::atomic<uint64_t> EventSPINs{0};
    std::atomic<uint64_t> EventSPICount{0};
    std::atomic<uint64_t> EventMathNs{0};
    std::atomic<uint64_t> EventMathCount{0};
    std::atomic<uint64_t> EventTimerNs{0};
    std::atomic<uint64_t> EventTimerCount{0};
    std::atomic<uint64_t> EventOtherNs{0};
    std::atomic<uint64_t> EventOtherCount{0};
    std::atomic<uint64_t> LCDStartHBlankNs{0};
    std::atomic<uint64_t> LCDStartHBlankCount{0};
    std::atomic<uint64_t> LCDStartScanlineNs{0};
    std::atomic<uint64_t> LCDStartScanlineCount{0};
    std::atomic<uint64_t> LCDFinishFrameNs{0};
    std::atomic<uint64_t> LCDFinishFrameCount{0};
    std::atomic<uint64_t> LCDHBlankDrawNs{0};
    std::atomic<uint64_t> LCDHBlankSpritePrepNs{0};
    std::atomic<uint64_t> LCDHBlankDMANs{0};
    std::atomic<uint64_t> LCDHBlank3DNs{0};
    std::atomic<uint64_t> LCDHBlankIRQNs{0};
    std::atomic<uint64_t> LCDHBlankScheduleNs{0};
    std::atomic<uint64_t> LCDScanlineStateNs{0};
    std::atomic<uint64_t> LCDScanlineWindowNs{0};
    std::atomic<uint64_t> LCDScanlineDMANs{0};
    std::atomic<uint64_t> LCDScanlineVBlankNs{0};
    std::atomic<uint64_t> LCDScanlineScheduleNs{0};
    std::atomic<uint64_t> GPU3DRenderStateNs{0};
    std::atomic<uint64_t> GPU3DRenderTextureNs{0};
    std::atomic<uint64_t> GPU3DRenderTexPalNs{0};
    std::atomic<uint64_t> GPU3DRenderClearNs{0};
    std::atomic<uint64_t> GPU3DRenderGeometryNs{0};
    std::atomic<uint64_t> GPU3DRenderSceneNs{0};

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
inline constexpr bool kEnabled = true;

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
    gFrame.EventLCDNs.store(0, std::memory_order_relaxed);
    gFrame.EventLCDCount.store(0, std::memory_order_relaxed);
    gFrame.EventSPUNs.store(0, std::memory_order_relaxed);
    gFrame.EventSPUCount.store(0, std::memory_order_relaxed);
    gFrame.EventDisplayFIFONs.store(0, std::memory_order_relaxed);
    gFrame.EventDisplayFIFOCount.store(0, std::memory_order_relaxed);
    gFrame.EventWifiNs.store(0, std::memory_order_relaxed);
    gFrame.EventWifiCount.store(0, std::memory_order_relaxed);
    gFrame.EventRTCNs.store(0, std::memory_order_relaxed);
    gFrame.EventRTCCount.store(0, std::memory_order_relaxed);
    gFrame.EventCartNs.store(0, std::memory_order_relaxed);
    gFrame.EventCartCount.store(0, std::memory_order_relaxed);
    gFrame.EventSPINs.store(0, std::memory_order_relaxed);
    gFrame.EventSPICount.store(0, std::memory_order_relaxed);
    gFrame.EventMathNs.store(0, std::memory_order_relaxed);
    gFrame.EventMathCount.store(0, std::memory_order_relaxed);
    gFrame.EventTimerNs.store(0, std::memory_order_relaxed);
    gFrame.EventTimerCount.store(0, std::memory_order_relaxed);
    gFrame.EventOtherNs.store(0, std::memory_order_relaxed);
    gFrame.EventOtherCount.store(0, std::memory_order_relaxed);
    gFrame.LCDStartHBlankNs.store(0, std::memory_order_relaxed);
    gFrame.LCDStartHBlankCount.store(0, std::memory_order_relaxed);
    gFrame.LCDStartScanlineNs.store(0, std::memory_order_relaxed);
    gFrame.LCDStartScanlineCount.store(0, std::memory_order_relaxed);
    gFrame.LCDFinishFrameNs.store(0, std::memory_order_relaxed);
    gFrame.LCDFinishFrameCount.store(0, std::memory_order_relaxed);
    gFrame.LCDHBlankDrawNs.store(0, std::memory_order_relaxed);
    gFrame.LCDHBlankSpritePrepNs.store(0, std::memory_order_relaxed);
    gFrame.LCDHBlankDMANs.store(0, std::memory_order_relaxed);
    gFrame.LCDHBlank3DNs.store(0, std::memory_order_relaxed);
    gFrame.LCDHBlankIRQNs.store(0, std::memory_order_relaxed);
    gFrame.LCDHBlankScheduleNs.store(0, std::memory_order_relaxed);
    gFrame.LCDScanlineStateNs.store(0, std::memory_order_relaxed);
    gFrame.LCDScanlineWindowNs.store(0, std::memory_order_relaxed);
    gFrame.LCDScanlineDMANs.store(0, std::memory_order_relaxed);
    gFrame.LCDScanlineVBlankNs.store(0, std::memory_order_relaxed);
    gFrame.LCDScanlineScheduleNs.store(0, std::memory_order_relaxed);
    gFrame.GPU3DRenderStateNs.store(0, std::memory_order_relaxed);
    gFrame.GPU3DRenderTextureNs.store(0, std::memory_order_relaxed);
    gFrame.GPU3DRenderTexPalNs.store(0, std::memory_order_relaxed);
    gFrame.GPU3DRenderClearNs.store(0, std::memory_order_relaxed);
    gFrame.GPU3DRenderGeometryNs.store(0, std::memory_order_relaxed);
    gFrame.GPU3DRenderSceneNs.store(0, std::memory_order_relaxed);
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
    gWindow.EventLCDNs.store(0, std::memory_order_relaxed);
    gWindow.EventLCDCount.store(0, std::memory_order_relaxed);
    gWindow.EventSPUNs.store(0, std::memory_order_relaxed);
    gWindow.EventSPUCount.store(0, std::memory_order_relaxed);
    gWindow.EventDisplayFIFONs.store(0, std::memory_order_relaxed);
    gWindow.EventDisplayFIFOCount.store(0, std::memory_order_relaxed);
    gWindow.EventWifiNs.store(0, std::memory_order_relaxed);
    gWindow.EventWifiCount.store(0, std::memory_order_relaxed);
    gWindow.EventRTCNs.store(0, std::memory_order_relaxed);
    gWindow.EventRTCCount.store(0, std::memory_order_relaxed);
    gWindow.EventCartNs.store(0, std::memory_order_relaxed);
    gWindow.EventCartCount.store(0, std::memory_order_relaxed);
    gWindow.EventSPINs.store(0, std::memory_order_relaxed);
    gWindow.EventSPICount.store(0, std::memory_order_relaxed);
    gWindow.EventMathNs.store(0, std::memory_order_relaxed);
    gWindow.EventMathCount.store(0, std::memory_order_relaxed);
    gWindow.EventTimerNs.store(0, std::memory_order_relaxed);
    gWindow.EventTimerCount.store(0, std::memory_order_relaxed);
    gWindow.EventOtherNs.store(0, std::memory_order_relaxed);
    gWindow.EventOtherCount.store(0, std::memory_order_relaxed);
    gWindow.LCDStartHBlankNs.store(0, std::memory_order_relaxed);
    gWindow.LCDStartHBlankCount.store(0, std::memory_order_relaxed);
    gWindow.LCDStartScanlineNs.store(0, std::memory_order_relaxed);
    gWindow.LCDStartScanlineCount.store(0, std::memory_order_relaxed);
    gWindow.LCDFinishFrameNs.store(0, std::memory_order_relaxed);
    gWindow.LCDFinishFrameCount.store(0, std::memory_order_relaxed);
    gWindow.LCDHBlankDrawNs.store(0, std::memory_order_relaxed);
    gWindow.LCDHBlankSpritePrepNs.store(0, std::memory_order_relaxed);
    gWindow.LCDHBlankDMANs.store(0, std::memory_order_relaxed);
    gWindow.LCDHBlank3DNs.store(0, std::memory_order_relaxed);
    gWindow.LCDHBlankIRQNs.store(0, std::memory_order_relaxed);
    gWindow.LCDHBlankScheduleNs.store(0, std::memory_order_relaxed);
    gWindow.LCDScanlineStateNs.store(0, std::memory_order_relaxed);
    gWindow.LCDScanlineWindowNs.store(0, std::memory_order_relaxed);
    gWindow.LCDScanlineDMANs.store(0, std::memory_order_relaxed);
    gWindow.LCDScanlineVBlankNs.store(0, std::memory_order_relaxed);
    gWindow.LCDScanlineScheduleNs.store(0, std::memory_order_relaxed);
    gWindow.GPU3DRenderStateNs.store(0, std::memory_order_relaxed);
    gWindow.GPU3DRenderTextureNs.store(0, std::memory_order_relaxed);
    gWindow.GPU3DRenderTexPalNs.store(0, std::memory_order_relaxed);
    gWindow.GPU3DRenderClearNs.store(0, std::memory_order_relaxed);
    gWindow.GPU3DRenderGeometryNs.store(0, std::memory_order_relaxed);
    gWindow.GPU3DRenderSceneNs.store(0, std::memory_order_relaxed);
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
    MergeCounter(gWindow.EventLCDNs, gFrame.EventLCDNs);
    MergeCounter(gWindow.EventLCDCount, gFrame.EventLCDCount);
    MergeCounter(gWindow.EventSPUNs, gFrame.EventSPUNs);
    MergeCounter(gWindow.EventSPUCount, gFrame.EventSPUCount);
    MergeCounter(gWindow.EventDisplayFIFONs, gFrame.EventDisplayFIFONs);
    MergeCounter(gWindow.EventDisplayFIFOCount, gFrame.EventDisplayFIFOCount);
    MergeCounter(gWindow.EventWifiNs, gFrame.EventWifiNs);
    MergeCounter(gWindow.EventWifiCount, gFrame.EventWifiCount);
    MergeCounter(gWindow.EventRTCNs, gFrame.EventRTCNs);
    MergeCounter(gWindow.EventRTCCount, gFrame.EventRTCCount);
    MergeCounter(gWindow.EventCartNs, gFrame.EventCartNs);
    MergeCounter(gWindow.EventCartCount, gFrame.EventCartCount);
    MergeCounter(gWindow.EventSPINs, gFrame.EventSPINs);
    MergeCounter(gWindow.EventSPICount, gFrame.EventSPICount);
    MergeCounter(gWindow.EventMathNs, gFrame.EventMathNs);
    MergeCounter(gWindow.EventMathCount, gFrame.EventMathCount);
    MergeCounter(gWindow.EventTimerNs, gFrame.EventTimerNs);
    MergeCounter(gWindow.EventTimerCount, gFrame.EventTimerCount);
    MergeCounter(gWindow.EventOtherNs, gFrame.EventOtherNs);
    MergeCounter(gWindow.EventOtherCount, gFrame.EventOtherCount);
    MergeCounter(gWindow.LCDStartHBlankNs, gFrame.LCDStartHBlankNs);
    MergeCounter(gWindow.LCDStartHBlankCount, gFrame.LCDStartHBlankCount);
    MergeCounter(gWindow.LCDStartScanlineNs, gFrame.LCDStartScanlineNs);
    MergeCounter(gWindow.LCDStartScanlineCount, gFrame.LCDStartScanlineCount);
    MergeCounter(gWindow.LCDFinishFrameNs, gFrame.LCDFinishFrameNs);
    MergeCounter(gWindow.LCDFinishFrameCount, gFrame.LCDFinishFrameCount);
    MergeCounter(gWindow.LCDHBlankDrawNs, gFrame.LCDHBlankDrawNs);
    MergeCounter(gWindow.LCDHBlankSpritePrepNs, gFrame.LCDHBlankSpritePrepNs);
    MergeCounter(gWindow.LCDHBlankDMANs, gFrame.LCDHBlankDMANs);
    MergeCounter(gWindow.LCDHBlank3DNs, gFrame.LCDHBlank3DNs);
    MergeCounter(gWindow.LCDHBlankIRQNs, gFrame.LCDHBlankIRQNs);
    MergeCounter(gWindow.LCDHBlankScheduleNs, gFrame.LCDHBlankScheduleNs);
    MergeCounter(gWindow.LCDScanlineStateNs, gFrame.LCDScanlineStateNs);
    MergeCounter(gWindow.LCDScanlineWindowNs, gFrame.LCDScanlineWindowNs);
    MergeCounter(gWindow.LCDScanlineDMANs, gFrame.LCDScanlineDMANs);
    MergeCounter(gWindow.LCDScanlineVBlankNs, gFrame.LCDScanlineVBlankNs);
    MergeCounter(gWindow.LCDScanlineScheduleNs, gFrame.LCDScanlineScheduleNs);
    MergeCounter(gWindow.GPU3DRenderStateNs, gFrame.GPU3DRenderStateNs);
    MergeCounter(gWindow.GPU3DRenderTextureNs, gFrame.GPU3DRenderTextureNs);
    MergeCounter(gWindow.GPU3DRenderTexPalNs, gFrame.GPU3DRenderTexPalNs);
    MergeCounter(gWindow.GPU3DRenderClearNs, gFrame.GPU3DRenderClearNs);
    MergeCounter(gWindow.GPU3DRenderGeometryNs, gFrame.GPU3DRenderGeometryNs);
    MergeCounter(gWindow.GPU3DRenderSceneNs, gFrame.GPU3DRenderSceneNs);
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

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] events lcd=%.3fms/%.1f spu=%.3fms/%.1f fifo=%.3fms/%.1f wifi=%.3fms/%.1f rtc=%.3fms/%.1f cart=%.3fms/%.1f spi=%.3fms/%.1f math=%.3fms/%.1f timer=%.3fms/%.1f other=%.3fms/%.1f",
        NsPerFrame(gWindow.EventLCDNs), CountPerFrame(gWindow.EventLCDCount),
        NsPerFrame(gWindow.EventSPUNs), CountPerFrame(gWindow.EventSPUCount),
        NsPerFrame(gWindow.EventDisplayFIFONs), CountPerFrame(gWindow.EventDisplayFIFOCount),
        NsPerFrame(gWindow.EventWifiNs), CountPerFrame(gWindow.EventWifiCount),
        NsPerFrame(gWindow.EventRTCNs), CountPerFrame(gWindow.EventRTCCount),
        NsPerFrame(gWindow.EventCartNs), CountPerFrame(gWindow.EventCartCount),
        NsPerFrame(gWindow.EventSPINs), CountPerFrame(gWindow.EventSPICount),
        NsPerFrame(gWindow.EventMathNs), CountPerFrame(gWindow.EventMathCount),
        NsPerFrame(gWindow.EventTimerNs), CountPerFrame(gWindow.EventTimerCount),
        NsPerFrame(gWindow.EventOtherNs), CountPerFrame(gWindow.EventOtherCount));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] lcd hblank=%.3fms/%.1f scanline=%.3fms/%.1f finish=%.3fms/%.1f",
        NsPerFrame(gWindow.LCDStartHBlankNs), CountPerFrame(gWindow.LCDStartHBlankCount),
        NsPerFrame(gWindow.LCDStartScanlineNs), CountPerFrame(gWindow.LCDStartScanlineCount),
        NsPerFrame(gWindow.LCDFinishFrameNs), CountPerFrame(gWindow.LCDFinishFrameCount));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] lcd_hblank draw=%.3fms spriteprep=%.3fms dma=%.3fms vcount215=%.3fms irq=%.3fms sched=%.3fms",
        NsPerFrame(gWindow.LCDHBlankDrawNs),
        NsPerFrame(gWindow.LCDHBlankSpritePrepNs),
        NsPerFrame(gWindow.LCDHBlankDMANs),
        NsPerFrame(gWindow.LCDHBlank3DNs),
        NsPerFrame(gWindow.LCDHBlankIRQNs),
        NsPerFrame(gWindow.LCDHBlankScheduleNs));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] lcd_scanline state=%.3fms windows=%.3fms dma=%.3fms vblank=%.3fms sched=%.3fms",
        NsPerFrame(gWindow.LCDScanlineStateNs),
        NsPerFrame(gWindow.LCDScanlineWindowNs),
        NsPerFrame(gWindow.LCDScanlineDMANs),
        NsPerFrame(gWindow.LCDScanlineVBlankNs),
        NsPerFrame(gWindow.LCDScanlineScheduleNs));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] gpu3d_render state=%.3fms tex=%.3fms texpal=%.3fms clear=%.3fms geom=%.3fms scene=%.3fms",
        NsPerFrame(gWindow.GPU3DRenderStateNs),
        NsPerFrame(gWindow.GPU3DRenderTextureNs),
        NsPerFrame(gWindow.GPU3DRenderTexPalNs),
        NsPerFrame(gWindow.GPU3DRenderClearNs),
        NsPerFrame(gWindow.GPU3DRenderGeometryNs),
        NsPerFrame(gWindow.GPU3DRenderSceneNs));

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
