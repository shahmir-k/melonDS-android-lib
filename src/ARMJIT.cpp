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

#include "ARMJIT.h"
#include "ARMJIT_Memory.h"
#include <string.h>
#include <assert.h>
#include <unordered_map>

#define XXH_STATIC_LINKING_ONLY
#include "xxhash/xxhash.h"

#include "Platform.h"

#include "ARMJIT_Internal.h"
#include "ARMJIT_Memory.h"
#include "ARMJIT_Compiler.h"
#include "ARMJIT_Global.h"

#include "ARMInterpreter_ALU.h"
#include "ARMInterpreter_LoadStore.h"
#include "ARMInterpreter_Branch.h"
#include "ARMInterpreter.h"

#include "DSi.h"
#include "GPU.h"
#include "GPU3D.h"
#include "SPU.h"
#include "Wifi.h"
#include "NDSCart.h"
#include "LiteProfile.h"
#include "Platform.h"
#include "ARMJIT_x64/ARMJIT_Offsets.h"

namespace melonDS
{
using Platform::Log;
using Platform::LogLevel;

static_assert(offsetof(ARM, CPSR) == ARM_CPSR_offset, "");
static_assert(offsetof(ARM, Cycles) == ARM_Cycles_offset, "");
static_assert(offsetof(ARM, StopExecution) == ARM_StopExecution_offset, "");

extern "C" JitBlockEntry ARM9_ContinueBlock(ARM* cpuBase)
{
    constexpr s32 kChainCycleBudget = 128;

    auto* cpu = static_cast<ARMv5*>(cpuBase);
    auto& nds = cpu->NDS;

    LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9JitChainAttempts);

    if (cpu->StopExecution || cpu->Halted || cpu->IdleLoop)
    {
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9JitChainMissStop);
        return nullptr;
    }

    if (cpu->Cycles >= kChainCycleBudget)
    {
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9JitChainMissBudget);
        return nullptr;
    }

    if (nds.ARM9Timestamp + cpu->Cycles >= nds.ARM9Target)
    {
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9JitChainMissTarget);
        return nullptr;
    }

    const u32 instrAddr = cpu->R[15] - ((cpu->CPSR & 0x20) ? 2 : 4);
    if (nds.ARM9LibHLE.TryHandle(*cpu, instrAddr))
    {
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9LibHLEHits);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9JitChainMissHLE);
        return nullptr;
    }

    if ((instrAddr < cpu->FastBlockLookupStart || instrAddr >= (cpu->FastBlockLookupStart + cpu->FastBlockLookupSize))
        && !nds.JIT.SetupExecutableRegion(0, instrAddr, cpu->FastBlockLookup, cpu->FastBlockLookupStart, cpu->FastBlockLookupSize))
    {
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9JitChainMissSetup);
        return nullptr;
    }

    JitBlockEntry block = nds.JIT.LookUpBlock(0, cpu->FastBlockLookup,
        instrAddr - cpu->FastBlockLookupStart, instrAddr);
    if (!block)
    {
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9JitChainMissLookup);
        return nullptr;
    }

    cpu->LastJitBlockAddr = instrAddr;
    cpu->LastJitBlockEntry = block;
    LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9JitChainHits);
    return block;
}


#define JIT_DEBUGPRINT(msg, ...)
//#define JIT_DEBUGPRINT(msg, ...) Platform::Log(Platform::LogLevel::Debug, msg, ## __VA_ARGS__)

const u32 CodeRegionSizes[ARMJIT_Memory::memregions_Count] =
{
    0,
    ITCMPhysicalSize,
    0,
    ARM9BIOSSize,
    MainRAMMaxSize,
    SharedWRAMSize,
    0,
    0x100000,
    ARM7BIOSSize,
    ARM7WRAMSize,
    0,
    0,
    0x40000,
    0x10000,
    0x10000,
    NWRAMSize,
    NWRAMSize,
    NWRAMSize,
};

template <bool Write>
__attribute__((always_inline)) inline void CopyBlockWords(u8* mem, u32 addr, u64* data, u32 num)
{
    u32* words = reinterpret_cast<u32*>(&mem[addr]);
    if constexpr (Write)
    {
        for (u32 i = 0; i < num; i++)
            words[i] = static_cast<u32>(data[i]);
    }
    else
    {
        for (u32 i = 0; i < num; i++)
            data[i] = words[i];
    }
}

template <bool Write>
__attribute__((always_inline)) inline void CopyBlockWordsTiny2(u8* mem, u32 addr, u64* data, u32 num)
{
    u32* words = reinterpret_cast<u32*>(&mem[addr]);
    if constexpr (Write)
    {
        words[0] = static_cast<u32>(data[0]);
        if (num == 2)
            words[1] = static_cast<u32>(data[1]);
    }
    else
    {
        data[0] = words[0];
        if (num == 2)
            data[1] = words[1];
    }
}

template <bool Write>
__attribute__((always_inline)) inline void CopyBlockWordsTiny4(u8* mem, u32 addr, u64* data, u32 num)
{
    u32* words = reinterpret_cast<u32*>(&mem[addr]);
    if constexpr (Write)
    {
        switch (num)
        {
        case 4:
            words[3] = static_cast<u32>(data[3]);
            [[fallthrough]];
        case 3:
            words[2] = static_cast<u32>(data[2]);
            [[fallthrough]];
        case 2:
            words[1] = static_cast<u32>(data[1]);
            [[fallthrough]];
        case 1:
            words[0] = static_cast<u32>(data[0]);
            break;
        default:
            break;
        }
    }
    else
    {
        switch (num)
        {
        case 4:
            data[3] = words[3];
            [[fallthrough]];
        case 3:
            data[2] = words[2];
            [[fallthrough]];
        case 2:
            data[1] = words[1];
            [[fallthrough]];
        case 1:
            data[0] = words[0];
            break;
        default:
            break;
        }
    }
}

template <bool Write, u32 num, int region>
__attribute__((always_inline)) inline bool TryBlockTransferLinear(
    ARMJIT& jit,
    u8* mem,
    u32 mask,
    u32 addr,
    u64* data,
    u32 words) noexcept
{
    u32 offset = addr & mask;
    u32 bytes = words * sizeof(u32);
    if (offset > mask || bytes > (mask + 1) - offset)
        return false;

    if constexpr (Write)
        jit.CheckAndInvalidateRange<num, region>(addr, bytes);
    CopyBlockWords<Write>(mem, offset, data, words);
    return true;
}

u32 ARMJIT::LocaliseCodeAddress(u32 num, u32 addr) const noexcept
{
    int region = num == 0
        ? Memory.ClassifyAddress9(addr)
        : Memory.ClassifyAddress7(addr);

    if (CodeMemRegions[region])
        return Memory.LocaliseAddress(region, num, addr);
    return 0;
}

template <typename T, int ConsoleType>
T SlowRead9(u32 addr, ARMv5* cpu)
{
    LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadCalls);
    LITE_PROFILE_SCOPE(timer, LiteProfile::gFrame.ARM9SlowReadNs);
    u32 offset = addr & 0x3;
    addr &= ~(sizeof(T) - 1);

    auto& nds = cpu->NDS;
    T val;
    if (addr < cpu->ITCMSize)
        val = *(T*)&cpu->ITCM[addr & 0x7FFF];
    else if ((addr & cpu->DTCMMask) == cpu->DTCMBase)
        val = *(T*)&cpu->DTCM[addr & 0x3FFF];
    else
    {
        switch (addr & 0xFF000000)
        {
        case 0x02000000:
        {
            LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadMainRAM);
            const uint64_t mainStart = LITE_PROFILE_NOW_NS();
            val = *(T*)&nds.MainRAM[addr & nds.MainRAMMask];
            LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowReadMainRAMNs, LITE_PROFILE_NOW_NS() - mainStart);
            break;
        }

        case 0x03000000:
        {
            LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadSharedWRAM);
            const uint64_t swramStart = LITE_PROFILE_NOW_NS();
            if (nds.SWRAM_ARM9.Mem)
                val = *(T*)&nds.SWRAM_ARM9.Mem[addr & nds.SWRAM_ARM9.Mask];
            else
                val = 0;
            LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowReadSharedWRAMNs, LITE_PROFILE_NOW_NS() - swramStart);
            break;
        }

        case 0x06000000:
        {
            LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadVRAM);
            const uint64_t vramStart = LITE_PROFILE_NOW_NS();
            if constexpr (std::is_same_v<T, u32>)
                val = nds.ARM9Read32(addr);
            else if constexpr (std::is_same_v<T, u16>)
                val = nds.ARM9Read16(addr);
            else
                val = nds.ARM9Read8(addr);
            LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowReadVRAMNs, LITE_PROFILE_NOW_NS() - vramStart);
            break;
        }

        default:
        {
#if LITEV_PROFILE
            uint64_t ioStart = 0;
            std::atomic<uint64_t>* ioBucketNs = nullptr;
#endif
            if ((addr & 0xFF000000) == 0x04000000)
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadIO);
#if LITEV_PROFILE
                ioStart = LITE_PROFILE_NOW_NS();
                ioBucketNs = &LiteProfile::gFrame.ARM9SlowReadIOOtherNs;
#endif
                switch (addr & 0xFFFFFFF0)
                {
                case 0x04000000:
                    LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadIODispStat);
#if LITEV_PROFILE
                    ioBucketNs = &LiteProfile::gFrame.ARM9SlowReadIODispStatNs;
#endif
                    break;
                default:
                    if (addr >= 0x040000B0 && addr < 0x040000F0)
                    {
                        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadIODMA);
#if LITEV_PROFILE
                        ioBucketNs = &LiteProfile::gFrame.ARM9SlowReadIODMANs;
#endif
                    }
                    else if (addr >= 0x04000100 && addr < 0x04000110)
                    {
                        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadIOTimer);
#if LITEV_PROFILE
                        ioBucketNs = &LiteProfile::gFrame.ARM9SlowReadIOTimerNs;
#endif
                    }
                    else if (addr >= 0x04000130 && addr < 0x04000134)
                    {
                        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadIOKey);
#if LITEV_PROFILE
                        ioBucketNs = &LiteProfile::gFrame.ARM9SlowReadIOKeyNs;
#endif
                    }
                    else if (addr >= 0x04000180 && addr < 0x04000190)
                    {
                        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadIOIPC);
#if LITEV_PROFILE
                        ioBucketNs = &LiteProfile::gFrame.ARM9SlowReadIOIPCNs;
#endif
                    }
                    else if (addr >= 0x040001A0 && addr < 0x040001B0)
                    {
                        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadIOCart);
#if LITEV_PROFILE
                        ioBucketNs = &LiteProfile::gFrame.ARM9SlowReadIOCartNs;
#endif
                    }
                    else if ((addr >= 0x04000204 && addr < 0x04000218) || addr == 0x04000208)
                    {
                        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadIOIRQ);
#if LITEV_PROFILE
                        ioBucketNs = &LiteProfile::gFrame.ARM9SlowReadIOIRQNs;
#endif
                    }
                    else if (addr >= 0x04000240 && addr < 0x0400024C)
                    {
                        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadIOVRAMCtl);
#if LITEV_PROFILE
                        ioBucketNs = &LiteProfile::gFrame.ARM9SlowReadIOVRAMCtlNs;
#endif
                    }
                    else if (addr >= 0x04000280 && addr < 0x040002C0)
                    {
                        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadIODivSqrt);
#if LITEV_PROFILE
                        ioBucketNs = &LiteProfile::gFrame.ARM9SlowReadIODivSqrtNs;
#endif
                    }
                    else
                    {
                        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadIOOther);
#if LITEV_PROFILE
                        ioBucketNs = &LiteProfile::gFrame.ARM9SlowReadIOOtherNs;
