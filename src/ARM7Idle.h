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

// liteDS-v2 Unit 6 (M2.3) — ARM7 IPC/SPI idle detection.
//
// Ported CONCEPT (not code) from the v1 fork's LITEV_ARM7_HLE_AUDIO idle
// detector (melonDS-android-lib ARM7HLE_Core.{h,cpp}). Rather than requiring
// firmware symbol addresses, we count consecutive "nothing to do" polls on two
// ARM7-side IO registers and, past a small threshold, set ARM7.IdleLoop so the
// EXISTING branch-to-self fast-forward in ARMv4::Execute advances ARM7Timestamp
// to the slice target (== the true next scheduled event under LITEV_EVENT_SLICES).
// The ARM7 wakes naturally: any IPC data arrival, SPI transfer start, or IRQ
// delivered to the ARM7 resets the counters.
//
//   * 0x04000184 (IPCFIFOCNT7): bit 0x0100 set <-> ARM7's receive FIFO
//     (IPCFIFO9, ARM9->ARM7) is empty. Spinning on this while empty == idle.
//   * 0x040001C0 (SPICNT): bit 7 clear (not busy) + device-select == touchscreen
//     (bits 8..9 == 1). Polling for transfer-complete on an idle bus == idle.
//
// HARD SAFETY CONSTRAINTS (see the Unit 6 brief):
//   * This detector lives ONLY on the IPC/SPI read paths. It never touches any
//     W_* WiFi register or the 0x048xxxxx Marvell range. WiFi timing is untouched.
//   * Setting IdleLoop also zeroes the slice budget (ForceExecutionExit), mirroring
//     the emitted IdleLoop STRB site (Unit 3), so the JIT dispatcher/link guards
//     exit to C++ (StopExecution-first) and the fast-forward engages.
//   * The counters reset on ANY interrupt delivery to the ARM7 (NDS::UpdateIRQ),
//     on IPC FIFO writes from the ARM9, and on ARM7 SPI transfer start.
//
// Flag-ON changes timing (skips idle intervals) -> NOT bit-comparable to the
// flag-OFF goldens. Flag-OFF (default) compiles this to nothing.

#pragma once

#ifdef LITEV_ARM7_IDLE

#include "types.h"
#include "ARM.h"
#include "LiteProfile.h"

namespace melonDS
{

class ARM7IdleDetect
{
public:
    void Reset() noexcept
    {
        IPCEmptyCount = 0;
        SPIIdleCount = 0;
    }

    // ── IPC FIFO idle (0x04000184 read, receive FIFO empty) ─────────────────
    void NoteIPCFIFOEmptyRead(ARMv4& arm7) noexcept
    {
        if (++IPCEmptyCount >= kIPCIdleThreshold)
        {
            EngageIdle(arm7);
            IPCEmptyCount = 0;
        }
    }
    // ARM9 pushed a command into IPCFIFO9 (ARM7's receive FIFO).
    void NoteIPCFIFOGotData() noexcept { IPCEmptyCount = 0; }

    // ── SPI idle (0x040001C0 read, bus not busy / touchscreen selected) ──────
    void NoteSPIIdleRead(ARMv4& arm7, u16 spicnt) noexcept
    {
        // bit 7 (0x0080) = transfer in progress; device select (bits 8..9) == 1
        // is the touchscreen. Only count polls that see an idle touchscreen bus.
        if (!(spicnt & 0x0080) && ((spicnt >> 8) & 0x3) == 0x1)
        {
            if (++SPIIdleCount >= kSPIIdleThreshold)
            {
                EngageIdle(arm7);
                SPIIdleCount = 0;
            }
        }
        else
        {
            SPIIdleCount = 0;
        }
    }
    void NoteSPITransferStart() noexcept { SPIIdleCount = 0; }

    // Any IRQ delivered to the ARM7 means it has work again.
    void NoteInterrupt() noexcept
    {
        IPCEmptyCount = 0;
        SPIIdleCount = 0;
    }

private:
    void EngageIdle(ARMv4& arm7) noexcept
    {
        arm7.IdleLoop = 1;          // union-aliases StopExecution -> dispatcher exits
        arm7.ForceExecutionExit();  // zero the slice budget, per the Unit 3 invariant
        LITE_PROFILE_ADD(melonDS::LiteProfile::g_Frame.ARM7IdleSkips);
    }

    int IPCEmptyCount = 0;
    int SPIIdleCount = 0;

    static constexpr int kIPCIdleThreshold = 4; // consecutive empty polls before skip
    static constexpr int kSPIIdleThreshold = 4;
};

} // namespace melonDS

#endif // LITEV_ARM7_IDLE
