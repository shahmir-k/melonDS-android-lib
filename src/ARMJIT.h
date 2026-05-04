/*
    Copyright 2016-2025 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef ARMJIT_H
#define ARMJIT_H

#include <algorithm>
#include <optional>
#include <memory>
#include <vector>
#include "types.h"
#include "MemConstants.h"
#include "Args.h"
#include "ARMJIT_Memory.h"

#ifdef LITEV_ARM7_PROFILE
#include <atomic>
#include <cstdint>
#endif

#ifdef JIT_ENABLED
#include "JitBlock.h"

#if defined(__APPLE__) && defined(__aarch64__)
    #include <pthread.h>
#endif

#include "ARMJIT_Internal.h"
#include "ARMJIT_Compiler.h"

namespace melonDS
{
class ARM;

class JitBlock;

enum JitTracePlanStopReason : u8
{
    TracePlanStopNone = 0,
    TracePlanStopMissingStart,
    TracePlanStopMaxBlocks,
    TracePlanStopDynamicExit,
    TracePlanStopConditionalSplit,
    TracePlanStopModeChange,
    TracePlanStopMissingSuccessor,
    TracePlanStopLoop,
    TracePlanStopNonTraceableExit,
};

struct JitTracePlan
{
    u32 Num = 0;
    u32 StartAddr = 0;
    u32 EndAddr = 0;
    u32 TotalInstrs = 0;
    u8 Thumb = 0;
    u8 StopReason = TracePlanStopNone;
    std::vector<u32> Blocks {};
};

struct JitTraceSideExit
{
    u32 SourceAddr = 0;
    u32 ExitInstrAddr = 0;
    u32 PrimaryTarget = 0;
    u32 SecondaryTarget = 0;
    u8 Exit = JitBlock::ExitUnknown;
    u8 Branch = JitBlock::ExitBranchNone;
    u8 BranchReg = 0xFF;
    u8 Thumb = 0;
    u8 HasMemory = 0;
    u8 HasDynamicExit = 0;
    u8 HasConditionalExit = 0;
    u8 PrimaryTargetKind = JitBlock::TraceTargetNone;
    u8 SecondaryTargetKind = JitBlock::TraceTargetNone;
    u8 PrimaryTargetSameMode = 0;
    u8 SecondaryTargetSameMode = 0;
};

struct JitTrace
{
    u32 Num = 0;
    u32 StartAddr = 0;
    u32 EndAddr = 0;
    u32 TotalInstrs = 0;
    u8 Thumb = 0;
    u8 StopReason = TracePlanStopNone;
    std::vector<u32> Blocks {};
    std::vector<JitBlockEntry> Entries {};
    JitTraceSideExit SideExit {};
};

#if LITEV_PROFILE
struct JitBlockRecipe
{
    u32 Num = 0;
    u32 StartAddr = 0;
    u8 Thumb = 0;
    u8 HasMemInstr = 0;
    std::vector<FetchedInstr> Instrs {};
};

struct JitTraceRecipe
{
    u32 Num = 0;
    u32 StartAddr = 0;
    u32 EndAddr = 0;
    u32 TotalInstrs = 0;
    u8 Thumb = 0;
    u8 HasMemInstr = 0;
    u8 StopReason = TracePlanStopNone;
    std::vector<u32> Blocks {};
    std::vector<FetchedInstr> Instrs {};
};
#endif

class ARMJIT
{
public:
    ARMJIT(melonDS::NDS& nds, std::optional<JITArgs> jit) noexcept;
    ~ARMJIT() noexcept;
    void InvalidateByAddr(u32) noexcept;
    void CheckAndInvalidateWVRAM(int) noexcept;
    void CheckAndInvalidateITCM() noexcept;
    void Reset() noexcept;
    void JitEnableWrite() noexcept;
    void JitEnableExecute() noexcept;
    void CompileBlock(ARM* cpu) noexcept;
    void ResetBlockCache() noexcept;

    template <u32 num, int region>
    void CheckAndInvalidate(u32 addr) noexcept
    {
        u32 localAddr = Memory.LocaliseAddress(region, num, addr);
        if (CodeMemRegions[region][(localAddr & 0x7FFFFFF) / 512].Code & (1 << ((localAddr & 0x1FF) / 16)))
            InvalidateByAddr(localAddr);
    }
    template <u32 num, int region>
    void CheckAndInvalidateRange(u32 addr, u32 size) noexcept
    {
        if (!size)
            return;

        u32 localAddr = Memory.LocaliseAddress(region, num, addr);
        u32 endAddr = localAddr + size - 1;

        for (u32 page = localAddr & ~0x1FF; page <= (endAddr & ~0x1FF); page += 0x200)
        {
            AddressRange* range = &CodeMemRegions[region][(page & 0x7FFFFFF) / 512];
            if (!range->Code)
                continue;

            u32 firstChunk = (page == (localAddr & ~0x1FF)) ? ((localAddr & 0x1FF) >> 4) : 0;
            u32 lastChunk = (page == (endAddr & ~0x1FF)) ? ((endAddr & 0x1FF) >> 4) : 31;

            for (u32 chunk = firstChunk; chunk <= lastChunk; chunk++)
            {
                if (range->Code & (1u << chunk))
                    InvalidateByAddr(page | (chunk << 4));
            }
        }
    }
    JitBlockEntry LookUpBlock(u32 num, u64* entries, u32 offset, u32 addr) noexcept;
    bool SetupExecutableRegion(u32 num, u32 blockAddr, u64*& entry, u32& start, u32& size) noexcept;
    u32 LocaliseCodeAddress(u32 num, u32 addr) const noexcept;
    const JitBlock* FindJitBlock(u32 num, u32 addr) const noexcept;
#if LITEV_PROFILE
    const JitTrace* FindLinearTrace(u32 num, u32 startAddr) const noexcept;
    const JitBlockRecipe* FindBlockRecipe(u32 num, u32 startAddr) const noexcept;
    bool BuildLinearTracePlan(u32 num, u32 startAddr, u32 maxBlocks, JitTracePlan& out) const noexcept;
    bool BuildLinearTrace(u32 num, u32 startAddr, u32 maxBlocks, JitTrace& out) const noexcept;
    bool BuildTraceRecipe(u32 num, u32 startAddr, u32 maxBlocks, JitTraceRecipe& out) const noexcept;
    void RefreshLinearTracePlanSummary(u32 num, u32 startAddr, u32 maxBlocks = 8) noexcept;
    void RefreshLinearTrace(u32 num, u32 startAddr, u32 maxBlocks = 8) noexcept;
    void InvalidateLinearTracesForBlock(u32 num, u32 blockAddr) noexcept;
#endif

    ARMJIT_Memory Memory;
private:
    int MaxBlockSize {};
    bool LiteralOptimizations = false;
    bool BranchOptimizations = false;
    bool FastMemory = false;

public:
    melonDS::NDS& NDS;
    TinyVector<u32> InvalidLiterals {};
    friend class ARMJIT_Memory;
    void blockSanityCheck(u32 num, u32 blockAddr, JitBlockEntry entry) noexcept;
    void RetireJitBlock(JitBlock* block) noexcept;

    int GetMaxBlockSize() const noexcept { return MaxBlockSize; }
    bool LiteralOptimizationsEnabled() const noexcept { return LiteralOptimizations; }
    bool BranchOptimizationsEnabled() const noexcept { return BranchOptimizations; }
    bool FastMemoryEnabled() const noexcept { return FastMemory; }

    void SetJITArgs(JITArgs args) noexcept;
    void SetMaxBlockSize(int size) noexcept;
    void SetLiteralOptimizations(bool enabled) noexcept;
    void SetBranchOptimizations(bool enabled) noexcept;
    void SetFastMemory(bool enabled) noexcept;

#ifdef LITEV_ARM7_PROFILE
    struct JitStats
    {
        std::atomic<uint64_t> BlockCompiles    {0};
        std::atomic<uint64_t> BlockRestores    {0};
        std::atomic<uint64_t> TotalInstructions{0};
        std::atomic<uint64_t> FastLookupHits   {0};
        std::atomic<uint64_t> FastLookupMisses {0};

        uint64_t AverageBlockLength() const noexcept
        {
            uint64_t c = BlockCompiles.load();
            return c > 0 ? TotalInstructions.load() / c : 0;
        }
        double HitRate() const noexcept
        {
            uint64_t total = FastLookupHits.load() + FastLookupMisses.load();
            return total > 0 ? (double)FastLookupHits.load() / total : 0.0;
        }
    } Stats;

    void ResetJitStats() noexcept
    {
        Stats.BlockCompiles = 0;
        Stats.BlockRestores = 0;
        Stats.TotalInstructions = 0;
        Stats.FastLookupHits = 0;
        Stats.FastLookupMisses = 0;
    }
#endif // LITEV_ARM7_PROFILE

    Compiler JITCompiler;
    std::unordered_map<u32, JitBlock*> JitBlocks9 {};
    std::unordered_map<u32, JitBlock*> JitBlocks7 {};
#if LITEV_PROFILE
    std::unordered_map<u32, JitTrace> LinearTraces9 {};
    std::unordered_map<u32, JitTrace> LinearTraces7 {};
    std::unordered_map<u32, JitBlockRecipe> BlockRecipes9 {};
    std::unordered_map<u32, JitBlockRecipe> BlockRecipes7 {};
#endif

    std::unordered_map<u32, JitBlock*> RestoreCandidates {};


    AddressRange CodeIndexITCM[ITCMPhysicalSize / 512] {};
    AddressRange CodeIndexMainRAM[MainRAMMaxSize / 512] {};
    AddressRange CodeIndexSWRAM[SharedWRAMSize / 512] {};
    AddressRange CodeIndexVRAM[0x100000 / 512] {};
    AddressRange CodeIndexARM9BIOS[ARM9BIOSSize / 512] {};
    AddressRange CodeIndexARM7BIOS[ARM7BIOSSize / 512] {};
    AddressRange CodeIndexARM7WRAM[ARM7WRAMSize / 512] {};
    AddressRange CodeIndexARM7WVRAM[0x40000 / 512] {};
    AddressRange CodeIndexBIOS9DSi[0x10000 / 512] {};
    AddressRange CodeIndexBIOS7DSi[0x10000 / 512] {};
    AddressRange CodeIndexNWRAM_A[NWRAMSize / 512] {};
    AddressRange CodeIndexNWRAM_B[NWRAMSize / 512] {};
    AddressRange CodeIndexNWRAM_C[NWRAMSize / 512] {};

    u64 FastBlockLookupITCM[ITCMPhysicalSize / 2] {};
    u64 FastBlockLookupMainRAM[MainRAMMaxSize / 2] {};
    u64 FastBlockLookupSWRAM[SharedWRAMSize / 2] {};
    u64 FastBlockLookupVRAM[0x100000 / 2] {};
    u64 FastBlockLookupARM9BIOS[ARM9BIOSSize / 2] {};
    u64 FastBlockLookupARM7BIOS[ARM7BIOSSize / 2] {};
    u64 FastBlockLookupARM7WRAM[ARM7WRAMSize / 2] {};
    u64 FastBlockLookupARM7WVRAM[0x40000 / 2] {};
    u64 FastBlockLookupBIOS9DSi[0x10000 / 2] {};
    u64 FastBlockLookupBIOS7DSi[0x10000 / 2] {};
    u64 FastBlockLookupNWRAM_A[NWRAMSize / 2] {};
    u64 FastBlockLookupNWRAM_B[NWRAMSize / 2] {};
    u64 FastBlockLookupNWRAM_C[NWRAMSize / 2] {};

    AddressRange* const CodeMemRegions[ARMJIT_Memory::memregions_Count] =
    {
        NULL,
        CodeIndexITCM,
        NULL,
        CodeIndexARM9BIOS,
        CodeIndexMainRAM,
        CodeIndexSWRAM,
        NULL,
        CodeIndexVRAM,
        CodeIndexARM7BIOS,
        CodeIndexARM7WRAM,
        NULL,
        NULL,
        CodeIndexARM7WVRAM,
        CodeIndexBIOS9DSi,
        CodeIndexBIOS7DSi,
        CodeIndexNWRAM_A,
        CodeIndexNWRAM_B,
        CodeIndexNWRAM_C
    };

    u64* const FastBlockLookupRegions[ARMJIT_Memory::memregions_Count] =
    {
        NULL,
        FastBlockLookupITCM,
        NULL,
        FastBlockLookupARM9BIOS,
        FastBlockLookupMainRAM,
        FastBlockLookupSWRAM,
        NULL,
        FastBlockLookupVRAM,
        FastBlockLookupARM7BIOS,
        FastBlockLookupARM7WRAM,
        NULL,
        NULL,
        FastBlockLookupARM7WVRAM,
        FastBlockLookupBIOS9DSi,
        FastBlockLookupBIOS7DSi,
        FastBlockLookupNWRAM_A,
        FastBlockLookupNWRAM_B,
        FastBlockLookupNWRAM_C
    };
};
}

// Defined in assembly
extern "C" void ARM_Dispatch(melonDS::ARM* cpu, melonDS::JitBlockEntry entry);
#else
namespace melonDS
{
class ARM;

// This version is a stub; the methods all do nothing,
// but there's still a Memory member.
class ARMJIT
{
public:
    ARMJIT(melonDS::NDS& nds, std::optional<JITArgs>) noexcept : Memory(nds) {}
    ~ARMJIT() noexcept {}
    void InvalidateByAddr(u32) noexcept {}
    void CheckAndInvalidateWVRAM(int) noexcept {}
    void CheckAndInvalidateITCM() noexcept {}
    void Reset() noexcept {}
    void JitEnableWrite() noexcept {}
    void JitEnableExecute() noexcept {}
    void CompileBlock(ARM*) noexcept {}
    void ResetBlockCache() noexcept {}
    template <u32, int>
    void CheckAndInvalidate(u32 addr) noexcept {}

    ARMJIT_Memory Memory;
};
}
#endif // JIT_ENABLED

#endif // ARMJIT_H