#endif
                    }
                    break;
                }

                if constexpr (!std::is_same_v<T, u8>)
                {
                    if (addr >= 0x04000204 && addr < 0x04000218)
                    {
                        if constexpr (std::is_same_v<T, u32>)
                        {}
                        else
                        {}
                    }
                }
            }
            else
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowReadOther);
            if constexpr (std::is_same_v<T, u32>)
                val = nds.ARM9Read32(addr);
            else if constexpr (std::is_same_v<T, u16>)
                val = nds.ARM9Read16(addr);
            else
                val = nds.ARM9Read8(addr);
#if LITEV_PROFILE
            if (ioBucketNs)
            {
                const uint64_t ioElapsed = LITE_PROFILE_NOW_NS() - ioStart;
                LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowReadIONs, ioElapsed);
                LITE_PROFILE_ADD_VALUE(*ioBucketNs, ioElapsed);
            }
#endif
            break;
        }
        }
    }

    if constexpr (std::is_same_v<T, u32>)
        return ROR(val, offset << 3);
    else
        return val;
}

template <typename T, int ConsoleType>
T SlowRead7(u32 addr)
{
    u32 offset = addr & 0x3;
    addr &= ~(sizeof(T) - 1);

    T val;
    if (std::is_same<T, u32>::value)
        val = NDS::Current->ARM7Read32(addr);
    else if (std::is_same<T, u16>::value)
        val = NDS::Current->ARM7Read16(addr);
    else
        val = NDS::Current->ARM7Read8(addr);

    if (std::is_same<T, u32>::value)
        return ROR(val, offset << 3);
    else
        return val;
}

template <typename T, int ConsoleType>
void SlowWrite9(u32 addr, ARMv5* cpu, u32 val)
{
    LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowWriteCalls);
    LITE_PROFILE_SCOPE(timer, LiteProfile::gFrame.ARM9SlowWriteNs);
    addr &= ~(sizeof(T) - 1);

    auto& nds = cpu->NDS;
    if (addr < cpu->ITCMSize)
    {
        nds.JIT.CheckAndInvalidate<0, ARMJIT_Memory::memregion_ITCM>(addr);
        *(T*)&cpu->ITCM[addr & 0x7FFF] = val;
    }
    else if ((addr & cpu->DTCMMask) == cpu->DTCMBase)
    {
        *(T*)&cpu->DTCM[addr & 0x3FFF] = val;
    }
    else
    {
        switch (addr & 0xFF000000)
        {
        case 0x02000000:
        {
            LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowWriteMainRAM);
            const uint64_t mainStart = LITE_PROFILE_NOW_NS();
            nds.JIT.CheckAndInvalidate<0, ARMJIT_Memory::memregion_MainRAM>(addr);
            *(T*)&nds.MainRAM[addr & nds.MainRAMMask] = val;
            LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowWriteMainRAMNs, LITE_PROFILE_NOW_NS() - mainStart);
            break;
        }

        case 0x03000000:
        {
            LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowWriteSharedWRAM);
            const uint64_t swramStart = LITE_PROFILE_NOW_NS();
            if (nds.SWRAM_ARM9.Mem)
            {
                nds.JIT.CheckAndInvalidate<0, ARMJIT_Memory::memregion_SharedWRAM>(addr);
                *(T*)&nds.SWRAM_ARM9.Mem[addr & nds.SWRAM_ARM9.Mask] = val;
            }
            LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowWriteSharedWRAMNs, LITE_PROFILE_NOW_NS() - swramStart);
            break;
        }

        case 0x04000000:
        {
            LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowWriteIO);
            const uint64_t ioStart = LITE_PROFILE_NOW_NS();
            if constexpr (std::is_same_v<T, u32>)
            {
                if (nds.ARM9FastIOWrite32(addr, val))
                {
                    LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowWriteIONs, LITE_PROFILE_NOW_NS() - ioStart);
                    return;
                }
                nds.ARM9Write32(addr, val);
            }
            else if constexpr (std::is_same_v<T, u16>)
            {
                if (nds.ARM9FastIOWrite16(addr, val))
                {
                    LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowWriteIONs, LITE_PROFILE_NOW_NS() - ioStart);
                    return;
                }
                nds.ARM9Write16(addr, val);
            }
            else
                nds.ARM9Write8(addr, val);
            LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowWriteIONs, LITE_PROFILE_NOW_NS() - ioStart);
            break;
        }

        case 0x06000000:
        {
            LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowWriteVRAM);
            const uint64_t vramStart = LITE_PROFILE_NOW_NS();
            if constexpr (std::is_same_v<T, u32>)
                nds.ARM9Write32(addr, val);
            else if constexpr (std::is_same_v<T, u16>)
                nds.ARM9Write16(addr, val);
            else
                nds.ARM9Write8(addr, val);
            LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowWriteVRAMNs, LITE_PROFILE_NOW_NS() - vramStart);
            break;
        }

        default:
        {
            LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowWriteOther);
            const uint64_t otherStart = LITE_PROFILE_NOW_NS();
            if constexpr (std::is_same_v<T, u32>)
            {
                if ((addr & 0xFF000000) == 0x04000000 && nds.ARM9FastIOWrite32(addr, val))
                {
                    LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowWriteOtherNs, LITE_PROFILE_NOW_NS() - otherStart);
                    return;
                }
                nds.ARM9Write32(addr, val);
            }
            else if constexpr (std::is_same_v<T, u16>)
            {
                if ((addr & 0xFF000000) == 0x04000000 && nds.ARM9FastIOWrite16(addr, val))
                {
                    LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowWriteOtherNs, LITE_PROFILE_NOW_NS() - otherStart);
                    return;
                }
                nds.ARM9Write16(addr, val);
            }
            else
                nds.ARM9Write8(addr, val);
            LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowWriteOtherNs, LITE_PROFILE_NOW_NS() - otherStart);
            break;
        }
        }
    }
}

template <typename T, int ConsoleType>
void SlowWrite7(u32 addr, u32 val)
{
    addr &= ~(sizeof(T) - 1);

    if (std::is_same<T, u32>::value)
        NDS::Current->ARM7Write32(addr, val);
    else if (std::is_same<T, u16>::value)
        NDS::Current->ARM7Write16(addr, val);
    else
        NDS::Current->ARM7Write8(addr, val);
}

template <bool Write>
static inline bool TryDirectDTCMBlockTransfer9(u32 addr, u64* data, u32 num, ARMv5* cpu)
{
    addr &= ~0x3;
    if ((addr & cpu->DTCMMask) != cpu->DTCMBase)
        return false;

    const u32 offset = addr & 0x3FFF;
    const u32 bytes = num * sizeof(u32);
    if (bytes > 0x4000 - offset)
        return false;

    if (num <= 4)
        CopyBlockWordsTiny4<Write>(cpu->DTCM, offset, data, num);
    else
        CopyBlockWords<Write>(cpu->DTCM, offset, data, num);
    return true;
}

template <bool Write, int ConsoleType>
static void SlowBlockTransfer9Impl(u32 addr, u64* data, u32 num, ARMv5* cpu)
{
    addr &= ~0x3;
    auto& nds = cpu->NDS;
    const u32 bytes = num * sizeof(u32);

#if LITEV_PROFILE
    const uint64_t prePathStart = LITE_PROFILE_NOW_NS();
    auto noteRegion = [&](std::atomic<uint64_t>& dtcm,
                          std::atomic<uint64_t>& main,
                          std::atomic<uint64_t>& shared,
                          std::atomic<uint64_t>& io,
                          std::atomic<uint64_t>& other)
    {
        if (addr < cpu->ITCMSize)
        {
            LITE_PROFILE_ADD(other);
        }
        else if ((addr & cpu->DTCMMask) == cpu->DTCMBase)
        {
            LITE_PROFILE_ADD(dtcm);
        }
        else
        {
            switch (addr & 0xFF000000)
            {
            case 0x02000000:
                LITE_PROFILE_ADD(main);
                break;
            case 0x03000000:
                LITE_PROFILE_ADD(shared);
                break;
            case 0x04000000:
                LITE_PROFILE_ADD(io);
                break;
            default:
                LITE_PROFILE_ADD(other);
                break;
            }
        }
    };

    auto noteDTCMSizeBucket = [&](std::atomic<uint64_t>& b1_2,
                                  std::atomic<uint64_t>& b3_4,
                                  std::atomic<uint64_t>& b5_8,
                                  std::atomic<uint64_t>& b9_16,
                                  std::atomic<uint64_t>& b17p)
    {
        if (num <= 2)
            LITE_PROFILE_ADD(b1_2);
        else if (num <= 4)
            LITE_PROFILE_ADD(b3_4);
        else if (num <= 8)
            LITE_PROFILE_ADD(b5_8);
        else if (num <= 16)
            LITE_PROFILE_ADD(b9_16);
        else
            LITE_PROFILE_ADD(b17p);
    };

    auto noteMainRAMSizeBucket = [&](std::atomic<uint64_t>& b1_2,
                                     std::atomic<uint64_t>& b3_4,
                                     std::atomic<uint64_t>& b5_8,
                                     std::atomic<uint64_t>& b9_16,
                                     std::atomic<uint64_t>& b17p)
    {
        if (num <= 2)
            LITE_PROFILE_ADD(b1_2);
        else if (num <= 4)
            LITE_PROFILE_ADD(b3_4);
        else if (num <= 8)
            LITE_PROFILE_ADD(b5_8);
        else if (num <= 16)
            LITE_PROFILE_ADD(b9_16);
        else
            LITE_PROFILE_ADD(b17p);
    };

    if constexpr (Write)
    {
        noteRegion(LiteProfile::gFrame.ARM9SlowBlockWriteDTCM,
            LiteProfile::gFrame.ARM9SlowBlockWriteMainRAM,
            LiteProfile::gFrame.ARM9SlowBlockWriteSharedWRAM,
            LiteProfile::gFrame.ARM9SlowBlockWriteIO,
            LiteProfile::gFrame.ARM9SlowBlockWriteOther);
    }
    else
    {
        noteRegion(LiteProfile::gFrame.ARM9SlowBlockReadDTCM,
            LiteProfile::gFrame.ARM9SlowBlockReadMainRAM,
            LiteProfile::gFrame.ARM9SlowBlockReadSharedWRAM,
            LiteProfile::gFrame.ARM9SlowBlockReadIO,
            LiteProfile::gFrame.ARM9SlowBlockReadOther);
    }

    LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowBlockPrePathNs, LITE_PROFILE_NOW_NS() - prePathStart);
