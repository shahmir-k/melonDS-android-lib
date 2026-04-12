/*
    Copyright 2016-2025 melonDS team
    liteDS modifications: ARM7 power management HLE (Phase 2.4)

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

// ── Phase 2.4: Power management HLE ─────────────────────────────────────────
//
// The ARM7 communicates with the DS power management IC (Mitsumi MM1581A,
// SPI device 3) to read battery level, detect lid state, and enter sleep mode.
// These are infrequent but the polling loops still consume ARM7 cycles.
//
// Planned implementation: intercept SPI device 3 read/write sequences and
// return static "battery full, lid open, no sleep" values.  This is safe
// because no game logic depends on battery level during normal emulation.
//
// This is not yet implemented.  The idle detection in ARM7HLECore covers
// the SPI polling overhead regardless of which device is being polled.
//
// Multiplayer risk: none.  The power management IC has no relationship to
// WiFi.

// (no code here — to be implemented in a future session)

#endif // LITEV_ARM7_HLE_AUDIO
