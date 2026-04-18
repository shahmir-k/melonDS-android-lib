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
    std::atomic<uint64_t> ARM9JitDispatchNs{0};
    std::atomic<uint64_t> ARM9JitCompileNs{0};
    std::atomic<uint64_t> ARM9JitLookupNs{0};
    std::atomic<uint64_t> ARM9JitDispatchCalls{0};
    std::atomic<uint64_t> ARM9JitCompileCalls{0};
    std::atomic<uint64_t> ARM9JitLookupCalls{0};
    std::atomic<uint64_t> ARM9JitGuestCycles{0};
    std::atomic<uint64_t> ARM9JitLastBlockHits{0};
    std::atomic<uint64_t> ARM9JitChainAttempts{0};
    std::atomic<uint64_t> ARM9JitChainHits{0};
    std::atomic<uint64_t> ARM9JitReturnsNormal{0};
    std::atomic<uint64_t> ARM9JitReturnsStop{0};
    std::atomic<uint64_t> ARM9JitReturnsIdle{0};
    std::atomic<uint64_t> ARM9JitReturnsHalt{0};
    std::atomic<uint64_t> ARM9JitBlockCacheHits{0};
    std::atomic<uint64_t> ARM9JitBlockCacheMisses{0};
    std::atomic<uint64_t> ARM9LibHLEHits{0};
    std::atomic<uint64_t> ARM9SlowReadNs{0};
    std::atomic<uint64_t> ARM9SlowWriteNs{0};
    std::atomic<uint64_t> ARM9SlowReadCalls{0};
    std::atomic<uint64_t> ARM9SlowWriteCalls{0};
    std::atomic<uint64_t> ARM9SlowBlockTransferCalls{0};
    std::atomic<uint64_t> ARM9SlowReadMainRAM{0};
    std::atomic<uint64_t> ARM9SlowReadSharedWRAM{0};
    std::atomic<uint64_t> ARM9SlowReadIO{0};
    std::atomic<uint64_t> ARM9SlowReadIODispStat{0};
    std::atomic<uint64_t> ARM9SlowReadIODMA{0};
    std::atomic<uint64_t> ARM9SlowReadIOTimer{0};
    std::atomic<uint64_t> ARM9SlowReadIOKey{0};
    std::atomic<uint64_t> ARM9SlowReadIOIPC{0};
    std::atomic<uint64_t> ARM9SlowReadIOCart{0};
    std::atomic<uint64_t> ARM9SlowReadIOIRQ{0};
    std::atomic<uint64_t> ARM9SlowReadIOVRAMCtl{0};
    std::atomic<uint64_t> ARM9SlowReadIODivSqrt{0};
    std::atomic<uint64_t> ARM9SlowReadIOOther{0};
    std::atomic<uint64_t> ARM9SlowReadVRAM{0};
    std::atomic<uint64_t> ARM9SlowReadOther{0};
    std::atomic<uint64_t> ARM9SlowWriteMainRAM{0};
    std::atomic<uint64_t> ARM9SlowWriteSharedWRAM{0};
    std::atomic<uint64_t> ARM9SlowWriteIO{0};
    std::atomic<uint64_t> ARM9SlowWriteVRAM{0};
    std::atomic<uint64_t> ARM9SlowWriteOther{0};
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
    std::atomic<uint64_t> GPU3DSceneOpaqueNs{0};
    std::atomic<uint64_t> GPU3DSceneClearWorkaroundNs{0};
    std::atomic<uint64_t> GPU3DSceneTranslucentNs{0};
    std::atomic<uint64_t> GPU3DScenePostNs{0};
    std::atomic<uint64_t> GPU3DSceneOpaqueBatchCalls{0};
    std::atomic<uint64_t> GPU3DSceneOpaqueBatchPolys{0};
    std::atomic<uint64_t> GPU3DSceneClearBatchCalls{0};
    std::atomic<uint64_t> GPU3DSceneClearBatchPolys{0};
    std::atomic<uint64_t> GPU3DSceneClearSingleCalls{0};
    std::atomic<uint64_t> GPU3DSceneClearNeedOpaqueSingles{0};
    std::atomic<uint64_t> GPU3DSceneTransBatchCalls{0};
    std::atomic<uint64_t> GPU3DSceneTransBatchPolys{0};
    std::atomic<uint64_t> GPU3DSceneTransSingleCalls{0};
    std::atomic<uint64_t> GPU3DSceneTransNeedOpaqueSingles{0};

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
    gFrame.ARM9JitDispatchNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitCompileNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitLookupNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitDispatchCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitCompileCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitLookupCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitGuestCycles.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitLastBlockHits.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitChainAttempts.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitChainHits.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnsNormal.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnsStop.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnsIdle.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnsHalt.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitBlockCacheHits.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitBlockCacheMisses.store(0, std::memory_order_relaxed);
    gFrame.ARM9LibHLEHits.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockTransferCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadMainRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadSharedWRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIO.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIODispStat.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIODMA.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOTimer.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOKey.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOIPC.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOCart.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOIRQ.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOVRAMCtl.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIODivSqrt.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOOther.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadVRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadOther.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteMainRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteSharedWRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteIO.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteVRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteOther.store(0, std::memory_order_relaxed);
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
    gFrame.GPU3DSceneOpaqueNs.store(0, std::memory_order_relaxed);
    gFrame.GPU3DSceneClearWorkaroundNs.store(0, std::memory_order_relaxed);
    gFrame.GPU3DSceneTranslucentNs.store(0, std::memory_order_relaxed);
    gFrame.GPU3DScenePostNs.store(0, std::memory_order_relaxed);
    gFrame.GPU3DSceneOpaqueBatchCalls.store(0, std::memory_order_relaxed);
    gFrame.GPU3DSceneOpaqueBatchPolys.store(0, std::memory_order_relaxed);
    gFrame.GPU3DSceneClearBatchCalls.store(0, std::memory_order_relaxed);
    gFrame.GPU3DSceneClearBatchPolys.store(0, std::memory_order_relaxed);
    gFrame.GPU3DSceneClearSingleCalls.store(0, std::memory_order_relaxed);
    gFrame.GPU3DSceneClearNeedOpaqueSingles.store(0, std::memory_order_relaxed);
    gFrame.GPU3DSceneTransBatchCalls.store(0, std::memory_order_relaxed);
    gFrame.GPU3DSceneTransBatchPolys.store(0, std::memory_order_relaxed);
    gFrame.GPU3DSceneTransSingleCalls.store(0, std::memory_order_relaxed);
    gFrame.GPU3DSceneTransNeedOpaqueSingles.store(0, std::memory_order_relaxed);
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
    gWindow.ARM9JitDispatchNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitCompileNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitLookupNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitDispatchCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitCompileCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitLookupCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitGuestCycles.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitLastBlockHits.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitChainAttempts.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitChainHits.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnsNormal.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnsStop.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnsIdle.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnsHalt.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitBlockCacheHits.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitBlockCacheMisses.store(0, std::memory_order_relaxed);
    gWindow.ARM9LibHLEHits.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockTransferCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadMainRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadSharedWRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIO.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIODispStat.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIODMA.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOTimer.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOKey.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOIPC.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOCart.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOIRQ.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOVRAMCtl.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIODivSqrt.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOOther.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadVRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadOther.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteMainRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteSharedWRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteIO.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteVRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteOther.store(0, std::memory_order_relaxed);
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
    gWindow.GPU3DSceneOpaqueNs.store(0, std::memory_order_relaxed);
    gWindow.GPU3DSceneClearWorkaroundNs.store(0, std::memory_order_relaxed);
    gWindow.GPU3DSceneTranslucentNs.store(0, std::memory_order_relaxed);
    gWindow.GPU3DScenePostNs.store(0, std::memory_order_relaxed);
    gWindow.GPU3DSceneOpaqueBatchCalls.store(0, std::memory_order_relaxed);
    gWindow.GPU3DSceneOpaqueBatchPolys.store(0, std::memory_order_relaxed);
    gWindow.GPU3DSceneClearBatchCalls.store(0, std::memory_order_relaxed);
    gWindow.GPU3DSceneClearBatchPolys.store(0, std::memory_order_relaxed);
    gWindow.GPU3DSceneClearSingleCalls.store(0, std::memory_order_relaxed);
    gWindow.GPU3DSceneClearNeedOpaqueSingles.store(0, std::memory_order_relaxed);
    gWindow.GPU3DSceneTransBatchCalls.store(0, std::memory_order_relaxed);
    gWindow.GPU3DSceneTransBatchPolys.store(0, std::memory_order_relaxed);
    gWindow.GPU3DSceneTransSingleCalls.store(0, std::memory_order_relaxed);
    gWindow.GPU3DSceneTransNeedOpaqueSingles.store(0, std::memory_order_relaxed);
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

