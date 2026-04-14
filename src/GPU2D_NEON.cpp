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

// NEON-accelerated 2D pixel operations for liteDS (Phase 1.2).
//
// Each function has both an ARM NEON path (4 pixels/cycle or better) and a
// scalar fallback that produces identical output.  The NEON path is selected
// at compile time via #if defined(__ARM_NEON).
//
// Internal pixel format (melonDS):
//   [31:24] = flags/alpha   [21:16] = Blue6   [13:8] = Green6   [5:0] = Red6
//
// BGRA output format:
//   [31:24] = 0xFF   [23:16] = Red8   [15:8] = Green8   [7:0] = Blue8
//   where X8 = (X6 << 2) | (X6 >> 4)   (standard 6→8 bit expansion)

#include "GPU2D_NEON.h"

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace melonDS
{
namespace GPU2D
{

// ─────────────────────────────────────────────────────────────────────────────
// NEON_ConvertToBGRA
// ─────────────────────────────────────────────────────────────────────────────

void NEON_ConvertToBGRA(u32* buf, int count) noexcept
{
#if defined(__ARM_NEON)
    // Process 4 pixels per iteration.
    // Each channel: 6-bit value → 8-bit via (x6 << 2) | (x6 >> 4).
    const uint32x4_t k_mask_r  = vdupq_n_u32(0x0000003F);  // bits  5:0  = Red
    const uint32x4_t k_mask_g  = vdupq_n_u32(0x00003F00);  // bits 13:8  = Green
    const uint32x4_t k_mask_b  = vdupq_n_u32(0x003F0000);  // bits 21:16 = Blue
    const uint32x4_t k_alpha   = vdupq_n_u32(0xFF000000);

    int i = 0;
    for (; i <= count - 4; i += 4)
    {
        uint32x4_t c = vld1q_u32(buf + i);

        // Extract channels into bits 5:0 each
        uint32x4_t r6 = vandq_u32(c, k_mask_r);
        uint32x4_t g6 = vshrq_n_u32(vandq_u32(c, k_mask_g), 8);
        uint32x4_t b6 = vshrq_n_u32(vandq_u32(c, k_mask_b), 16);

        // Scale 6→8 bit: (x6 << 2) | (x6 >> 4)
        uint32x4_t r8 = vorrq_u32(vshlq_n_u32(r6, 2), vshrq_n_u32(r6, 4));
        uint32x4_t g8 = vorrq_u32(vshlq_n_u32(g6, 2), vshrq_n_u32(g6, 4));
        uint32x4_t b8 = vorrq_u32(vshlq_n_u32(b6, 2), vshrq_n_u32(b6, 4));

        // Pack BGRA: [7:0]=B [15:8]=G [23:16]=R [31:24]=0xFF
        uint32x4_t result = vorrq_u32(
            vorrq_u32(b8, vshlq_n_u32(g8, 8)),
            vorrq_u32(vshlq_n_u32(r8, 16), k_alpha)
        );

        vst1q_u32(buf + i, result);
    }
    // Scalar tail (handles any remainder when count is not a multiple of 4)
    for (; i < count; i++)
    {
        u32 c = buf[i];
        u32 r6 = c & 0x3F;
        u32 g6 = (c >> 8) & 0x3F;
        u32 b6 = (c >> 16) & 0x3F;
        u32 r8 = (r6 << 2) | (r6 >> 4);
        u32 g8 = (g6 << 2) | (g6 >> 4);
        u32 b8 = (b6 << 2) | (b6 >> 4);
        buf[i] = b8 | (g8 << 8) | (r8 << 16) | 0xFF000000;
    }
#else
    // Scalar fallback — identical logic to the original GPU2D_Soft.cpp loop.
    // Processes 2 pixels at a time using u64 to match the existing scalar code.
    int i = 0;
    for (; i <= count - 2; i += 2)
    {
        u64 c = *(u64*)(buf + i);

        u64 r = (c << 18) & 0x00FC000000FC0000ULL;
        u64 g = (c << 2)  & 0x000000FC000000FCULL << 8;  // bits 15:10 and 47:42
        u64 b = (c >> 14) & 0x000000FC000000FCULL;       // bits 7:2 and 39:34
        c = r | g | b;

        *(u64*)(buf + i) = c | ((c & 0x00C0C0C000C0C0C0ULL) >> 6) | 0xFF000000FF000000ULL;
    }
    // Scalar single-pixel tail
    for (; i < count; i++)
    {
        u32 c = buf[i];
        u32 r6 = c & 0x3F;
        u32 g6 = (c >> 8) & 0x3F;
        u32 b6 = (c >> 16) & 0x3F;
        u32 r8 = (r6 << 2) | (r6 >> 4);
        u32 g8 = (g6 << 2) | (g6 >> 4);
        u32 b8 = (b6 << 2) | (b6 >> 4);
        buf[i] = b8 | (g8 << 8) | (r8 << 16) | 0xFF000000;
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// NEON_BrightnessUp
// Equivalent to: buf[i] = ColorBrightnessUp(buf[i], factor, 0)
//   per-channel: c' = c + ((63 - c) * factor) >> 4
// ─────────────────────────────────────────────────────────────────────────────

void NEON_BrightnessUp(u32* buf, int count, u32 factor) noexcept
{
    if (factor == 0) return;

#if defined(__ARM_NEON)
    const uint32x4_t k_mask6  = vdupq_n_u32(0x3F);
    const uint32x4_t k_max6   = vdupq_n_u32(63);
    const uint32x4_t k_alpha  = vdupq_n_u32(0xFF000000);
    const uint32x4_t k_factor = vdupq_n_u32(factor);

    int i = 0;
    for (; i <= count - 4; i += 4)
    {
        uint32x4_t c = vld1q_u32(buf + i);

        // Extract each 6-bit channel into bits 5:0
        uint32x4_t r = vandq_u32(c, k_mask6);
        uint32x4_t g = vshrq_n_u32(vandq_u32(c, vdupq_n_u32(0x3F00)), 8);
        uint32x4_t b = vshrq_n_u32(vandq_u32(c, vdupq_n_u32(0x3F0000)), 16);

        // c' = c + ((63 - c) * factor) >> 4   (no bias for BrightnessUp)
        r = vaddq_u32(r, vshrq_n_u32(vmulq_u32(vsubq_u32(k_max6, r), k_factor), 4));
        g = vaddq_u32(g, vshrq_n_u32(vmulq_u32(vsubq_u32(k_max6, g), k_factor), 4));
        b = vaddq_u32(b, vshrq_n_u32(vmulq_u32(vsubq_u32(k_max6, b), k_factor), 4));

        // Clamp to 6 bits (result can reach 63 but not overflow a u32)
        r = vminq_u32(r, k_max6);
        g = vminq_u32(g, k_max6);
        b = vminq_u32(b, k_max6);

        // Repack into internal format
        uint32x4_t result = vorrq_u32(k_alpha,
                            vorrq_u32(r,
                            vorrq_u32(vshlq_n_u32(g, 8), vshlq_n_u32(b, 16))));
        vst1q_u32(buf + i, result);
    }
    for (; i < count; i++)
    {
        u32 c = buf[i];
        u32 rb = c & 0x3F003F;
        u32 g  = c & 0x003F00;
        rb += ((((0x3F003F - rb) * factor)) >> 4) & 0x3F003F;
        g  += ((((0x003F00 - g)  * factor)) >> 4) & 0x003F00;
        buf[i] = rb | g | 0xFF000000;
    }
#else
    for (int i = 0; i < count; i++)
    {
        u32 c  = buf[i];
        u32 rb = c & 0x3F003F;
        u32 g  = c & 0x003F00;
        rb += ((((0x3F003F - rb) * factor)) >> 4) & 0x3F003F;
        g  += ((((0x003F00 - g)  * factor)) >> 4) & 0x003F00;
        buf[i] = rb | g | 0xFF000000;
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// NEON_BrightnessDown
// Equivalent to: buf[i] = ColorBrightnessDown(buf[i], factor, 15)
//   per-channel: c' = c - ((c * factor + 15) >> 4)
// ─────────────────────────────────────────────────────────────────────────────

void NEON_BrightnessDown(u32* buf, int count, u32 factor) noexcept
{
    if (factor == 0) return;

#if defined(__ARM_NEON)
    const uint32x4_t k_mask6  = vdupq_n_u32(0x3F);
    const uint32x4_t k_bias   = vdupq_n_u32(15);   // bias = 0xF
    const uint32x4_t k_alpha  = vdupq_n_u32(0xFF000000);
    const uint32x4_t k_factor = vdupq_n_u32(factor);

    int i = 0;
    for (; i <= count - 4; i += 4)
    {
        uint32x4_t c = vld1q_u32(buf + i);

        uint32x4_t r = vandq_u32(c, k_mask6);
        uint32x4_t g = vshrq_n_u32(vandq_u32(c, vdupq_n_u32(0x3F00)), 8);
        uint32x4_t b = vshrq_n_u32(vandq_u32(c, vdupq_n_u32(0x3F0000)), 16);

        // c' = c - ((c * factor + 15) >> 4)
        // The subtracted value is at most c (when factor=16), so no underflow.
        uint32x4_t sub_r = vshrq_n_u32(vaddq_u32(vmulq_u32(r, k_factor), k_bias), 4);
        uint32x4_t sub_g = vshrq_n_u32(vaddq_u32(vmulq_u32(g, k_factor), k_bias), 4);
        uint32x4_t sub_b = vshrq_n_u32(vaddq_u32(vmulq_u32(b, k_factor), k_bias), 4);

        r = vsubq_u32(r, vminq_u32(sub_r, r));
        g = vsubq_u32(g, vminq_u32(sub_g, g));
        b = vsubq_u32(b, vminq_u32(sub_b, b));

        uint32x4_t result = vorrq_u32(k_alpha,
                            vorrq_u32(r,
                            vorrq_u32(vshlq_n_u32(g, 8), vshlq_n_u32(b, 16))));
        vst1q_u32(buf + i, result);
    }
    for (; i < count; i++)
    {
        u32 c  = buf[i];
        u32 rb = c & 0x3F003F;
        u32 g  = c & 0x003F00;
        rb -= (((rb * factor) + (15u * 0x010001u)) >> 4) & 0x3F003F;
        g  -= (((g  * factor) + (15u * 0x000100u)) >> 4) & 0x003F00;
        buf[i] = rb | g | 0xFF000000;
    }
#else
    for (int i = 0; i < count; i++)
    {
        u32 c  = buf[i];
        u32 rb = c & 0x3F003F;
        u32 g  = c & 0x003F00;
        rb -= (((rb * factor) + (15u * 0x010001u)) >> 4) & 0x3F003F;
        g  -= (((g  * factor) + (15u * 0x000100u)) >> 4) & 0x003F00;
        buf[i] = rb | g | 0xFF000000;
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// NEON_RGB555ToBGRA
// Converts RGB555 (u16) directly to 8-bit BGRA (u32) in one pass.
// Replaces the two-step RGB555→internal→BGRA used in VRAM display mode.
//
// RGB555 input: [15] unused  [14:10]=B5  [9:5]=G5  [4:0]=R5
// BGRA output:  [31:24]=0xFF [23:16]=R8  [15:8]=G8 [7:0]=B8
// where X8 = (X5 << 3) | (X5 >> 2)  (standard 5→8 bit expansion)
// ─────────────────────────────────────────────────────────────────────────────

void NEON_RGB555ToBGRA(const u16* src, u32* dst, int count) noexcept
{
#if defined(__ARM_NEON)
    // Process 8 pixels per iteration using uint16x8 for the source.
    int i = 0;
    for (; i <= count - 8; i += 8)
    {
        uint16x8_t s = vld1q_u16(src + i);

        // Extract 5-bit channels
        uint16x8_t r5 = vandq_u16(s, vdupq_n_u16(0x001F));           // bits 4:0
        uint16x8_t g5 = vandq_u16(vshrq_n_u16(s, 5),  vdupq_n_u16(0x001F)); // bits 9:5
        uint16x8_t b5 = vandq_u16(vshrq_n_u16(s, 10), vdupq_n_u16(0x001F)); // bits 14:10

        // Scale 5→8 bit: x8 = (x5 << 3) | (x5 >> 2)
        uint16x8_t r8 = vorrq_u16(vshlq_n_u16(r5, 3), vshrq_n_u16(r5, 2));
        uint16x8_t g8 = vorrq_u16(vshlq_n_u16(g5, 3), vshrq_n_u16(g5, 2));
        uint16x8_t b8 = vorrq_u16(vshlq_n_u16(b5, 3), vshrq_n_u16(b5, 2));

        // Widen to 32-bit and pack BGRA
        uint32x4_t r8_lo = vmovl_u16(vget_low_u16(r8));
        uint32x4_t r8_hi = vmovl_u16(vget_high_u16(r8));
        uint32x4_t g8_lo = vmovl_u16(vget_low_u16(g8));
        uint32x4_t g8_hi = vmovl_u16(vget_high_u16(g8));
        uint32x4_t b8_lo = vmovl_u16(vget_low_u16(b8));
        uint32x4_t b8_hi = vmovl_u16(vget_high_u16(b8));

        const uint32x4_t k_alpha = vdupq_n_u32(0xFF000000);

        uint32x4_t lo = vorrq_u32(b8_lo,
                        vorrq_u32(vshlq_n_u32(g8_lo, 8),
                        vorrq_u32(vshlq_n_u32(r8_lo, 16), k_alpha)));
        uint32x4_t hi = vorrq_u32(b8_hi,
                        vorrq_u32(vshlq_n_u32(g8_hi, 8),
                        vorrq_u32(vshlq_n_u32(r8_hi, 16), k_alpha)));

        vst1q_u32(dst + i,     lo);
        vst1q_u32(dst + i + 4, hi);
    }
    // Scalar tail
    for (; i < count; i++)
    {
        u16 c = src[i];
        u32 r5 = c & 0x1F;
        u32 g5 = (c >> 5) & 0x1F;
        u32 b5 = (c >> 10) & 0x1F;
        u32 r8 = (r5 << 3) | (r5 >> 2);
        u32 g8 = (g5 << 3) | (g5 >> 2);
        u32 b8 = (b5 << 3) | (b5 >> 2);
        dst[i] = b8 | (g8 << 8) | (r8 << 16) | 0xFF000000;
    }
#else
    // Scalar fallback
    for (int i = 0; i < count; i++)
    {
        u16 c = src[i];
        u32 r5 = c & 0x1F;
        u32 g5 = (c >> 5) & 0x1F;
        u32 b5 = (c >> 10) & 0x1F;
        u32 r8 = (r5 << 3) | (r5 >> 2);
        u32 g8 = (g5 << 3) | (g5 >> 2);
        u32 b8 = (b5 << 3) | (b5 >> 2);
        dst[i] = b8 | (g8 << 8) | (r8 << 16) | 0xFF000000;
    }
#endif
}

} // namespace GPU2D
} // namespace melonDS
