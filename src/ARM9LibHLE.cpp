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

#include "ARM9LibHLE.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "ARM.h"
#include "ARMJIT.h"
#include "ARMJIT_Memory.h"
#include "MemRegion.h"
#include "NDS.h"
#include "NDSCart.h"

namespace melonDS
{
namespace
{
template <size_t N>
u32 FindPattern(const u8* code, u32 size, const std::array<u32, N>& words) noexcept
{
    const u32 patternBytes = static_cast<u32>(words.size() * sizeof(u32));
    if (size < patternBytes)
        return 0;

    for (u32 off = 0; off + patternBytes <= size; off += 4)
    {
        if (std::memcmp(code + off, words.data(), patternBytes) == 0)
            return off;
    }

    return 0;
}

struct LinearRange
{
    u8* Ptr = nullptr;
    u32 MaxLen = 0;
    u32 Addr = 0;
    int Region = -1;
};

bool GetLinearRange(NDS& nds, u32 addr, bool write, LinearRange& range) noexcept
{
    MemRegion region = {};
    if (!nds.ARM9GetMemRegion(addr, write, &region) || !region.Mem)
        return false;

    range.Ptr = &region.Mem[addr & region.Mask];
    range.MaxLen = (region.Mask + 1) - (addr & region.Mask);
    range.Addr = addr;

    switch (addr & 0xFF000000)
    {
    case 0x02000000:
        range.Region = ARMJIT_Memory::memregion_MainRAM;
        return true;
    case 0x03000000:
        range.Region = ARMJIT_Memory::memregion_SharedWRAM;
        return true;
    default:
        range.Region = -1;
        return false;
    }
}

void InvalidateLinearWrite(NDS& nds, const LinearRange& range, u32 size) noexcept
{
    if (!size)
        return;

#ifdef JIT_ENABLED
    switch (range.Region)
    {
    case ARMJIT_Memory::memregion_MainRAM:
        nds.JIT.CheckAndInvalidateRange<0, ARMJIT_Memory::memregion_MainRAM>(range.Addr, size);
        break;
    case ARMJIT_Memory::memregion_SharedWRAM:
        nds.JIT.CheckAndInvalidateRange<0, ARMJIT_Memory::memregion_SharedWRAM>(range.Addr, size);
        break;
    default:
        break;
    }
#else
    (void)nds;
    (void)range;
    (void)size;
#endif
}

void FinishCall(ARMv5& cpu, u32 extraCycles) noexcept
{
    cpu.Cycles += static_cast<s32>(extraCycles);
    cpu.JumpTo(cpu.R[14]);
}
}

void ARM9LibHLE::Reset() noexcept
{
    Entries.fill({});
    ScanBase = 0;
    ScanSize = 0;
    Scanned = false;
}

bool ARM9LibHLE::TryHandle(ARMv5& cpu, u32 pc) noexcept
{
    if (!Scanned)
        EnsureScanned(cpu.NDS);

    if (pc == Entries[static_cast<size_t>(Func::StrCmp)].Addr)
        return HandleStrCmp(cpu);
    if (pc == Entries[static_cast<size_t>(Func::StrChr)].Addr)
        return HandleStrChr(cpu);
    if (pc == Entries[static_cast<size_t>(Func::MemSet8)].Addr)
        return HandleMemSet8(cpu);
    if (pc == Entries[static_cast<size_t>(Func::MemFill32)].Addr)
        return HandleMemFill32(cpu);

    return false;
}

void ARM9LibHLE::EnsureScanned(NDS& nds) noexcept
{
    const NDSCart::CartCommon* cart = nds.GetNDSCart();
    if (!cart)
    {
        Reset();
        return;
    }

    const NDSHeader& header = cart->GetHeader();
    if (Scanned && ScanBase == header.ARM9RAMAddress && ScanSize == header.ARM9Size)
        return;

    Reset();

    LinearRange range = {};
    if (!GetLinearRange(nds, header.ARM9RAMAddress, false, range))
        return;
    if (header.ARM9Size > range.MaxLen)
        return;

    ScanBase = header.ARM9RAMAddress;
    ScanSize = header.ARM9Size;
    ScanLoadedCode(nds, header.ARM9RAMAddress, range.Ptr, header.ARM9Size);
    Scanned = true;
}

void ARM9LibHLE::ScanLoadedCode(NDS& nds, u32 loadAddr, const u8* code, u32 size) noexcept
{
    (void)nds;

    static constexpr std::array<u32, 8> StrCmpSig = {
        0xEA000001u, 0xE2800001u, 0xE2811001u, 0xE5D03000u,
        0xE3530000u, 0x0A000002u, 0xE5D12000u, 0xE1530002u,
    };
    static constexpr std::array<u32, 8> StrChrSig = {
        0xE5D02000u, 0xEA000002u, 0xE1520001u, 0x012FFF1Eu,
        0xE5F02001u, 0xE3520000u, 0x1AFFFFFAu, 0xE3A00000u,
    };
    static constexpr std::array<u32, 7> MemSet8Sig = {
        0xEA000001u, 0xE1403091u, 0xE2800001u, 0xE3520000u,
        0xE2422001u, 0x1AFFFFFAu, 0xE12FFF1Eu,
    };
    static constexpr std::array<u32, 10> MemFill32Sig = {
        0xE92D03F0u, 0xE0819002u, 0xE1A0C2A2u, 0xE081C28Cu, 0xE1A02000u,
        0xE1A03002u, 0xE1A04002u, 0xE1A05002u, 0xE1A06002u, 0xE1A07002u,
    };

    if (u32 off = FindPattern(code, size, StrCmpSig))
        Entries[static_cast<size_t>(Func::StrCmp)].Addr = loadAddr + off;
    if (u32 off = FindPattern(code, size, StrChrSig))
        Entries[static_cast<size_t>(Func::StrChr)].Addr = loadAddr + off;
    if (u32 off = FindPattern(code, size, MemSet8Sig))
        Entries[static_cast<size_t>(Func::MemSet8)].Addr = loadAddr + off;
    if (u32 off = FindPattern(code, size, MemFill32Sig))
        Entries[static_cast<size_t>(Func::MemFill32)].Addr = loadAddr + off;
}

bool ARM9LibHLE::HandleStrCmp(ARMv5& cpu) noexcept
{
    LinearRange lhs = {};
    LinearRange rhs = {};
    if (!GetLinearRange(cpu.NDS, cpu.R[0], false, lhs) || !GetLinearRange(cpu.NDS, cpu.R[1], false, rhs))
        return false;

    const u32 limit = std::min(lhs.MaxLen, rhs.MaxLen);
    if (!limit)
        return false;

    u32 i = 0;
    for (; i < limit; i++)
    {
        const u8 a = lhs.Ptr[i];
        const u8 b = rhs.Ptr[i];
        if (a != b || a == 0)
        {
            cpu.R[0] = a - b;
            FinishCall(cpu, EstimateCycles(i + 1, 3, 4));
            return true;
        }
    }

    return false;
}

bool ARM9LibHLE::HandleStrChr(ARMv5& cpu) noexcept
{
    LinearRange haystack = {};
    if (!GetLinearRange(cpu.NDS, cpu.R[0], false, haystack))
        return false;

    const u8 needle = static_cast<u8>(cpu.R[1]);
    for (u32 i = 0; i < haystack.MaxLen; i++)
    {
        const u8 val = haystack.Ptr[i];
        if (val == needle)
        {
            cpu.R[0] = cpu.R[0] + i;
            FinishCall(cpu, EstimateCycles(i + 1, 3, 4));
            return true;
        }
        if (val == 0)
        {
            cpu.R[0] = 0;
            FinishCall(cpu, EstimateCycles(i + 1, 3, 4));
            return true;
        }
    }

    return false;
}

bool ARM9LibHLE::HandleMemSet8(ARMv5& cpu) noexcept
{
    const u32 len = cpu.R[2];
    if (!len)
    {
        FinishCall(cpu, 1);
        return true;
    }

    LinearRange dst = {};
    if (!GetLinearRange(cpu.NDS, cpu.R[0], true, dst) || len > dst.MaxLen)
        return false;

    InvalidateLinearWrite(cpu.NDS, dst, len);
    std::memset(dst.Ptr, static_cast<u8>(cpu.R[1]), len);
    FinishCall(cpu, EstimateCycles(len, 4, 4));
    return true;
}

bool ARM9LibHLE::HandleMemFill32(ARMv5& cpu) noexcept
{
    const u32 len = cpu.R[2];
    if ((len & 3) != 0)
        return false;

    LinearRange dst = {};
    if (!GetLinearRange(cpu.NDS, cpu.R[1], true, dst) || len > dst.MaxLen || (cpu.R[1] & 3))
        return false;

    InvalidateLinearWrite(cpu.NDS, dst, len);
    u32* out = reinterpret_cast<u32*>(dst.Ptr);
    const u32 words = len >> 2;
    for (u32 i = 0; i < words; i++)
        out[i] = cpu.R[0];

    FinishCall(cpu, EstimateCycles(len, 5, 4));
    return true;
}

u32 ARM9LibHLE::EstimateCycles(u32 bytes, u32 shift, u32 base) noexcept
{
    return std::max(base + (bytes >> shift), 1u);
}
}
