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

#ifndef MELONDS_JITBLOCK_H
#define MELONDS_JITBLOCK_H

#include "types.h"
#include "TinyVector.h"

#ifndef LITEV_PROFILE
#define LITEV_PROFILE 0
#endif

namespace melonDS
{
typedef void (*JitBlockEntry)();

class JitBlock
{
public:
#if LITEV_PROFILE
    enum ExitKind : u8
    {
        ExitUnknown = 0,
        ExitEndBlock,
        ExitMaxBlockSize,
        ExitIRQBoundary,
        ExitHaltBoundary,
    };

    enum ExitBranchKind : u8
    {
        ExitBranchNone = 0,
        ExitBranchARMImm,
        ExitBranchARMReg,
        ExitBranchThumbCond,
        ExitBranchThumbImm,
        ExitBranchThumbReg,
    };
#endif

    JitBlock(u32 num, u32 literalHash, u32 numAddresses, u32 numLiterals)
    {
        Num = num;
        NumAddresses = numAddresses;
        NumLiterals = numLiterals;
#if LITEV_PROFILE
        InstrCount = 0;
        Exit = ExitUnknown;
        ExitIsBranch = 0;
        ExitIsCondBranch = 0;
        ExitBranchFamily = ExitBranchNone;
        ExitBranchReg = 0xFF;
        ExitBranchIsLink = 0;
        ExecALUCount = 0;
        ExecMulCount = 0;
        ExecSingleLoadCount = 0;
        ExecSingleStoreCount = 0;
        ExecBlockLoadCount = 0;
        ExecBlockStoreCount = 0;
        ExecStackBlockLoadCount = 0;
        ExecStackBlockStoreCount = 0;
        ExecStackLoadCount = 0;
        ExecStackStoreCount = 0;
        ExecBranchCondCount = 0;
        ExecBranchImmCount = 0;
        ExecBranchRegCount = 0;
        ExecSysCount = 0;
        ExecOtherCount = 0;
        ExecLiteralLoadCount = 0;
        ExecMemITCMCount = 0;
        ExecMemDTCMCount = 0;
        ExecMemMainCount = 0;
        ExecMemSharedCount = 0;
        ExecMemIOCount = 0;
        ExecMemVRAMCount = 0;
        ExecMemOtherCount = 0;
        HasMemoryInstr = 0;
        IsThumb = 0;
        HasLoadInstr = 0;
        HasStoreInstr = 0;
        MemRegionMask = 0;
#endif
        Data.SetLength(numAddresses * 2 + numLiterals);
    }

    u32 StartAddr;
    u32 StartAddrLocal;
    u32 InstrHash, LiteralHash;
    u8 Num;
    u16 NumAddresses;
    u16 NumLiterals;
#if LITEV_PROFILE
    u16 InstrCount;
    u8 Exit;
    u8 ExitIsBranch;
    u8 ExitIsCondBranch;
    u8 ExitBranchFamily;
    u8 ExitBranchReg;
    u8 ExitBranchIsLink;
    u16 ExecALUCount;
    u16 ExecMulCount;
    u16 ExecSingleLoadCount;
    u16 ExecSingleStoreCount;
    u16 ExecBlockLoadCount;
    u16 ExecBlockStoreCount;
    u16 ExecStackBlockLoadCount;
    u16 ExecStackBlockStoreCount;
    u16 ExecStackLoadCount;
    u16 ExecStackStoreCount;
    u16 ExecBranchCondCount;
    u16 ExecBranchImmCount;
    u16 ExecBranchRegCount;
    u16 ExecSysCount;
    u16 ExecOtherCount;
    u16 ExecLiteralLoadCount;
    u16 ExecMemITCMCount;
    u16 ExecMemDTCMCount;
    u16 ExecMemMainCount;
    u16 ExecMemSharedCount;
    u16 ExecMemIOCount;
    u16 ExecMemVRAMCount;
    u16 ExecMemOtherCount;
    u8 HasMemoryInstr;
    u8 IsThumb;
    u8 HasLoadInstr;
    u8 HasStoreInstr;
    u8 MemRegionMask;
#endif

    JitBlockEntry EntryPoint;

    const u32* AddressRanges() const { return &Data[0]; }
    u32* AddressRanges() { return &Data[0]; }
    const u32* AddressMasks() const { return &Data[NumAddresses]; }
    u32* AddressMasks() { return &Data[NumAddresses]; }
    const u32* Literals() const { return &Data[NumAddresses * 2]; }
    u32* Literals() { return &Data[NumAddresses * 2]; }

private:
    TinyVector<u32> Data;
};
}

#endif //MELONDS_JITBLOCK_H
