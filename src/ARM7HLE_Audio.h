/*
    Copyright 2016-2025 melonDS team
    liteDS modifications: ARM7 audio HLE (Phase 2.2)

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

namespace melonDS
{

class ARM7HLECore;
class NDS;

// ── ARM7HLE_Audio ─────────────────────────────────────────────────────────────
//
// Provides firmware signature scanning and HLE handlers for the DS Nitro SDK
// ARM7 audio driver.
//
// Strategy (Phase 2.2):
//   The primary gain comes from the IPC idle optimisation in ARM7HLECore
//   (NoteIPCFIFOEmptyRead), which eliminates the tight polling loop the ARM7
//   runs while waiting for audio commands.  No firmware address knowledge is
//   required for that optimisation.
//
//   For games whose ARM7 WRAM contains recognisable Nitro SDK v3/v4/v5
//   signatures, ScanAndRegister() additionally registers HLE entry points for
//   the per-channel sound command dispatcher and the SPU channel setup routine.
//   These handlers translate the incoming IPC command directly into SPU register
//   writes, skipping the ARM7 firmware decode loop entirely.
//
// WiFi safety: none of the registered entry points are in WiFi register space.
// The ARM7HLECore::Register() call enforces this independently.
//
// Reference implementations consulted (pattern study only, no code copied):
//   • melonDS DSi_DSP.cpp — local HLE pattern
//   • DSVita (Grarak/DSVita) — ARM7 HLE in Rust, GPLv2-compatible reference
//   • DeSmuMEPSP — older ARM7 HLE, entry-point cross-reference

namespace ARM7HLE_Audio
{

// Scan ARM7 WRAM (0x03800000–0x0380FFFF) and BIOS (0x00000000–0x00003FFF)
// for Nitro SDK audio driver signatures and register the matching HLE handlers
// with `core`.  Safe to call even if no known signatures are present — the
// IPC idle optimisation is always active regardless.
void ScanAndRegister(ARM7HLECore& core, NDS& nds) noexcept;

} // namespace ARM7HLE_Audio
} // namespace melonDS

#endif // LITEV_ARM7_HLE_AUDIO