#endif

    if (addr < cpu->ITCMSize)
    {
        u32 offset = addr & 0x7FFF;
        if (bytes <= cpu->ITCMSize - addr)
        {
            if constexpr (Write)
                nds.JIT.CheckAndInvalidateRange<0, ARMJIT_Memory::memregion_ITCM>(addr, bytes);
            CopyBlockWords<Write>(cpu->ITCM, offset, data, num);
            return;
        }
    }
    else if ((addr & cpu->DTCMMask) == cpu->DTCMBase)
    {
        u32 offset = addr & 0x3FFF;
        if (bytes <= 0x4000 - offset)
        {
#if LITEV_PROFILE
            LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastDTCMCalls);
            const uint64_t dtcmPathStart = LITE_PROFILE_NOW_NS();
            const uint64_t dtcmSetupStart = dtcmPathStart;
            std::atomic<uint64_t>* dtcmBucketNs = nullptr;
            if constexpr (Write)
            {
                noteDTCMSizeBucket(LiteProfile::gFrame.ARM9SlowBlockWriteDTCM_1_2,
                    LiteProfile::gFrame.ARM9SlowBlockWriteDTCM_3_4,
                    LiteProfile::gFrame.ARM9SlowBlockWriteDTCM_5_8,
                    LiteProfile::gFrame.ARM9SlowBlockWriteDTCM_9_16,
                    LiteProfile::gFrame.ARM9SlowBlockWriteDTCM_17P);
                if (num <= 2)
                    dtcmBucketNs = &LiteProfile::gFrame.ARM9SlowBlockWriteDTCM_1_2_Ns;
                else if (num <= 4)
                    dtcmBucketNs = &LiteProfile::gFrame.ARM9SlowBlockWriteDTCM_3_4_Ns;
                else if (num <= 8)
                    dtcmBucketNs = &LiteProfile::gFrame.ARM9SlowBlockWriteDTCM_5_8_Ns;
                else if (num <= 16)
                    dtcmBucketNs = &LiteProfile::gFrame.ARM9SlowBlockWriteDTCM_9_16_Ns;
                else
                    dtcmBucketNs = &LiteProfile::gFrame.ARM9SlowBlockWriteDTCM_17P_Ns;
            }
            else
            {
                noteDTCMSizeBucket(LiteProfile::gFrame.ARM9SlowBlockReadDTCM_1_2,
                    LiteProfile::gFrame.ARM9SlowBlockReadDTCM_3_4,
                    LiteProfile::gFrame.ARM9SlowBlockReadDTCM_5_8,
                    LiteProfile::gFrame.ARM9SlowBlockReadDTCM_9_16,
                    LiteProfile::gFrame.ARM9SlowBlockReadDTCM_17P);
                if (num <= 2)
                    dtcmBucketNs = &LiteProfile::gFrame.ARM9SlowBlockReadDTCM_1_2_Ns;
                else if (num <= 4)
                    dtcmBucketNs = &LiteProfile::gFrame.ARM9SlowBlockReadDTCM_3_4_Ns;
                else if (num <= 8)
                    dtcmBucketNs = &LiteProfile::gFrame.ARM9SlowBlockReadDTCM_5_8_Ns;
                else if (num <= 16)
                    dtcmBucketNs = &LiteProfile::gFrame.ARM9SlowBlockReadDTCM_9_16_Ns;
                else
                    dtcmBucketNs = &LiteProfile::gFrame.ARM9SlowBlockReadDTCM_17P_Ns;
            }
            LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowBlockFastDTCMSetupNs, LITE_PROFILE_NOW_NS() - dtcmSetupStart);
            const uint64_t bucketStart = LITE_PROFILE_NOW_NS();
#endif
            if (num <= 4)
                CopyBlockWordsTiny4<Write>(cpu->DTCM, offset, data, num);
            else
                CopyBlockWords<Write>(cpu->DTCM, offset, data, num);
#if LITEV_PROFILE
            if (dtcmBucketNs)
                LITE_PROFILE_ADD_VALUE(*dtcmBucketNs, LITE_PROFILE_NOW_NS() - bucketStart);
            LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowBlockFastDTCMNs, LITE_PROFILE_NOW_NS() - dtcmPathStart);
#endif
            return;
        }
    }
    else
    {
        switch (addr & 0xFF000000)
        {
        case 0x02000000:
        {
#if LITEV_PROFILE
            LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastMainRAMCalls);
            const uint64_t mainPathStart = LITE_PROFILE_NOW_NS();
            const uint64_t mainSetupStart = mainPathStart;
            if constexpr (Write)
            {
                noteMainRAMSizeBucket(LiteProfile::gFrame.ARM9SlowBlockWriteMainRAM_1_2,
                    LiteProfile::gFrame.ARM9SlowBlockWriteMainRAM_3_4,
                    LiteProfile::gFrame.ARM9SlowBlockWriteMainRAM_5_8,
                    LiteProfile::gFrame.ARM9SlowBlockWriteMainRAM_9_16,
                    LiteProfile::gFrame.ARM9SlowBlockWriteMainRAM_17P);
            }
            else
            {
                noteMainRAMSizeBucket(LiteProfile::gFrame.ARM9SlowBlockReadMainRAM_1_2,
                    LiteProfile::gFrame.ARM9SlowBlockReadMainRAM_3_4,
                    LiteProfile::gFrame.ARM9SlowBlockReadMainRAM_5_8,
                    LiteProfile::gFrame.ARM9SlowBlockReadMainRAM_9_16,
                    LiteProfile::gFrame.ARM9SlowBlockReadMainRAM_17P);
            }
            LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowBlockFastMainRAMSetupNs, LITE_PROFILE_NOW_NS() - mainSetupStart);
#endif
            if (TryBlockTransferLinear<Write, 0, ARMJIT_Memory::memregion_MainRAM>(
                    nds.JIT, nds.MainRAM, nds.MainRAMMask, addr, data, num))
            {
#if LITEV_PROFILE
                LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowBlockFastMainRAMNs, LITE_PROFILE_NOW_NS() - mainPathStart);
#endif
                return;
            }
            break;
        }

        case 0x03000000:
            if (nds.SWRAM_ARM9.Mem
                && TryBlockTransferLinear<Write, 0, ARMJIT_Memory::memregion_SharedWRAM>(
                    nds.JIT, nds.SWRAM_ARM9.Mem, nds.SWRAM_ARM9.Mask, addr, data, num))
                return;
            break;
        }
    }

#if LITEV_PROFILE
    LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFallbackLoopCalls);
    const uint64_t fallbackLoopStart = LITE_PROFILE_NOW_NS();
#endif
    for (u32 i = 0; i < num; i++)
    {
        if (Write)
            SlowWrite9<u32, ConsoleType>(addr, cpu, data[i]);
        else
            data[i] = SlowRead9<u32, ConsoleType>(addr, cpu);
        addr += 4;
    }
#if LITEV_PROFILE
    LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowBlockFallbackLoopNs, LITE_PROFILE_NOW_NS() - fallbackLoopStart);
#endif
}

template <int Tag>
static inline void NoteSlowBlockSource()
{
#if LITEV_PROFILE
    switch (Tag)
    {
    case SlowBlockProfile_GenericLoad:
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadCalls);
        break;
    case SlowBlockProfile_GenericStore:
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericStoreCalls);
        break;
    case SlowBlockProfile_FastStackLoad:
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStackLoadCalls);
        break;
    case SlowBlockProfile_FastStore:
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStoreCalls);
        break;
    case SlowBlockProfile_GenericLoadFastmemOff:
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadCalls);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadFastmemOff);
        break;
    case SlowBlockProfile_GenericLoadUsermode:
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadCalls);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadUsermode);
        break;
    case SlowBlockProfile_GenericLoadCondIncompatible:
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadCalls);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadCondIncompatible);
        break;
    case SlowBlockProfile_GenericLoadNonStackDTCM:
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadCalls);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadNonFastPathShape);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadTargetDTCM);
        break;
    case SlowBlockProfile_GenericLoadNonStackMainRAM:
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadCalls);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadNonFastPathShape);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadTargetMainRAM);
        break;
    case SlowBlockProfile_GenericLoadNonStackSharedWRAM:
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadCalls);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadNonFastPathShape);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadTargetSharedWRAM);
        break;
    case SlowBlockProfile_GenericLoadNonStackIO:
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadCalls);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadNonFastPathShape);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadTargetIO);
        break;
    case SlowBlockProfile_GenericLoadNonStackOther:
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadCalls);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadNonFastPathShape);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadTargetOther);
        break;
    case SlowBlockProfile_GenericStoreFastmemOff:
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericStoreCalls);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericStoreFastmemOff);
        break;
    case SlowBlockProfile_GenericStoreUsermode:
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericStoreCalls);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericStoreUsermode);
        break;
    case SlowBlockProfile_GenericStoreCondIncompatible:
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericStoreCalls);
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericStoreCondIncompatible);
        break;
    }
#endif
}

#if LITEV_PROFILE
static inline void NoteSlowBlockGenericLoadShape(u32 meta)
{
    if (meta & SlowBlockProfileShape_Thumb)
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadShapeThumb);
    else
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadShapeARM);

    if (meta & SlowBlockProfileShape_SkipBase)
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadShapeSkipBase);
    else
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadShapeKeepBase);

    if (meta & SlowBlockProfileShape_BaseSP)
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadShapeBaseSP);
    else
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadShapeBaseOther);

    if (meta & SlowBlockProfileShape_Regs5P)
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadShapeRegs5P);
    else if (meta & SlowBlockProfileShape_Regs3_4)
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadShapeRegs3_4);
    else
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadShapeRegs1_2);

    if (meta & SlowBlockProfileShape_Dec)
    {
        if (meta & SlowBlockProfileShape_Pre)
            LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadShapeDecPre);
        else
            LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadShapeDecPost);
    }
    else
    {
        if (meta & SlowBlockProfileShape_Pre)
            LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadShapeIncPre);
        else
            LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockGenericLoadShapeIncPost);
    }
}
#endif

template <bool Write, int ConsoleType>
void SlowBlockTransfer9(u32 addr, u64* data, u32 num, ARMv5* cpu)
{
    LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockTransferCalls);
    if constexpr (Write)
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockTransferWrites);
    else
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockTransferReads);
    LITE_PROFILE_SCOPE(timer, LiteProfile::gFrame.ARM9SlowBlockTransferNs);
    SlowBlockTransfer9Impl<Write, ConsoleType>(addr, data, num, cpu);
}

template <bool Write, int ConsoleType, int Tag>
void SlowBlockTransfer9Profiled(u32 addr, u64* data, u32 num, ARMv5* cpu)
{
    const uint64_t totalStart = LITE_PROFILE_NOW_NS();
    NoteSlowBlockSource<Tag>();
    const uint64_t sourceStart = LITE_PROFILE_NOW_NS();
    constexpr bool kShapeTaggedLoad = !Write
        && (Tag == SlowBlockProfile_GenericLoadNonStackDTCM
            || Tag == SlowBlockProfile_GenericLoadNonStackMainRAM
            || Tag == SlowBlockProfile_GenericLoadNonStackSharedWRAM
            || Tag == SlowBlockProfile_GenericLoadNonStackIO
            || Tag == SlowBlockProfile_GenericLoadNonStackOther);
    if constexpr (kShapeTaggedLoad)
    {
#if LITEV_PROFILE
        NoteSlowBlockGenericLoadShape(num >> SlowBlockProfileShapeShift);
#endif
        num &= SlowBlockProfileNumMask;
    }
    SlowBlockTransfer9<Write, ConsoleType>(addr, data, num, cpu);
#if LITEV_PROFILE
    const uint64_t totalElapsed = LITE_PROFILE_NOW_NS() - totalStart;
    const uint64_t sourceElapsed = LITE_PROFILE_NOW_NS() - sourceStart;
    if constexpr (!Write)
    {
        LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowBlockGenericLoadNs, sourceElapsed);
        LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowBlockGenericLoadTotalNs, totalElapsed);
    }
    else
    {
        LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowBlockGenericStoreNs, sourceElapsed);
        LITE_PROFILE_ADD_VALUE(LiteProfile::gFrame.ARM9SlowBlockGenericStoreTotalNs, totalElapsed);
    }
#endif
}

template <bool Write, int ConsoleType, int Tag>
void SlowBlockTransfer9FastDTCMProfiled(u32 addr, u64* data, u32 num, ARMv5* cpu)
{
    LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockTransferCalls);
    if constexpr (Write)
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockTransferWrites);
    else
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockTransferReads);
    NoteSlowBlockSource<Tag>();

    if (TryDirectDTCMBlockTransfer9<Write>(addr, data, num, cpu))
    {
#if LITEV_PROFILE
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastDTCMDirectCalls);
        if constexpr (Tag == SlowBlockProfile_FastStackLoad)
        {
            if (num <= 2)
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStackLoad_1_2);
            }
            else if (num <= 4)
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStackLoad_3_4);
            }
            else if (num <= 8)
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStackLoad_5_8);
            }
            else
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStackLoad_9P);
            }
        }
        else if constexpr (Tag == SlowBlockProfile_FastStore)
        {
            if (num <= 2)
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStore_1_2);
            }
            else if (num <= 4)
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStore_3_4);
            }
            else if (num <= 8)
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStore_5_8);
            }
            else
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStore_9P);
            }
        }
#endif
        return;
    }

    SlowBlockTransfer9Impl<Write, ConsoleType>(addr, data, num, cpu);
#if LITEV_PROFILE
    LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastDTCMFallbackCalls);
