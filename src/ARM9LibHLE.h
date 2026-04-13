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

#ifndef ARM9LIBHLE_H
#define ARM9LIBHLE_H

#include <array>

#include "types.h"

namespace melonDS
{
class ARMv5;
class NDS;

class ARM9LibHLE
{
public:
    void Reset() noexcept;
    [[nodiscard]] bool TryHandle(ARMv5& cpu, u32 pc) noexcept;

private:
    enum class Func : u32
    {
        StrCmp = 0,
        StrChr,
        MemSet8,
        MemFill32,
        Count
    };

    struct Entry
    {
        u32 Addr = 0;
    };

    void EnsureScanned(NDS& nds) noexcept;
    void ScanLoadedCode(NDS& nds, u32 loadAddr, const u8* code, u32 size) noexcept;

    [[nodiscard]] bool HandleStrCmp(ARMv5& cpu) noexcept;
    [[nodiscard]] bool HandleStrChr(ARMv5& cpu) noexcept;
    [[nodiscard]] bool HandleMemSet8(ARMv5& cpu) noexcept;
    [[nodiscard]] bool HandleMemFill32(ARMv5& cpu) noexcept;

    static u32 EstimateCycles(u32 bytes, u32 shift, u32 base) noexcept;

    std::array<Entry, static_cast<size_t>(Func::Count)> Entries = {};
    u32 ScanBase = 0;
    u32 ScanSize = 0;
    bool Scanned = false;
};
}

#endif
