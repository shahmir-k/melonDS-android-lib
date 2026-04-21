/*
    Lightweight frame breakdown profiler for debug builds.
*/

#pragma once

#ifndef LITEV_PROFILE
#define LITEV_PROFILE 0
#endif

#if LITEV_PROFILE

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
    std::atomic<uint64_t> ARM9JitSetupRegionNs{0};
    std::atomic<uint64_t> ARM9JitHLECheckNs{0};
    std::atomic<uint64_t> ARM9JitExecuteBodyNs{0};
    std::atomic<uint64_t> ARM9JitPostDispatchNs{0};
    std::atomic<uint64_t> ARM9JitDispatchCalls{0};
    std::atomic<uint64_t> ARM9JitCompileCalls{0};
    std::atomic<uint64_t> ARM9JitLookupCalls{0};
    std::atomic<uint64_t> ARM9JitSetupRegionCalls{0};
    std::atomic<uint64_t> ARM9JitSetupRegionFails{0};
    std::atomic<uint64_t> ARM9JitHLECheckCalls{0};
    std::atomic<uint64_t> ARM9JitGuestCycles{0};
    std::atomic<uint64_t> ARM9JitLastBlockHits{0};
    std::atomic<uint64_t> ARM9ExecInstrs{0};
    std::atomic<uint64_t> ARM9ExecARMALU{0};
    std::atomic<uint64_t> ARM9ExecARMMul{0};
    std::atomic<uint64_t> ARM9ExecARMSingleLoad{0};
    std::atomic<uint64_t> ARM9ExecARMSingleStore{0};
    std::atomic<uint64_t> ARM9ExecARMBlockLoad{0};
    std::atomic<uint64_t> ARM9ExecARMBlockStore{0};
    std::atomic<uint64_t> ARM9ExecARMStackBlockLoad{0};
    std::atomic<uint64_t> ARM9ExecARMStackBlockStore{0};
    std::atomic<uint64_t> ARM9ExecARMStackLoad{0};
    std::atomic<uint64_t> ARM9ExecARMStackStore{0};
    std::atomic<uint64_t> ARM9ExecARMBranchImm{0};
    std::atomic<uint64_t> ARM9ExecARMBranchReg{0};
    std::atomic<uint64_t> ARM9ExecARMSys{0};
    std::atomic<uint64_t> ARM9ExecARMOther{0};
    std::atomic<uint64_t> ARM9ExecThumbALU{0};
    std::atomic<uint64_t> ARM9ExecThumbMul{0};
    std::atomic<uint64_t> ARM9ExecThumbSingleLoad{0};
    std::atomic<uint64_t> ARM9ExecThumbSingleStore{0};
    std::atomic<uint64_t> ARM9ExecThumbBlockLoad{0};
    std::atomic<uint64_t> ARM9ExecThumbBlockStore{0};
    std::atomic<uint64_t> ARM9ExecThumbStackLoad{0};
    std::atomic<uint64_t> ARM9ExecThumbStackStore{0};
    std::atomic<uint64_t> ARM9ExecThumbBranchCond{0};
    std::atomic<uint64_t> ARM9ExecThumbBranchImm{0};
    std::atomic<uint64_t> ARM9ExecThumbBranchReg{0};
    std::atomic<uint64_t> ARM9ExecThumbSys{0};
    std::atomic<uint64_t> ARM9ExecThumbOther{0};
    std::atomic<uint64_t> ARM9ExecLiteralLoads{0};
    std::atomic<uint64_t> ARM9ExecMemITCM{0};
    std::atomic<uint64_t> ARM9ExecMemDTCM{0};
    std::atomic<uint64_t> ARM9ExecMemMainRAM{0};
    std::atomic<uint64_t> ARM9ExecMemSharedWRAM{0};
    std::atomic<uint64_t> ARM9ExecMemIO{0};
    std::atomic<uint64_t> ARM9ExecMemVRAM{0};
    std::atomic<uint64_t> ARM9ExecMemOther{0};
    std::atomic<uint64_t> ARM9JitChainAttempts{0};
    std::atomic<uint64_t> ARM9JitChainHits{0};
    std::atomic<uint64_t> ARM9JitChainMissStop{0};
    std::atomic<uint64_t> ARM9JitChainMissBudget{0};
    std::atomic<uint64_t> ARM9JitChainMissTarget{0};
    std::atomic<uint64_t> ARM9JitChainMissHLE{0};
    std::atomic<uint64_t> ARM9JitChainMissSetup{0};
    std::atomic<uint64_t> ARM9JitChainMissLookup{0};
    std::atomic<uint64_t> ARM9JitReturnsNormal{0};
    std::atomic<uint64_t> ARM9JitReturnsStop{0};
    std::atomic<uint64_t> ARM9JitReturnsIdle{0};
    std::atomic<uint64_t> ARM9JitReturnsHalt{0};
    std::atomic<uint64_t> ARM9JitReturnEndBlock{0};
    std::atomic<uint64_t> ARM9JitReturnEndBranch{0};
    std::atomic<uint64_t> ARM9JitReturnEndCondBranch{0};
    std::atomic<uint64_t> ARM9JitReturnEndOther{0};
    std::atomic<uint64_t> ARM9JitReturnEndARMImm{0};
    std::atomic<uint64_t> ARM9JitReturnEndARMReg{0};
    std::atomic<uint64_t> ARM9JitReturnEndARMRegARM{0};
    std::atomic<uint64_t> ARM9JitReturnEndARMRegThumb{0};
    std::atomic<uint64_t> ARM9JitReturnEndARMRegLR{0};
    std::atomic<uint64_t> ARM9JitReturnEndARMRegOther{0};
    std::atomic<uint64_t> ARM9JitReturnEndARMRegLink{0};
    std::atomic<uint64_t> ARM9JitReturnEndThumbCond{0};
    std::atomic<uint64_t> ARM9JitReturnEndThumbImm{0};
    std::atomic<uint64_t> ARM9JitReturnEndThumbReg{0};
    std::atomic<uint64_t> ARM9JitReturnMaxBlock{0};
    std::atomic<uint64_t> ARM9JitReturnMaxARM{0};
    std::atomic<uint64_t> ARM9JitReturnMaxThumb{0};
    std::atomic<uint64_t> ARM9JitReturnMaxWithMemory{0};
    std::atomic<uint64_t> ARM9JitReturnMaxNoMemory{0};
    std::atomic<uint64_t> ARM9JitReturnMaxWithLoad{0};
    std::atomic<uint64_t> ARM9JitReturnMaxWithStore{0};
    std::atomic<uint64_t> ARM9JitReturnMaxITCM{0};
    std::atomic<uint64_t> ARM9JitReturnMaxDTCM{0};
    std::atomic<uint64_t> ARM9JitReturnMaxMainRAM{0};
    std::atomic<uint64_t> ARM9JitReturnMaxSharedWRAM{0};
    std::atomic<uint64_t> ARM9JitReturnMaxIO{0};
    std::atomic<uint64_t> ARM9JitReturnMaxVRAM{0};
    std::atomic<uint64_t> ARM9JitReturnMaxOtherMem{0};
    std::atomic<uint64_t> ARM9JitReturnIRQBoundary{0};
    std::atomic<uint64_t> ARM9JitReturnHaltBoundary{0};
    std::atomic<uint64_t> ARM9JitBlockCacheHits{0};
    std::atomic<uint64_t> ARM9JitBlockCacheMisses{0};
    std::atomic<uint64_t> ARM9LibHLEHits{0};
    std::atomic<uint64_t> ARM9SlowReadNs{0};
    std::atomic<uint64_t> ARM9SlowWriteNs{0};
    std::atomic<uint64_t> ARM9SlowBlockTransferNs{0};
    std::atomic<uint64_t> ARM9SlowReadCalls{0};
    std::atomic<uint64_t> ARM9SlowWriteCalls{0};
    std::atomic<uint64_t> ARM9SlowBlockTransferCalls{0};
    std::atomic<uint64_t> ARM9SlowBlockTransferReads{0};
    std::atomic<uint64_t> ARM9SlowBlockTransferWrites{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadCalls{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericStoreCalls{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStackLoadCalls{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStoreCalls{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadNs{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericStoreNs{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadTotalNs{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericStoreTotalNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStackLoadNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStoreNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStackLoadTotalNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStoreTotalNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStackLoadDirectNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStackLoadFallbackNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStoreDirectNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStoreFallbackNs{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStackPreTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStackWrapTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStackPostTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStackPackTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStackSaveTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStackCallTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStackRestoreTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStackUnpackTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStorePreTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStoreWrapTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStorePostTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStorePackTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStoreSaveTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStoreCallTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStoreRestoreTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteFastStoreUnpackTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericLoadPreTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericLoadWrapTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericLoadPostTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericLoadPackTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericLoadSaveTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericLoadCallTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericLoadRestoreTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericLoadUnpackTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericStorePreTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericStoreWrapTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericStorePostTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericStorePackTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericStoreSaveTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericStoreCallTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericStoreRestoreTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockCallsiteGenericStoreUnpackTicks{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStackLoad_1_2{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStackLoad_3_4{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStackLoad_5_8{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStackLoad_9P{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStore_1_2{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStore_3_4{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStore_5_8{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStore_9P{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStackLoad_1_2_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStackLoad_3_4_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStackLoad_5_8_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStackLoad_9P_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStore_1_2_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStore_3_4_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStore_5_8_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockFastStore_9P_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadFastmemOff{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadUsermode{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadNonFastPathShape{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadCondIncompatible{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadTargetDTCM{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadTargetMainRAM{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadTargetSharedWRAM{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadTargetIO{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadTargetOther{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadShapeARM{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadShapeThumb{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadShapeSkipBase{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadShapeKeepBase{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadShapeBaseSP{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadShapeBaseOther{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadShapeRegs1_2{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadShapeRegs3_4{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadShapeRegs5P{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadShapeIncPre{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadShapeIncPost{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadShapeDecPre{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericLoadShapeDecPost{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericStoreFastmemOff{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericStoreUsermode{0};
    std::atomic<uint64_t> ARM9SlowBlockGenericStoreCondIncompatible{0};
    std::atomic<uint64_t> ARM9SlowBlockPrePathNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFastDTCMSetupNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFastMainRAMSetupNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFastDTCMNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFastMainRAMNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFallbackLoopNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFastDTCMCalls{0};
    std::atomic<uint64_t> ARM9SlowBlockFastDTCMDirectCalls{0};
    std::atomic<uint64_t> ARM9SlowBlockFastDTCMFallbackCalls{0};
    std::atomic<uint64_t> ARM9SlowBlockFastDTCMDirectNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFastDTCMFallbackNs{0};
    std::atomic<uint64_t> ARM9SlowBlockFastMainRAMCalls{0};
    std::atomic<uint64_t> ARM9SlowBlockFallbackLoopCalls{0};
    std::atomic<uint64_t> ARM9SlowBlockReadDTCM{0};
    std::atomic<uint64_t> ARM9SlowBlockReadMainRAM{0};
    std::atomic<uint64_t> ARM9SlowBlockReadSharedWRAM{0};
    std::atomic<uint64_t> ARM9SlowBlockReadIO{0};
    std::atomic<uint64_t> ARM9SlowBlockReadOther{0};
    std::atomic<uint64_t> ARM9SlowBlockReadDTCM_1_2{0};
    std::atomic<uint64_t> ARM9SlowBlockReadDTCM_3_4{0};
    std::atomic<uint64_t> ARM9SlowBlockReadDTCM_5_8{0};
    std::atomic<uint64_t> ARM9SlowBlockReadDTCM_9_16{0};
    std::atomic<uint64_t> ARM9SlowBlockReadDTCM_17P{0};
    std::atomic<uint64_t> ARM9SlowBlockReadDTCM_1_2_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockReadDTCM_3_4_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockReadDTCM_5_8_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockReadDTCM_9_16_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockReadDTCM_17P_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockReadMainRAM_1_2{0};
    std::atomic<uint64_t> ARM9SlowBlockReadMainRAM_3_4{0};
    std::atomic<uint64_t> ARM9SlowBlockReadMainRAM_5_8{0};
    std::atomic<uint64_t> ARM9SlowBlockReadMainRAM_9_16{0};
    std::atomic<uint64_t> ARM9SlowBlockReadMainRAM_17P{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteDTCM{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteMainRAM{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteSharedWRAM{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteIO{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteOther{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteDTCM_1_2{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteDTCM_3_4{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteDTCM_5_8{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteDTCM_9_16{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteDTCM_17P{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteDTCM_1_2_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteDTCM_3_4_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteDTCM_5_8_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteDTCM_9_16_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteDTCM_17P_Ns{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteMainRAM_1_2{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteMainRAM_3_4{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteMainRAM_5_8{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteMainRAM_9_16{0};
    std::atomic<uint64_t> ARM9SlowBlockWriteMainRAM_17P{0};
    std::atomic<uint64_t> ARM9SlowReadMainRAM{0};
    std::atomic<uint64_t> ARM9SlowReadMainRAMNs{0};
    std::atomic<uint64_t> ARM9SlowReadSharedWRAM{0};
    std::atomic<uint64_t> ARM9SlowReadSharedWRAMNs{0};
    std::atomic<uint64_t> ARM9SlowReadIO{0};
    std::atomic<uint64_t> ARM9SlowReadIONs{0};
    std::atomic<uint64_t> ARM9SlowReadIODispStat{0};
    std::atomic<uint64_t> ARM9SlowReadIODispStatNs{0};
    std::atomic<uint64_t> ARM9SlowReadIODMA{0};
    std::atomic<uint64_t> ARM9SlowReadIODMANs{0};
    std::atomic<uint64_t> ARM9SlowReadIOTimer{0};
    std::atomic<uint64_t> ARM9SlowReadIOTimerNs{0};
    std::atomic<uint64_t> ARM9SlowReadIOKey{0};
    std::atomic<uint64_t> ARM9SlowReadIOKeyNs{0};
    std::atomic<uint64_t> ARM9SlowReadIOIPC{0};
    std::atomic<uint64_t> ARM9SlowReadIOIPCNs{0};
    std::atomic<uint64_t> ARM9SlowReadIOCart{0};
    std::atomic<uint64_t> ARM9SlowReadIOCartNs{0};
    std::atomic<uint64_t> ARM9SlowReadIOIRQ{0};
    std::atomic<uint64_t> ARM9SlowReadIOIRQNs{0};
    std::atomic<uint64_t> ARM9SlowReadIOVRAMCtl{0};
    std::atomic<uint64_t> ARM9SlowReadIOVRAMCtlNs{0};
    std::atomic<uint64_t> ARM9SlowReadIODivSqrt{0};
    std::atomic<uint64_t> ARM9SlowReadIODivSqrtNs{0};
    std::atomic<uint64_t> ARM9SlowReadIOOther{0};
    std::atomic<uint64_t> ARM9SlowReadIOOtherNs{0};
    std::atomic<uint64_t> ARM9SlowReadVRAM{0};
    std::atomic<uint64_t> ARM9SlowReadVRAMNs{0};
    std::atomic<uint64_t> ARM9SlowReadOther{0};
    std::atomic<uint64_t> ARM9SlowReadOtherNs{0};
    std::atomic<uint64_t> ARM9SlowWriteMainRAM{0};
    std::atomic<uint64_t> ARM9SlowWriteMainRAMNs{0};
    std::atomic<uint64_t> ARM9SlowWriteSharedWRAM{0};
    std::atomic<uint64_t> ARM9SlowWriteSharedWRAMNs{0};
    std::atomic<uint64_t> ARM9SlowWriteIO{0};
    std::atomic<uint64_t> ARM9SlowWriteIONs{0};
    std::atomic<uint64_t> ARM9SlowWriteVRAM{0};
    std::atomic<uint64_t> ARM9SlowWriteVRAMNs{0};
    std::atomic<uint64_t> ARM9SlowWriteOther{0};
    std::atomic<uint64_t> ARM9SlowWriteOtherNs{0};
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

inline uint64_t CounterFreqHz()
{
    if (!kEnabled)
        return 1000000000ULL;
    static uint64_t freq = []() -> uint64_t
    {
#if defined(__aarch64__)
        uint64_t value = 0;
        asm volatile("mrs %0, cntfrq_el0" : "=r"(value));
        return value ? value : 1000000000ULL;
#else
        return 1000000000ULL;
#endif
    }();
    return freq;
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
    gFrame.ARM9JitSetupRegionNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitHLECheckNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitExecuteBodyNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitPostDispatchNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitDispatchCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitCompileCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitLookupCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitSetupRegionCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitSetupRegionFails.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitHLECheckCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitGuestCycles.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitLastBlockHits.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecInstrs.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecARMALU.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecARMMul.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecARMSingleLoad.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecARMSingleStore.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecARMBlockLoad.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecARMBlockStore.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecARMStackBlockLoad.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecARMStackBlockStore.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecARMStackLoad.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecARMStackStore.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecARMBranchImm.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecARMBranchReg.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecARMSys.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecARMOther.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecThumbALU.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecThumbMul.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecThumbSingleLoad.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecThumbSingleStore.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecThumbBlockLoad.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecThumbBlockStore.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecThumbStackLoad.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecThumbStackStore.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecThumbBranchCond.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecThumbBranchImm.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecThumbBranchReg.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecThumbSys.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecThumbOther.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecLiteralLoads.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecMemITCM.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecMemDTCM.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecMemMainRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecMemSharedWRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecMemIO.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecMemVRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9ExecMemOther.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitChainAttempts.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitChainHits.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitChainMissStop.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitChainMissBudget.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitChainMissTarget.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitChainMissHLE.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitChainMissSetup.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitChainMissLookup.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnsNormal.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnsStop.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnsIdle.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnsHalt.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnEndBlock.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnEndBranch.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnEndCondBranch.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnEndOther.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnEndARMImm.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnEndARMReg.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnEndARMRegARM.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnEndARMRegThumb.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnEndARMRegLR.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnEndARMRegOther.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnEndARMRegLink.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnEndThumbCond.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnEndThumbImm.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnEndThumbReg.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnMaxBlock.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnMaxARM.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnMaxThumb.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnMaxWithMemory.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnMaxNoMemory.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnMaxWithLoad.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnMaxWithStore.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnMaxITCM.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnMaxDTCM.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnMaxMainRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnMaxSharedWRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnMaxIO.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnMaxVRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnMaxOtherMem.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnIRQBoundary.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitReturnHaltBoundary.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitBlockCacheHits.store(0, std::memory_order_relaxed);
    gFrame.ARM9JitBlockCacheMisses.store(0, std::memory_order_relaxed);
    gFrame.ARM9LibHLEHits.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockTransferNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockTransferCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockTransferReads.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockTransferWrites.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericStoreCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStackLoadCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStoreCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericStoreNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadTotalNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericStoreTotalNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStackLoadNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStoreNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStackLoadTotalNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStoreTotalNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStackLoadDirectNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStackLoadFallbackNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStoreDirectNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStoreFallbackNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStackPreTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStackWrapTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStackPostTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStackPackTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStackSaveTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStackCallTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStackRestoreTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStackUnpackTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStorePreTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStoreWrapTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStorePostTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStorePackTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStoreSaveTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStoreCallTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStoreRestoreTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteFastStoreUnpackTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericLoadPreTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericLoadWrapTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericLoadPostTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericLoadPackTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericLoadSaveTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericLoadCallTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericLoadRestoreTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericLoadUnpackTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericStorePreTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericStoreWrapTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericStorePostTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericStorePackTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericStoreSaveTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericStoreCallTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericStoreRestoreTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockCallsiteGenericStoreUnpackTicks.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStackLoad_1_2.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStackLoad_3_4.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStackLoad_5_8.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStackLoad_9P.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStore_1_2.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStore_3_4.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStore_5_8.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStore_9P.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStackLoad_1_2_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStackLoad_3_4_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStackLoad_5_8_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStackLoad_9P_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStore_1_2_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStore_3_4_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStore_5_8_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastStore_9P_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadFastmemOff.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadUsermode.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadNonFastPathShape.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadCondIncompatible.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadTargetDTCM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadTargetMainRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadTargetSharedWRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadTargetIO.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadTargetOther.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadShapeARM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadShapeThumb.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadShapeSkipBase.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadShapeKeepBase.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadShapeBaseSP.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadShapeBaseOther.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadShapeRegs1_2.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadShapeRegs3_4.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadShapeRegs5P.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadShapeIncPre.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadShapeIncPost.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadShapeDecPre.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericLoadShapeDecPost.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericStoreFastmemOff.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericStoreUsermode.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockGenericStoreCondIncompatible.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockPrePathNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastDTCMSetupNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastMainRAMSetupNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastDTCMNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastMainRAMNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFallbackLoopNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastDTCMCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastDTCMDirectCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastDTCMFallbackCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastDTCMDirectNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastDTCMFallbackNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFastMainRAMCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockFallbackLoopCalls.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadDTCM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadMainRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadSharedWRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadIO.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadOther.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadDTCM_1_2.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadDTCM_3_4.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadDTCM_5_8.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadDTCM_9_16.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadDTCM_17P.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadDTCM_1_2_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadDTCM_3_4_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadDTCM_5_8_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadDTCM_9_16_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadDTCM_17P_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadMainRAM_1_2.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadMainRAM_3_4.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadMainRAM_5_8.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadMainRAM_9_16.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockReadMainRAM_17P.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteDTCM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteMainRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteSharedWRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteIO.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteOther.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteDTCM_1_2.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteDTCM_3_4.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteDTCM_5_8.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteDTCM_9_16.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteDTCM_17P.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteDTCM_1_2_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteDTCM_3_4_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteDTCM_5_8_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteDTCM_9_16_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteDTCM_17P_Ns.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteMainRAM_1_2.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteMainRAM_3_4.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteMainRAM_5_8.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteMainRAM_9_16.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowBlockWriteMainRAM_17P.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadMainRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadMainRAMNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadSharedWRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadSharedWRAMNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIO.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIONs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIODispStat.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIODispStatNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIODMA.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIODMANs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOTimer.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOTimerNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOKey.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOKeyNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOIPC.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOIPCNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOCart.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOCartNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOIRQ.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOIRQNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOVRAMCtl.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOVRAMCtlNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIODivSqrt.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIODivSqrtNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOOther.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadIOOtherNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadVRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadVRAMNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadOther.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowReadOtherNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteMainRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteMainRAMNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteSharedWRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteSharedWRAMNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteIO.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteIONs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteVRAM.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteVRAMNs.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteOther.store(0, std::memory_order_relaxed);
    gFrame.ARM9SlowWriteOtherNs.store(0, std::memory_order_relaxed);
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
    gWindow.ARM9JitSetupRegionNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitHLECheckNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitExecuteBodyNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitPostDispatchNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitDispatchCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitCompileCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitLookupCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitSetupRegionCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitSetupRegionFails.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitHLECheckCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitGuestCycles.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitLastBlockHits.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecInstrs.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecARMALU.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecARMMul.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecARMSingleLoad.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecARMSingleStore.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecARMBlockLoad.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecARMBlockStore.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecARMStackBlockLoad.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecARMStackBlockStore.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecARMStackLoad.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecARMStackStore.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecARMBranchImm.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecARMBranchReg.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecARMSys.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecARMOther.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecThumbALU.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecThumbMul.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecThumbSingleLoad.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecThumbSingleStore.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecThumbBlockLoad.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecThumbBlockStore.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecThumbStackLoad.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecThumbStackStore.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecThumbBranchCond.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecThumbBranchImm.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecThumbBranchReg.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecThumbSys.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecThumbOther.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecLiteralLoads.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecMemITCM.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecMemDTCM.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecMemMainRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecMemSharedWRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecMemIO.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecMemVRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9ExecMemOther.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitChainAttempts.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitChainHits.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitChainMissStop.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitChainMissBudget.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitChainMissTarget.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitChainMissHLE.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitChainMissSetup.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitChainMissLookup.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnsNormal.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnsStop.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnsIdle.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnsHalt.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnEndBlock.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnEndBranch.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnEndCondBranch.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnEndOther.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnEndARMImm.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnEndARMReg.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnEndARMRegARM.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnEndARMRegThumb.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnEndARMRegLR.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnEndARMRegOther.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnEndARMRegLink.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnEndThumbCond.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnEndThumbImm.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnEndThumbReg.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnMaxBlock.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnMaxARM.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnMaxThumb.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnMaxWithMemory.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnMaxNoMemory.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnMaxWithLoad.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnMaxWithStore.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnMaxITCM.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnMaxDTCM.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnMaxMainRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnMaxSharedWRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnMaxIO.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnMaxVRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnMaxOtherMem.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnIRQBoundary.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitReturnHaltBoundary.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitBlockCacheHits.store(0, std::memory_order_relaxed);
    gWindow.ARM9JitBlockCacheMisses.store(0, std::memory_order_relaxed);
    gWindow.ARM9LibHLEHits.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockTransferNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockTransferCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockTransferReads.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockTransferWrites.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericStoreCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStackLoadCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStoreCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericStoreNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadTotalNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericStoreTotalNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStackLoadNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStoreNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStackLoadTotalNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStoreTotalNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStackLoadDirectNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStackLoadFallbackNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStoreDirectNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStoreFallbackNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStackPreTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStackWrapTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStackPostTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStackPackTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStackSaveTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStackCallTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStackRestoreTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStackUnpackTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStorePreTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStoreWrapTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStorePostTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStorePackTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStoreSaveTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStoreCallTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStoreRestoreTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteFastStoreUnpackTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericLoadPreTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericLoadWrapTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericLoadPostTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericLoadPackTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericLoadSaveTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericLoadCallTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericLoadRestoreTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericLoadUnpackTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericStorePreTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericStoreWrapTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericStorePostTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericStorePackTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericStoreSaveTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericStoreCallTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericStoreRestoreTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockCallsiteGenericStoreUnpackTicks.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStackLoad_1_2.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStackLoad_3_4.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStackLoad_5_8.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStackLoad_9P.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStore_1_2.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStore_3_4.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStore_5_8.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStore_9P.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStackLoad_1_2_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStackLoad_3_4_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStackLoad_5_8_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStackLoad_9P_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStore_1_2_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStore_3_4_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStore_5_8_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastStore_9P_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadFastmemOff.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadUsermode.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadNonFastPathShape.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadCondIncompatible.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadTargetDTCM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadTargetMainRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadTargetSharedWRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadTargetIO.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadTargetOther.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadShapeARM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadShapeThumb.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadShapeSkipBase.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadShapeKeepBase.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadShapeBaseSP.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadShapeBaseOther.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadShapeRegs1_2.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadShapeRegs3_4.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadShapeRegs5P.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadShapeIncPre.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadShapeIncPost.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadShapeDecPre.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericLoadShapeDecPost.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericStoreFastmemOff.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericStoreUsermode.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockGenericStoreCondIncompatible.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockPrePathNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastDTCMSetupNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastMainRAMSetupNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastDTCMNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastMainRAMNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFallbackLoopNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastDTCMCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastDTCMDirectCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastDTCMFallbackCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastDTCMDirectNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastDTCMFallbackNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFastMainRAMCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockFallbackLoopCalls.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadDTCM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadMainRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadSharedWRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadIO.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadOther.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadDTCM_1_2.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadDTCM_3_4.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadDTCM_5_8.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadDTCM_9_16.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadDTCM_17P.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadDTCM_1_2_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadDTCM_3_4_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadDTCM_5_8_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadDTCM_9_16_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadDTCM_17P_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadMainRAM_1_2.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadMainRAM_3_4.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadMainRAM_5_8.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadMainRAM_9_16.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockReadMainRAM_17P.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteDTCM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteMainRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteSharedWRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteIO.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteOther.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteDTCM_1_2.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteDTCM_3_4.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteDTCM_5_8.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteDTCM_9_16.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteDTCM_17P.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteDTCM_1_2_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteDTCM_3_4_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteDTCM_5_8_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteDTCM_9_16_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteDTCM_17P_Ns.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteMainRAM_1_2.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteMainRAM_3_4.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteMainRAM_5_8.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteMainRAM_9_16.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowBlockWriteMainRAM_17P.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadMainRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadMainRAMNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadSharedWRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadSharedWRAMNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIO.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIONs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIODispStat.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIODispStatNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIODMA.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIODMANs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOTimer.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOTimerNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOKey.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOKeyNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOIPC.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOIPCNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOCart.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOCartNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOIRQ.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOIRQNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOVRAMCtl.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOVRAMCtlNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIODivSqrt.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIODivSqrtNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOOther.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadIOOtherNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadVRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadVRAMNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadOther.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowReadOtherNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteMainRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteMainRAMNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteSharedWRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteSharedWRAMNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteIO.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteIONs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteVRAM.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteVRAMNs.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteOther.store(0, std::memory_order_relaxed);
    gWindow.ARM9SlowWriteOtherNs.store(0, std::memory_order_relaxed);
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

inline uint64_t CounterValue(const std::atomic<uint64_t>& counter)
{
    return counter.load(std::memory_order_relaxed);
}

inline double TicksValuePerFrameToMs(uint64_t ticks)
{
    return (static_cast<double>(ticks) /
            static_cast<double>(kLogEveryFrames) * 1000.0) /
           static_cast<double>(CounterFreqHz());
}

inline double TicksPerFrameToMs(const std::atomic<uint64_t>& counter)
{
    return TicksValuePerFrameToMs(CounterValue(counter));
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
    MergeCounter(gWindow.ARM9JitSetupRegionNs, gFrame.ARM9JitSetupRegionNs);
    MergeCounter(gWindow.ARM9JitHLECheckNs, gFrame.ARM9JitHLECheckNs);
    MergeCounter(gWindow.ARM9JitExecuteBodyNs, gFrame.ARM9JitExecuteBodyNs);
    MergeCounter(gWindow.ARM9JitPostDispatchNs, gFrame.ARM9JitPostDispatchNs);
    MergeCounter(gWindow.ARM9JitDispatchCalls, gFrame.ARM9JitDispatchCalls);
    MergeCounter(gWindow.ARM9JitCompileCalls, gFrame.ARM9JitCompileCalls);
    MergeCounter(gWindow.ARM9JitLookupCalls, gFrame.ARM9JitLookupCalls);
    MergeCounter(gWindow.ARM9JitSetupRegionCalls, gFrame.ARM9JitSetupRegionCalls);
    MergeCounter(gWindow.ARM9JitSetupRegionFails, gFrame.ARM9JitSetupRegionFails);
    MergeCounter(gWindow.ARM9JitHLECheckCalls, gFrame.ARM9JitHLECheckCalls);
    MergeCounter(gWindow.ARM9JitGuestCycles, gFrame.ARM9JitGuestCycles);
    MergeCounter(gWindow.ARM9JitLastBlockHits, gFrame.ARM9JitLastBlockHits);
    MergeCounter(gWindow.ARM9ExecInstrs, gFrame.ARM9ExecInstrs);
    MergeCounter(gWindow.ARM9ExecARMALU, gFrame.ARM9ExecARMALU);
    MergeCounter(gWindow.ARM9ExecARMMul, gFrame.ARM9ExecARMMul);
    MergeCounter(gWindow.ARM9ExecARMSingleLoad, gFrame.ARM9ExecARMSingleLoad);
    MergeCounter(gWindow.ARM9ExecARMSingleStore, gFrame.ARM9ExecARMSingleStore);
    MergeCounter(gWindow.ARM9ExecARMBlockLoad, gFrame.ARM9ExecARMBlockLoad);
    MergeCounter(gWindow.ARM9ExecARMBlockStore, gFrame.ARM9ExecARMBlockStore);
    MergeCounter(gWindow.ARM9ExecARMStackBlockLoad, gFrame.ARM9ExecARMStackBlockLoad);
    MergeCounter(gWindow.ARM9ExecARMStackBlockStore, gFrame.ARM9ExecARMStackBlockStore);
    MergeCounter(gWindow.ARM9ExecARMStackLoad, gFrame.ARM9ExecARMStackLoad);
    MergeCounter(gWindow.ARM9ExecARMStackStore, gFrame.ARM9ExecARMStackStore);
    MergeCounter(gWindow.ARM9ExecARMBranchImm, gFrame.ARM9ExecARMBranchImm);
    MergeCounter(gWindow.ARM9ExecARMBranchReg, gFrame.ARM9ExecARMBranchReg);
    MergeCounter(gWindow.ARM9ExecARMSys, gFrame.ARM9ExecARMSys);
    MergeCounter(gWindow.ARM9ExecARMOther, gFrame.ARM9ExecARMOther);
    MergeCounter(gWindow.ARM9ExecThumbALU, gFrame.ARM9ExecThumbALU);
    MergeCounter(gWindow.ARM9ExecThumbMul, gFrame.ARM9ExecThumbMul);
    MergeCounter(gWindow.ARM9ExecThumbSingleLoad, gFrame.ARM9ExecThumbSingleLoad);
    MergeCounter(gWindow.ARM9ExecThumbSingleStore, gFrame.ARM9ExecThumbSingleStore);
    MergeCounter(gWindow.ARM9ExecThumbBlockLoad, gFrame.ARM9ExecThumbBlockLoad);
    MergeCounter(gWindow.ARM9ExecThumbBlockStore, gFrame.ARM9ExecThumbBlockStore);
    MergeCounter(gWindow.ARM9ExecThumbStackLoad, gFrame.ARM9ExecThumbStackLoad);
    MergeCounter(gWindow.ARM9ExecThumbStackStore, gFrame.ARM9ExecThumbStackStore);
    MergeCounter(gWindow.ARM9ExecThumbBranchCond, gFrame.ARM9ExecThumbBranchCond);
    MergeCounter(gWindow.ARM9ExecThumbBranchImm, gFrame.ARM9ExecThumbBranchImm);
    MergeCounter(gWindow.ARM9ExecThumbBranchReg, gFrame.ARM9ExecThumbBranchReg);
    MergeCounter(gWindow.ARM9ExecThumbSys, gFrame.ARM9ExecThumbSys);
    MergeCounter(gWindow.ARM9ExecThumbOther, gFrame.ARM9ExecThumbOther);
    MergeCounter(gWindow.ARM9ExecLiteralLoads, gFrame.ARM9ExecLiteralLoads);
    MergeCounter(gWindow.ARM9ExecMemITCM, gFrame.ARM9ExecMemITCM);
    MergeCounter(gWindow.ARM9ExecMemDTCM, gFrame.ARM9ExecMemDTCM);
    MergeCounter(gWindow.ARM9ExecMemMainRAM, gFrame.ARM9ExecMemMainRAM);
    MergeCounter(gWindow.ARM9ExecMemSharedWRAM, gFrame.ARM9ExecMemSharedWRAM);
    MergeCounter(gWindow.ARM9ExecMemIO, gFrame.ARM9ExecMemIO);
    MergeCounter(gWindow.ARM9ExecMemVRAM, gFrame.ARM9ExecMemVRAM);
    MergeCounter(gWindow.ARM9ExecMemOther, gFrame.ARM9ExecMemOther);
    MergeCounter(gWindow.ARM9JitChainAttempts, gFrame.ARM9JitChainAttempts);
    MergeCounter(gWindow.ARM9JitChainHits, gFrame.ARM9JitChainHits);
    MergeCounter(gWindow.ARM9JitChainMissStop, gFrame.ARM9JitChainMissStop);
    MergeCounter(gWindow.ARM9JitChainMissBudget, gFrame.ARM9JitChainMissBudget);
    MergeCounter(gWindow.ARM9JitChainMissTarget, gFrame.ARM9JitChainMissTarget);
    MergeCounter(gWindow.ARM9JitChainMissHLE, gFrame.ARM9JitChainMissHLE);
    MergeCounter(gWindow.ARM9JitChainMissSetup, gFrame.ARM9JitChainMissSetup);
    MergeCounter(gWindow.ARM9JitChainMissLookup, gFrame.ARM9JitChainMissLookup);
    MergeCounter(gWindow.ARM9JitReturnsNormal, gFrame.ARM9JitReturnsNormal);
    MergeCounter(gWindow.ARM9JitReturnsStop, gFrame.ARM9JitReturnsStop);
    MergeCounter(gWindow.ARM9JitReturnsIdle, gFrame.ARM9JitReturnsIdle);
    MergeCounter(gWindow.ARM9JitReturnsHalt, gFrame.ARM9JitReturnsHalt);
    MergeCounter(gWindow.ARM9JitReturnEndBlock, gFrame.ARM9JitReturnEndBlock);
    MergeCounter(gWindow.ARM9JitReturnEndBranch, gFrame.ARM9JitReturnEndBranch);
    MergeCounter(gWindow.ARM9JitReturnEndCondBranch, gFrame.ARM9JitReturnEndCondBranch);
    MergeCounter(gWindow.ARM9JitReturnEndOther, gFrame.ARM9JitReturnEndOther);
    MergeCounter(gWindow.ARM9JitReturnEndARMImm, gFrame.ARM9JitReturnEndARMImm);
    MergeCounter(gWindow.ARM9JitReturnEndARMReg, gFrame.ARM9JitReturnEndARMReg);
    MergeCounter(gWindow.ARM9JitReturnEndARMRegARM, gFrame.ARM9JitReturnEndARMRegARM);
    MergeCounter(gWindow.ARM9JitReturnEndARMRegThumb, gFrame.ARM9JitReturnEndARMRegThumb);
    MergeCounter(gWindow.ARM9JitReturnEndARMRegLR, gFrame.ARM9JitReturnEndARMRegLR);
    MergeCounter(gWindow.ARM9JitReturnEndARMRegOther, gFrame.ARM9JitReturnEndARMRegOther);
    MergeCounter(gWindow.ARM9JitReturnEndARMRegLink, gFrame.ARM9JitReturnEndARMRegLink);
    MergeCounter(gWindow.ARM9JitReturnEndThumbCond, gFrame.ARM9JitReturnEndThumbCond);
    MergeCounter(gWindow.ARM9JitReturnEndThumbImm, gFrame.ARM9JitReturnEndThumbImm);
    MergeCounter(gWindow.ARM9JitReturnEndThumbReg, gFrame.ARM9JitReturnEndThumbReg);
    MergeCounter(gWindow.ARM9JitReturnMaxBlock, gFrame.ARM9JitReturnMaxBlock);
    MergeCounter(gWindow.ARM9JitReturnMaxARM, gFrame.ARM9JitReturnMaxARM);
    MergeCounter(gWindow.ARM9JitReturnMaxThumb, gFrame.ARM9JitReturnMaxThumb);
    MergeCounter(gWindow.ARM9JitReturnMaxWithMemory, gFrame.ARM9JitReturnMaxWithMemory);
    MergeCounter(gWindow.ARM9JitReturnMaxNoMemory, gFrame.ARM9JitReturnMaxNoMemory);
    MergeCounter(gWindow.ARM9JitReturnMaxWithLoad, gFrame.ARM9JitReturnMaxWithLoad);
    MergeCounter(gWindow.ARM9JitReturnMaxWithStore, gFrame.ARM9JitReturnMaxWithStore);
    MergeCounter(gWindow.ARM9JitReturnMaxITCM, gFrame.ARM9JitReturnMaxITCM);
    MergeCounter(gWindow.ARM9JitReturnMaxDTCM, gFrame.ARM9JitReturnMaxDTCM);
    MergeCounter(gWindow.ARM9JitReturnMaxMainRAM, gFrame.ARM9JitReturnMaxMainRAM);
    MergeCounter(gWindow.ARM9JitReturnMaxSharedWRAM, gFrame.ARM9JitReturnMaxSharedWRAM);
    MergeCounter(gWindow.ARM9JitReturnMaxIO, gFrame.ARM9JitReturnMaxIO);
    MergeCounter(gWindow.ARM9JitReturnMaxVRAM, gFrame.ARM9JitReturnMaxVRAM);
    MergeCounter(gWindow.ARM9JitReturnMaxOtherMem, gFrame.ARM9JitReturnMaxOtherMem);
    MergeCounter(gWindow.ARM9JitReturnIRQBoundary, gFrame.ARM9JitReturnIRQBoundary);
    MergeCounter(gWindow.ARM9JitReturnHaltBoundary, gFrame.ARM9JitReturnHaltBoundary);
    MergeCounter(gWindow.ARM9JitBlockCacheHits, gFrame.ARM9JitBlockCacheHits);
    MergeCounter(gWindow.ARM9JitBlockCacheMisses, gFrame.ARM9JitBlockCacheMisses);
    MergeCounter(gWindow.ARM9LibHLEHits, gFrame.ARM9LibHLEHits);
    MergeCounter(gWindow.ARM9SlowReadNs, gFrame.ARM9SlowReadNs);
    MergeCounter(gWindow.ARM9SlowWriteNs, gFrame.ARM9SlowWriteNs);
    MergeCounter(gWindow.ARM9SlowBlockTransferNs, gFrame.ARM9SlowBlockTransferNs);
    MergeCounter(gWindow.ARM9SlowReadCalls, gFrame.ARM9SlowReadCalls);
    MergeCounter(gWindow.ARM9SlowWriteCalls, gFrame.ARM9SlowWriteCalls);
    MergeCounter(gWindow.ARM9SlowBlockTransferCalls, gFrame.ARM9SlowBlockTransferCalls);
    MergeCounter(gWindow.ARM9SlowBlockTransferReads, gFrame.ARM9SlowBlockTransferReads);
    MergeCounter(gWindow.ARM9SlowBlockTransferWrites, gFrame.ARM9SlowBlockTransferWrites);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadCalls, gFrame.ARM9SlowBlockGenericLoadCalls);
    MergeCounter(gWindow.ARM9SlowBlockGenericStoreCalls, gFrame.ARM9SlowBlockGenericStoreCalls);
    MergeCounter(gWindow.ARM9SlowBlockFastStackLoadCalls, gFrame.ARM9SlowBlockFastStackLoadCalls);
    MergeCounter(gWindow.ARM9SlowBlockFastStoreCalls, gFrame.ARM9SlowBlockFastStoreCalls);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadNs, gFrame.ARM9SlowBlockGenericLoadNs);
    MergeCounter(gWindow.ARM9SlowBlockGenericStoreNs, gFrame.ARM9SlowBlockGenericStoreNs);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadTotalNs, gFrame.ARM9SlowBlockGenericLoadTotalNs);
    MergeCounter(gWindow.ARM9SlowBlockGenericStoreTotalNs, gFrame.ARM9SlowBlockGenericStoreTotalNs);
    MergeCounter(gWindow.ARM9SlowBlockFastStackLoadNs, gFrame.ARM9SlowBlockFastStackLoadNs);
    MergeCounter(gWindow.ARM9SlowBlockFastStoreNs, gFrame.ARM9SlowBlockFastStoreNs);
    MergeCounter(gWindow.ARM9SlowBlockFastStackLoadTotalNs, gFrame.ARM9SlowBlockFastStackLoadTotalNs);
    MergeCounter(gWindow.ARM9SlowBlockFastStoreTotalNs, gFrame.ARM9SlowBlockFastStoreTotalNs);
    MergeCounter(gWindow.ARM9SlowBlockFastStackLoadDirectNs, gFrame.ARM9SlowBlockFastStackLoadDirectNs);
    MergeCounter(gWindow.ARM9SlowBlockFastStackLoadFallbackNs, gFrame.ARM9SlowBlockFastStackLoadFallbackNs);
    MergeCounter(gWindow.ARM9SlowBlockFastStoreDirectNs, gFrame.ARM9SlowBlockFastStoreDirectNs);
    MergeCounter(gWindow.ARM9SlowBlockFastStoreFallbackNs, gFrame.ARM9SlowBlockFastStoreFallbackNs);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStackPreTicks, gFrame.ARM9SlowBlockCallsiteFastStackPreTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStackWrapTicks, gFrame.ARM9SlowBlockCallsiteFastStackWrapTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStackPostTicks, gFrame.ARM9SlowBlockCallsiteFastStackPostTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStackPackTicks, gFrame.ARM9SlowBlockCallsiteFastStackPackTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStackSaveTicks, gFrame.ARM9SlowBlockCallsiteFastStackSaveTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStackCallTicks, gFrame.ARM9SlowBlockCallsiteFastStackCallTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStackRestoreTicks, gFrame.ARM9SlowBlockCallsiteFastStackRestoreTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStackUnpackTicks, gFrame.ARM9SlowBlockCallsiteFastStackUnpackTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStorePreTicks, gFrame.ARM9SlowBlockCallsiteFastStorePreTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStoreWrapTicks, gFrame.ARM9SlowBlockCallsiteFastStoreWrapTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStorePostTicks, gFrame.ARM9SlowBlockCallsiteFastStorePostTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStorePackTicks, gFrame.ARM9SlowBlockCallsiteFastStorePackTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStoreSaveTicks, gFrame.ARM9SlowBlockCallsiteFastStoreSaveTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStoreCallTicks, gFrame.ARM9SlowBlockCallsiteFastStoreCallTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStoreRestoreTicks, gFrame.ARM9SlowBlockCallsiteFastStoreRestoreTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteFastStoreUnpackTicks, gFrame.ARM9SlowBlockCallsiteFastStoreUnpackTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericLoadPreTicks, gFrame.ARM9SlowBlockCallsiteGenericLoadPreTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericLoadWrapTicks, gFrame.ARM9SlowBlockCallsiteGenericLoadWrapTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericLoadPostTicks, gFrame.ARM9SlowBlockCallsiteGenericLoadPostTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericLoadPackTicks, gFrame.ARM9SlowBlockCallsiteGenericLoadPackTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericLoadSaveTicks, gFrame.ARM9SlowBlockCallsiteGenericLoadSaveTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericLoadCallTicks, gFrame.ARM9SlowBlockCallsiteGenericLoadCallTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericLoadRestoreTicks, gFrame.ARM9SlowBlockCallsiteGenericLoadRestoreTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericLoadUnpackTicks, gFrame.ARM9SlowBlockCallsiteGenericLoadUnpackTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericStorePreTicks, gFrame.ARM9SlowBlockCallsiteGenericStorePreTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericStoreWrapTicks, gFrame.ARM9SlowBlockCallsiteGenericStoreWrapTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericStorePostTicks, gFrame.ARM9SlowBlockCallsiteGenericStorePostTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericStorePackTicks, gFrame.ARM9SlowBlockCallsiteGenericStorePackTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericStoreSaveTicks, gFrame.ARM9SlowBlockCallsiteGenericStoreSaveTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericStoreCallTicks, gFrame.ARM9SlowBlockCallsiteGenericStoreCallTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericStoreRestoreTicks, gFrame.ARM9SlowBlockCallsiteGenericStoreRestoreTicks);
    MergeCounter(gWindow.ARM9SlowBlockCallsiteGenericStoreUnpackTicks, gFrame.ARM9SlowBlockCallsiteGenericStoreUnpackTicks);
    MergeCounter(gWindow.ARM9SlowBlockFastStackLoad_1_2, gFrame.ARM9SlowBlockFastStackLoad_1_2);
    MergeCounter(gWindow.ARM9SlowBlockFastStackLoad_3_4, gFrame.ARM9SlowBlockFastStackLoad_3_4);
    MergeCounter(gWindow.ARM9SlowBlockFastStackLoad_5_8, gFrame.ARM9SlowBlockFastStackLoad_5_8);
    MergeCounter(gWindow.ARM9SlowBlockFastStackLoad_9P, gFrame.ARM9SlowBlockFastStackLoad_9P);
    MergeCounter(gWindow.ARM9SlowBlockFastStore_1_2, gFrame.ARM9SlowBlockFastStore_1_2);
    MergeCounter(gWindow.ARM9SlowBlockFastStore_3_4, gFrame.ARM9SlowBlockFastStore_3_4);
    MergeCounter(gWindow.ARM9SlowBlockFastStore_5_8, gFrame.ARM9SlowBlockFastStore_5_8);
    MergeCounter(gWindow.ARM9SlowBlockFastStore_9P, gFrame.ARM9SlowBlockFastStore_9P);
    MergeCounter(gWindow.ARM9SlowBlockFastStackLoad_1_2_Ns, gFrame.ARM9SlowBlockFastStackLoad_1_2_Ns);
    MergeCounter(gWindow.ARM9SlowBlockFastStackLoad_3_4_Ns, gFrame.ARM9SlowBlockFastStackLoad_3_4_Ns);
    MergeCounter(gWindow.ARM9SlowBlockFastStackLoad_5_8_Ns, gFrame.ARM9SlowBlockFastStackLoad_5_8_Ns);
    MergeCounter(gWindow.ARM9SlowBlockFastStackLoad_9P_Ns, gFrame.ARM9SlowBlockFastStackLoad_9P_Ns);
    MergeCounter(gWindow.ARM9SlowBlockFastStore_1_2_Ns, gFrame.ARM9SlowBlockFastStore_1_2_Ns);
    MergeCounter(gWindow.ARM9SlowBlockFastStore_3_4_Ns, gFrame.ARM9SlowBlockFastStore_3_4_Ns);
    MergeCounter(gWindow.ARM9SlowBlockFastStore_5_8_Ns, gFrame.ARM9SlowBlockFastStore_5_8_Ns);
    MergeCounter(gWindow.ARM9SlowBlockFastStore_9P_Ns, gFrame.ARM9SlowBlockFastStore_9P_Ns);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadFastmemOff, gFrame.ARM9SlowBlockGenericLoadFastmemOff);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadUsermode, gFrame.ARM9SlowBlockGenericLoadUsermode);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadNonFastPathShape, gFrame.ARM9SlowBlockGenericLoadNonFastPathShape);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadCondIncompatible, gFrame.ARM9SlowBlockGenericLoadCondIncompatible);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadTargetDTCM, gFrame.ARM9SlowBlockGenericLoadTargetDTCM);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadTargetMainRAM, gFrame.ARM9SlowBlockGenericLoadTargetMainRAM);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadTargetSharedWRAM, gFrame.ARM9SlowBlockGenericLoadTargetSharedWRAM);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadTargetIO, gFrame.ARM9SlowBlockGenericLoadTargetIO);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadTargetOther, gFrame.ARM9SlowBlockGenericLoadTargetOther);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadShapeARM, gFrame.ARM9SlowBlockGenericLoadShapeARM);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadShapeThumb, gFrame.ARM9SlowBlockGenericLoadShapeThumb);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadShapeSkipBase, gFrame.ARM9SlowBlockGenericLoadShapeSkipBase);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadShapeKeepBase, gFrame.ARM9SlowBlockGenericLoadShapeKeepBase);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadShapeBaseSP, gFrame.ARM9SlowBlockGenericLoadShapeBaseSP);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadShapeBaseOther, gFrame.ARM9SlowBlockGenericLoadShapeBaseOther);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadShapeRegs1_2, gFrame.ARM9SlowBlockGenericLoadShapeRegs1_2);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadShapeRegs3_4, gFrame.ARM9SlowBlockGenericLoadShapeRegs3_4);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadShapeRegs5P, gFrame.ARM9SlowBlockGenericLoadShapeRegs5P);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadShapeIncPre, gFrame.ARM9SlowBlockGenericLoadShapeIncPre);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadShapeIncPost, gFrame.ARM9SlowBlockGenericLoadShapeIncPost);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadShapeDecPre, gFrame.ARM9SlowBlockGenericLoadShapeDecPre);
    MergeCounter(gWindow.ARM9SlowBlockGenericLoadShapeDecPost, gFrame.ARM9SlowBlockGenericLoadShapeDecPost);
    MergeCounter(gWindow.ARM9SlowBlockGenericStoreFastmemOff, gFrame.ARM9SlowBlockGenericStoreFastmemOff);
    MergeCounter(gWindow.ARM9SlowBlockGenericStoreUsermode, gFrame.ARM9SlowBlockGenericStoreUsermode);
    MergeCounter(gWindow.ARM9SlowBlockGenericStoreCondIncompatible, gFrame.ARM9SlowBlockGenericStoreCondIncompatible);
    MergeCounter(gWindow.ARM9SlowBlockPrePathNs, gFrame.ARM9SlowBlockPrePathNs);
    MergeCounter(gWindow.ARM9SlowBlockFastDTCMSetupNs, gFrame.ARM9SlowBlockFastDTCMSetupNs);
    MergeCounter(gWindow.ARM9SlowBlockFastMainRAMSetupNs, gFrame.ARM9SlowBlockFastMainRAMSetupNs);
    MergeCounter(gWindow.ARM9SlowBlockFastDTCMNs, gFrame.ARM9SlowBlockFastDTCMNs);
    MergeCounter(gWindow.ARM9SlowBlockFastMainRAMNs, gFrame.ARM9SlowBlockFastMainRAMNs);
    MergeCounter(gWindow.ARM9SlowBlockFallbackLoopNs, gFrame.ARM9SlowBlockFallbackLoopNs);
    MergeCounter(gWindow.ARM9SlowBlockFastDTCMCalls, gFrame.ARM9SlowBlockFastDTCMCalls);
    MergeCounter(gWindow.ARM9SlowBlockFastDTCMDirectCalls, gFrame.ARM9SlowBlockFastDTCMDirectCalls);
    MergeCounter(gWindow.ARM9SlowBlockFastDTCMFallbackCalls, gFrame.ARM9SlowBlockFastDTCMFallbackCalls);
    MergeCounter(gWindow.ARM9SlowBlockFastDTCMDirectNs, gFrame.ARM9SlowBlockFastDTCMDirectNs);
    MergeCounter(gWindow.ARM9SlowBlockFastDTCMFallbackNs, gFrame.ARM9SlowBlockFastDTCMFallbackNs);
    MergeCounter(gWindow.ARM9SlowBlockFastMainRAMCalls, gFrame.ARM9SlowBlockFastMainRAMCalls);
    MergeCounter(gWindow.ARM9SlowBlockFallbackLoopCalls, gFrame.ARM9SlowBlockFallbackLoopCalls);
    MergeCounter(gWindow.ARM9SlowBlockReadDTCM, gFrame.ARM9SlowBlockReadDTCM);
    MergeCounter(gWindow.ARM9SlowBlockReadMainRAM, gFrame.ARM9SlowBlockReadMainRAM);
    MergeCounter(gWindow.ARM9SlowBlockReadSharedWRAM, gFrame.ARM9SlowBlockReadSharedWRAM);
    MergeCounter(gWindow.ARM9SlowBlockReadIO, gFrame.ARM9SlowBlockReadIO);
    MergeCounter(gWindow.ARM9SlowBlockReadOther, gFrame.ARM9SlowBlockReadOther);
    MergeCounter(gWindow.ARM9SlowBlockReadDTCM_1_2, gFrame.ARM9SlowBlockReadDTCM_1_2);
    MergeCounter(gWindow.ARM9SlowBlockReadDTCM_3_4, gFrame.ARM9SlowBlockReadDTCM_3_4);
    MergeCounter(gWindow.ARM9SlowBlockReadDTCM_5_8, gFrame.ARM9SlowBlockReadDTCM_5_8);
    MergeCounter(gWindow.ARM9SlowBlockReadDTCM_9_16, gFrame.ARM9SlowBlockReadDTCM_9_16);
    MergeCounter(gWindow.ARM9SlowBlockReadDTCM_17P, gFrame.ARM9SlowBlockReadDTCM_17P);
    MergeCounter(gWindow.ARM9SlowBlockReadDTCM_1_2_Ns, gFrame.ARM9SlowBlockReadDTCM_1_2_Ns);
    MergeCounter(gWindow.ARM9SlowBlockReadDTCM_3_4_Ns, gFrame.ARM9SlowBlockReadDTCM_3_4_Ns);
    MergeCounter(gWindow.ARM9SlowBlockReadDTCM_5_8_Ns, gFrame.ARM9SlowBlockReadDTCM_5_8_Ns);
    MergeCounter(gWindow.ARM9SlowBlockReadDTCM_9_16_Ns, gFrame.ARM9SlowBlockReadDTCM_9_16_Ns);
    MergeCounter(gWindow.ARM9SlowBlockReadDTCM_17P_Ns, gFrame.ARM9SlowBlockReadDTCM_17P_Ns);
    MergeCounter(gWindow.ARM9SlowBlockReadMainRAM_1_2, gFrame.ARM9SlowBlockReadMainRAM_1_2);
    MergeCounter(gWindow.ARM9SlowBlockReadMainRAM_3_4, gFrame.ARM9SlowBlockReadMainRAM_3_4);
    MergeCounter(gWindow.ARM9SlowBlockReadMainRAM_5_8, gFrame.ARM9SlowBlockReadMainRAM_5_8);
    MergeCounter(gWindow.ARM9SlowBlockReadMainRAM_9_16, gFrame.ARM9SlowBlockReadMainRAM_9_16);
    MergeCounter(gWindow.ARM9SlowBlockReadMainRAM_17P, gFrame.ARM9SlowBlockReadMainRAM_17P);
    MergeCounter(gWindow.ARM9SlowBlockWriteDTCM, gFrame.ARM9SlowBlockWriteDTCM);
    MergeCounter(gWindow.ARM9SlowBlockWriteMainRAM, gFrame.ARM9SlowBlockWriteMainRAM);
    MergeCounter(gWindow.ARM9SlowBlockWriteSharedWRAM, gFrame.ARM9SlowBlockWriteSharedWRAM);
    MergeCounter(gWindow.ARM9SlowBlockWriteIO, gFrame.ARM9SlowBlockWriteIO);
    MergeCounter(gWindow.ARM9SlowBlockWriteOther, gFrame.ARM9SlowBlockWriteOther);
    MergeCounter(gWindow.ARM9SlowBlockWriteDTCM_1_2, gFrame.ARM9SlowBlockWriteDTCM_1_2);
    MergeCounter(gWindow.ARM9SlowBlockWriteDTCM_3_4, gFrame.ARM9SlowBlockWriteDTCM_3_4);
    MergeCounter(gWindow.ARM9SlowBlockWriteDTCM_5_8, gFrame.ARM9SlowBlockWriteDTCM_5_8);
    MergeCounter(gWindow.ARM9SlowBlockWriteDTCM_9_16, gFrame.ARM9SlowBlockWriteDTCM_9_16);
    MergeCounter(gWindow.ARM9SlowBlockWriteDTCM_17P, gFrame.ARM9SlowBlockWriteDTCM_17P);
    MergeCounter(gWindow.ARM9SlowBlockWriteDTCM_1_2_Ns, gFrame.ARM9SlowBlockWriteDTCM_1_2_Ns);
    MergeCounter(gWindow.ARM9SlowBlockWriteDTCM_3_4_Ns, gFrame.ARM9SlowBlockWriteDTCM_3_4_Ns);
    MergeCounter(gWindow.ARM9SlowBlockWriteDTCM_5_8_Ns, gFrame.ARM9SlowBlockWriteDTCM_5_8_Ns);
    MergeCounter(gWindow.ARM9SlowBlockWriteDTCM_9_16_Ns, gFrame.ARM9SlowBlockWriteDTCM_9_16_Ns);
    MergeCounter(gWindow.ARM9SlowBlockWriteDTCM_17P_Ns, gFrame.ARM9SlowBlockWriteDTCM_17P_Ns);
    MergeCounter(gWindow.ARM9SlowBlockWriteMainRAM_1_2, gFrame.ARM9SlowBlockWriteMainRAM_1_2);
    MergeCounter(gWindow.ARM9SlowBlockWriteMainRAM_3_4, gFrame.ARM9SlowBlockWriteMainRAM_3_4);
    MergeCounter(gWindow.ARM9SlowBlockWriteMainRAM_5_8, gFrame.ARM9SlowBlockWriteMainRAM_5_8);
    MergeCounter(gWindow.ARM9SlowBlockWriteMainRAM_9_16, gFrame.ARM9SlowBlockWriteMainRAM_9_16);
    MergeCounter(gWindow.ARM9SlowBlockWriteMainRAM_17P, gFrame.ARM9SlowBlockWriteMainRAM_17P);
    MergeCounter(gWindow.ARM9SlowReadMainRAM, gFrame.ARM9SlowReadMainRAM);
    MergeCounter(gWindow.ARM9SlowReadMainRAMNs, gFrame.ARM9SlowReadMainRAMNs);
    MergeCounter(gWindow.ARM9SlowReadSharedWRAM, gFrame.ARM9SlowReadSharedWRAM);
    MergeCounter(gWindow.ARM9SlowReadSharedWRAMNs, gFrame.ARM9SlowReadSharedWRAMNs);
    MergeCounter(gWindow.ARM9SlowReadIO, gFrame.ARM9SlowReadIO);
    MergeCounter(gWindow.ARM9SlowReadIONs, gFrame.ARM9SlowReadIONs);
    MergeCounter(gWindow.ARM9SlowReadIODispStat, gFrame.ARM9SlowReadIODispStat);
    MergeCounter(gWindow.ARM9SlowReadIODispStatNs, gFrame.ARM9SlowReadIODispStatNs);
    MergeCounter(gWindow.ARM9SlowReadIODMA, gFrame.ARM9SlowReadIODMA);
    MergeCounter(gWindow.ARM9SlowReadIODMANs, gFrame.ARM9SlowReadIODMANs);
    MergeCounter(gWindow.ARM9SlowReadIOTimer, gFrame.ARM9SlowReadIOTimer);
    MergeCounter(gWindow.ARM9SlowReadIOTimerNs, gFrame.ARM9SlowReadIOTimerNs);
    MergeCounter(gWindow.ARM9SlowReadIOKey, gFrame.ARM9SlowReadIOKey);
    MergeCounter(gWindow.ARM9SlowReadIOKeyNs, gFrame.ARM9SlowReadIOKeyNs);
    MergeCounter(gWindow.ARM9SlowReadIOIPC, gFrame.ARM9SlowReadIOIPC);
    MergeCounter(gWindow.ARM9SlowReadIOIPCNs, gFrame.ARM9SlowReadIOIPCNs);
    MergeCounter(gWindow.ARM9SlowReadIOCart, gFrame.ARM9SlowReadIOCart);
    MergeCounter(gWindow.ARM9SlowReadIOCartNs, gFrame.ARM9SlowReadIOCartNs);
    MergeCounter(gWindow.ARM9SlowReadIOIRQ, gFrame.ARM9SlowReadIOIRQ);
    MergeCounter(gWindow.ARM9SlowReadIOIRQNs, gFrame.ARM9SlowReadIOIRQNs);
    MergeCounter(gWindow.ARM9SlowReadIOVRAMCtl, gFrame.ARM9SlowReadIOVRAMCtl);
    MergeCounter(gWindow.ARM9SlowReadIOVRAMCtlNs, gFrame.ARM9SlowReadIOVRAMCtlNs);
    MergeCounter(gWindow.ARM9SlowReadIODivSqrt, gFrame.ARM9SlowReadIODivSqrt);
    MergeCounter(gWindow.ARM9SlowReadIODivSqrtNs, gFrame.ARM9SlowReadIODivSqrtNs);
    MergeCounter(gWindow.ARM9SlowReadIOOther, gFrame.ARM9SlowReadIOOther);
    MergeCounter(gWindow.ARM9SlowReadIOOtherNs, gFrame.ARM9SlowReadIOOtherNs);
    MergeCounter(gWindow.ARM9SlowReadVRAM, gFrame.ARM9SlowReadVRAM);
    MergeCounter(gWindow.ARM9SlowReadVRAMNs, gFrame.ARM9SlowReadVRAMNs);
    MergeCounter(gWindow.ARM9SlowReadOther, gFrame.ARM9SlowReadOther);
    MergeCounter(gWindow.ARM9SlowReadOtherNs, gFrame.ARM9SlowReadOtherNs);
    MergeCounter(gWindow.ARM9SlowWriteMainRAM, gFrame.ARM9SlowWriteMainRAM);
    MergeCounter(gWindow.ARM9SlowWriteMainRAMNs, gFrame.ARM9SlowWriteMainRAMNs);
    MergeCounter(gWindow.ARM9SlowWriteSharedWRAM, gFrame.ARM9SlowWriteSharedWRAM);
    MergeCounter(gWindow.ARM9SlowWriteSharedWRAMNs, gFrame.ARM9SlowWriteSharedWRAMNs);
    MergeCounter(gWindow.ARM9SlowWriteIO, gFrame.ARM9SlowWriteIO);
    MergeCounter(gWindow.ARM9SlowWriteIONs, gFrame.ARM9SlowWriteIONs);
    MergeCounter(gWindow.ARM9SlowWriteVRAM, gFrame.ARM9SlowWriteVRAM);
    MergeCounter(gWindow.ARM9SlowWriteVRAMNs, gFrame.ARM9SlowWriteVRAMNs);
    MergeCounter(gWindow.ARM9SlowWriteOther, gFrame.ARM9SlowWriteOther);
    MergeCounter(gWindow.ARM9SlowWriteOtherNs, gFrame.ARM9SlowWriteOtherNs);
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
    const uint64_t fastStackCallsiteTicks =
        CounterValue(gWindow.ARM9SlowBlockCallsiteFastStackPreTicks) +
        CounterValue(gWindow.ARM9SlowBlockCallsiteFastStackWrapTicks) +
        CounterValue(gWindow.ARM9SlowBlockCallsiteFastStackPostTicks);
    const uint64_t fastStoreCallsiteTicks =
        CounterValue(gWindow.ARM9SlowBlockCallsiteFastStorePreTicks) +
        CounterValue(gWindow.ARM9SlowBlockCallsiteFastStoreWrapTicks) +
        CounterValue(gWindow.ARM9SlowBlockCallsiteFastStorePostTicks);
    const uint64_t genericLoadCallsiteTicks =
        CounterValue(gWindow.ARM9SlowBlockCallsiteGenericLoadPreTicks) +
        CounterValue(gWindow.ARM9SlowBlockCallsiteGenericLoadWrapTicks) +
        CounterValue(gWindow.ARM9SlowBlockCallsiteGenericLoadPostTicks);
    const uint64_t genericStoreCallsiteTicks =
        CounterValue(gWindow.ARM9SlowBlockCallsiteGenericStorePreTicks) +
        CounterValue(gWindow.ARM9SlowBlockCallsiteGenericStoreWrapTicks) +
        CounterValue(gWindow.ARM9SlowBlockCallsiteGenericStorePostTicks);
    const uint64_t arm9SlowBlockCallsiteTicks =
        fastStackCallsiteTicks +
        fastStoreCallsiteTicks +
        genericLoadCallsiteTicks +
        genericStoreCallsiteTicks;

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
        "[LITEV_PROFILE] arm9_jit dispatch=%.3fms/%.1f lookup=%.3fms/%.1f compile=%.3fms/%.1f guest_cycles=%.1f last_hit=%.1f chain=%.1f/%.1f ret_normal=%.1f ret_stop=%.1f ret_idle=%.1f ret_halt=%.1f cache_hit=%.1f cache_miss=%.1f hle=%.1f slowread=%.3fms/%.1f slowwrite=%.3fms/%.1f slowblock=%.3fms/%.1f",
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
        TicksValuePerFrameToMs(arm9SlowBlockCallsiteTicks),
        CountPerFrame(gWindow.ARM9SlowBlockTransferCalls));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] arm9_dispatch_breakdown hle_check=%.3fms/%.1f setup=%.3fms/%.1f fail=%.1f execute=%.3fms post=%.3fms",
        NsPerFrame(gWindow.ARM9JitHLECheckNs),
        CountPerFrame(gWindow.ARM9JitHLECheckCalls),
        NsPerFrame(gWindow.ARM9JitSetupRegionNs),
        CountPerFrame(gWindow.ARM9JitSetupRegionCalls),
        CountPerFrame(gWindow.ARM9JitSetupRegionFails),
        NsPerFrame(gWindow.ARM9JitExecuteBodyNs),
        NsPerFrame(gWindow.ARM9JitPostDispatchNs));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] arm9_mix total=%.1f arm_alu=%.1f arm_mul=%.1f arm_ld=%.1f arm_st=%.1f arm_ldm=%.1f arm_stm=%.1f arm_stack_ld=%.1f arm_stack_st=%.1f arm_stack_ldm=%.1f arm_stack_stm=%.1f arm_bimm=%.1f arm_breg=%.1f arm_sys=%.1f arm_other=%.1f",
        CountPerFrame(gWindow.ARM9ExecInstrs),
        CountPerFrame(gWindow.ARM9ExecARMALU),
        CountPerFrame(gWindow.ARM9ExecARMMul),
        CountPerFrame(gWindow.ARM9ExecARMSingleLoad),
        CountPerFrame(gWindow.ARM9ExecARMSingleStore),
        CountPerFrame(gWindow.ARM9ExecARMBlockLoad),
        CountPerFrame(gWindow.ARM9ExecARMBlockStore),
        CountPerFrame(gWindow.ARM9ExecARMStackLoad),
        CountPerFrame(gWindow.ARM9ExecARMStackStore),
        CountPerFrame(gWindow.ARM9ExecARMStackBlockLoad),
        CountPerFrame(gWindow.ARM9ExecARMStackBlockStore),
        CountPerFrame(gWindow.ARM9ExecARMBranchImm),
        CountPerFrame(gWindow.ARM9ExecARMBranchReg),
        CountPerFrame(gWindow.ARM9ExecARMSys),
        CountPerFrame(gWindow.ARM9ExecARMOther));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] arm9_mix_thumb alu=%.1f mul=%.1f ld=%.1f st=%.1f ldm=%.1f stm=%.1f stack_ld=%.1f stack_st=%.1f bcond=%.1f bimm=%.1f breg=%.1f sys=%.1f other=%.1f literal=%.1f",
        CountPerFrame(gWindow.ARM9ExecThumbALU),
        CountPerFrame(gWindow.ARM9ExecThumbMul),
        CountPerFrame(gWindow.ARM9ExecThumbSingleLoad),
        CountPerFrame(gWindow.ARM9ExecThumbSingleStore),
        CountPerFrame(gWindow.ARM9ExecThumbBlockLoad),
        CountPerFrame(gWindow.ARM9ExecThumbBlockStore),
        CountPerFrame(gWindow.ARM9ExecThumbStackLoad),
        CountPerFrame(gWindow.ARM9ExecThumbStackStore),
        CountPerFrame(gWindow.ARM9ExecThumbBranchCond),
        CountPerFrame(gWindow.ARM9ExecThumbBranchImm),
        CountPerFrame(gWindow.ARM9ExecThumbBranchReg),
        CountPerFrame(gWindow.ARM9ExecThumbSys),
        CountPerFrame(gWindow.ARM9ExecThumbOther),
        CountPerFrame(gWindow.ARM9ExecLiteralLoads));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] arm9_mix_mem itcm=%.1f dtcm=%.1f main=%.1f shared=%.1f io=%.1f vram=%.1f other=%.1f slowblock_r=%.1f slowblock_w=%.1f slowblock_r_dtcm=%.1f slowblock_r_main=%.1f slowblock_r_shared=%.1f slowblock_r_io=%.1f slowblock_r_other=%.1f slowblock_w_dtcm=%.1f slowblock_w_main=%.1f slowblock_w_shared=%.1f slowblock_w_io=%.1f slowblock_w_other=%.1f dtcm_r_1_2=%.1f dtcm_r_3_4=%.1f dtcm_r_5_8=%.1f dtcm_r_9_16=%.1f dtcm_r_17p=%.1f dtcm_w_1_2=%.1f dtcm_w_3_4=%.1f dtcm_w_5_8=%.1f dtcm_w_9_16=%.1f dtcm_w_17p=%.1f main_r_1_2=%.1f main_r_3_4=%.1f main_r_5_8=%.1f main_r_9_16=%.1f main_r_17p=%.1f main_w_1_2=%.1f main_w_3_4=%.1f main_w_5_8=%.1f main_w_9_16=%.1f main_w_17p=%.1f",
        CountPerFrame(gWindow.ARM9ExecMemITCM),
        CountPerFrame(gWindow.ARM9ExecMemDTCM),
        CountPerFrame(gWindow.ARM9ExecMemMainRAM),
        CountPerFrame(gWindow.ARM9ExecMemSharedWRAM),
        CountPerFrame(gWindow.ARM9ExecMemIO),
        CountPerFrame(gWindow.ARM9ExecMemVRAM),
        CountPerFrame(gWindow.ARM9ExecMemOther),
        CountPerFrame(gWindow.ARM9SlowBlockTransferReads),
        CountPerFrame(gWindow.ARM9SlowBlockTransferWrites),
        CountPerFrame(gWindow.ARM9SlowBlockReadDTCM),
        CountPerFrame(gWindow.ARM9SlowBlockReadMainRAM),
        CountPerFrame(gWindow.ARM9SlowBlockReadSharedWRAM),
        CountPerFrame(gWindow.ARM9SlowBlockReadIO),
        CountPerFrame(gWindow.ARM9SlowBlockReadOther),
        CountPerFrame(gWindow.ARM9SlowBlockWriteDTCM),
        CountPerFrame(gWindow.ARM9SlowBlockWriteMainRAM),
        CountPerFrame(gWindow.ARM9SlowBlockWriteSharedWRAM),
        CountPerFrame(gWindow.ARM9SlowBlockWriteIO),
        CountPerFrame(gWindow.ARM9SlowBlockWriteOther),
        CountPerFrame(gWindow.ARM9SlowBlockReadDTCM_1_2),
        CountPerFrame(gWindow.ARM9SlowBlockReadDTCM_3_4),
        CountPerFrame(gWindow.ARM9SlowBlockReadDTCM_5_8),
        CountPerFrame(gWindow.ARM9SlowBlockReadDTCM_9_16),
        CountPerFrame(gWindow.ARM9SlowBlockReadDTCM_17P),
        CountPerFrame(gWindow.ARM9SlowBlockWriteDTCM_1_2),
        CountPerFrame(gWindow.ARM9SlowBlockWriteDTCM_3_4),
        CountPerFrame(gWindow.ARM9SlowBlockWriteDTCM_5_8),
        CountPerFrame(gWindow.ARM9SlowBlockWriteDTCM_9_16),
        CountPerFrame(gWindow.ARM9SlowBlockWriteDTCM_17P),
        CountPerFrame(gWindow.ARM9SlowBlockReadMainRAM_1_2),
        CountPerFrame(gWindow.ARM9SlowBlockReadMainRAM_3_4),
        CountPerFrame(gWindow.ARM9SlowBlockReadMainRAM_5_8),
        CountPerFrame(gWindow.ARM9SlowBlockReadMainRAM_9_16),
        CountPerFrame(gWindow.ARM9SlowBlockReadMainRAM_17P),
        CountPerFrame(gWindow.ARM9SlowBlockWriteMainRAM_1_2),
        CountPerFrame(gWindow.ARM9SlowBlockWriteMainRAM_3_4),
        CountPerFrame(gWindow.ARM9SlowBlockWriteMainRAM_5_8),
        CountPerFrame(gWindow.ARM9SlowBlockWriteMainRAM_9_16),
        CountPerFrame(gWindow.ARM9SlowBlockWriteMainRAM_17P));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] dtcm_slowblock_ns r_1_2=%.3fms r_3_4=%.3fms r_5_8=%.3fms r_9_16=%.3fms r_17p=%.3fms w_1_2=%.3fms w_3_4=%.3fms w_5_8=%.3fms w_9_16=%.3fms w_17p=%.3fms",
        NsPerFrame(gWindow.ARM9SlowBlockReadDTCM_1_2_Ns),
        NsPerFrame(gWindow.ARM9SlowBlockReadDTCM_3_4_Ns),
        NsPerFrame(gWindow.ARM9SlowBlockReadDTCM_5_8_Ns),
        NsPerFrame(gWindow.ARM9SlowBlockReadDTCM_9_16_Ns),
        NsPerFrame(gWindow.ARM9SlowBlockReadDTCM_17P_Ns),
        NsPerFrame(gWindow.ARM9SlowBlockWriteDTCM_1_2_Ns),
        NsPerFrame(gWindow.ARM9SlowBlockWriteDTCM_3_4_Ns),
        NsPerFrame(gWindow.ARM9SlowBlockWriteDTCM_5_8_Ns),
        NsPerFrame(gWindow.ARM9SlowBlockWriteDTCM_9_16_Ns),
        NsPerFrame(gWindow.ARM9SlowBlockWriteDTCM_17P_Ns));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] slowblock_sources generic_load=%.1f generic_store=%.1f fast_stack_load=%.1f fast_store=%.1f",
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadCalls),
        CountPerFrame(gWindow.ARM9SlowBlockGenericStoreCalls),
        CountPerFrame(gWindow.ARM9SlowBlockFastStackLoadCalls),
        CountPerFrame(gWindow.ARM9SlowBlockFastStoreCalls));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] slowblock_source_ns generic_load=%.3fms total=%.3fms generic_store=%.3fms total=%.3fms fast_stack_callsite=%.3fms fast_store_callsite=%.3fms",
        NsPerFrame(gWindow.ARM9SlowBlockGenericLoadNs),
        NsPerFrame(gWindow.ARM9SlowBlockGenericLoadTotalNs),
        NsPerFrame(gWindow.ARM9SlowBlockGenericStoreNs),
        NsPerFrame(gWindow.ARM9SlowBlockGenericStoreTotalNs),
        TicksValuePerFrameToMs(fastStackCallsiteTicks),
        TicksValuePerFrameToMs(fastStoreCallsiteTicks));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] slowblock_callsite_ms fast_stack_pre=%.3fms wrap=%.3fms post=%.3fms fast_store_pre=%.3fms wrap=%.3fms post=%.3fms generic_load_pre=%.3fms wrap=%.3fms post=%.3fms generic_store_pre=%.3fms wrap=%.3fms post=%.3fms",
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStackPreTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStackWrapTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStackPostTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStorePreTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStoreWrapTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStorePostTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericLoadPreTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericLoadWrapTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericLoadPostTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericStorePreTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericStoreWrapTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericStorePostTicks));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] slowblock_callsite_detail_ms fast_stack pack=%.3f save=%.3f call=%.3f restore=%.3f unpack=%.3f fast_store pack=%.3f save=%.3f call=%.3f restore=%.3f unpack=%.3f",
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStackPackTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStackSaveTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStackCallTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStackRestoreTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStackUnpackTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStorePackTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStoreSaveTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStoreCallTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStoreRestoreTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteFastStoreUnpackTicks));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] slowblock_callsite_detail_generic_ms generic_load pack=%.3f save=%.3f call=%.3f restore=%.3f unpack=%.3f generic_store pack=%.3f save=%.3f call=%.3f restore=%.3f unpack=%.3f",
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericLoadPackTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericLoadSaveTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericLoadCallTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericLoadRestoreTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericLoadUnpackTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericStorePackTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericStoreSaveTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericStoreCallTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericStoreRestoreTicks),
        TicksPerFrameToMs(gWindow.ARM9SlowBlockCallsiteGenericStoreUnpackTicks));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] slowblock_fastdtcm fast_stack_callsite=%.3fms fast_store_callsite=%.3fms direct_calls=%.1f fallback_calls=%.1f",
        TicksValuePerFrameToMs(fastStackCallsiteTicks),
        TicksValuePerFrameToMs(fastStoreCallsiteTicks),
        CountPerFrame(gWindow.ARM9SlowBlockFastDTCMDirectCalls),
        CountPerFrame(gWindow.ARM9SlowBlockFastDTCMFallbackCalls));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] slowblock_fastdtcm_sizes stack_1_2=%.1f stack_3_4=%.1f stack_5_8=%.1f stack_9p=%.1f store_1_2=%.1f store_3_4=%.1f store_5_8=%.1f store_9p=%.1f",
        CountPerFrame(gWindow.ARM9SlowBlockFastStackLoad_1_2),
        CountPerFrame(gWindow.ARM9SlowBlockFastStackLoad_3_4),
        CountPerFrame(gWindow.ARM9SlowBlockFastStackLoad_5_8),
        CountPerFrame(gWindow.ARM9SlowBlockFastStackLoad_9P),
        CountPerFrame(gWindow.ARM9SlowBlockFastStore_1_2),
        CountPerFrame(gWindow.ARM9SlowBlockFastStore_3_4),
        CountPerFrame(gWindow.ARM9SlowBlockFastStore_5_8),
        CountPerFrame(gWindow.ARM9SlowBlockFastStore_9P));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] slowblock_generic_load why fastmem_off=%.1f usermode=%.1f non_fast_shape=%.1f cond_incompat=%.1f target_dtcm=%.1f target_main=%.1f target_shared=%.1f target_io=%.1f target_other=%.1f",
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadFastmemOff),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadUsermode),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadNonFastPathShape),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadCondIncompatible),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadTargetDTCM),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadTargetMainRAM),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadTargetSharedWRAM),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadTargetIO),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadTargetOther));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] slowblock_generic_load shape arm=%.1f thumb=%.1f skip_base=%.1f keep_base=%.1f base_sp=%.1f base_other=%.1f regs_1_2=%.1f regs_3_4=%.1f regs_5p=%.1f inc_pre=%.1f inc_post=%.1f dec_pre=%.1f dec_post=%.1f",
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadShapeARM),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadShapeThumb),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadShapeSkipBase),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadShapeKeepBase),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadShapeBaseSP),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadShapeBaseOther),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadShapeRegs1_2),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadShapeRegs3_4),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadShapeRegs5P),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadShapeIncPre),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadShapeIncPost),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadShapeDecPre),
        CountPerFrame(gWindow.ARM9SlowBlockGenericLoadShapeDecPost));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] slowblock_generic_store why fastmem_off=%.1f usermode=%.1f cond_incompat=%.1f",
        CountPerFrame(gWindow.ARM9SlowBlockGenericStoreFastmemOff),
        CountPerFrame(gWindow.ARM9SlowBlockGenericStoreUsermode),
        CountPerFrame(gWindow.ARM9SlowBlockGenericStoreCondIncompatible));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] slowblock_overhead pre=%.3fms dtcm_setup=%.3fms main_setup=%.3fms",
        NsPerFrame(gWindow.ARM9SlowBlockPrePathNs),
        NsPerFrame(gWindow.ARM9SlowBlockFastDTCMSetupNs),
        NsPerFrame(gWindow.ARM9SlowBlockFastMainRAMSetupNs));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] slowblock_paths dtcm=%.3fms/%.1f main=%.3fms/%.1f fallback=%.3fms/%.1f",
        NsPerFrame(gWindow.ARM9SlowBlockFastDTCMNs),
        CountPerFrame(gWindow.ARM9SlowBlockFastDTCMCalls),
        NsPerFrame(gWindow.ARM9SlowBlockFastMainRAMNs),
        CountPerFrame(gWindow.ARM9SlowBlockFastMainRAMCalls),
        NsPerFrame(gWindow.ARM9SlowBlockFallbackLoopNs),
        CountPerFrame(gWindow.ARM9SlowBlockFallbackLoopCalls));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] arm9_chain miss_stop=%.1f miss_budget=%.1f miss_target=%.1f miss_hle=%.1f miss_setup=%.1f miss_lookup=%.1f",
        CountPerFrame(gWindow.ARM9JitChainMissStop),
        CountPerFrame(gWindow.ARM9JitChainMissBudget),
        CountPerFrame(gWindow.ARM9JitChainMissTarget),
        CountPerFrame(gWindow.ARM9JitChainMissHLE),
        CountPerFrame(gWindow.ARM9JitChainMissSetup),
        CountPerFrame(gWindow.ARM9JitChainMissLookup));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] arm9_ret normal_end=%.1f end_branch=%.1f end_cond=%.1f end_other=%.1f normal_max=%.1f normal_irq=%.1f normal_halt=%.1f",
        CountPerFrame(gWindow.ARM9JitReturnEndBlock),
        CountPerFrame(gWindow.ARM9JitReturnEndBranch),
        CountPerFrame(gWindow.ARM9JitReturnEndCondBranch),
        CountPerFrame(gWindow.ARM9JitReturnEndOther),
        CountPerFrame(gWindow.ARM9JitReturnMaxBlock),
        CountPerFrame(gWindow.ARM9JitReturnIRQBoundary),
        CountPerFrame(gWindow.ARM9JitReturnHaltBoundary));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] arm9_ret_max arm=%.1f thumb=%.1f mem=%.1f nomem=%.1f",
        CountPerFrame(gWindow.ARM9JitReturnMaxARM),
        CountPerFrame(gWindow.ARM9JitReturnMaxThumb),
        CountPerFrame(gWindow.ARM9JitReturnMaxWithMemory),
        CountPerFrame(gWindow.ARM9JitReturnMaxNoMemory));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] arm9_ret_max_mem load=%.1f store=%.1f itcm=%.1f dtcm=%.1f main=%.1f shared=%.1f io=%.1f vram=%.1f other=%.1f",
        CountPerFrame(gWindow.ARM9JitReturnMaxWithLoad),
        CountPerFrame(gWindow.ARM9JitReturnMaxWithStore),
        CountPerFrame(gWindow.ARM9JitReturnMaxITCM),
        CountPerFrame(gWindow.ARM9JitReturnMaxDTCM),
        CountPerFrame(gWindow.ARM9JitReturnMaxMainRAM),
        CountPerFrame(gWindow.ARM9JitReturnMaxSharedWRAM),
        CountPerFrame(gWindow.ARM9JitReturnMaxIO),
        CountPerFrame(gWindow.ARM9JitReturnMaxVRAM),
        CountPerFrame(gWindow.ARM9JitReturnMaxOtherMem));

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] arm9_ret_branch arm_imm=%.1f arm_reg=%.1f arm_reg_arm=%.1f arm_reg_thumb=%.1f arm_reg_lr=%.1f arm_reg_other=%.1f arm_reg_link=%.1f thumb_cond=%.1f thumb_imm=%.1f thumb_reg=%.1f",
        CountPerFrame(gWindow.ARM9JitReturnEndARMImm),
        CountPerFrame(gWindow.ARM9JitReturnEndARMReg),
        CountPerFrame(gWindow.ARM9JitReturnEndARMRegARM),
        CountPerFrame(gWindow.ARM9JitReturnEndARMRegThumb),
        CountPerFrame(gWindow.ARM9JitReturnEndARMRegLR),
        CountPerFrame(gWindow.ARM9JitReturnEndARMRegOther),
        CountPerFrame(gWindow.ARM9JitReturnEndARMRegLink),
        CountPerFrame(gWindow.ARM9JitReturnEndThumbCond),
        CountPerFrame(gWindow.ARM9JitReturnEndThumbImm),
        CountPerFrame(gWindow.ARM9JitReturnEndThumbReg));

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
        "[LITEV_PROFILE] arm9_slowmem_ns read main=%.3fms swram=%.3fms io=%.3fms vram=%.3fms other=%.3fms write main=%.3fms swram=%.3fms io=%.3fms vram=%.3fms other=%.3fms",
        NsPerFrame(gWindow.ARM9SlowReadMainRAMNs),
        NsPerFrame(gWindow.ARM9SlowReadSharedWRAMNs),
        NsPerFrame(gWindow.ARM9SlowReadIONs),
        NsPerFrame(gWindow.ARM9SlowReadVRAMNs),
        NsPerFrame(gWindow.ARM9SlowReadOtherNs),
        NsPerFrame(gWindow.ARM9SlowWriteMainRAMNs),
        NsPerFrame(gWindow.ARM9SlowWriteSharedWRAMNs),
        NsPerFrame(gWindow.ARM9SlowWriteIONs),
        NsPerFrame(gWindow.ARM9SlowWriteVRAMNs),
        NsPerFrame(gWindow.ARM9SlowWriteOtherNs));

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

    Platform::Log(Platform::LogLevel::Info,
        "[LITEV_PROFILE] arm9_slowio_ns dispstat=%.3fms dma=%.3fms timer=%.3fms key=%.3fms ipc=%.3fms cart=%.3fms irq=%.3fms vramctl=%.3fms divsqrt=%.3fms other=%.3fms",
        NsPerFrame(gWindow.ARM9SlowReadIODispStatNs),
        NsPerFrame(gWindow.ARM9SlowReadIODMANs),
        NsPerFrame(gWindow.ARM9SlowReadIOTimerNs),
        NsPerFrame(gWindow.ARM9SlowReadIOKeyNs),
        NsPerFrame(gWindow.ARM9SlowReadIOIPCNs),
        NsPerFrame(gWindow.ARM9SlowReadIOCartNs),
        NsPerFrame(gWindow.ARM9SlowReadIOIRQNs),
        NsPerFrame(gWindow.ARM9SlowReadIOVRAMCtlNs),
        NsPerFrame(gWindow.ARM9SlowReadIODivSqrtNs),
        NsPerFrame(gWindow.ARM9SlowReadIOOtherNs));

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

#define LITE_PROFILE_SCOPE(var_name, counter) melonDS::LiteProfile::ScopeTimer var_name(counter)
#define LITE_PROFILE_ADD(counter) melonDS::LiteProfile::AddAtomic(counter)
#define LITE_PROFILE_ADD_VALUE(counter, value) melonDS::LiteProfile::AddAtomic((counter), (value))
#define LITE_PROFILE_RESET_FRAME() melonDS::LiteProfile::ResetFrame()
#define LITE_PROFILE_END_FRAME() melonDS::LiteProfile::EndFrame()
#define LITE_PROFILE_NOW_NS() melonDS::LiteProfile::NowNs()

#else

#define LITE_PROFILE_SCOPE(var_name, counter) do { } while (0)
#define LITE_PROFILE_ADD(counter) do { } while (0)
#define LITE_PROFILE_ADD_VALUE(counter, value) do { } while (0)
#define LITE_PROFILE_RESET_FRAME() do { } while (0)
#define LITE_PROFILE_END_FRAME() do { } while (0)
#define LITE_PROFILE_NOW_NS() 0ULL

#endif
