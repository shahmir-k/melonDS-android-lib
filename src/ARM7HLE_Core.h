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

#pragma once

#ifdef LITEV_ARM7_HLE_AUDIO

#include <algorithm>
#include <vector>
#include "types.h"

namespace melonDS
{

class ARMv4;
class NDS;

// ─── HLE handler function type ──────────────────────────────────────────────
//
// An HLE handler is called instead of the ARM7 executing the instruction at
// the registered PC. The handler must:
//   1. Perform the equivalent work of the firmware function.
//   2. Advance NDS.ARM7Timestamp by a plausible cycle count.
//   3. Set cpu->R[15] = cpu->R[14] to simulate a BX LR return, OR set it
//      to any branch target to tail-call another firmware function.
//   4. Return true so the dispatch loop skips LLE for this block.
//
// Returning false is safe: the LLE interpreter will handle the block normally.
// This allows gradual, per-game tuning of which functions are HLE'd.
using ARM7HLEFn = bool (*)(ARMv4* cpu, NDS& nds);

struct ARM7HLEEntry
{
    u32        addr;
    ARM7HLEFn  fn;
};

// ─── Safety: WiFi LLE-only address ranges ───────────────────────────────────
//
// The ARM7 WiFi firmware accesses the Marvell 88W8686 register interface.
// Code in these PC ranges MUST execute via LLE.  The HLE dispatcher checks
// this before every dispatch and refuses to intercept matching addresses.
// This invariant is non-negotiable for multiplayer correctness.
//
// See docs/wifi-protocol/README.md for the full register map.
struct ARM7HLEWiFiRange
{
    u32 lo;  // inclusive
    u32 hi;  // exclusive
};

static constexpr ARM7HLEWiFiRange kWiFiLLERanges[] = {
    { 0x04800000u, 0x05000000u },  // Marvell 88W8686 WiFi registers
};

// ─── ARM7HLECore ─────────────────────────────────────────────────────────────
//
// Maintains the sorted entry-point table and the per-IO-poll idle counters.
// One instance lives in NDS under LITEV_ARM7_HLE_AUDIO.
//
// Idle-loop optimisations (Phases 2.2 / 2.3):
//   Rather than requiring firmware symbol addresses, we track consecutive
//   "nothing to do" reads on two key IO registers:
//     • 0x04000184 (IPCFIFOCNT7): bit 8 set ↔ receive FIFO empty.
//       When the ARM7 spins reading this register with no new IPC commands,
//       we set ARM7.IdleLoop to advance the clock via the existing mechanism.
//     • 0x040001C0 (SPICNT): bit 7 set ↔ SPI transfer in progress.
//       When the ARM7 polls for SPI completion and the bus is idle, same trick.
//
// Both optimisations are safe: the ARM7 wakes up naturally when the trigger
// condition changes (IPC FIFO non-empty, SPI transfer complete).

class ARM7HLECore
{
public:
    // Called at NDS::Reset() and ROM load.
    void Reset() noexcept;

    // Register an entry point.  Silently rejects WiFi-range addresses.
    void Register(u32 addr, ARM7HLEFn fn) noexcept;

    // Check whether pc falls in a WiFi LLE-required range.
    static bool IsWiFiRange(u32 pc) noexcept;

    // Try to dispatch pc.  Returns true if the handler took over.
    // The ARM.Execute() loop calls this before executing each block/instruction.
    bool TryDispatch(ARMv4* cpu, NDS& nds, u32 pc) const noexcept;

    // Scan ARM7 WRAM/BIOS for Nitro SDK firmware signatures and register
    // handlers.  Called once after ROM is loaded into memory.
    void ScanFirmware(NDS& nds) noexcept;

    // ── IPC FIFO idle detection (Phase 2.2) ─────────────────────────────────
    // Called from NDS::ARM7IORead16(0x04000184) when the result indicates
    // receive FIFO empty (IPCFIFO9 empty from ARM7's perspective).
    void NoteIPCFIFOEmptyRead(ARMv4& arm7) noexcept;

    // Called when IPCFIFO9 becomes non-empty (ARM9 has sent a command).
    void NoteIPCFIFOGotData() noexcept;

    // ── SPI idle detection (Phase 2.3) ──────────────────────────────────────
    // Called from NDS::ARM7IORead8/16(0x040001C0) when SPICNT bit 7 = 0
    // (transfer complete / SPI not busy).
    void NoteSPIIdleRead(ARMv4& arm7) noexcept;

    // Called when a new SPI transfer is started (resets the idle counter).
    void NoteSPITransferStart() noexcept;

private:
    std::vector<ARM7HLEEntry> entries_; // sorted by addr, binary-searched

    // IPC idle state
    int ipcEmptyCount_ = 0;
    static constexpr int kIPCIdleThreshold = 4; // consecutive empty reads before skip

    // SPI idle state
    int spiIdleCount_ = 0;
    static constexpr int kSPIIdleThreshold = 4;
};

} // namespace melonDS

#endif // LITEV_ARM7_HLE_AUDIO
