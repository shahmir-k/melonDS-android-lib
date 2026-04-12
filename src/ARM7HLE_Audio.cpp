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

#ifdef LITEV_ARM7_HLE_AUDIO

#include "ARM7HLE_Audio.h"
#include "ARM7HLE_Core.h"
#include "ARM.h"
#include "NDS.h"
#include "Platform.h"

#include <cstring>

namespace melonDS
{
using Platform::Log;
using Platform::LogLevel;

namespace ARM7HLE_Audio
{

// ─── Nitro SDK firmware signature table ────────────────────────────────────
//
// Each entry describes a byte pattern that identifies a known Nitro SDK audio
// driver function inside ARM7 WRAM (0x03800000-0x0380FFFF).
//
// Patterns are ARM instruction sequences at the function prologue.
// Offsets within the match are in bytes from the start of the pattern.
//
// To add a new SDK version: capture ARM7 WRAM from a game that uses it,
// disassemble the audio subsystem, and record the prologue bytes here.
//
// Currently populated: none.  The IPC idle optimisation (ARM7HLECore) is
// sufficient for the primary performance goal without needing these.
// Adding concrete entries is left as future work once profiling data from
// real devices identifies the most common SDK versions.
//
// TODO: add Nitro SDK 3.x / 4.x / 5.x / TWL SDK signatures.

struct FirmwareSig
{
    const u8* bytes;     // byte pattern to search for
    u32       length;    // pattern length
    s32       fnOffset;  // byte offset from pattern start to function entry
    ARM7HLEFn handler;   // HLE handler to register at that address
    const char* name;    // human-readable name (for logging)
};

// (empty — see TODO above)
static constexpr FirmwareSig kSigs[] = {};

// ─── ScanAndRegister ────────────────────────────────────────────────────────

void ScanAndRegister(ARM7HLECore& core, NDS& nds) noexcept
{
    if (sizeof(kSigs) == 0)
    {
        // No signatures defined yet.  The IPC idle optimisation in
        // ARM7HLECore is active regardless; this function just adds
        // optional entry-point HLE on top.
        Log(LogLevel::Debug,
            "ARM7HLE_Audio: no firmware signatures defined; "
            "relying on IPC idle optimisation only\n");
        return;
    }

    // Scan ARM7 WRAM for each known signature pattern.
    const u32 wramBase = 0x03800000;
    const u32 wramSize = nds.ARM7WRAMSize;   // 0x10000

    for (const auto& sig : kSigs)
    {
        if (sig.length == 0 || sig.length > wramSize)
            continue;

        for (u32 off = 0; off <= wramSize - sig.length; ++off)
        {
            bool match = true;
            for (u32 b = 0; b < sig.length; ++b)
            {
                if (nds.ARM7Read8(wramBase + off + b) != sig.bytes[b])
                {
                    match = false;
                    break;
                }
            }
            if (!match)
                continue;

            u32 entryAddr = wramBase + off + static_cast<u32>(sig.fnOffset);
            core.Register(entryAddr, sig.handler);
            Log(LogLevel::Info,
                "ARM7HLE_Audio: registered %s at %08X\n",
                sig.name, entryAddr);
        }
    }
}

} // namespace ARM7HLE_Audio
} // namespace melonDS

#endif // LITEV_ARM7_HLE_AUDIO
