/*
    Copyright 2016-2025 melonDS team
    liteDS modifications: ARM7 SPI/touchscreen HLE (Phase 2.3)

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

// ── Phase 2.3: SPI / touchscreen HLE ────────────────────────────────────────
//
// The ARM7 polls the SPI bus (SPICNT 0x040001C0) to read touchscreen
// coordinates and pass them to the ARM9 via IPC.  This is a tight polling
// loop that burns ARM7 cycles on every frame.
//
// This module's contribution to Phase 2.3 is the idle detection via
// ARM7HLECore::NoteSPIIdleRead() / NoteSPITransferStart().  Those hooks are
// called from NDS::ARM7IORead8/16(0x040001C0) and NDS::ARM7IOWrite16(0x040001C0)
// respectively.  No PC-level interception is implemented here yet.
//
// The SPI touchscreen channel (device 1 on the SPI bus) is the idle target.
// The firmware EEPROM (device 0) and power management IC (device 3) must
// remain on LLE.  The hooks in NDS.cpp call NoteSPIIdleRead() only when the
// selected SPI device is the touchscreen (bit 8-9 of SPICNT = 0b01).
//
// Future work: add WRAM signature scan for the SPI polling loop entry point
// and intercept it with a handler that reads SPI.GetTouchXY() directly.

// (no code here — the idle path is in ARM7HLE_Core.cpp / NDS.cpp)

#endif // LITEV_ARM7_HLE_AUDIO
