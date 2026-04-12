/*
    Copyright 2016-2025 melonDS team
    liteDS modifications: ARM7 HLE dispatch framework (Phase 2.5)

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

#ifdef LITEV_ARM7_HLE_AUDIO

#include "ARM7HLE_Core.h"
#include "ARM7HLE_Audio.h"  // audio HLE scan/handlers

// ARM and NDS must be included in this order:
#include "ARM.h"
#include "NDS.h"

namespace melonDS
{

// ── IsWiFiRange ──────────────────────────────────────────────────────────────

bool ARM7HLECore::IsWiFiRange(u32 pc) noexcept
{
    for (const auto& r : kWiFiLLERanges)
        if (pc >= r.lo && pc < r.hi)
            return true;
    return false;
}

// ── Reset ────────────────────────────────────────────────────────────────────

void ARM7HLECore::Reset() noexcept
{
    entries_.clear();
    ipcEmptyCount_ = 0;
    spiIdleCount_  = 0;
}

// ── Register ─────────────────────────────────────────────────────────────────

void ARM7HLECore::Register(u32 addr, ARM7HLEFn fn) noexcept
{
    // Safety: never intercept code in WiFi LLE-required ranges.
    if (IsWiFiRange(addr))
        return;

    // Maintain sorted order for binary search in TryDispatch.
    auto it = std::lower_bound(entries_.begin(), entries_.end(), addr,
        [](const ARM7HLEEntry& e, u32 a) { return e.addr < a; });

    if (it != entries_.end() && it->addr == addr)
        it->fn = fn;  // replace existing handler for this address
    else
        entries_.insert(it, {addr, fn});
}

// ── TryDispatch ──────────────────────────────────────────────────────────────

bool ARM7HLECore::TryDispatch(ARMv4* cpu, NDS& nds, u32 pc) const noexcept
{
    if (entries_.empty())
        return false;

    // Double-check: never dispatch WiFi-range PCs.
    if (IsWiFiRange(pc))
        return false;

    auto it = std::lower_bound(entries_.begin(), entries_.end(), pc,
        [](const ARM7HLEEntry& e, u32 a) { return e.addr < a; });

    if (it == entries_.end() || it->addr != pc)
        return false;

    return it->fn(cpu, nds);
}

// ── ScanFirmware ─────────────────────────────────────────────────────────────

void ARM7HLECore::ScanFirmware(NDS& nds) noexcept
{
    // Delegate to the audio subsystem scanner (ARM7HLE_Audio.cpp).
    ARM7HLE_Audio::ScanAndRegister(*this, nds);
}

// ── IPC FIFO idle detection ──────────────────────────────────────────────────
//
// When the ARM7 spins polling the IPC receive FIFO (waiting for ARM9 audio
// commands) we detect the idle loop and let the existing Halt/IdleLoop
// mechanism advance ARM7Timestamp to the next scheduled event.
//
// Safety: the ARM7 wakes naturally when IPCFIFO9 becomes non-empty (the
// ARM9 write path calls NoteIPCFIFOGotData() which resets the counter).

void ARM7HLECore::NoteIPCFIFOEmptyRead(ARMv4& arm7) noexcept
{
    ++ipcEmptyCount_;
    if (ipcEmptyCount_ >= kIPCIdleThreshold)
    {
        arm7.IdleLoop = 1;
        ipcEmptyCount_ = 0;
    }
}

void ARM7HLECore::NoteIPCFIFOGotData() noexcept
{
    ipcEmptyCount_ = 0;
}

// ── SPI idle detection ────────────────────────────────────────────────────────
//
// The ARM7 polls SPICNT (0x040001C0) waiting for the transfer-complete bit.
// When the SPI bus is idle and there is nothing to transfer, skip forward.

void ARM7HLECore::NoteSPIIdleRead(ARMv4& arm7) noexcept
{
    ++spiIdleCount_;
    if (spiIdleCount_ >= kSPIIdleThreshold)
    {
        arm7.IdleLoop = 1;
        spiIdleCount_ = 0;
    }
}

void ARM7HLECore::NoteSPITransferStart() noexcept
{
    spiIdleCount_ = 0;
}

} // namespace melonDS

#endif // LITEV_ARM7_HLE_AUDIO