#endif
}

template <bool Write, int ConsoleType, int Tag, u32 FixedNum>
void SlowBlockTransfer9FastDTCMFixedProfiled(u32 addr, u64* data, ARMv5* cpu)
{
    LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockTransferCalls);
    if constexpr (Write)
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockTransferWrites);
    else
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockTransferReads);
    NoteSlowBlockSource<Tag>();

    constexpr u32 num = FixedNum;
    if (TryDirectDTCMBlockTransfer9<Write>(addr, data, num, cpu))
    {
#if LITEV_PROFILE
        LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastDTCMDirectCalls);
        if constexpr (Tag == SlowBlockProfile_FastStackLoad)
        {
            if (num <= 2)
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStackLoad_1_2);
            }
            else if (num <= 4)
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStackLoad_3_4);
            }
            else if (num <= 8)
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStackLoad_5_8);
            }
            else
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStackLoad_9P);
            }
        }
        else if constexpr (Tag == SlowBlockProfile_FastStore)
        {
            if (num <= 2)
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStore_1_2);
            }
            else if (num <= 4)
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStore_3_4);
            }
            else if (num <= 8)
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStore_5_8);
            }
            else
            {
                LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastStore_9P);
            }
        }
#endif
        return;
    }

    SlowBlockTransfer9Impl<Write, ConsoleType>(addr, data, num, cpu);
#if LITEV_PROFILE
    LITE_PROFILE_ADD(LiteProfile::gFrame.ARM9SlowBlockFastDTCMFallbackCalls);
#endif
}

template <bool Write, int ConsoleType>
void SlowBlockTransfer7(u32 addr, u64* data, u32 num)
{
    addr &= ~0x3;
    for (u32 i = 0; i < num; i++)
    {
        if (Write)
            SlowWrite7<u32, ConsoleType>(addr, data[i]);
        else
            data[i] = SlowRead7<u32, ConsoleType>(addr);
        addr += 4;
    }
}

#define INSTANTIATE_SLOWMEM(consoleType) \
    template void SlowWrite9<u32, consoleType>(u32, ARMv5*, u32); \
    template void SlowWrite9<u16, consoleType>(u32, ARMv5*, u32); \
    template void SlowWrite9<u8, consoleType>(u32, ARMv5*, u32); \
    \
    template u32 SlowRead9<u32, consoleType>(u32, ARMv5*); \
    template u16 SlowRead9<u16, consoleType>(u32, ARMv5*); \
    template u8 SlowRead9<u8, consoleType>(u32, ARMv5*); \
    \
    template void SlowWrite7<u32, consoleType>(u32, u32); \
    template void SlowWrite7<u16, consoleType>(u32, u32); \
    template void SlowWrite7<u8, consoleType>(u32, u32); \
    \
    template u32 SlowRead7<u32, consoleType>(u32); \
    template u16 SlowRead7<u16, consoleType>(u32); \
    template u8 SlowRead7<u8, consoleType>(u32); \
    \
    template void SlowBlockTransfer9<false, consoleType>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9<true, consoleType>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9Profiled<false, consoleType, SlowBlockProfile_GenericLoad>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9Profiled<true, consoleType, SlowBlockProfile_GenericStore>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9Profiled<false, consoleType, SlowBlockProfile_FastStackLoad>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9Profiled<true, consoleType, SlowBlockProfile_FastStore>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9Profiled<false, consoleType, SlowBlockProfile_GenericLoadFastmemOff>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9Profiled<false, consoleType, SlowBlockProfile_GenericLoadUsermode>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9Profiled<false, consoleType, SlowBlockProfile_GenericLoadCondIncompatible>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9Profiled<false, consoleType, SlowBlockProfile_GenericLoadNonStackDTCM>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9Profiled<false, consoleType, SlowBlockProfile_GenericLoadNonStackMainRAM>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9Profiled<false, consoleType, SlowBlockProfile_GenericLoadNonStackSharedWRAM>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9Profiled<false, consoleType, SlowBlockProfile_GenericLoadNonStackIO>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9Profiled<false, consoleType, SlowBlockProfile_GenericLoadNonStackOther>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9Profiled<true, consoleType, SlowBlockProfile_GenericStoreFastmemOff>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9Profiled<true, consoleType, SlowBlockProfile_GenericStoreUsermode>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9Profiled<true, consoleType, SlowBlockProfile_GenericStoreCondIncompatible>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9FastDTCMProfiled<false, consoleType, SlowBlockProfile_FastStackLoad>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9FastDTCMProfiled<true, consoleType, SlowBlockProfile_FastStore>(u32, u64*, u32, ARMv5*); \
    template void SlowBlockTransfer9FastDTCMFixedProfiled<false, consoleType, SlowBlockProfile_FastStackLoad, 1>(u32, u64*, ARMv5*); \
    template void SlowBlockTransfer9FastDTCMFixedProfiled<false, consoleType, SlowBlockProfile_FastStackLoad, 2>(u32, u64*, ARMv5*); \
    template void SlowBlockTransfer9FastDTCMFixedProfiled<false, consoleType, SlowBlockProfile_FastStackLoad, 3>(u32, u64*, ARMv5*); \
    template void SlowBlockTransfer9FastDTCMFixedProfiled<false, consoleType, SlowBlockProfile_FastStackLoad, 4>(u32, u64*, ARMv5*); \
    template void SlowBlockTransfer9FastDTCMFixedProfiled<true, consoleType, SlowBlockProfile_FastStore, 1>(u32, u64*, ARMv5*); \
    template void SlowBlockTransfer9FastDTCMFixedProfiled<true, consoleType, SlowBlockProfile_FastStore, 2>(u32, u64*, ARMv5*); \
    template void SlowBlockTransfer9FastDTCMFixedProfiled<true, consoleType, SlowBlockProfile_FastStore, 3>(u32, u64*, ARMv5*); \
    template void SlowBlockTransfer9FastDTCMFixedProfiled<true, consoleType, SlowBlockProfile_FastStore, 4>(u32, u64*, ARMv5*); \
    template void SlowBlockTransfer7<false, consoleType>(u32 addr, u64* data, u32 num); \
    template void SlowBlockTransfer7<true, consoleType>(u32 addr, u64* data, u32 num); \

INSTANTIATE_SLOWMEM(0)
INSTANTIATE_SLOWMEM(1)

ARMJIT::~ARMJIT() noexcept
{
    JitEnableWrite();
    ResetBlockCache();
}

void ARMJIT::Reset() noexcept
{
    JitEnableWrite();
    ResetBlockCache();

    Memory.Reset();
}

void FloodFillSetFlags(FetchedInstr instrs[], int start, u8 flags)
{
    for (int j = start; j >= 0; j--)
    {
        u8 match = instrs[j].Info.WriteFlags & flags;
        u8 matchMaybe = (instrs[j].Info.WriteFlags >> 4) & flags;
        if (matchMaybe) // writes flags maybe
            instrs[j].SetFlags |= matchMaybe;
        if (match)
        {
            instrs[j].SetFlags |= match;
            flags &= ~match;
            if (!flags)
                return;
        }
    }
}

bool DecodeLiteral(bool thumb, const FetchedInstr& instr, u32& addr)
{
    if (!thumb)
    {
        switch (instr.Info.Kind)
        {
        case ARMInstrInfo::ak_LDR_IMM:
        case ARMInstrInfo::ak_LDRB_IMM:
            addr = (instr.Addr + 8) + ((instr.Instr & 0xFFF) * (instr.Instr & (1 << 23) ? 1 : -1));
            return true;
        case ARMInstrInfo::ak_LDRH_IMM:
            addr = (instr.Addr + 8) + (((instr.Instr & 0xF00) >> 4 | (instr.Instr & 0xF)) * (instr.Instr & (1 << 23) ? 1 : -1));
            return true;
        default:
            break;
        }
    }
    else if (instr.Info.Kind == ARMInstrInfo::tk_LDR_PCREL)
    {
        addr = ((instr.Addr + 4) & ~0x2) + ((instr.Instr & 0xFF) << 2);
        return true;
    }

    JIT_DEBUGPRINT("Literal %08x %x not recognised %d\n", instr.Instr, instr.Addr, instr.Info.Kind);
    return false;
}

bool DecodeBranch(bool thumb, const FetchedInstr& instr, u32& cond, bool hasLink, u32 lr, bool& link,
    u32& linkAddr, u32& targetAddr)
{
    if (thumb)
    {
        u32 r15 = instr.Addr + 4;
        cond = 0xE;

        link = instr.Info.Kind == ARMInstrInfo::tk_BL_LONG;
        linkAddr = instr.Addr + 4;

        if (instr.Info.Kind == ARMInstrInfo::tk_BL_LONG && !(instr.Instr & (1 << 12)))
        {
            targetAddr = r15 + ((s32)((instr.Instr & 0x7FF) << 21) >> 9);
            targetAddr += ((instr.Instr >> 16) & 0x7FF) << 1;
            return true;
        }
        else if (instr.Info.Kind == ARMInstrInfo::tk_B)
        {
            s32 offset = (s32)((instr.Instr & 0x7FF) << 21) >> 20;
            targetAddr = r15 + offset;
            return true;
        }
        else if (instr.Info.Kind == ARMInstrInfo::tk_BCOND)
        {
            cond = (instr.Instr >> 8) & 0xF;
            s32 offset = (s32)(instr.Instr << 24) >> 23;
            targetAddr = r15 + offset;
            return true;
        }
        else if (hasLink && instr.Info.Kind == ARMInstrInfo::tk_BX && instr.A_Reg(3) == 14)
        {
            JIT_DEBUGPRINT("returning!\n");
            targetAddr = lr;
            return true;
        }
    }
    else
    {
        link = instr.Info.Kind == ARMInstrInfo::ak_BL;
        linkAddr = instr.Addr + 4;

        cond = instr.Cond();
        if (instr.Info.Kind == ARMInstrInfo::ak_BL
            || instr.Info.Kind == ARMInstrInfo::ak_B)
        {
            s32 offset = (s32)(instr.Instr << 8) >> 6;
            u32 r15 = instr.Addr + 8;
            targetAddr = r15 + offset;
            return true;
        }
        else if (hasLink && instr.Info.Kind == ARMInstrInfo::ak_BX && instr.A_Reg(0) == 14)
        {
            JIT_DEBUGPRINT("returning!\n");
            targetAddr = lr;
            return true;
        }
    }
    return false;
}

bool IsIdleLoop(bool thumb, FetchedInstr* instrs, int instrsCount)
{
    // see https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/PowerPC/PPCAnalyst.cpp#L678
    // it basically checks if one iteration of a loop depends on another
    // the rules are quite simple

    JIT_DEBUGPRINT("checking potential idle loop\n");
    u16 regsWrittenTo = 0;
    u16 regsDisallowedToWrite = 0;
    for (int i = 0; i < instrsCount; i++)
    {
        JIT_DEBUGPRINT("instr %d %08x regs(%x %x) %x %x\n", i, instrs[i].Instr, instrs[i].Info.DstRegs, instrs[i].Info.SrcRegs, regsWrittenTo, regsDisallowedToWrite);
        if (instrs[i].Info.SpecialKind == ARMInstrInfo::special_WriteMem)
            return false;
        if (!thumb && instrs[i].Info.Kind >= ARMInstrInfo::ak_MSR_IMM && instrs[i].Info.Kind <= ARMInstrInfo::ak_MRC)
            return false;
        if (i < instrsCount - 1 && instrs[i].Info.Branches())
            return false;

        u16 srcRegs = instrs[i].Info.SrcRegs & ~(1 << 15);
        u16 dstRegs = instrs[i].Info.DstRegs & ~(1 << 15);

        regsDisallowedToWrite |= srcRegs & ~regsWrittenTo;

        if (dstRegs & regsDisallowedToWrite)
            return false;
        regsWrittenTo |= dstRegs;
    }
    return true;
}