inline double Ratio(uint64_t num, uint64_t den)
{
    if (den == 0)
        return 0.0;
    return static_cast<double>(num) / static_cast<double>(den);
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
    MergeCounter(gWindow.ARM9JitDispatchNs, gFrame.ARM9JitDispatchNs);
    MergeCounter(gWindow.ARM9JitCompileNs, gFrame.ARM9JitCompileNs);
    MergeCounter(gWindow.ARM9JitLookupNs, gFrame.ARM9JitLookupNs);
    MergeCounter(gWindow.ARM9JitDispatchCalls, gFrame.ARM9JitDispatchCalls);
    MergeCounter(gWindow.ARM9JitCompileCalls, gFrame.ARM9JitCompileCalls);
    MergeCounter(gWindow.ARM9JitLookupCalls, gFrame.ARM9JitLookupCalls);
    MergeCounter(gWindow.ARM9JitGuestCycles, gFrame.ARM9JitGuestCycles);
    MergeCounter(gWindow.ARM9JitLastBlockHits, gFrame.ARM9JitLastBlockHits);
    MergeCounter(gWindow.ARM9JitChainAttempts, gFrame.ARM9JitChainAttempts);
    MergeCounter(gWindow.ARM9JitChainHits, gFrame.ARM9JitChainHits);
    MergeCounter(gWindow.ARM9JitReturnsNormal, gFrame.ARM9JitReturnsNormal);
    MergeCounter(gWindow.ARM9JitReturnsStop, gFrame.ARM9JitReturnsStop);
    MergeCounter(gWindow.ARM9JitReturnsIdle, gFrame.ARM9JitReturnsIdle);
    MergeCounter(gWindow.ARM9JitReturnsHalt, gFrame.ARM9JitReturnsHalt);
    MergeCounter(gWindow.ARM9JitBlockCacheHits, gFrame.ARM9JitBlockCacheHits);
    MergeCounter(gWindow.ARM9JitBlockCacheMisses, gFrame.ARM9JitBlockCacheMisses);
    MergeCounter(gWindow.ARM9LibHLEHits, gFrame.ARM9LibHLEHits);
    MergeCounter(gWindow.ARM9SlowReadNs, gFrame.ARM9SlowReadNs);
    MergeCounter(gWindow.ARM9SlowWriteNs, gFrame.ARM9SlowWriteNs);
    MergeCounter(gWindow.ARM9SlowReadCalls, gFrame.ARM9SlowReadCalls);
    MergeCounter(gWindow.ARM9SlowWriteCalls, gFrame.ARM9SlowWriteCalls);
    MergeCounter(gWindow.ARM9SlowBlockTransferCalls, gFrame.ARM9SlowBlockTransferCalls);
    MergeCounter(gWindow.ARM9SlowReadMainRAM, gFrame.ARM9SlowReadMainRAM);
    MergeCounter(gWindow.ARM9SlowReadSharedWRAM, gFrame.ARM9SlowReadSharedWRAM);
    MergeCounter(gWindow.ARM9SlowReadIO, gFrame.ARM9SlowReadIO);
    MergeCounter(gWindow.ARM9SlowReadIODispStat, gFrame.ARM9SlowReadIODispStat);
    MergeCounter(gWindow.ARM9SlowReadIODMA, gFrame.ARM9SlowReadIODMA);
    MergeCounter(gWindow.ARM9SlowReadIOTimer, gFrame.ARM9SlowReadIOTimer);
    MergeCounter(gWindow.ARM9SlowReadIOKey, gFrame.ARM9SlowReadIOKey);
    MergeCounter(gWindow.ARM9SlowReadIOIPC, gFrame.ARM9SlowReadIOIPC);
    MergeCounter(gWindow.ARM9SlowReadIOCart, gFrame.ARM9SlowReadIOCart);
    MergeCounter(gWindow.ARM9SlowReadIOIRQ, gFrame.ARM9SlowReadIOIRQ);
    MergeCounter(gWindow.ARM9SlowReadIOVRAMCtl, gFrame.ARM9SlowReadIOVRAMCtl);
    MergeCounter(gWindow.ARM9SlowReadIODivSqrt, gFrame.ARM9SlowReadIODivSqrt);
    MergeCounter(gWindow.ARM9SlowReadIOOther, gFrame.ARM9SlowReadIOOther);
    MergeCounter(gWindow.ARM9SlowReadVRAM, gFrame.ARM9SlowReadVRAM);
    MergeCounter(gWindow.ARM9SlowReadOther, gFrame.ARM9SlowReadOther);
    MergeCounter(gWindow.ARM9SlowWriteMainRAM, gFrame.ARM9SlowWriteMainRAM);
    MergeCounter(gWindow.ARM9SlowWriteSharedWRAM, gFrame.ARM9SlowWriteSharedWRAM);
    MergeCounter(gWindow.ARM9SlowWriteIO, gFrame.ARM9SlowWriteIO);
    MergeCounter(gWindow.ARM9SlowWriteVRAM, gFrame.ARM9SlowWriteVRAM);
    MergeCounter(gWindow.ARM9SlowWriteOther, gFrame.ARM9SlowWriteOther);
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
    MergeCounter(gWindow.GPU3DSceneOpaqueNs, gFrame.GPU3DSceneOpaqueNs);
    MergeCounter(gWindow.GPU3DSceneClearWorkaroundNs, gFrame.GPU3DSceneClearWorkaroundNs);
    MergeCounter(gWindow.GPU3DSceneTranslucentNs, gFrame.GPU3DSceneTranslucentNs);
    MergeCounter(gWindow.GPU3DScenePostNs, gFrame.GPU3DScenePostNs);
    MergeCounter(gWindow.GPU3DSceneOpaqueBatchCalls, gFrame.GPU3DSceneOpaqueBatchCalls);
    MergeCounter(gWindow.GPU3DSceneOpaqueBatchPolys, gFrame.GPU3DSceneOpaqueBatchPolys);
    MergeCounter(gWindow.GPU3DSceneClearBatchCalls, gFrame.GPU3DSceneClearBatchCalls);
    MergeCounter(gWindow.GPU3DSceneClearBatchPolys, gFrame.GPU3DSceneClearBatchPolys);
    MergeCounter(gWindow.GPU3DSceneClearSingleCalls, gFrame.GPU3DSceneClearSingleCalls);
    MergeCounter(gWindow.GPU3DSceneClearNeedOpaqueSingles, gFrame.GPU3DSceneClearNeedOpaqueSingles);
    MergeCounter(gWindow.GPU3DSceneTransBatchCalls, gFrame.GPU3DSceneTransBatchCalls);
    MergeCounter(gWindow.GPU3DSceneTransBatchPolys, gFrame.GPU3DSceneTransBatchPolys);
    MergeCounter(gWindow.GPU3DSceneTransSingleCalls, gFrame.GPU3DSceneTransSingleCalls);
    MergeCounter(gWindow.GPU3DSceneTransNeedOpaqueSingles, gFrame.GPU3DSceneTransNeedOpaqueSingles);
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
        "[LITEV_PROFILE] arm9_jit dispatch=%.3fms/%.1f lookup=%.3fms/%.1f compile=%.3fms/%.1f guest_cycles=%.1f last_hit=%.1f chain=%.1f/%.1f ret_normal=%.1f ret_stop=%.1f ret_idle=%.1f ret_halt=%.1f cache_hit=%.1f cache_miss=%.1f hle=%.1f slowread=%.3fms/%.1f slowwrite=%.3fms/%.1f slowblock=%.1f",
        NsPerFrame(gWindow.ARM9JitDispatchNs),
        CountPerFrame(gWindow.ARM9JitDispatchCalls),
        NsPerFrame(gWindow.ARM9JitLookupNs),
        CountPerFrame(gWindow.ARM9JitLookupCalls),
        NsPerFrame(gWindow.ARM9JitCompileNs),
        CountPerFrame(gWindow.ARM9JitCompileCalls),
        CountPerFrame(gWindow.ARM9JitGuestCycles),
        CountPerFrame(gWindow.ARM9JitLastBlockHits),
        CountPerFrame(gWindow.ARM9JitChainHits),
        CountPerFrame(gWindow.ARM9JitChainAttempts),
        CountPerFrame(gWindow.ARM9JitReturnsNormal),
        CountPerFrame(gWindow.ARM9JitReturnsStop),
        CountPerFrame(gWindow.ARM9JitReturnsIdle),
        CountPerFrame(gWindow.ARM9JitReturnsHalt),
        CountPerFrame(gWindow.ARM9JitBlockCacheHits),
        CountPerFrame(gWindow.ARM9JitBlockCacheMisses),
        CountPerFrame(gWindow.ARM9LibHLEHits),
        NsPerFrame(gWindow.ARM9SlowReadNs),
        CountPerFrame(gWindow.ARM9SlowReadCalls),
        NsPerFrame(gWindow.ARM9SlowWriteNs),
        CountPerFrame(gWindow.ARM9SlowWriteCalls),
        CountPerFrame(gWindow.ARM9SlowBlockTransferCalls));

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

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] gpu3d_scene opaque=%.3fms clearwork=%.3fms translucent=%.3fms post=%.3fms",
        NsPerFrame(gWindow.GPU3DSceneOpaqueNs),
        NsPerFrame(gWindow.GPU3DSceneClearWorkaroundNs),
        NsPerFrame(gWindow.GPU3DSceneTranslucentNs),
        NsPerFrame(gWindow.GPU3DScenePostNs));

    const uint64_t opaqueBatchCalls = gWindow.GPU3DSceneOpaqueBatchCalls.load(std::memory_order_relaxed);
    const uint64_t opaqueBatchPolys = gWindow.GPU3DSceneOpaqueBatchPolys.load(std::memory_order_relaxed);
    const uint64_t clearBatchCalls = gWindow.GPU3DSceneClearBatchCalls.load(std::memory_order_relaxed);
    const uint64_t clearBatchPolys = gWindow.GPU3DSceneClearBatchPolys.load(std::memory_order_relaxed);
    const uint64_t transBatchCalls = gWindow.GPU3DSceneTransBatchCalls.load(std::memory_order_relaxed);
    const uint64_t transBatchPolys = gWindow.GPU3DSceneTransBatchPolys.load(std::memory_order_relaxed);

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] gpu3d_draws opaque_batch=%.1f opaque_ppb=%.2f clear_batch=%.1f clear_ppb=%.2f clear_single=%.1f clear_needopaque=%.1f trans_batch=%.1f trans_ppb=%.2f trans_single=%.1f trans_needopaque=%.1f",
        CountPerFrame(gWindow.GPU3DSceneOpaqueBatchCalls),
        Ratio(opaqueBatchPolys, opaqueBatchCalls),
        CountPerFrame(gWindow.GPU3DSceneClearBatchCalls),
        Ratio(clearBatchPolys, clearBatchCalls),
        CountPerFrame(gWindow.GPU3DSceneClearSingleCalls),
        CountPerFrame(gWindow.GPU3DSceneClearNeedOpaqueSingles),
        CountPerFrame(gWindow.GPU3DSceneTransBatchCalls),
        Ratio(transBatchPolys, transBatchCalls),
        CountPerFrame(gWindow.GPU3DSceneTransSingleCalls),
        CountPerFrame(gWindow.GPU3DSceneTransNeedOpaqueSingles));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] arm9_slowmem read main=%.1f swram=%.1f io=%.1f vram=%.1f other=%.1f write main=%.1f swram=%.1f io=%.1f vram=%.1f other=%.1f",
        CountPerFrame(gWindow.ARM9SlowReadMainRAM),
        CountPerFrame(gWindow.ARM9SlowReadSharedWRAM),
        CountPerFrame(gWindow.ARM9SlowReadIO),
        CountPerFrame(gWindow.ARM9SlowReadVRAM),
        CountPerFrame(gWindow.ARM9SlowReadOther),
        CountPerFrame(gWindow.ARM9SlowWriteMainRAM),
        CountPerFrame(gWindow.ARM9SlowWriteSharedWRAM),
        CountPerFrame(gWindow.ARM9SlowWriteIO),
        CountPerFrame(gWindow.ARM9SlowWriteVRAM),
        CountPerFrame(gWindow.ARM9SlowWriteOther));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] arm9_slowio dispstat=%.1f dma=%.1f timer=%.1f key=%.1f ipc=%.1f cart=%.1f irq=%.1f vramctl=%.1f divsqrt=%.1f other=%.1f",
        CountPerFrame(gWindow.ARM9SlowReadIODispStat),
        CountPerFrame(gWindow.ARM9SlowReadIODMA),
        CountPerFrame(gWindow.ARM9SlowReadIOTimer),
        CountPerFrame(gWindow.ARM9SlowReadIOKey),
        CountPerFrame(gWindow.ARM9SlowReadIOIPC),
        CountPerFrame(gWindow.ARM9SlowReadIOCart),
        CountPerFrame(gWindow.ARM9SlowReadIOIRQ),
        CountPerFrame(gWindow.ARM9SlowReadIOVRAMCtl),
        CountPerFrame(gWindow.ARM9SlowReadIODivSqrt),
        CountPerFrame(gWindow.ARM9SlowReadIOOther));

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
