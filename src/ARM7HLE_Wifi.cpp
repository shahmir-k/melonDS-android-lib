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

#ifdef LITEV_ARM7_HLE_WIFI

#include "ARM7HLE_Wifi.h"
#include "ARM7HLE_Core.h"
#include "ARM.h"
#include "NDS.h"
#include "Wifi.h"
#include "Platform.h"

#include <cstring>

namespace melonDS
{
using Platform::Log;
using Platform::LogLevel;

namespace ARM7HLE_Wifi
{

// ─── Nitro SDK WiFi firmware signature table ──────────────────────────────────
//
// Each entry describes a byte pattern that identifies a known Nitro SDK WiFi
// driver function inside the ARM7 binary (loaded into main RAM or WRAM).
//
// Patterns are ARM/THUMB instruction sequences at the function prologue.
// All offsets are in bytes from the start of the pattern match.
//
// ── How to add a new signature ────────────────────────────────────────────────
// 1. Run the target game to the multiplayer lobby with LITEV_ARM7_PROFILE=ON.
//    Check arm7-profile output to confirm WiFi firmware is consuming cycles.
// 2. Dump ARM7 main RAM (0x02000000, 4 MB) and WRAM (0x03800000, 64 KB) via
//    a debug build.
// 3. Disassemble the ARM7 binary; locate the Nitro SDK WXSystem/WX functions:
//    - WX_StartMP       — initiates CMD round-trip (arms TXSlotCmd)
//    - WX_EndMP         — concludes MP session
//    - WXi_MpSendFrame  — low-level frame TX (arms any TXSlot)
//    - WXi_MpRecvFrame  — RX polling / frame copy to shared RAM + IPC FIFO word
// 4. Record 8–12 byte prologue bytes of each target function (ARM mode = 4-byte
//    aligned; THUMB mode = 2-byte aligned; use the lower 2 bits of fnOffset to
//    signal THUMB if needed).
// 5. Implement a handler (see handler stubs below) and add an entry to kSigs[].
// 6. Enable LITEV_ARM7_HLE_WIFI in a test build and run the multiplayer smoke
//    test suite (docs/wifi-protocol/README.md §11) before committing.
//
// SDK versions known to exist in cartridge ARM7 binaries:
//   Nitro SDK 3.1 / 3.2  (earlier 2006–2007 titles)
//   Nitro SDK 4.0 / 4.1  (most 2007–2008 titles)
//   Nitro SDK 5.0         (later titles, overlaps with TWL SDK)
//   TWL-SDK 5.4 / 5.5    (DSi-enhanced titles)
//
// Currently populated: none.
// The WiFi path is fully covered by LLE (src/Wifi.cpp) until real signatures
// are captured and validated on target hardware.
//
// TODO (Phase 3.1 continuation):
//   • Capture ARM7 RAM dumps from Mario Kart DS, Pokémon Diamond/Pearl,
//     Metroid Prime Hunters at the multiplayer lobby screen.
//   • Identify WX_StartMP / WXi_MpSendFrame prologues for SDK 3.x and 4.x.
//   • Add patterns here; one entry per (SDK version × function) combination.
//
// ─────────────────────────────────────────────────────────────────────────────

struct FirmwareSig
{
    const u8* bytes;     // byte pattern to search for
    u32       length;    // pattern length (0 = entry disabled)
    s32       fnOffset;  // byte offset from pattern start to function entry
                         // (negative values allowed; bit 0 = 1 means THUMB mode)
    ARM7HLEFn handler;   // HLE handler called when ARM7 PC reaches that address
    const char* name;    // human-readable name (for logging only)
};

// ─── Handler stubs ────────────────────────────────────────────────────────────
//
// Each handler receives a pointer to the ARM7 CPU state and the NDS context.
// Return true  → HLE handled the call; ARM7HLECore skips LLE execution.
// Return false → Fall through to LLE (safe fallback when unimplemented).
//
// Handler contract (AAPCS calling convention):
//   R[0]–R[3]  = first four arguments passed by the game's ARM7 code
//   R[14]      = link register (return address)
//   After handling: set R[0] = return value, set R[15] = R[14] + offset
//                   (offset = 0 for ARM, 0 for THUMB — ARM7HLECore advances PC)
//
// ARM7HLECore advances R[15] to (R[14] | THUMB_bit) after returning true,
// matching the calling convention the firmware would have used.

// WXi_MpSendFrame stub:
//   When real signatures are available, this handler will:
//   1. Read frame address and length from R[0], R[1].
//   2. Write the frame address to W_TXSlotCmd (via nds.Wifi.Write).
//   3. Pulse W_TXReqSet.
//   4. Set R[0] = 0 (success) and return true.
//   The existing Wifi.cpp USTimer will then complete the TX as normal.
//
// Currently returns false (fall through to LLE) until signatures are defined.
[[maybe_unused]]
static bool HLE_WXi_MpSendFrame(ARMv4* arm7, NDS& nds)
{
    // Not yet implemented — no firmware signatures defined.
    // Fall through to LLE.
    return false;
}

// WX_StartMP stub:
//   Would intercept the host CMD transmission and reply-collection cycle,
//   skipping the ARM7 polling loops entirely.
//   Returns false (LLE) until signatures are defined.
[[maybe_unused]]
static bool HLE_WX_StartMP(ARMv4* arm7, NDS& nds)
{
    return false;
}

// WXi_MpRecvFrame stub:
//   Would intercept RX processing — reading frames from the WiFi RAM ring
//   buffer, copying payload to ARM7 shared RAM, and sending the IPC FIFO word.
//   This is the hardest function to HLE because the IPC FIFO word format is
//   game-specific.  Leave as LLE indefinitely until a generic approach is found.
[[maybe_unused]]
static bool HLE_WXi_MpRecvFrame(ARMv4* arm7, NDS& nds)
{
    return false;
}

// (empty — see TODO above)
static constexpr FirmwareSig kSigs[] = {};

// ─── ScanAndRegister ──────────────────────────────────────────────────────────

void ScanAndRegister(ARM7HLECore& core, NDS& nds) noexcept
{
    if (sizeof(kSigs) == 0)
    {
        Log(LogLevel::Debug,
            "ARM7HLE_Wifi: no firmware signatures defined; "
            "WiFi path remains fully LLE\n");
        return;
    }

    // Scan regions where the ARM7 WiFi driver may reside.
    // The Nitro SDK ARM7 binary is normally loaded by the firmware to one of:
    //   • 0x02000000 — Nitro SDK default ARM7 load address (main RAM)
    //   • 0x037F8000 — alternative WRAM high load address
    // We also scan ARM7 WRAM (0x03800000) where runtime firmware fragments land.

    struct ScanRegion { u32 base; u32 size; const char* name; };
    static constexpr ScanRegion kRegions[] = {
        { 0x02000000, 0x00080000, "ARM7 main-RAM (first 512 KB)" },
        { 0x03800000, 0x00010000, "ARM7 WRAM" },
    };

    for (const auto& region : kRegions)
    {
        for (const auto& sig : kSigs)
        {
            if (sig.length == 0 || sig.length > region.size)
                continue;

            for (u32 off = 0; off <= region.size - sig.length; ++off)
            {
                bool match = true;
                for (u32 b = 0; b < sig.length; ++b)
                {
                    if (nds.ARM7Read8(region.base + off + b) != sig.bytes[b])
                    {
                        match = false;
                        break;
                    }
                }
                if (!match)
                    continue;

                // fnOffset may be negative (pattern starts after the prologue)
                // or have bit 0 set to flag THUMB mode.  ARM7HLECore::Register
                // records the address; TryDispatch checks against it exactly.
                u32 entryAddr = static_cast<u32>(
                    static_cast<s32>(region.base + off) + sig.fnOffset);

                core.Register(entryAddr, sig.handler);
                Log(LogLevel::Info,
                    "ARM7HLE_Wifi: registered %s at %08X (region %s)\n",
                    sig.name, entryAddr, region.name);
            }
        }
    }
}

} // namespace ARM7HLE_Wifi
} // namespace melonDS

#endif // LITEV_ARM7_HLE_WIFI
