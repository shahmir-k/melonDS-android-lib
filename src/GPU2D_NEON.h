/*
    Copyright 2016-2025 melonDS team
    liteDS modifications: NEON SIMD 2D renderer (Phase 1.2)

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

#include "types.h"

// NEON-accelerated 2D compositing helpers.
// All functions require count to be a multiple of 4.
// Each function has a scalar fallback that is used when NEON is unavailable.
//
// Pixel format used throughout (internal melonDS format):
//   [31:24] = flags / alpha
//   [21:16] = Blue  (6-bit)
//   [13:8]  = Green (6-bit)
//   [5:0]   = Red   (6-bit)
//
// BGRA output format (written to the display framebuffer):
//   [31:24] = 0xFF  (alpha)
//   [23:16] = Red   (8-bit, scaled from 6-bit)
//   [15:8]  = Green (8-bit, scaled from 6-bit)
//   [7:0]   = Blue  (8-bit, scaled from 6-bit)

namespace melonDS
{
namespace GPU2D
{

// Convert buf[0..count) from internal format to BGRA in-place.
// Equivalent to the scalar loop in GPU2D_Soft.cpp::DrawScanline.
void NEON_ConvertToBGRA(u32* buf, int count) noexcept;

// Apply master brightness increase to buf[0..count).
// factor: brightness increase factor (0–16).
// Equivalent to ColorBrightnessUp(buf[i], factor, 0) for each pixel.
void NEON_BrightnessUp(u32* buf, int count, u32 factor) noexcept;

// Apply master brightness decrease to buf[0..count).
// factor: brightness decrease factor (0–16).
// Equivalent to ColorBrightnessDown(buf[i], factor, 15) for each pixel.
void NEON_BrightnessDown(u32* buf, int count, u32 factor) noexcept;

// Convert buf[0..count) from RGB555 to BGRA in a single pass.
// src: array of u16 RGB555 values (5:5:5 format, bit 0 = blue LSB).
// Combines the VRAM-display RGB555 → internal → BGRA chain into one step.
void NEON_RGB555ToBGRA(const u16* src, u32* dst, int count) noexcept;

} // namespace GPU2D
} // namespace melonDS