typedef void (*InterpreterFunc)(ARM* cpu);

void NOP(ARM* cpu) {}

#define F(x) &ARMInterpreter::A_##x
#define F_ALU(name, s) \
    F(name##_REG_LSL_IMM##s), F(name##_REG_LSR_IMM##s), F(name##_REG_ASR_IMM##s), F(name##_REG_ROR_IMM##s), \
    F(name##_REG_LSL_REG##s), F(name##_REG_LSR_REG##s), F(name##_REG_ASR_REG##s), F(name##_REG_ROR_REG##s), F(name##_IMM##s)
#define F_MEM_WB(name) \
    F(name##_REG_LSL), F(name##_REG_LSR), F(name##_REG_ASR), F(name##_REG_ROR), F(name##_IMM), \
    F(name##_POST_REG_LSL), F(name##_POST_REG_LSR), F(name##_POST_REG_ASR), F(name##_POST_REG_ROR), F(name##_POST_IMM)
#define F_MEM_HD(name) \
    F(name##_REG), F(name##_IMM), F(name##_POST_REG), F(name##_POST_IMM)
InterpreterFunc InterpretARM[ARMInstrInfo::ak_Count] =
{
    F_ALU(AND,), F_ALU(AND,_S),
    F_ALU(EOR,), F_ALU(EOR,_S),
    F_ALU(SUB,), F_ALU(SUB,_S),
    F_ALU(RSB,), F_ALU(RSB,_S),
    F_ALU(ADD,), F_ALU(ADD,_S),
    F_ALU(ADC,), F_ALU(ADC,_S),
    F_ALU(SBC,), F_ALU(SBC,_S),
    F_ALU(RSC,), F_ALU(RSC,_S),
    F_ALU(ORR,), F_ALU(ORR,_S),
    F_ALU(MOV,), F_ALU(MOV,_S),
    F_ALU(BIC,), F_ALU(BIC,_S),
    F_ALU(MVN,), F_ALU(MVN,_S),
    F_ALU(TST,),
    F_ALU(TEQ,),
    F_ALU(CMP,),
    F_ALU(CMN,),

    F(MUL), F(MLA), F(UMULL), F(UMLAL), F(SMULL), F(SMLAL), F(SMLAxy), F(SMLAWy), F(SMULWy), F(SMLALxy), F(SMULxy),
    F(CLZ), F(QADD), F(QSUB), F(QDADD), F(QDSUB),

    F_MEM_WB(STR),
    F_MEM_WB(STRB),
    F_MEM_WB(LDR),
    F_MEM_WB(LDRB),

    F_MEM_HD(STRH),
    F_MEM_HD(LDRD),
    F_MEM_HD(STRD),
    F_MEM_HD(LDRH),
    F_MEM_HD(LDRSB),
    F_MEM_HD(LDRSH),

    F(SWP), F(SWPB),
    F(LDM), F(STM),

    F(B), F(BL), F(BLX_IMM), F(BX), F(BLX_REG),
    F(UNK), F(MSR_IMM), F(MSR_REG), F(MRS), F(MCR), F(MRC), F(SVC),
    NOP
};
#undef F_ALU
#undef F_MEM_WB
#undef F_MEM_HD
#undef F

void T_BL_LONG(ARM* cpu)
{
    ARMInterpreter::T_BL_LONG_1(cpu);
    cpu->R[15] += 2;
    ARMInterpreter::T_BL_LONG_2(cpu);
}

#define F(x) ARMInterpreter::T_##x
InterpreterFunc InterpretTHUMB[ARMInstrInfo::tk_Count] =
{
    F(LSL_IMM), F(LSR_IMM), F(ASR_IMM),
    F(ADD_REG_), F(SUB_REG_), F(ADD_IMM_), F(SUB_IMM_),
    F(MOV_IMM), F(CMP_IMM), F(ADD_IMM), F(SUB_IMM),
    F(AND_REG), F(EOR_REG), F(LSL_REG), F(LSR_REG), F(ASR_REG),
    F(ADC_REG), F(SBC_REG), F(ROR_REG), F(TST_REG), F(NEG_REG),
    F(CMP_REG), F(CMN_REG), F(ORR_REG), F(MUL_REG), F(BIC_REG), F(MVN_REG),
    F(ADD_HIREG), F(CMP_HIREG), F(MOV_HIREG),
    F(ADD_PCREL), F(ADD_SPREL), F(ADD_SP),
    F(LDR_PCREL), F(STR_REG), F(STRB_REG), F(LDR_REG), F(LDRB_REG), F(STRH_REG),
    F(LDRSB_REG), F(LDRH_REG), F(LDRSH_REG), F(STR_IMM), F(LDR_IMM), F(STRB_IMM),
    F(LDRB_IMM), F(STRH_IMM), F(LDRH_IMM), F(STR_SPREL), F(LDR_SPREL),
    F(PUSH), F(POP), F(LDMIA), F(STMIA),
    F(BCOND), F(BX), F(BLX_REG), F(B), F(BL_LONG_1), F(BL_LONG_2),
    F(UNK), F(SVC),
    T_BL_LONG // BL_LONG psudo opcode
};
#undef F

static constexpr u32 LITEV_JIT_MAX_BLOCK_SIZE = 512;

ARMJIT::ARMJIT(melonDS::NDS& nds, std::optional<JITArgs> jit) noexcept :
        NDS(nds),
        Memory(nds),
        JITCompiler(nds),
        MaxBlockSize(jit.has_value() ? std::clamp(jit->MaxBlockSize, 1u, LITEV_JIT_MAX_BLOCK_SIZE) : 32),
        LiteralOptimizations(jit.has_value() ? jit->LiteralOptimizations : false),
        BranchOptimizations(jit.has_value() ? jit->BranchOptimizations : false),
        FastMemory((jit.has_value() ? jit->FastMemory : false) && ARMJIT_Memory::IsFastMemSupported())
{}

void ARMJIT::RetireJitBlock(JitBlock* block) noexcept
{
    auto it = RestoreCandidates.find(block->InstrHash);
    if (it != RestoreCandidates.end())
    {
        delete it->second;
        it->second = block;
    }
    else
    {
        RestoreCandidates[block->InstrHash] = block;
    }
}

void ARMJIT::SetJITArgs(JITArgs args) noexcept
{
    args.FastMemory = args.FastMemory && ARMJIT_Memory::IsFastMemSupported();
    args.MaxBlockSize = std::clamp(args.MaxBlockSize, 1u, LITEV_JIT_MAX_BLOCK_SIZE);

    if (MaxBlockSize != args.MaxBlockSize
        || LiteralOptimizations != args.LiteralOptimizations
        || BranchOptimizations != args.BranchOptimizations
        || FastMemory != args.FastMemory)
        ResetBlockCache();

    MaxBlockSize = args.MaxBlockSize;
    LiteralOptimizations = args.LiteralOptimizations;
    BranchOptimizations = args.BranchOptimizations;
    FastMemory = args.FastMemory;
}

void ARMJIT::SetMaxBlockSize(int size) noexcept
{
    SetJITArgs(JITArgs{static_cast<unsigned>(size), LiteralOptimizations, LiteralOptimizations, FastMemory});
}

void ARMJIT::SetLiteralOptimizations(bool enabled) noexcept
{
    SetJITArgs(JITArgs{static_cast<unsigned>(MaxBlockSize), enabled, BranchOptimizations, FastMemory});
}

void ARMJIT::SetBranchOptimizations(bool enabled) noexcept
{
    SetJITArgs(JITArgs{static_cast<unsigned>(MaxBlockSize), LiteralOptimizations, enabled, FastMemory});
}

void ARMJIT::SetFastMemory(bool enabled) noexcept
{
    SetJITArgs(JITArgs{static_cast<unsigned>(MaxBlockSize), LiteralOptimizations, BranchOptimizations, enabled});
}

void ARMJIT::CompileBlock(ARM* cpu) noexcept
{
    NDS.ARM9.ClearJitCache();
    NDS.ARM7.ClearJitCache();

    bool thumb = cpu->CPSR & 0x20;
    u32 blockAddr = cpu->R[15] - (thumb ? 2 : 4);

    u32 localAddr = LocaliseCodeAddress(cpu->Num, blockAddr);
    if (!localAddr)
    {
        Log(LogLevel::Warn, "trying to compile non executable code? %x\n", blockAddr);
    }

    auto& map = cpu->Num == 0 ? JitBlocks9 : JitBlocks7;
    auto existingBlockIt = map.find(blockAddr);
    if (existingBlockIt != map.end())
    {
        // there's already a block, though it's not inside the fast map
        // could be that there are two blocks at the same physical addr
        // but different mirrors
        u32 otherLocalAddr = existingBlockIt->second->StartAddrLocal;

        if (localAddr == otherLocalAddr)
        {
            JIT_DEBUGPRINT("switching out block %x %x %x\n", localAddr, blockAddr, existingBlockIt->second->StartAddr);

            u64* entry = &FastBlockLookupRegions[localAddr >> 27][(localAddr & 0x7FFFFFF) / 2];
            *entry = ((u64)blockAddr | cpu->Num) << 32;
            *entry |= JITCompiler.SubEntryOffset(existingBlockIt->second->EntryPoint);
            return;
        }

        // some memory has been remapped
        RetireJitBlock(existingBlockIt->second);
        map.erase(existingBlockIt);
    }

    FetchedInstr instrs[MaxBlockSize];
    int i = 0;
    u32 r15 = cpu->R[15];

    u32 addressRanges[MaxBlockSize];
    u32 addressMasks[MaxBlockSize];
    memset(addressMasks, 0, MaxBlockSize * sizeof(u32));
    u32 numAddressRanges = 0;

    u32 numLiterals = 0;
    u32 literalLoadAddrs[MaxBlockSize];
    // they are going to be hashed
    u32 literalValues[MaxBlockSize];
    u32 instrValues[MaxBlockSize];
    // due to instruction merging i might not reflect the amount of actual instructions
    u32 numInstrs = 0;

    u32 writeAddrs[MaxBlockSize];
    u32 numWriteAddrs = 0, writeAddrsTranslated = 0;

    cpu->FillPipeline();
    u32 nextInstr[2] = {cpu->NextInstr[0], cpu->NextInstr[1]};
    u32 nextInstrAddr[2] = {blockAddr, r15};

    JIT_DEBUGPRINT("start block %x %08x (%x)\n", blockAddr, cpu->CPSR, localAddr);

    u32 lastSegmentStart = blockAddr;
    u32 lr;
    bool hasLink = false;

    bool hasMemoryInstr = false;
#if LITEV_PROFILE
    bool hasLoadInstr = false;
    bool hasStoreInstr = false;
    u8 memRegionMask = 0;
    u16 execALUCount = 0;
    u16 execMulCount = 0;
    u16 execSingleLoadCount = 0;
    u16 execSingleStoreCount = 0;
    u16 execBlockLoadCount = 0;
    u16 execBlockStoreCount = 0;
    u16 execStackBlockLoadCount = 0;
    u16 execStackBlockStoreCount = 0;
    u16 execStackLoadCount = 0;
    u16 execStackStoreCount = 0;
    u16 execBranchCondCount = 0;
    u16 execBranchImmCount = 0;
    u16 execBranchRegCount = 0;
    u16 execSysCount = 0;
    u16 execOtherCount = 0;
    u16 execLiteralLoadCount = 0;
    u16 execMemITCMCount = 0;
    u16 execMemDTCMCount = 0;
    u16 execMemMainCount = 0;
    u16 execMemSharedCount = 0;
    u16 execMemIOCount = 0;
    u16 execMemVRAMCount = 0;
    u16 execMemOtherCount = 0;

    auto classifyMemRegionBit = [&](u32 dataAddr) -> u8
    {
        if (cpu->Num == 0)
        {
            ARMv5* cpuv5 = static_cast<ARMv5*>(cpu);
            if (dataAddr < cpuv5->ITCMSize)
                return 1 << 0;
            if ((dataAddr & cpuv5->DTCMMask) == cpuv5->DTCMBase)
                return 1 << 1;
        }

        switch (dataAddr >> 24)
        {
        case 0x02: return 1 << 2;
        case 0x03: return 1 << 3;
        case 0x04: return 1 << 4;
        case 0x06: return 1 << 5;
        default:   return 1 << 6;
        }
    };

    auto noteMemRegion = [&](u8 regionBit)
    {
        switch (regionBit)
        {
        case 1 << 0: execMemITCMCount++; break;
        case 1 << 1: execMemDTCMCount++; break;
        case 1 << 2: execMemMainCount++; break;
        case 1 << 3: execMemSharedCount++; break;
        case 1 << 4: execMemIOCount++; break;
        case 1 << 5: execMemVRAMCount++; break;
        default:     execMemOtherCount++; break;
        }
    };
#endif

    do
    {
        r15 += thumb ? 2 : 4;

        instrs[i].BranchFlags = 0;
        instrs[i].SetFlags = 0;
        instrs[i].Instr = nextInstr[0];
        nextInstr[0] = nextInstr[1];

        instrs[i].Addr = nextInstrAddr[0];
        nextInstrAddr[0] = nextInstrAddr[1];
        nextInstrAddr[1] = r15;
        JIT_DEBUGPRINT("instr %08x %x\n", instrs[i].Instr & (thumb ? 0xFFFF : ~0), instrs[i].Addr);

        instrValues[numInstrs++] = instrs[i].Instr;

        u32 translatedAddr = LocaliseCodeAddress(cpu->Num, instrs[i].Addr);
        assert(translatedAddr >> 27);
        u32 translatedAddrRounded = translatedAddr & ~0x1FF;
        if (i == 0 || translatedAddrRounded != addressRanges[numAddressRanges - 1])
        {
            bool returning = false;
            for (u32 j = 0; j < numAddressRanges; j++)
            {
                if (addressRanges[j] == translatedAddrRounded)
                {
                    std::swap(addressRanges[j], addressRanges[numAddressRanges - 1]);
                    std::swap(addressMasks[j], addressMasks[numAddressRanges - 1]);
                    returning = true;
                    break;
                }
            }
            if (!returning)
                addressRanges[numAddressRanges++] = translatedAddrRounded;
        }
        addressMasks[numAddressRanges - 1] |= 1 << ((translatedAddr & 0x1FF) / 16);

        if (cpu->Num == 0)
        {
            ARMv5* cpuv5 = (ARMv5*)cpu;
            if (thumb && r15 & 0x2)
            {
                nextInstr[1] >>= 16;
                instrs[i].CodeCycles = 0;
            }
            else
            {
                nextInstr[1] = cpuv5->CodeRead32(r15, false);
                instrs[i].CodeCycles = cpu->CodeCycles;
            }
        }
        else
        {
            ARMv4* cpuv4 = (ARMv4*)cpu;
            if (thumb)
                nextInstr[1] = cpuv4->CodeRead16(r15);
            else
                nextInstr[1] = cpuv4->CodeRead32(r15);
            instrs[i].CodeCycles = cpu->CodeCycles;
        }
        instrs[i].Info = ARMInstrInfo::Decode(thumb, cpu->Num, instrs[i].Instr, LiteralOptimizations);

        hasMemoryInstr |= thumb
            ? (instrs[i].Info.Kind >= ARMInstrInfo::tk_LDR_PCREL && instrs[i].Info.Kind <= ARMInstrInfo::tk_STMIA)
            : (instrs[i].Info.Kind >= ARMInstrInfo::ak_STR_REG_LSL && instrs[i].Info.Kind <= ARMInstrInfo::ak_STM);

        cpu->R[15] = r15;
        cpu->CurInstr = instrs[i].Instr;
        cpu->CodeCycles = instrs[i].CodeCycles;

        if (instrs[i].Info.DstRegs & (1 << 14)
            || (!thumb
                && (instrs[i].Info.Kind == ARMInstrInfo::ak_MSR_IMM || instrs[i].Info.Kind == ARMInstrInfo::ak_MSR_REG)
                && instrs[i].Instr & (1 << 16)))
            hasLink = false;

        if (thumb)
        {
            InterpretTHUMB[instrs[i].Info.Kind](cpu);
        }
        else
        {
            if (cpu->Num == 0 && instrs[i].Info.Kind == ARMInstrInfo::ak_BLX_IMM)
            {
                ARMInterpreter::A_BLX_IMM(cpu);
            }
            else
            {
                u32 icode = ((instrs[i].Instr >> 4) & 0xF) | ((instrs[i].Instr >> 16) & 0xFF0);
                assert(InterpretARM[instrs[i].Info.Kind] == ARMInterpreter::ARMInstrTable[icode]
                    || instrs[i].Info.Kind == ARMInstrInfo::ak_MOV_REG_LSL_IMM
                    || instrs[i].Info.Kind == ARMInstrInfo::ak_Nop
                    || instrs[i].Info.Kind == ARMInstrInfo::ak_UNK);
                if (cpu->CheckCondition(instrs[i].Cond()))
                    InterpretARM[instrs[i].Info.Kind](cpu);
                else
                    cpu->AddCycles_C();
            }
        }

        instrs[i].DataCycles = cpu->DataCycles;
        instrs[i].DataRegion = cpu->DataRegion;
#if LITEV_PROFILE
        const u16 kind = instrs[i].Info.Kind;
        const bool isLiteralLoad = instrs[i].Info.SpecialKind == ARMInstrInfo::special_LoadLiteral;
        const bool isMemLoad = instrs[i].Info.SpecialKind == ARMInstrInfo::special_LoadMem || isLiteralLoad;
        const bool isMemStore = instrs[i].Info.SpecialKind == ARMInstrInfo::special_WriteMem;
        const bool isBlockLoad = thumb
            ? (kind == ARMInstrInfo::tk_POP || kind == ARMInstrInfo::tk_LDMIA)
            : (kind == ARMInstrInfo::ak_LDM);
        const bool isBlockStore = thumb
            ? (kind == ARMInstrInfo::tk_PUSH || kind == ARMInstrInfo::tk_STMIA)
            : (kind == ARMInstrInfo::ak_STM);
        const bool isBranchCond = thumb && kind == ARMInstrInfo::tk_BCOND;
        const bool isBranchImm = thumb
            ? (kind == ARMInstrInfo::tk_B || kind == ARMInstrInfo::tk_BL_LONG)
            : (kind == ARMInstrInfo::ak_B || kind == ARMInstrInfo::ak_BL || kind == ARMInstrInfo::ak_BLX_IMM);
        const bool isBranchReg = thumb
            ? (kind == ARMInstrInfo::tk_BX || kind == ARMInstrInfo::tk_BLX_REG)
            : (kind == ARMInstrInfo::ak_BX || kind == ARMInstrInfo::ak_BLX_REG);
        const bool isMul = thumb
            ? (kind == ARMInstrInfo::tk_MUL_REG)
            : (kind >= ARMInstrInfo::ak_MUL && kind <= ARMInstrInfo::ak_SMULxy);
        const bool isSys = thumb
            ? (kind == ARMInstrInfo::tk_SVC || kind == ARMInstrInfo::tk_UNK)
            : (kind == ARMInstrInfo::ak_MSR_IMM || kind == ARMInstrInfo::ak_MSR_REG
                || kind == ARMInstrInfo::ak_MRS || kind == ARMInstrInfo::ak_MCR
                || kind == ARMInstrInfo::ak_MRC || kind == ARMInstrInfo::ak_SVC
                || kind == ARMInstrInfo::ak_UNK);
        const bool isALU = thumb
            ? (kind < ARMInstrInfo::tk_LDR_PCREL || kind == ARMInstrInfo::tk_ADD_PCREL
                || kind == ARMInstrInfo::tk_ADD_SPREL || kind == ARMInstrInfo::tk_ADD_SP
                || kind == ARMInstrInfo::tk_ADD_HIREG || kind == ARMInstrInfo::tk_CMP_HIREG
                || kind == ARMInstrInfo::tk_MOV_HIREG)
            : (kind < ARMInstrInfo::ak_MUL
                || (kind >= ARMInstrInfo::ak_CLZ && kind <= ARMInstrInfo::ak_QDSUB)
                || kind == ARMInstrInfo::ak_Nop);
        bool isStackLoad = false;
        bool isStackStore = false;

        if (instrs[i].Info.SpecialKind == ARMInstrInfo::special_LoadMem
            || instrs[i].Info.SpecialKind == ARMInstrInfo::special_LoadLiteral)
            hasLoadInstr = true;
        if (instrs[i].Info.SpecialKind == ARMInstrInfo::special_WriteMem)
            hasStoreInstr = true;
        if (isMemLoad || isMemStore)
        {
            u32 dataAddr = instrs[i].DataRegion;
            u8 regionBit = classifyMemRegionBit(dataAddr);
            memRegionMask |= regionBit;
            if (!isLiteralLoad)
                noteMemRegion(regionBit);
        }

        if (thumb)
        {
            if (kind == ARMInstrInfo::tk_LDR_SPREL || kind == ARMInstrInfo::tk_POP)
                isStackLoad = true;
            else if (kind == ARMInstrInfo::tk_STR_SPREL || kind == ARMInstrInfo::tk_PUSH)
                isStackStore = true;
        }
        else if ((kind >= ARMInstrInfo::ak_STR_REG_LSL && kind <= ARMInstrInfo::ak_LDRSH_POST_IMM)
            || kind == ARMInstrInfo::ak_LDM || kind == ARMInstrInfo::ak_STM)
        {
            const u32 baseReg = instrs[i].A_Reg(16);
            if (baseReg == 13)
            {
                isStackLoad = isMemLoad;
                isStackStore = isMemStore;
            }
        }

        if (isBlockLoad)
            execBlockLoadCount++;
        else if (isBlockStore)
            execBlockStoreCount++;
        else if (isMemLoad)
            execSingleLoadCount++;
        else if (isMemStore)
            execSingleStoreCount++;
        else if (isBranchCond)
            execBranchCondCount++;
        else if (isBranchImm)
            execBranchImmCount++;
        else if (isBranchReg)
            execBranchRegCount++;
        else if (isMul)
            execMulCount++;
        else if (isSys)
            execSysCount++;
        else if (isALU)
            execALUCount++;
        else
            execOtherCount++;

        if (isStackLoad)
            execStackLoadCount++;
        if (isStackStore)
            execStackStoreCount++;
        if (isStackLoad && isBlockLoad)
            execStackBlockLoadCount++;
        if (isStackStore && isBlockStore)
            execStackBlockStoreCount++;
        if (isLiteralLoad)
            execLiteralLoadCount++;
#endif

        u32 literalAddr;
        if (LiteralOptimizations
            && instrs[i].Info.SpecialKind == ARMInstrInfo::special_LoadLiteral
            && DecodeLiteral(thumb, instrs[i], literalAddr))
        {
            u32 translatedAddr = LocaliseCodeAddress(cpu->Num, literalAddr);
            if (!translatedAddr)
            {
                Log(LogLevel::Warn,"literal in non executable memory?\n");
            }
            if (InvalidLiterals.Find(translatedAddr) == -1)
            {
                u32 translatedAddrRounded = translatedAddr & ~0x1FF;

                u32 j = 0;
                for (; j < numAddressRanges; j++)
                    if (addressRanges[j] == translatedAddrRounded)
                        break;
                if (j == numAddressRanges)
                    addressRanges[numAddressRanges++] = translatedAddrRounded;
                addressMasks[j] |= 1 << ((translatedAddr & 0x1FF) / 16);
                JIT_DEBUGPRINT("literal loading %08x %08x %08x %08x\n", literalAddr, translatedAddr, addressMasks[j], addressRanges[j]);
                cpu->DataRead32(literalAddr, &literalValues[numLiterals]);
                literalLoadAddrs[numLiterals++] = translatedAddr;
            }
        }
        else if (instrs[i].Info.SpecialKind == ARMInstrInfo::special_WriteMem)
            writeAddrs[numWriteAddrs++] = instrs[i].DataRegion;
        else if (thumb && instrs[i].Info.Kind == ARMInstrInfo::tk_BL_LONG_2 && i > 0
            && instrs[i - 1].Info.Kind == ARMInstrInfo::tk_BL_LONG_1)
        {
            i--;
            instrs[i].Info.Kind = ARMInstrInfo::tk_BL_LONG;
            instrs[i].Instr = (instrs[i].Instr & 0xFFFF) | (instrs[i + 1].Instr << 16);
            instrs[i].Info.DstRegs = 0xC000;
            instrs[i].Info.SrcRegs = 0;
            instrs[i].Info.EndBlock = true;
            JIT_DEBUGPRINT("merged BL\n");
        }

        if (instrs[i].Info.Branches() && BranchOptimizations
            && instrs[i].Info.Kind != (thumb ? ARMInstrInfo::tk_SVC : ARMInstrInfo::ak_SVC))
        {
            bool hasBranched = cpu->R[15] != r15;

            bool link;
            u32 cond, target, linkAddr;
            bool staticBranch = DecodeBranch(thumb, instrs[i], cond, hasLink, lr, link, linkAddr, target);
            JIT_DEBUGPRINT("branch cond %x target %x (%d)\n", cond, target, hasBranched);

            if (staticBranch)
            {
                instrs[i].BranchFlags |= branch_StaticTarget;

                bool isBackJump = false;
                if (hasBranched)
                {
                    for (int j = 0; j < i; j++)
                    {
                        if (instrs[j].Addr == target)
                        {
                            isBackJump = true;
                            break;
                        }
                    }
                }

                if (cond < 0xE && target < instrs[i].Addr && target >= lastSegmentStart)
                {
                    // we might have an idle loop
                    u32 backwardsOffset = (instrs[i].Addr - target) / (thumb ? 2 : 4);
                    if (IsIdleLoop(thumb, &instrs[i - backwardsOffset], backwardsOffset + 1))
                    {
                        instrs[i].BranchFlags |= branch_IdleBranch;
                        JIT_DEBUGPRINT("found %s idle loop %d in block %08x\n", thumb ? "thumb" : "arm", cpu->Num, blockAddr);
                    }
                }
                else if (hasBranched && !isBackJump && i + 1 < MaxBlockSize)
                {
                    if (link)
                    {
                        lr = linkAddr;
                        hasLink = true;
                    }

                    r15 = target + (thumb ? 2 : 4);
                    assert(r15 == cpu->R[15]);

                    JIT_DEBUGPRINT("block lengthened by static branch (target %x)\n", target);

                    nextInstr[0] = cpu->NextInstr[0];
                    nextInstr[1] = cpu->NextInstr[1];

                    nextInstrAddr[0] = target;
                    nextInstrAddr[1] = r15;

                    lastSegmentStart = target;

                    instrs[i].Info.EndBlock = false;

                    if (cond < 0xE)
                        instrs[i].BranchFlags |= branch_FollowCondTaken;
                }
            }

            if (!hasBranched && cond < 0xE && i + 1 < MaxBlockSize)
            {
                JIT_DEBUGPRINT("block lengthened by untaken branch\n");
                instrs[i].Info.EndBlock = false;
                instrs[i].BranchFlags |= branch_FollowCondNotTaken;
            }
        }

        i++;

        bool canCompile = JITCompiler.CanCompile(thumb, instrs[i - 1].Info.Kind);
        bool secondaryFlagReadCond = !canCompile || (instrs[i - 1].BranchFlags & (branch_FollowCondTaken | branch_FollowCondNotTaken));
        if (instrs[i - 1].Info.ReadFlags != 0 || secondaryFlagReadCond)
            FloodFillSetFlags(instrs, i - 2, !secondaryFlagReadCond ? instrs[i - 1].Info.ReadFlags : 0xF);
    } while(!instrs[i - 1].Info.EndBlock && i < MaxBlockSize && !cpu->Halted && (!cpu->IRQ || (cpu->CPSR & 0x80)));

#if LITEV_PROFILE
    u8 exitKind = JitBlock::ExitEndBlock;
    bool exitIsBranch = instrs[i - 1].Info.Branches();
    bool exitIsCondBranch = thumb
        ? instrs[i - 1].Info.Kind == ARMInstrInfo::tk_BCOND
        : instrs[i - 1].Cond() < 0xE;
    if (cpu->Halted)
        exitKind = JitBlock::ExitHaltBoundary;
    else if (i >= MaxBlockSize && !instrs[i - 1].Info.EndBlock)
        exitKind = JitBlock::ExitMaxBlockSize;
    else if (cpu->IRQ && !(cpu->CPSR & 0x80) && !instrs[i - 1].Info.EndBlock)
        exitKind = JitBlock::ExitIRQBoundary;
#endif

    if (numLiterals)
    {
        for (u32 j = 0; j < numWriteAddrs; j++)
        {
            u32 translatedAddr = LocaliseCodeAddress(cpu->Num, writeAddrs[j]);
            if (translatedAddr)
            {
                for (u32 k = 0; k < numLiterals; k++)
                {
                    if (literalLoadAddrs[k] == translatedAddr)
                    {
                        if (InvalidLiterals.Find(translatedAddr) == -1)
                            InvalidLiterals.Add(translatedAddr);
                        break;
                    }
                }
            }
        }
    }

    u32 literalHash = (u32)XXH3_64bits(literalValues, numLiterals * 4);
    u32 instrHash = (u32)XXH3_64bits(instrValues, numInstrs * 4);

    auto prevBlockIt = RestoreCandidates.find(instrHash);
    JitBlock* prevBlock = NULL;
    bool mayRestore = true;
    if (prevBlockIt != RestoreCandidates.end())
    {
        prevBlock = prevBlockIt->second;
        RestoreCandidates.erase(prevBlockIt);

        mayRestore = prevBlock->StartAddr == blockAddr && prevBlock->LiteralHash == literalHash;

        if (mayRestore && prevBlock->NumAddresses == numAddressRanges)
        {
            for (u32 j = 0; j < numAddressRanges; j++)
            {
                if (prevBlock->AddressRanges()[j] != addressRanges[j]
                    || prevBlock->AddressMasks()[j] != addressMasks[j])
                {
                    mayRestore = false;
                    break;
                }
            }
        }
        else
            mayRestore = false;
    }
    else
    {
        mayRestore = false;
    }

    JitBlock* block;
    if (!mayRestore)
    {
        if (prevBlock)
            delete prevBlock;

        block = new JitBlock(cpu->Num, i, numAddressRanges, numLiterals);
        block->LiteralHash = literalHash;
        block->InstrHash = instrHash;
        for (u32 j = 0; j < numAddressRanges; j++)
            block->AddressRanges()[j] = addressRanges[j];
        for (u32 j = 0; j < numAddressRanges; j++)
            block->AddressMasks()[j] = addressMasks[j];
        for (int j = 0; j < numLiterals; j++)
            block->Literals()[j] = literalLoadAddrs[j];

        block->StartAddr = blockAddr;
        block->StartAddrLocal = localAddr;
#if LITEV_PROFILE
        block->InstrCount = i;
        block->Exit = exitKind;
        block->ExitIsBranch = exitIsBranch;
        block->ExitIsCondBranch = exitIsCondBranch;
        block->ExitBranchFamily = JitBlock::ExitBranchNone;
        block->ExitBranchReg = 0xFF;
        block->ExitBranchIsLink = 0;
        block->ExecALUCount = execALUCount;
        block->ExecMulCount = execMulCount;
        block->ExecSingleLoadCount = execSingleLoadCount;
        block->ExecSingleStoreCount = execSingleStoreCount;
        block->ExecBlockLoadCount = execBlockLoadCount;
        block->ExecBlockStoreCount = execBlockStoreCount;
        block->ExecStackBlockLoadCount = execStackBlockLoadCount;
        block->ExecStackBlockStoreCount = execStackBlockStoreCount;
        block->ExecStackLoadCount = execStackLoadCount;
        block->ExecStackStoreCount = execStackStoreCount;
        block->ExecBranchCondCount = execBranchCondCount;
        block->ExecBranchImmCount = execBranchImmCount;
        block->ExecBranchRegCount = execBranchRegCount;
        block->ExecSysCount = execSysCount;
        block->ExecOtherCount = execOtherCount;
        block->ExecLiteralLoadCount = execLiteralLoadCount;
        block->ExecMemITCMCount = execMemITCMCount;
        block->ExecMemDTCMCount = execMemDTCMCount;
        block->ExecMemMainCount = execMemMainCount;
        block->ExecMemSharedCount = execMemSharedCount;
        block->ExecMemIOCount = execMemIOCount;
        block->ExecMemVRAMCount = execMemVRAMCount;
        block->ExecMemOtherCount = execMemOtherCount;
        block->HasMemoryInstr = hasMemoryInstr;
        block->IsThumb = thumb;
        block->HasLoadInstr = hasLoadInstr;
        block->HasStoreInstr = hasStoreInstr;
        block->MemRegionMask = memRegionMask;
        if (exitIsBranch)
        {
            const auto& exitInstr = instrs[i - 1];
            if (thumb)
            {
                switch (exitInstr.Info.Kind)
                {
                case ARMInstrInfo::tk_BCOND:
                    block->ExitBranchFamily = JitBlock::ExitBranchThumbCond;
                    break;
                case ARMInstrInfo::tk_B:
                case ARMInstrInfo::tk_BL_LONG:
                    block->ExitBranchFamily = JitBlock::ExitBranchThumbImm;
                    break;
                case ARMInstrInfo::tk_BX:
                case ARMInstrInfo::tk_BLX_REG:
                    block->ExitBranchFamily = JitBlock::ExitBranchThumbReg;
                    break;
                case ARMInstrInfo::tk_POP:
                    if (exitInstr.Instr & (1 << 8))
                        block->ExitBranchFamily = JitBlock::ExitBranchThumbPCStack;
                    break;
                default:
                    break;
                }
            }
            else
            {
                switch (exitInstr.Info.Kind)
                {
                case ARMInstrInfo::ak_B:
                case ARMInstrInfo::ak_BL:
                case ARMInstrInfo::ak_BLX_IMM:
                    block->ExitBranchFamily = JitBlock::ExitBranchARMImm;
                    break;
                case ARMInstrInfo::ak_BX:
                case ARMInstrInfo::ak_BLX_REG:
                    block->ExitBranchFamily = JitBlock::ExitBranchARMReg;
                    block->ExitBranchReg = exitInstr.A_Reg(0);
                    block->ExitBranchIsLink = exitInstr.Info.Kind == ARMInstrInfo::ak_BLX_REG;
                    break;
                case ARMInstrInfo::ak_LDM:
                    if (exitInstr.Instr & (1 << 15))
                    {
                        if (exitInstr.A_Reg(16) == 13)
                            block->ExitBranchFamily = JitBlock::ExitBranchARMPCStack;
                        else
                            block->ExitBranchFamily = JitBlock::ExitBranchARMPCOther;
                    }
                    break;
                default:
                    break;
                }
            }
        }
#endif

        FloodFillSetFlags(instrs, i - 1, 0xF);

        JitEnableWrite();
        block->EntryPoint = JITCompiler.CompileBlock(cpu, thumb, instrs, i, hasMemoryInstr);
        JitEnableExecute();

        JIT_DEBUGPRINT("block start %p\n", block->EntryPoint);
    }
    else
    {
        JIT_DEBUGPRINT("restored! %p\n", prevBlock);
        block = prevBlock;
#ifdef LITEV_ARM7_PROFILE
        Stats.BlockRestores.fetch_add(1, std::memory_order_relaxed);
#endif
    }

#ifdef LITEV_ARM7_PROFILE
    Stats.BlockCompiles.fetch_add(1, std::memory_order_relaxed);
    Stats.TotalInstructions.fetch_add((uint64_t)i, std::memory_order_relaxed);
#endif

    assert((localAddr & 1) == 0);
    for (u32 j = 0; j < numAddressRanges; j++)
    {
        assert(addressRanges[j] == block->AddressRanges()[j]);
        assert(addressMasks[j] == block->AddressMasks()[j]);
        assert(addressMasks[j] != 0);

        AddressRange* region = CodeMemRegions[addressRanges[j] >> 27];

        if (!PageContainsCode(&region[(addressRanges[j] & 0x7FFF000 & ~(Memory.PageSize - 1)) / 512], Memory.PageSize))
            Memory.SetCodeProtection(addressRanges[j] >> 27, addressRanges[j] & 0x7FFFFFF, true);

        AddressRange* range = &region[(addressRanges[j] & 0x7FFFFFF) / 512];
        range->Code |= addressMasks[j];
        range->Blocks.Add(block);
    }

    if (cpu->Num == 0)
        JitBlocks9[blockAddr] = block;
    else
        JitBlocks7[blockAddr] = block;

    u64* entry = &FastBlockLookupRegions[(localAddr >> 27)][(localAddr & 0x7FFFFFF) / 2];
    *entry = ((u64)blockAddr | cpu->Num) << 32;
    *entry |= JITCompiler.SubEntryOffset(block->EntryPoint);
}

void ARMJIT::InvalidateByAddr(u32 localAddr) noexcept
{
    JIT_DEBUGPRINT("invalidating by addr %x\n", localAddr);
    NDS.ARM9.ClearJitCache();
    NDS.ARM7.ClearJitCache();

    AddressRange* region = CodeMemRegions[localAddr >> 27];
    AddressRange* range = &region[(localAddr & 0x7FFFFFF) / 512];
    u32 mask = 1 << ((localAddr & 0x1FF) / 16);

    range->Code = 0;
    for (int i = 0; i < range->Blocks.Length;)
    {
        JitBlock* block = range->Blocks[i];

        bool invalidated = false;
        u32 mask = 0;
        for (int j = 0; j < block->NumAddresses; j++)
        {
            if (block->AddressRanges()[j] == (localAddr & ~0x1FF))
            {
                mask = block->AddressMasks()[j];
                invalidated = block->AddressMasks()[j] & mask;
                assert(mask);
                break;
            }
        }
        assert(mask);
        if (!invalidated)
        {
            range->Code |= mask;
            i++;
            continue;
        }
        range->Blocks.Remove(i);

        if (range->Blocks.Length == 0
            && !PageContainsCode(&region[(localAddr & 0x7FFF000 & ~(Memory.PageSize - 1)) / 512], Memory.PageSize))
        {
            Memory.SetCodeProtection(localAddr >> 27, localAddr & 0x7FFFFFF, false);
        }

        bool literalInvalidation = false;
        for (int j = 0; j < block->NumLiterals; j++)
        {
            u32 addr = block->Literals()[j];
            if (addr == localAddr)
            {
                if (InvalidLiterals.Find(localAddr) == -1)
                {
                    InvalidLiterals.Add(localAddr);
                    JIT_DEBUGPRINT("found invalid literal %d\n", InvalidLiterals.Length);
                }
                literalInvalidation = true;
                break;
            }
        }
        for (int j = 0; j < block->NumAddresses; j++)
        {
            u32 addr = block->AddressRanges()[j];
            if ((addr / 512) != (localAddr / 512))
            {
                AddressRange* otherRegion = CodeMemRegions[addr >> 27];
                AddressRange* otherRange = &otherRegion[(addr & 0x7FFFFFF) / 512];
                assert(otherRange != range);

                bool removed = otherRange->Blocks.RemoveByValue(block);
                assert(removed);

                if (otherRange->Blocks.Length == 0)
                {
                    if (!PageContainsCode(&otherRegion[(addr & 0x7FFF000 & ~(Memory.PageSize - 1)) / 512], Memory.PageSize))
                        Memory.SetCodeProtection(addr >> 27, addr & 0x7FFFFFF, false);

                    otherRange->Code = 0;
                }
            }
        }

        FastBlockLookupRegions[block->StartAddrLocal >> 27][(block->StartAddrLocal & 0x7FFFFFF) / 2] = (u64)UINT32_MAX << 32;
        if (block->Num == 0)
            JitBlocks9.erase(block->StartAddr);
        else
            JitBlocks7.erase(block->StartAddr);

        if (!literalInvalidation)
        {
            RetireJitBlock(block);
        }
        else
        {
            delete block;
        }
    }
}

void ARMJIT::CheckAndInvalidateITCM() noexcept
{
    for (u32 i = 0; i < ITCMPhysicalSize; i+=512)
    {
        if (CodeIndexITCM[i / 512].Code)
        {
            // maybe using bitscan would be better here?
            // The thing is that in densely populated sets
            // The old fashioned way can actually be faster
            for (u32 j = 0; j < 512; j += 16)
            {
                if (CodeIndexITCM[i / 512].Code & (1 << ((j & 0x1FF) / 16)))
                    InvalidateByAddr((i+j) | (ARMJIT_Memory::memregion_ITCM << 27));
            }
        }
    }
}

void ARMJIT::CheckAndInvalidateWVRAM(int bank) noexcept
{
    u32 start = bank == 1 ? 0x20000 : 0;
    for (u32 i = start; i < start+0x20000; i+=512)
    {
        if (CodeIndexARM7WVRAM[i / 512].Code)
        {
            for (u32 j = 0; j < 512; j += 16)
            {
                if (CodeIndexARM7WVRAM[i / 512].Code & (1 << ((j & 0x1FF) / 16)))
                    InvalidateByAddr((i+j) | (ARMJIT_Memory::memregion_VWRAM << 27));
            }
        }
    }
}

JitBlockEntry ARMJIT::LookUpBlock(u32 num, u64* entries, u32 offset, u32 addr) noexcept
{
    u64* entry = &entries[offset / 2];
    if (*entry >> 32 == (addr | num))
    {
#ifdef LITEV_ARM7_PROFILE
        Stats.FastLookupHits.fetch_add(1, std::memory_order_relaxed);
#endif
        return JITCompiler.AddEntryOffset((u32)*entry);
    }
#ifdef LITEV_ARM7_PROFILE
    Stats.FastLookupMisses.fetch_add(1, std::memory_order_relaxed);
#endif
    return NULL;
}

void ARMJIT::blockSanityCheck(u32 num, u32 blockAddr, JitBlockEntry entry) noexcept
{
    u32 localAddr = LocaliseCodeAddress(num, blockAddr);
    assert(JITCompiler.AddEntryOffset((u32)FastBlockLookupRegions[localAddr >> 27][(localAddr & 0x7FFFFFF) / 2]) == entry);
}

bool ARMJIT::SetupExecutableRegion(u32 num, u32 blockAddr, u64*& entry, u32& start, u32& size) noexcept
{
    // amazingly ignoring the DTCM is the proper behaviour for code fetches
    int region = num == 0
        ? Memory.ClassifyAddress9(blockAddr)
        : Memory.ClassifyAddress7(blockAddr);

    u32 memoryOffset;
    if (FastBlockLookupRegions[region]
        && Memory.GetMirrorLocation(region, num, blockAddr, memoryOffset, start, size))
    {
        //printf("setup exec region %d %d %08x %08x %x %x\n", num, region, blockAddr, start, size, memoryOffset);
        entry = FastBlockLookupRegions[region] + memoryOffset / 2;
        return true;
    }
    return false;
}

template void ARMJIT::CheckAndInvalidate<0, ARMJIT_Memory::memregion_MainRAM>(u32) noexcept;
template void ARMJIT::CheckAndInvalidate<1, ARMJIT_Memory::memregion_MainRAM>(u32) noexcept;
template void ARMJIT::CheckAndInvalidate<0, ARMJIT_Memory::memregion_SharedWRAM>(u32) noexcept;
template void ARMJIT::CheckAndInvalidate<1, ARMJIT_Memory::memregion_SharedWRAM>(u32) noexcept;
template void ARMJIT::CheckAndInvalidate<1, ARMJIT_Memory::memregion_WRAM7>(u32) noexcept;
template void ARMJIT::CheckAndInvalidate<1, ARMJIT_Memory::memregion_VWRAM>(u32) noexcept;
template void ARMJIT::CheckAndInvalidate<0, ARMJIT_Memory::memregion_VRAM>(u32) noexcept;
template void ARMJIT::CheckAndInvalidate<0, ARMJIT_Memory::memregion_ITCM>(u32) noexcept;
template void ARMJIT::CheckAndInvalidate<0, ARMJIT_Memory::memregion_NewSharedWRAM_A>(u32) noexcept;
template void ARMJIT::CheckAndInvalidate<1, ARMJIT_Memory::memregion_NewSharedWRAM_A>(u32) noexcept;
template void ARMJIT::CheckAndInvalidate<0, ARMJIT_Memory::memregion_NewSharedWRAM_B>(u32) noexcept;
template void ARMJIT::CheckAndInvalidate<1, ARMJIT_Memory::memregion_NewSharedWRAM_B>(u32) noexcept;
template void ARMJIT::CheckAndInvalidate<0, ARMJIT_Memory::memregion_NewSharedWRAM_C>(u32) noexcept;
template void ARMJIT::CheckAndInvalidate<1, ARMJIT_Memory::memregion_NewSharedWRAM_C>(u32) noexcept;

void ARMJIT::ResetBlockCache() noexcept
{
    Log(LogLevel::Debug, "Resetting JIT block cache...\n");
    NDS.ARM9.ClearJitCache();
    NDS.ARM7.ClearJitCache();

    // could be replace through a function which only resets
    // the permissions but we're too lazy
    Memory.Reset();

    InvalidLiterals.Clear();
    for (int i = 0; i < ARMJIT_Memory::memregions_Count; i++)
    {
        if (FastBlockLookupRegions[i])
            memset(FastBlockLookupRegions[i], 0xFF, CodeRegionSizes[i] * sizeof(u64) / 2);
    }
    for (auto it = RestoreCandidates.begin(); it != RestoreCandidates.end(); it++)
        delete it->second;
    RestoreCandidates.clear();
    for (auto it : JitBlocks9)
    {
        JitBlock* block = it.second;
        for (int j = 0; j < block->NumAddresses; j++)
        {
            u32 addr = block->AddressRanges()[j];
            AddressRange* range = &CodeMemRegions[addr >> 27][(addr & 0x7FFFFFF) / 512];
            range->Blocks.Clear();
            range->Code = 0;
        }
        delete block;
    }
    for (auto it : JitBlocks7)
    {
        JitBlock* block = it.second;
        for (int j = 0; j < block->NumAddresses; j++)
        {
            u32 addr = block->AddressRanges()[j];
            AddressRange* range = &CodeMemRegions[addr >> 27][(addr & 0x7FFFFFF) / 512];
            range->Blocks.Clear();
            range->Code = 0;
        }
        delete block;
    }
    JitBlocks9.clear();
    JitBlocks7.clear();

    JITCompiler.Reset();
}

void ARMJIT::JitEnableWrite() noexcept
{
    #if defined(__APPLE__) && defined(__aarch64__)
        if (__builtin_available(macOS 11.0, *))
            pthread_jit_write_protect_np(false);
    #elif defined(__NetBSD__)
        mprotect(JITCompiler.CodeMemBase, ARMJIT_Global::CodeMemorySliceSize, PROT_READ | PROT_WRITE);
    #endif
}

void ARMJIT::JitEnableExecute() noexcept
{
    #if defined(__APPLE__) && defined(__aarch64__)
        if (__builtin_available(macOS 11.0, *))
            pthread_jit_write_protect_np(true);
    #elif defined(__NetBSD__)
        mprotect(JITCompiler.CodeMemBase, ARMJIT_Global::CodeMemorySliceSize, PROT_READ | PROT_EXEC);
    #endif
}

}
