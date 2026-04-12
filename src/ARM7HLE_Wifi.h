/*
    Copyright 2016-2025 melonDS team
    liteDS modifications: ARM7 WiFi HLE skeleton (Phase 3.2)

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

#ifdef LITEV_ARM7_HLE_WIFI

namespace melonDS
{

class ARM7HLECore;
class NDS;

// ── ARM7HLE_Wifi ─────────────────────────────────────────────────────────────
//
// Phase 3.2: WiFi firmware HLE skeleton.
//
// Strategy:
//   Scan ARM7 WRAM (0x03800000) and main RAM (0x02000000) for Nitro SDK WiFi
//   driver function prologues and register HLE handlers with ARM7HLECore.
//
//   The registered handlers replicate the ARM7 firmware's register-write
//   sequence directly in C++, skipping hundreds of ARM7 instructions per WiFi
//   TX operation.  The existing Wifi.cpp register-level emulation (USTimer,
//   StartTX_Cmd, MP_SendCmd / MP_SendReply, etc.) is unchanged — only the
//   ARM7-side firmware execution is accelerated.
//
// Safety invariants:
//   1. ARM7HLECore::Register() enforces the WiFi LLE allowlist: no handler
//      is ever registered at an address in 0x04800000–0x04FFFFFF.
//   2. All registered entry points are in ARM7 WRAM or main RAM.
//   3. The WiFi LLE path (src/Wifi.cpp) is NOT modified by this subsystem.
//
// WARNING — Phase 3 is OPTIONAL and HIGH RISK.
//   Enable LITEV_ARM7_HLE_WIFI only after:
//     • Phase 2 exit criteria are signed off.
//     • ARM7 profiler data shows WiFi firmware is a top cycle consumer.
//     • Multiplayer smoke tests pass with LITEV_ARM7_HLE_WIFI=OFF.
//
//   Requires LITEV_ARM7_HLE_AUDIO to be enabled (provides ARM7HLECore).
//   Build system enforces this with a fatal_error if not set.
//
//   See docs/wifi-protocol/README.md for the full protocol analysis.

namespace ARM7HLE_Wifi
{

// Scan ARM7 WRAM (0x03800000–0x0380FFFF) and the first 512 KB of ARM7 main RAM
// (0x02000000–0x02080000) for Nitro SDK WiFi driver signatures and register the
// matching HLE handlers with `core`.
//
// Safe to call when no signatures are present — returns without registering
// any handlers, leaving the full LLE WiFi path active.
//
// Call this from NDS::SetupDirectBoot() after the ROM has been loaded, so the
// ARM7 binary is already in memory.
void ScanAndRegister(ARM7HLECore& core, NDS& nds) noexcept;

} // namespace ARM7HLE_Wifi
} // namespace melonDS

#endif // LITEV_ARM7_HLE_WIFI
