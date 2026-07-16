/*
    Copyright 2016-2026 melonDS team

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

#include "GPU3D_Soft.h"

#include <algorithm>
#include <chrono>
#include <stdio.h>
#include <string.h>
#if defined(LITEV_SOFT3D_FAST) && (defined(__ARM_NEON) || defined(__aarch64__))
#include <arm_neon.h>
#define LITEV_SOFT3D_NEON 1
#endif
#include "NDS.h"
#include "GPU.h"
#include "LitevSoftProf.h"

#if defined(__ANDROID__) && defined(LITEV_PIN_RENDER)
#include <sched.h>
// Pin the software 3D render + band-worker threads to cores {0,1,2}, off the emu's core
// (3), so they stop preempting the critical emu thread.
//
// LITEV_RENDER_4CORE: once SOFT3D_ASYNC removed the emu's 3D barrier stall, the emu
// thread only needs ~12.8ms of a ~22.5ms frame -- core 3 sits ~43% IDLE while the 3D
// raster (the new critical path, ~14.7ms over 3 bands) is fenced off it. Widen the
// raster to all 4 cores and let the scheduler share core 3: the emu thread runs at
// nice -10 and the band workers at default nice, so the emu still PREEMPTS them
// whenever it needs the core -- we only harvest core 3's idle residue.
static void litevPinRenderThread()
{
    cpu_set_t set; CPU_ZERO(&set);
    CPU_SET(0, &set); CPU_SET(1, &set); CPU_SET(2, &set);
#ifdef LITEV_RENDER_4CORE
    CPU_SET(3, &set);
#endif
    sched_setaffinity(0, sizeof(set), &set);
}
#else
static void litevPinRenderThread() {}
#endif

namespace melonDS
{

// Always-available monotonic ms (the band load balancer needs timing whether or not
// the LITEV_SOFTPROF diagnostic build is on).
static inline double LitevSP_Now()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void RenderThreadFunc();

#ifdef LITEV_SOFT3D_BANDED
// Per-band render state (see GPU3D_Soft.h). thread_local => one copy per band thread.
thread_local SoftRenderer3D::RendererPolygon SoftRenderer3D::PolygonList[2048];
thread_local u8 SoftRenderer3D::StencilBuffer[256*2];
thread_local bool SoftRenderer3D::PrevIsShadowMask;
// Default full-frame window: on any thread that never runs a band (the main/emu
// thread or the non-threaded render path) the y-range gate is a no-op.
thread_local s32 SoftRenderer3D::BandY0 = 0;
thread_local s32 SoftRenderer3D::BandY1 = 192;
#endif

#ifdef LITEV_SOFT3D_BANDTILE
// Cache-resident raster tiles + per-chunk retarget pointers (see GPU3D_Soft.h). One copy
// per band thread. Pointers default to nullptr and are set to the real buffers at every
// raster entry (RenderBand / RenderPolygons) before any shadow reads them.
thread_local u32 SoftRenderer3D::BtTileColor[SoftRenderer3D::BtTileSlot * 2];
thread_local u32 SoftRenderer3D::BtTileDepth[SoftRenderer3D::BtTileSlot * 2];
thread_local u32 SoftRenderer3D::BtTileAttr [SoftRenderer3D::BtTileSlot * 2];
thread_local u32* SoftRenderer3D::BtCB = nullptr;
thread_local u32* SoftRenderer3D::BtDB = nullptr;
thread_local u32* SoftRenderer3D::BtAB = nullptr;
thread_local s32  SoftRenderer3D::BtSlot = 0;
thread_local s32  SoftRenderer3D::BtTileY0 = 0;

// Placed at the top of the raster fns (before any framebuffer / pixeladdr use). The buffer
// shadows retarget every ColorBuffer[pixeladdr]; the BufferSize shadow retargets the AA
// under-slot stride; the FirstPixelOffset shadow folds the chunk y-origin into pixeladdr so
// it becomes tile-relative. When Bt* hold the real buffers (default), this is a no-op.
#define BT_SHADOW_FB() \
    [[maybe_unused]] u32* const ColorBuffer = BtCB; \
    [[maybe_unused]] u32* const DepthBuffer = BtDB; \
    [[maybe_unused]] u32* const AttrBuffer  = BtAB; \
    [[maybe_unused]] const s32 BufferSize   = BtSlot; \
    [[maybe_unused]] const s32 FirstPixelOffset = (ScanlineWidth + 1) - BtTileY0 * ScanlineWidth
// PlotTranslucentPixel takes pixeladdr as an argument and uses neither BufferSize nor
// FirstPixelOffset, so shadow only the three buffer bases.
#define BT_SHADOW_FB_PLOT() \
    [[maybe_unused]] u32* const ColorBuffer = BtCB; \
    [[maybe_unused]] u32* const DepthBuffer = BtDB; \
    [[maybe_unused]] u32* const AttrBuffer  = BtAB
#endif

#ifdef LITEV_SOFT3D_PIPELINE2
// Pipeline step 1 shadow: retarget the 3D-plane accesses to a frame-parity BANK by shadowing
// the member arrays with a base offset by P2RenderBank (raster/producer) or P2ConsumeBank
// (consumer). Under depth-1 both offsets equal the current frame's bank -> byte-identical.
// P2_SHADOW_RENDER: raster + producer-side final pass / clear (write side).
#define P2_SHADOW_RENDER() \
    [[maybe_unused]] u32* const ColorBuffer = this->ColorBuffer + P2RenderBank; \
    [[maybe_unused]] u32* const DepthBuffer = this->DepthBuffer + P2RenderBank; \
    [[maybe_unused]] u32* const AttrBuffer  = this->AttrBuffer  + P2RenderBank
// P2_SHADOW_CONSUME: consumer-side final pass (OVERLAP GetLine path).
#define P2_SHADOW_CONSUME() \
    [[maybe_unused]] u32* const ColorBuffer = this->ColorBuffer + P2ConsumeBank; \
    [[maybe_unused]] u32* const DepthBuffer = this->DepthBuffer + P2ConsumeBank; \
    [[maybe_unused]] u32* const AttrBuffer  = this->AttrBuffer  + P2ConsumeBank
#endif

// UNDERCOLO top/under slot test. ON: bit9 (UnderOffset=512) tags the slot -- a cheap AND.
// OFF: the original half-buffer partition (top = [0,BufferSize), under = [BufferSize,..)).
// Both expand to the ORIGINAL expression when the flag is off (byte-identical codegen).
#ifdef LITEV_SOFT3D_UNDERCOLO
    #define PA_IS_TOP(pa)   (!((pa) & UnderOffset))
    #define PA_IS_UNDER(pa) ( ((pa) & UnderOffset))
#else
    #define PA_IS_TOP(pa)   ((pa) < BufferSize)
    #define PA_IS_UNDER(pa) ((pa) >= BufferSize)
#endif

#ifdef LITEV_SOFT3D_FAST
// The active band/thread points this at its own TexCaches[] slot (set in RenderBand
// and the non-banded RenderPolygons path). See GPU3D_Soft.h.
thread_local SoftRenderer3D::TexCacheState* SoftRenderer3D::CurTexCache = nullptr;
// Active-Edge-Table scratch (see GPU3D_Soft.h). One copy per band thread.
thread_local int SoftRenderer3D::AET_Bucket[2048];
thread_local int SoftRenderer3D::AET_Active[2048];
thread_local s32 SoftRenderer3D::AET_BucketStart[193];
#endif


void SoftRenderer3D::StopRenderThread()
{
    if (RenderThreadRunning.load(std::memory_order_relaxed))
    {
        // Tell the render thread to stop drawing new frames, and finish up the current one.
        RenderThreadRunning = false;

        Platform::Semaphore_Post(Sema_RenderStart);

        Platform::Thread_Wait(RenderThread);
        Platform::Thread_Free(RenderThread);
        RenderThread = nullptr;
    }

#ifdef LITEV_SOFT3D_BANDED
    // Tear down the persistent band-worker pool too (self-guards if never created).
    ShutdownBandPool();
#endif
}

void SoftRenderer3D::SetupRenderThread()
{
    if (Threaded)
    {
        if (!RenderThreadRunning.load(std::memory_order_relaxed))
        { // If the render thread isn't already running...
            RenderThreadRunning = true; // "Time for work, render thread!"
            RenderThread = Platform::Thread_Create([this]() {
                RenderThreadFunc();
            });
        }

        // "Be on standby, but don't start rendering until I tell you to!"
        Platform::Semaphore_Reset(Sema_RenderStart);

        // "Oh, sorry, were you already in the middle of a frame from the last iteration?"
        if (RenderThreadRendering)
            // "Tell me when you're done, I'll wait here."
            Platform::Semaphore_Wait(Sema_RenderDone);

        // "All good? Okay, let me give you your training."
        // "(Maybe you're still the same thread, but I have to tell you this stuff anyway.)"

        // "This is the signal you'll send when you're done with a frame."
        // "I'll listen for it when I need to show something to the frontend."
        Platform::Semaphore_Reset(Sema_RenderDone);

        // "This is the signal I'll send when I want you to start rendering."
        // "Don't do anything until you get the message."
        Platform::Semaphore_Reset(Sema_RenderStart);

        // "This is the signal you'll send every time you finish drawing a line."
        // "I might need some of your scanlines before you finish the whole buffer,"
        // "so let me know as soon as you're done with each one."
        Platform::Semaphore_Reset(Sema_ScanlineCount);
    }
    else
    {
        StopRenderThread();
    }
}

void SoftRenderer3D::EnableRenderThread()
{
    if (Threaded && Sema_RenderStart)
    {
        Platform::Semaphore_Post(Sema_RenderStart);
    }
}

SoftRenderer3D::SoftRenderer3D(melonDS::GPU3D& gpu3D, SoftRenderer& parent) noexcept
    : Renderer3D(gpu3D), Parent(parent)
{
    Sema_RenderStart = Platform::Semaphore_Create();
    Sema_RenderDone = Platform::Semaphore_Create();
    Sema_ScanlineCount = Platform::Semaphore_Create();

    RenderThreadRunning = false;
    RenderThreadRendering = false;
    RenderThread = nullptr;
}

SoftRenderer3D::~SoftRenderer3D()
{
    StopRenderThread();

    Platform::Semaphore_Free(Sema_RenderStart);
    Platform::Semaphore_Free(Sema_RenderDone);
    Platform::Semaphore_Free(Sema_ScanlineCount);

#ifdef LITEV_SOFT3D_FAST
    for (int b = 0; b < TexCacheMaxBands; b++)
    {
        delete[] TexCaches[b].Arena;
        TexCaches[b].Arena = nullptr;
    }
#endif
}

void SoftRenderer3D::Reset()
{
    memset(ColorBuffer, 0, PlaneWords * 4);
    memset(DepthBuffer, 0, PlaneWords * 4);
    memset(AttrBuffer, 0, PlaneWords * 4);

    PrevIsShadowMask = false;

    SetupRenderThread();
    EnableRenderThread();
}

void SoftRenderer3D::SetThreaded(bool threaded) noexcept
{
    if (Threaded != threaded)
    {
        Threaded = threaded;
        SetupRenderThread();
        EnableRenderThread();
    }
}

void SoftRenderer3D::TextureLookup(u32 texparam, u32 texpal, s16 s, s16 t, u16* color, u8* alpha) const
{
    // Exact per-pixel path: apply the DS texture wrapping/flip/clamp, then fetch.
    // The texel fetch + palette lookup lives in DecodeTexel (shared with the
    // decode-once texture cache under LITEV_SOFT3D_FAST).

    s32 width = 8 << ((texparam >> 20) & 0x7);
    s32 height = 8 << ((texparam >> 23) & 0x7);

    s32 si = s >> 4;
    s32 ti = t >> 4;

    // texture wrapping
    if (texparam & (1<<16))
    {
        if (texparam & (1<<18))
        {
            if (si & width) si = (width-1) - (si & (width-1));
            else            si = (si & (width-1));
        }
        else
            si &= width-1;
    }
    else
    {
        if (si < 0) si = 0;
        else if (si >= width) si = width-1;
    }

    if (texparam & (1<<17))
    {
        if (texparam & (1<<19))
        {
            if (ti & height) ti = (height-1) - (ti & (height-1));
            else             ti = (ti & (height-1));
        }
        else
            ti &= height-1;
    }
    else
    {
        if (ti < 0) ti = 0;
        else if (ti >= height) ti = height-1;
    }

    DecodeTexel(texparam, texpal, si, ti, color, alpha);
}

void SoftRenderer3D::DecodeTexel(u32 texparam, u32 texpal, s32 s, s32 t, u16* color, u8* alpha) const
{
    // Fetch one already-in-range (s,t) texel: format decode + palette lookup.
    // No wrapping (caller has done it).

    u32 vramaddr = (texparam & 0xFFFF) << 3;

    s32 width = 8 << ((texparam >> 20) & 0x7);

    u8 alpha0;
    if (texparam & (1<<29)) alpha0 = 0;
    else                    alpha0 = 31;

    switch ((texparam >> 26) & 0x7)
    {
    case 1: // A3I5
        {
            vramaddr += ((t * width) + s);
            u8 pixel = GPU.ReadVRAMFlat_Texture<u8>(vramaddr);

            texpal <<= 4;
            *color = GPU.ReadVRAMFlat_TexPal<u16>(texpal + ((pixel&0x1F)<<1));
            *alpha = ((pixel >> 3) & 0x1C) + (pixel >> 6);
        }
        break;

    case 2: // 4-color
        {
            vramaddr += (((t * width) + s) >> 2);
            u8 pixel = GPU.ReadVRAMFlat_Texture<u8>(vramaddr);
            pixel >>= ((s & 0x3) << 1);
            pixel &= 0x3;

            texpal <<= 3;
            *color = GPU.ReadVRAMFlat_TexPal<u16>(texpal + (pixel<<1));
            *alpha = (pixel==0) ? alpha0 : 31;
        }
        break;

    case 3: // 16-color
        {
            vramaddr += (((t * width) + s) >> 1);
            u8 pixel = GPU.ReadVRAMFlat_Texture<u8>(vramaddr);
            if (s & 0x1) pixel >>= 4;
            else         pixel &= 0xF;

            texpal <<= 4;
            *color = GPU.ReadVRAMFlat_TexPal<u16>(texpal + (pixel<<1));
            *alpha = (pixel==0) ? alpha0 : 31;
        }
        break;

    case 4: // 256-color
        {
            vramaddr += ((t * width) + s);
            u8 pixel = GPU.ReadVRAMFlat_Texture<u8>(vramaddr);

            texpal <<= 4;
            *color = GPU.ReadVRAMFlat_TexPal<u16>(texpal + (pixel<<1));
            *alpha = (pixel==0) ? alpha0 : 31;
        }
        break;

    case 5: // compressed
        {
            vramaddr += ((t & 0x3FC) * (width>>2)) + (s & 0x3FC);
            vramaddr += (t & 0x3);
            vramaddr &= 0x7FFFF; // address used for all calcs wraps around after slot 3

            u32 slot1addr = 0x20000 + ((vramaddr & 0x1FFFC) >> 1);
            if (vramaddr >= 0x40000)
                slot1addr += 0x10000;

            u8 val;
            if (vramaddr >= 0x20000 && vramaddr < 0x40000) // reading slot 1 for texels should always read 0
                val = 0;
            else
            {
                val = GPU.ReadVRAMFlat_Texture<u8>(vramaddr);
                val >>= (2 * (s & 0x3));
            }

            u16 palinfo = GPU.ReadVRAMFlat_Texture<u16>(slot1addr);
            u32 paloffset = (palinfo & 0x3FFF) << 2;
            texpal <<= 4;

            switch (val & 0x3)
            {
            case 0:
                *color = GPU.ReadVRAMFlat_TexPal<u16>(texpal + paloffset);
                *alpha = 31;
                break;

            case 1:
                *color = GPU.ReadVRAMFlat_TexPal<u16>(texpal + paloffset + 2);
                *alpha = 31;
                break;

            case 2:
                if ((palinfo >> 14) == 1)
                {
                    u16 color0 = GPU.ReadVRAMFlat_TexPal<u16>(texpal + paloffset);
                    u16 color1 = GPU.ReadVRAMFlat_TexPal<u16>(texpal + paloffset + 2);

                    u32 r0 = color0 & 0x001F;
                    u32 g0 = color0 & 0x03E0;
                    u32 b0 = color0 & 0x7C00;
                    u32 r1 = color1 & 0x001F;
                    u32 g1 = color1 & 0x03E0;
                    u32 b1 = color1 & 0x7C00;

                    u32 r = (r0 + r1) >> 1;
                    u32 g = ((g0 + g1) >> 1) & 0x03E0;
                    u32 b = ((b0 + b1) >> 1) & 0x7C00;

                    *color = r | g | b;
                }
                else if ((palinfo >> 14) == 3)
                {
                    u16 color0 = GPU.ReadVRAMFlat_TexPal<u16>(texpal + paloffset);
                    u16 color1 = GPU.ReadVRAMFlat_TexPal<u16>(texpal + paloffset + 2);

                    u32 r0 = color0 & 0x001F;
                    u32 g0 = color0 & 0x03E0;
                    u32 b0 = color0 & 0x7C00;
                    u32 r1 = color1 & 0x001F;
                    u32 g1 = color1 & 0x03E0;
                    u32 b1 = color1 & 0x7C00;

                    u32 r = (r0*5 + r1*3) >> 3;
                    u32 g = ((g0*5 + g1*3) >> 3) & 0x03E0;
                    u32 b = ((b0*5 + b1*3) >> 3) & 0x7C00;

                    *color = r | g | b;
                }
                else
                    *color = GPU.ReadVRAMFlat_TexPal<u16>(texpal + paloffset + 4);
                *alpha = 31;
                break;

            case 3:
                if ((palinfo >> 14) == 2)
                {
                    *color = GPU.ReadVRAMFlat_TexPal<u16>(texpal + paloffset + 6);
                    *alpha = 31;
                }
                else if ((palinfo >> 14) == 3)
                {
                    u16 color0 = GPU.ReadVRAMFlat_TexPal<u16>(texpal + paloffset);
                    u16 color1 = GPU.ReadVRAMFlat_TexPal<u16>(texpal + paloffset + 2);

                    u32 r0 = color0 & 0x001F;
                    u32 g0 = color0 & 0x03E0;
                    u32 b0 = color0 & 0x7C00;
                    u32 r1 = color1 & 0x001F;
                    u32 g1 = color1 & 0x03E0;
                    u32 b1 = color1 & 0x7C00;

                    u32 r = (r0*3 + r1*5) >> 3;
                    u32 g = ((g0*3 + g1*5) >> 3) & 0x03E0;
                    u32 b = ((b0*3 + b1*5) >> 3) & 0x7C00;

                    *color = r | g | b;
                    *alpha = 31;
                }
                else
                {
                    *color = 0;
                    *alpha = 0;
                }
                break;
            }
        }
        break;

    case 6: // A5I3
        {
            vramaddr += ((t * width) + s);
            u8 pixel = GPU.ReadVRAMFlat_Texture<u8>(vramaddr);

            texpal <<= 4;
            *color = GPU.ReadVRAMFlat_TexPal<u16>(texpal + ((pixel&0x7)<<1));
            *alpha = (pixel >> 3);
        }
        break;

    case 7: // direct color
        {
            vramaddr += (((t * width) + s) << 1);
            *color = GPU.ReadVRAMFlat_Texture<u16>(vramaddr);
            *alpha = (*color & 0x8000) ? 31 : 0;
        }
        break;
    }
}

#ifdef LITEV_SOFT3D_FAST
#ifdef LITEV_SOFT3D_TEX1B
// TEX1B: 1-byte-per-texel palette cache. For palette formats (1/2/3/4/6) the arena stores a
// 1-byte value per texel (index for 2/3/4; the raw byte for A3I5/A5I3) plus a small decoded
// u16 palette; the sample does packed = palette[arena1b[texAddr]], BIT-IDENTICAL to today's
// 2B value. Direct (7) / 4x4 (5) stay 2B exactly as the non-TEX1B path. Returns the palette
// (palette fmts) or the u16 arena base (direct); *out1b = the 1-byte index base or null.
const u16* SoftRenderer3D::ResolveTexCache(u32 texparam, u32 texpal, s32* outW, s32* outH, const u8** out1b)
{
    s32 W = 8 << ((texparam >> 20) & 0x7);
    s32 H = 8 << ((texparam >> 23) & 0x7);
    *outW = W;
    *outH = H;
    *out1b = nullptr;

    TexCacheState* tc = CurTexCache;
    if (!tc) return nullptr;

    // Hit?
    for (u32 i = 0; i < tc->Count; i++)
    {
        TexCacheEntry& e = tc->Entries[i];
        if (e.Param == texparam && e.Pal == texpal)
        {
            if (e.ElemSize == 1)
            {
                *out1b = tc->Arena + e.Offset;
                return (const u16*)(tc->Arena + e.PalOffset);   // f_texcache := decoded palette
            }
            return (const u16*)(tc->Arena + e.Offset);           // 2B direct/4x4
        }
    }

    const u32 fmt = (texparam >> 26) & 0x7;
    const bool isPalette = (fmt==1 || fmt==2 || fmt==3 || fmt==4 || fmt==6);

    const u32 nTexels = (u32)W * (u32)H;
    const u32 nPalEntries = isPalette ? ((fmt==2) ? 4u : (fmt==3) ? 16u : 256u) : 0u;
    // palette fmt: nTexels 1-byte indices + nPalEntries*2 palette bytes; direct: nTexels*2.
    const u32 needBytes = isPalette ? (nTexels + nPalEntries*2) : (nTexels*2);
    if (needBytes > TexCacheArenaBytes)
        return nullptr;

    if (!tc->Arena)
        tc->Arena = new u8[TexCacheArenaBytes];

    if (tc->Used + needBytes > TexCacheArenaBytes || tc->Count >= TexCacheSlots)
    {
        // Arena/slot table full this frame: reset + re-fill (safe -- the returned pointer is
        // used for the whole current polygon before the next ResolveTexCache).
        tc->Used = 0;
        tc->Count = 0;
    }

    // W,H are >=8 powers of two so nTexels and nPalEntries*2 are even; Used starts 0 and only
    // ever grows by even amounts, so the palette / u16 base stays 2-byte aligned.
    const u32 off = tc->Used;
    TexCacheEntry& e = tc->Entries[tc->Count++];
    e.Param = texparam; e.Pal = texpal; e.Offset = off; e.W = W; e.H = H;

    if (isPalette)
    {
        u8*  idx    = tc->Arena + off;
        const u32 paloff = off + nTexels;
        u16* pal    = (u16*)(tc->Arena + paloff);
        e.PalOffset = paloff;
        e.ElemSize  = 1;

        // Build the decoded palette so pal[v] == the exact u16 the current DecodeTexel+pack
        // produces for a texel whose stored 1-byte value is v (matches cases 1/2/3/4/6 and
        // the current 0/31 alpha rounding for A3I5/A5I3 -- deliberately NOT more accurate).
        const u8 alpha0 = (texparam & (1<<29)) ? 0 : 31;
        if (fmt == 2)
        {
            const u32 tp = texpal << 3;
            for (u32 v = 0; v < 4; v++)
            {
                u16 c = GPU.ReadVRAMFlat_TexPal<u16>(tp + (v<<1));
                u8  a = (v==0) ? alpha0 : 31;
                pal[v] = (u16)((c & 0x7FFF) | (a >= 16 ? 0x8000 : 0));
            }
        }
        else if (fmt == 3)
        {
            const u32 tp = texpal << 4;
            for (u32 v = 0; v < 16; v++)
            {
                u16 c = GPU.ReadVRAMFlat_TexPal<u16>(tp + (v<<1));
                u8  a = (v==0) ? alpha0 : 31;
                pal[v] = (u16)((c & 0x7FFF) | (a >= 16 ? 0x8000 : 0));
            }
        }
        else if (fmt == 4)
        {
            const u32 tp = texpal << 4;
            for (u32 v = 0; v < 256; v++)
            {
                u16 c = GPU.ReadVRAMFlat_TexPal<u16>(tp + (v<<1));
                u8  a = (v==0) ? alpha0 : 31;
                pal[v] = (u16)((c & 0x7FFF) | (a >= 16 ? 0x8000 : 0));
            }
        }
        else if (fmt == 1) // A3I5: 1-byte = raw texel byte (index+alpha)
        {
            const u32 tp = texpal << 4;
            for (u32 v = 0; v < 256; v++)
            {
                u16 c = GPU.ReadVRAMFlat_TexPal<u16>(tp + ((v & 0x1F)<<1));
                u8  a = ((v >> 3) & 0x1C) + (v >> 6);
                pal[v] = (u16)((c & 0x7FFF) | (a >= 16 ? 0x8000 : 0));
            }
        }
        else // fmt == 6, A5I3
        {
            const u32 tp = texpal << 4;
            for (u32 v = 0; v < 256; v++)
            {
                u16 c = GPU.ReadVRAMFlat_TexPal<u16>(tp + ((v & 0x7)<<1));
                u8  a = (v >> 3);
                pal[v] = (u16)((c & 0x7FFF) | (a >= 16 ? 0x8000 : 0));
            }
        }

        // Fill the 1-byte index arena from texture VRAM (mirrors DecodeTexel's index extract).
        const u32 base = (texparam & 0xFFFF) << 3;
        for (s32 tt = 0; tt < H; tt++)
        {
            u8* row = idx + (u32)tt * (u32)W;
            for (s32 ss = 0; ss < W; ss++)
            {
                u8 v;
                if (fmt == 3)
                {
                    u8 pixel = GPU.ReadVRAMFlat_Texture<u8>(base + (((u32)tt*(u32)W + ss) >> 1));
                    if (ss & 0x1) pixel >>= 4; else pixel &= 0xF;
                    v = pixel;
                }
                else if (fmt == 2)
                {
                    u8 pixel = GPU.ReadVRAMFlat_Texture<u8>(base + (((u32)tt*(u32)W + ss) >> 2));
                    pixel >>= ((ss & 0x3) << 1);
                    v = pixel & 0x3;
                }
                else // 4 / 1 / 6: one byte per texel
                {
                    v = GPU.ReadVRAMFlat_Texture<u8>(base + ((u32)tt*(u32)W + ss));
                }
                row[ss] = v;
            }
        }

        tc->Used += needBytes;
        *out1b = idx;
        return pal;
    }
    else
    {
        // Direct (7) / 4x4 (5): 2-byte u16 packed, identical to the non-TEX1B path.
        u16* dst = (u16*)(tc->Arena + off);
        e.PalOffset = 0;
        e.ElemSize  = 2;
        for (s32 tt = 0; tt < H; tt++)
        {
            u16* rowd = dst + (u32)tt * (u32)W;
            for (s32 ss = 0; ss < W; ss++)
            {
                u16 c; u8 a;
                DecodeTexel(texparam, texpal, ss, tt, &c, &a);
                rowd[ss] = (u16)((c & 0x7FFF) | (a >= 16 ? 0x8000 : 0));
            }
        }
        tc->Used += needBytes;
        return dst;
    }
}
#else
const u16* SoftRenderer3D::ResolveTexCache(u32 texparam, u32 texpal, s32* outW, s32* outH)
{
    s32 W = 8 << ((texparam >> 20) & 0x7);
    s32 H = 8 << ((texparam >> 23) & 0x7);
    *outW = W;
    *outH = H;

    TexCacheState* tc = CurTexCache;
    if (!tc) return nullptr; // no cache bound on this thread: fall back to per-pixel

    // Look for an already-decoded entry for this exact texture.
    for (u32 i = 0; i < tc->Count; i++)
    {
        TexCacheEntry& e = tc->Entries[i];
        if (e.Param == texparam && e.Pal == texpal)
            return tc->Arena + e.Offset;
    }

    // Miss: decode WxH texels once into the arena.
    u32 need = (u32)W * (u32)H;
    if (need > TexCacheArenaTexels)
        return nullptr; // pathologically large; fall back to per-pixel path

    if (!tc->Arena)
        tc->Arena = new u16[TexCacheArenaTexels];

    if (tc->Used + need > TexCacheArenaTexels || tc->Count >= TexCacheSlots)
    {
        // Arena / slot table exhausted this frame: reset and re-fill (rare; the
        // working set of one frame normally fits). Safe because we immediately use
        // the pointer we return before resolving the next texture.
        tc->Used = 0;
        tc->Count = 0;
    }

    u32 off = tc->Used;
    u16* dst = tc->Arena + off;
    for (s32 tt = 0; tt < H; tt++)
    {
        u16* row = dst + (u32)tt * (u32)W;
        for (s32 ss = 0; ss < W; ss++)
        {
            u16 c; u8 a;
            DecodeTexel(texparam, texpal, ss, tt, &c, &a);
            // RGBA5551: RGB555 in bits 0-14, opaque flag in bit 15. Exact for
            // binary-alpha formats; graded alpha (A3I5/A5I3) rounds to 0/31.
            row[ss] = (u16)((c & 0x7FFF) | (a >= 16 ? 0x8000 : 0));
        }
    }

    tc->Used += need;
    TexCacheEntry& e = tc->Entries[tc->Count++];
    e.Param = texparam;
    e.Pal = texpal;
    e.Offset = off;
    e.W = W;
    e.H = H;
    return dst;
}
#endif
#endif

// depth test is 'less or equal' instead of 'less than' under the following conditions:
// * when drawing a front-facing pixel over an opaque back-facing pixel
// * when drawing wireframe edges, under certain conditions (TODO)
//
// range is different based on depth-buffering mode
// Z-buffering: +-0x200
// W-buffering: +-0xFF

bool DepthTest_Equal_Z(s32 dstz, s32 z, u32 dstattr)
{
    s32 diff = dstz - z;
    if ((u32)(diff + 0x200) <= 0x400)
        return true;

    return false;
}

bool DepthTest_Equal_W(s32 dstz, s32 z, u32 dstattr)
{
    s32 diff = dstz - z;
    if ((u32)(diff + 0xFF) <= 0x1FE)
        return true;

    return false;
}

bool DepthTest_LessThan(s32 dstz, s32 z, u32 dstattr)
{
    if (z < dstz)
        return true;

    return false;
}

bool DepthTest_LessThan_FrontFacing(s32 dstz, s32 z, u32 dstattr)
{
    if ((dstattr & 0x00400010) == 0x00000010) // opaque, back facing
    {
        if (z <= dstz)
            return true;
    }
    else
    {
        if (z < dstz)
            return true;
    }

    return false;
}

#ifdef LITEV_SOFT3D_HANDNEON
// DraStic-faithful energy lever: DraStic's raster (FUN_0015fb8c / the 0x8dxxx
// plot kernels) inlines the depth compare as a branchless masked op (& 0x7fff
// depth, direct compare) INSIDE the fill; melonDS routes every pixel through a
// per-polygon FUNCTION POINTER (fnDepthTest) — an indirect call + register
// save/restore on EVERY pixel, thousands per frame. That call is pure dynamic-
// instruction energy (the heat that drives the RG DS throttle). This inlines
// all four compare modes; the mode is a per-polygon invariant so the switch is
// a perfectly-predicted branch that stays in registers with no call/return.
// Bit-identical to the four DepthTest_* functions above.
//   mode 0 = LessThan   1 = LessThan_FrontFacing   2 = Equal_Z   3 = Equal_W
static inline __attribute__((always_inline))
bool DepthTestInline(int mode, s32 dstz, s32 z, u32 dstattr)
{
    switch (mode)
    {
    case 0:
        return z < dstz;
    case 1:
        if ((dstattr & 0x00400010) == 0x00000010)
            return z <= dstz;
        return z < dstz;
    case 2:
        return (u32)((dstz - z) + 0x200) <= 0x400;
    default: // 3
        return (u32)((dstz - z) + 0xFF) <= 0x1FE;
    }
}

#ifdef LITEV_SOFT3D_DTEST_BRANCHLESS
// Fully BRANCHLESS twin of DepthTestInline: returns 1 if the pixel PASSES the depth test,
// 0 if it fails, with NO data-dependent branch (every mode is a compare/arithmetic
// expression, incl. mode 1). The NEON-batch survivor compaction uses `nb += pass` instead
// of `if(!DTEST) continue`, replacing the #1 mispredicted per-pixel branch on the in-order
// A55 with a predicated commit. Bit-identical to DepthTestInline for all four modes.
//   mode 0 = LessThan   1 = LessThan_FrontFacing   2 = Equal_Z   3 = Equal_W
static inline __attribute__((always_inline))
u32 DepthTestMask(int mode, s32 dstz, s32 z, u32 dstattr)
{
    switch (mode)
    {
    case 0:
        return (u32)(z < dstz);
    case 1:
        // opaque back-facing => z<=dstz, else z<dstz. z<=dstz == (z<dstz)|(z==dstz):
        return (u32)( (z < dstz) | (((dstattr & 0x00400010) == 0x00000010) & (z == dstz)) );
    case 2:
        return (u32)( (u32)((dstz - z) + 0x200) <= 0x400 );
    default: // 3
        return (u32)( (u32)((dstz - z) + 0xFF) <= 0x1FE );
    }
}
#endif

#define DTEST(dz, zz, da) DepthTestInline(f_dtmode, (dz), (zz), (da))
#else
#define DTEST(dz, zz, da) fnDepthTest((dz), (zz), (da))
#endif

u32 SoftRenderer3D::AlphaBlend(u32 srccolor, u32 dstcolor, u32 alpha) const noexcept
{
    u32 dstalpha = dstcolor >> 24;

    if (dstalpha == 0)
        return srccolor;

    u32 srcR = srccolor & 0x3F;
    u32 srcG = (srccolor >> 8) & 0x3F;
    u32 srcB = (srccolor >> 16) & 0x3F;

    if (GPU3D.RenderDispCnt & (1<<3))
    {
        u32 dstR = dstcolor & 0x3F;
        u32 dstG = (dstcolor >> 8) & 0x3F;
        u32 dstB = (dstcolor >> 16) & 0x3F;

        alpha++;
        srcR = ((srcR * alpha) + (dstR * (32-alpha))) >> 5;
        srcG = ((srcG * alpha) + (dstG * (32-alpha))) >> 5;
        srcB = ((srcB * alpha) + (dstB * (32-alpha))) >> 5;
        alpha--;
    }

    if (alpha > dstalpha)
        dstalpha = alpha;

    return srcR | (srcG << 8) | (srcB << 16) | (dstalpha << 24);
}

u32 SoftRenderer3D::RenderPixel(const Polygon* polygon, u8 vr, u8 vg, u8 vb, s16 s, s16 t) const
{
    u8 r, g, b, a;

    u32 blendmode = (polygon->Attr >> 4) & 0x3;
    u32 polyalpha = (polygon->Attr >> 16) & 0x1F;
    bool wireframe = (polyalpha == 0);

    if (blendmode == 2)
    {
        if (GPU3D.RenderDispCnt & (1<<1))
        {
            // highlight mode: color is calculated normally
            // except all vertex color components are set
            // to the red component
            // the toon color is added to the final color

            vg = vr;
            vb = vr;
        }
        else
        {
            // toon mode: vertex color is replaced by toon color

            u16 tooncolor = GPU3D.RenderToonTable[vr >> 1];

            vr = (tooncolor << 1) & 0x3E; if (vr) vr++;
            vg = (tooncolor >> 4) & 0x3E; if (vg) vg++;
            vb = (tooncolor >> 9) & 0x3E; if (vb) vb++;
        }
    }

    if ((GPU3D.RenderDispCnt & (1<<0)) && (((polygon->TexParam >> 26) & 0x7) != 0))
    {
        u8 tr, tg, tb;

        u16 tcolor; u8 talpha;
        TextureLookup(polygon->TexParam, polygon->TexPalette, s, t, &tcolor, &talpha);

        tr = (tcolor << 1) & 0x3E; if (tr) tr++;
        tg = (tcolor >> 4) & 0x3E; if (tg) tg++;
        tb = (tcolor >> 9) & 0x3E; if (tb) tb++;

        if (blendmode & 0x1)
        {
            // decal

            if (talpha == 0)
            {
                r = vr;
                g = vg;
                b = vb;
            }
            else if (talpha == 31)
            {
                r = tr;
                g = tg;
                b = tb;
            }
            else
            {
                r = ((tr * talpha) + (vr * (31-talpha))) >> 5;
                g = ((tg * talpha) + (vg * (31-talpha))) >> 5;
                b = ((tb * talpha) + (vb * (31-talpha))) >> 5;
            }
            a = polyalpha;
        }
        else
        {
            // modulate

            r = ((tr+1) * (vr+1) - 1) >> 6;
            g = ((tg+1) * (vg+1) - 1) >> 6;
            b = ((tb+1) * (vb+1) - 1) >> 6;
            a = ((talpha+1) * (polyalpha+1) - 1) >> 5;
        }
    }
    else
    {
        r = vr;
        g = vg;
        b = vb;
        a = polyalpha;
    }

    if ((blendmode == 2) && (GPU3D.RenderDispCnt & (1<<1)))
    {
        u16 tooncolor = GPU3D.RenderToonTable[vr >> 1];

        vr = (tooncolor << 1) & 0x3E; if (vr) vr++;
        vg = (tooncolor >> 4) & 0x3E; if (vg) vg++;
        vb = (tooncolor >> 9) & 0x3E; if (vb) vb++;

        r += vr;
        g += vg;
        b += vb;

        if (r > 63) r = 63;
        if (g > 63) g = 63;
        if (b > 63) b = 63;
    }

    // checkme: can wireframe polygons use texture alpha?
    if (wireframe) a = 31;

    return r | (g << 8) | (b << 16) | (a << 24);
}

void SoftRenderer3D::PlotTranslucentPixel(u32 pixeladdr, u32 color, u32 z, u32 polyattr, u32 shadow)
{
#if defined(LITEV_SOFT3D_BANDTILE)
    BT_SHADOW_FB_PLOT();
#elif defined(LITEV_SOFT3D_PIPELINE2)
    P2_SHADOW_RENDER();
#endif
    u32 dstattr = AttrBuffer[pixeladdr];
    u32 attr = (polyattr & 0xE0F0) | ((polyattr >> 8) & 0xFF0000) | (1<<22) | (dstattr & 0xFF001F0F);

    if (shadow)
    {
        // for shadows, opaque pixels are also checked
        if (dstattr & (1<<22))
        {
            if ((dstattr & 0x007F0000) == (attr & 0x007F0000))
                return;
        }
        else
        {
            if ((dstattr & 0x3F000000) == (polyattr & 0x3F000000))
                return;
        }
    }
    else
    {
        // skip if translucent polygon IDs are equal
        if ((dstattr & 0x007F0000) == (attr & 0x007F0000))
            return;
    }

    // fog flag
    if (!(dstattr & (1<<15)))
        attr &= ~(1<<15);

    color = AlphaBlend(color, ColorBuffer[pixeladdr], color>>24);

    if (z != -1)
        DepthBuffer[pixeladdr] = z;

    ColorBuffer[pixeladdr] = color;
    AttrBuffer[pixeladdr] = attr;
}

// ---- Compact-vertex read retarget (LITEV_SOFT3D_COMPACTVTX) --------------------------
// Every scattered raster vertex read goes through one of these accessors. When the flag
// is OFF each macro expands to the ORIGINAL expression verbatim (byte-identical machine
// code); when ON it reads the contiguous compact buffer instead.
//   * P*I(rp,poly,i): addressed by polygon-relative vertex index i.
//   * V*(rp,vp,i):    addressed by the existing Vertex* `vp` when OFF, or the compact
//                     index i when ON (used where the code already holds vlcur/vlnext/... ).
#ifdef LITEV_SOFT3D_COMPACTVTX
    #define PXI(rp,poly,i) ((rp)->CompactV[i].X)
    #define PYI(rp,poly,i) ((rp)->CompactV[i].Y)
    #define PWI(rp,poly,i) ((rp)->CompactV[i].W)
    #define PZI(rp,poly,i) ((rp)->CompactV[i].Z)
    #define VC0(rp,vp,i)   ((rp)->CompactV[i].R)
    #define VC1(rp,vp,i)   ((rp)->CompactV[i].G)
    #define VC2(rp,vp,i)   ((rp)->CompactV[i].B)
    #define VT0(rp,vp,i)   ((rp)->CompactV[i].S)
    #define VT1(rp,vp,i)   ((rp)->CompactV[i].T)
    #define VPX(rp,vp,i)   ((rp)->CompactV[i].X)
#else
    #define PXI(rp,poly,i) ((poly)->Vertices[i]->FinalPosition[0])
    #define PYI(rp,poly,i) ((poly)->Vertices[i]->FinalPosition[1])
    #define PWI(rp,poly,i) ((poly)->FinalW[i])
    #define PZI(rp,poly,i) ((poly)->FinalZ[i])
    #define VC0(rp,vp,i)   ((vp)->FinalColor[0])
    #define VC1(rp,vp,i)   ((vp)->FinalColor[1])
    #define VC2(rp,vp,i)   ((vp)->FinalColor[2])
    #define VT0(rp,vp,i)   ((vp)->TexCoords[0])
    #define VT1(rp,vp,i)   ((vp)->TexCoords[1])
    #define VPX(rp,vp,i)   ((vp)->FinalPosition[0])
#endif

// ---- Edge-endpoint hoist read retarget (LITEV_SOFT3D_EDGEHOIST) ----------------------
// The physical-left/right per-scanline endpoint reads (crossing-check Next Y, W, Z).
// ON: read the hot per-edge snapshot rp->EHL/EHR. OFF: the original compact/vertex read.
#ifdef LITEV_SOFT3D_EDGEHOIST
    #define EHY_L(rp,poly)  ((rp)->EHL.Y1)
    #define EHY_R(rp,poly)  ((rp)->EHR.Y1)
    #define EHWL0(rp,poly)  ((rp)->EHL.W0)
    #define EHWL1(rp,poly)  ((rp)->EHL.W1)
    #define EHWR0(rp,poly)  ((rp)->EHR.W0)
    #define EHWR1(rp,poly)  ((rp)->EHR.W1)
    #define EHZL0(rp,poly)  ((rp)->EHL.Z0)
    #define EHZL1(rp,poly)  ((rp)->EHL.Z1)
    #define EHZR0(rp,poly)  ((rp)->EHR.Z0)
    #define EHZR1(rp,poly)  ((rp)->EHR.Z1)
#else
    #define EHY_L(rp,poly)  PYI(rp,poly,(rp)->NextVL)
    #define EHY_R(rp,poly)  PYI(rp,poly,(rp)->NextVR)
    #define EHWL0(rp,poly)  PWI(rp,poly,(rp)->CurVL)
    #define EHWL1(rp,poly)  PWI(rp,poly,(rp)->NextVL)
    #define EHWR0(rp,poly)  PWI(rp,poly,(rp)->CurVR)
    #define EHWR1(rp,poly)  PWI(rp,poly,(rp)->NextVR)
    #define EHZL0(rp,poly)  PZI(rp,poly,(rp)->CurVL)
    #define EHZL1(rp,poly)  PZI(rp,poly,(rp)->NextVL)
    #define EHZR0(rp,poly)  PZI(rp,poly,(rp)->CurVR)
    #define EHZR1(rp,poly)  PZI(rp,poly,(rp)->NextVR)
#endif
// filledge Next-X inequality (evaluated only on the bottom scanline of X-major edges).
// ON: from the span-start/end snapshots (ehS/ehE); OFF: the original NextVL.X != NextVR.X.
// Object-like: captures the in-scope ehS/ehE (ON) or rp/polygon via PXI (OFF).
// Byte-exact: != is symmetric, so ehS/ehE order is irrelevant; OFF via PXI reproduces the
// pristine (vlnext->FinalPosition[0] != vrnext->FinalPosition[0]) under either COMPACTVTX state.
#ifdef LITEV_SOFT3D_EDGEHOIST
    #define EH_FXNE  (ehS->X1 != ehE->X1)
#else
    #define EH_FXNE  (PXI(rp,polygon,rp->NextVL) != PXI(rp,polygon,rp->NextVR))
#endif

#ifdef LITEV_SOFT3D_COMPACTVTX
// Build the per-frame compact vertex buffer over the sorted render poly list. Reads each
// polygon's fat vertices ONCE here (the scattered cold-touch, paid a single time and
// landed warm in the shared L2), writing the raster-needed fields into a tightly packed,
// contiguous, direct-indexed block per polygon. Called on the render/coordinator thread
// (or the synchronous path) BEFORE the band workers wake, so the built arena is published
// to them by the BandStartSema release/acquire.
void SoftRenderer3D::BuildCompactVtx(Polygon** polygons, int npolys)
{
    if (!CompactArena)
        CompactArena = std::make_unique<CompactVtx[]>((size_t)2048 * 10);

    u32 used = 0;
    for (int i = 0; i < npolys; i++)
    {
        Polygon* poly = polygons[i];
        CompactBase[i] = used;
        // Degenerate polys are skipped by every band (never read their CompactV); keep the
        // base valid but don't chase their (possibly stale) vertices.
        if (poly->Degenerate) continue;

        u32 nv = poly->NumVertices;
        CompactVtx* dst = &CompactArena[used];
        for (u32 k = 0; k < nv; k++)
        {
            const Vertex* v = poly->Vertices[k];
            dst[k].X = v->FinalPosition[0];
            dst[k].Y = v->FinalPosition[1];
            dst[k].W = poly->FinalW[k];
            dst[k].Z = poly->FinalZ[k];
            dst[k].R = v->FinalColor[0];
            dst[k].G = v->FinalColor[1];
            dst[k].B = v->FinalColor[2];
            dst[k].S = v->TexCoords[0];
            dst[k].T = v->TexCoords[1];
        }
        used += nv;
    }
}
#endif

#if defined(LITEV_SOFT3D_GRADIENT) || defined(LITEV_SOFT3D_EDGENEON)
void SoftRenderer3D::SnapshotEdgeL(SoftRenderer3D::RendererPolygon* rp) const
{
    Polygon* polygon = rp->PolyData;
    Vertex* vc = polygon->Vertices[rp->CurVL];
    Vertex* vn = polygon->Vertices[rp->NextVL];
    // 0=W, 1=R, 2=G, 3=B, 4=S, 5=T (texcoords sign-extend s16->s32 as the scalar path did)
    rp->EdgeL.v0[0] = PWI(rp,polygon,rp->CurVL); rp->EdgeL.v1[0] = PWI(rp,polygon,rp->NextVL);
    rp->EdgeL.v0[1] = VC0(rp,vc,rp->CurVL); rp->EdgeL.v1[1] = VC0(rp,vn,rp->NextVL);
    rp->EdgeL.v0[2] = VC1(rp,vc,rp->CurVL); rp->EdgeL.v1[2] = VC1(rp,vn,rp->NextVL);
    rp->EdgeL.v0[3] = VC2(rp,vc,rp->CurVL); rp->EdgeL.v1[3] = VC2(rp,vn,rp->NextVL);
    rp->EdgeL.v0[4] = VT0(rp,vc,rp->CurVL); rp->EdgeL.v1[4] = VT0(rp,vn,rp->NextVL);
    rp->EdgeL.v0[5] = VT1(rp,vc,rp->CurVL); rp->EdgeL.v1[5] = VT1(rp,vn,rp->NextVL);
    rp->EdgeL.z0 = PZI(rp,polygon,rp->CurVL); rp->EdgeL.z1 = PZI(rp,polygon,rp->NextVL);
#ifdef LITEV_SOFT3D_GRADIENT
    rp->EdgeL.rem = 0;                            // force DDA re-anchor at this crossing
    rp->EdgeL.segYBot = PYI(rp,polygon,rp->NextVL); // segment bottom (Next vertex Y)
#endif
#ifdef LITEV_SOFT3D_COMPACTVTX
    (void)vc; (void)vn;
#endif
}

void SoftRenderer3D::SnapshotEdgeR(SoftRenderer3D::RendererPolygon* rp) const
{
    Polygon* polygon = rp->PolyData;
    Vertex* vc = polygon->Vertices[rp->CurVR];
    Vertex* vn = polygon->Vertices[rp->NextVR];
    rp->EdgeR.v0[0] = PWI(rp,polygon,rp->CurVR); rp->EdgeR.v1[0] = PWI(rp,polygon,rp->NextVR);
    rp->EdgeR.v0[1] = VC0(rp,vc,rp->CurVR); rp->EdgeR.v1[1] = VC0(rp,vn,rp->NextVR);
    rp->EdgeR.v0[2] = VC1(rp,vc,rp->CurVR); rp->EdgeR.v1[2] = VC1(rp,vn,rp->NextVR);
    rp->EdgeR.v0[3] = VC2(rp,vc,rp->CurVR); rp->EdgeR.v1[3] = VC2(rp,vn,rp->NextVR);
    rp->EdgeR.v0[4] = VT0(rp,vc,rp->CurVR); rp->EdgeR.v1[4] = VT0(rp,vn,rp->NextVR);
    rp->EdgeR.v0[5] = VT1(rp,vc,rp->CurVR); rp->EdgeR.v1[5] = VT1(rp,vn,rp->NextVR);
    rp->EdgeR.z0 = PZI(rp,polygon,rp->CurVR); rp->EdgeR.z1 = PZI(rp,polygon,rp->NextVR);
#ifdef LITEV_SOFT3D_GRADIENT
    rp->EdgeR.rem = 0;
    rp->EdgeR.segYBot = PYI(rp,polygon,rp->NextVR);
#endif
#ifdef LITEV_SOFT3D_COMPACTVTX
    (void)vc; (void)vn;
#endif
}
#endif

#ifdef LITEV_SOFT3D_EDGEHOIST
// Snapshot the current left-edge endpoints (CurVL/NextVL) from the compact arena into the
// hot rp->EHL. Verbatim copies -> byte-identical to reading the compact fields live.
void SoftRenderer3D::SnapshotEdgeHoistL(SoftRenderer3D::RendererPolygon* rp) const
{
    const CompactVtx* c = rp->CompactV;
    const CompactVtx& vc = c[rp->CurVL];
    const CompactVtx& vn = c[rp->NextVL];
    rp->EHL.Y1 = vn.Y; rp->EHL.X1 = vn.X;
    rp->EHL.W0 = vc.W; rp->EHL.W1 = vn.W;
    rp->EHL.Z0 = vc.Z; rp->EHL.Z1 = vn.Z;
    rp->EHL.R0 = vc.R; rp->EHL.R1 = vn.R;
    rp->EHL.G0 = vc.G; rp->EHL.G1 = vn.G;
    rp->EHL.B0 = vc.B; rp->EHL.B1 = vn.B;
    rp->EHL.S0 = vc.S; rp->EHL.S1 = vn.S;
    rp->EHL.T0 = vc.T; rp->EHL.T1 = vn.T;
}

void SoftRenderer3D::SnapshotEdgeHoistR(SoftRenderer3D::RendererPolygon* rp) const
{
    const CompactVtx* c = rp->CompactV;
    const CompactVtx& vc = c[rp->CurVR];
    const CompactVtx& vn = c[rp->NextVR];
    rp->EHR.Y1 = vn.Y; rp->EHR.X1 = vn.X;
    rp->EHR.W0 = vc.W; rp->EHR.W1 = vn.W;
    rp->EHR.Z0 = vc.Z; rp->EHR.Z1 = vn.Z;
    rp->EHR.R0 = vc.R; rp->EHR.R1 = vn.R;
    rp->EHR.G0 = vc.G; rp->EHR.G1 = vn.G;
    rp->EHR.B0 = vc.B; rp->EHR.B1 = vn.B;
    rp->EHR.S0 = vc.S; rp->EHR.S1 = vn.S;
    rp->EHR.T0 = vc.T; rp->EHR.T1 = vn.T;
}
#endif

void SoftRenderer3D::SetupPolygonLeftEdge(SoftRenderer3D::RendererPolygon* rp, s32 y) const
{
    Polygon* polygon = rp->PolyData;

    while (y >= PYI(rp,polygon,rp->NextVL) && rp->CurVL != polygon->VBottom)
    {
        rp->CurVL = rp->NextVL;

        if (polygon->FacingView)
        {
            rp->NextVL = rp->CurVL + 1;
            if (rp->NextVL >= polygon->NumVertices)
                rp->NextVL = 0;
        }
        else
        {
            rp->NextVL = rp->CurVL - 1;
            if ((s32)rp->NextVL < 0)
                rp->NextVL = polygon->NumVertices - 1;
        }
    }

    rp->XL = rp->SlopeL.Setup(PXI(rp,polygon,rp->CurVL), PXI(rp,polygon,rp->NextVL),
                              PYI(rp,polygon,rp->CurVL), PYI(rp,polygon,rp->NextVL),
                              PWI(rp,polygon,rp->CurVL), PWI(rp,polygon,rp->NextVL), y, polygon->WBuffer);
#if defined(LITEV_SOFT3D_GRADIENT) || defined(LITEV_SOFT3D_EDGENEON)
    SnapshotEdgeL(rp);
#endif
#ifdef LITEV_SOFT3D_EDGEHOIST
    SnapshotEdgeHoistL(rp);
#endif
}

void SoftRenderer3D::SetupPolygonRightEdge(SoftRenderer3D::RendererPolygon* rp, s32 y) const
{
    Polygon* polygon = rp->PolyData;

    while (y >= PYI(rp,polygon,rp->NextVR) && rp->CurVR != polygon->VBottom)
    {
        rp->CurVR = rp->NextVR;

        if (polygon->FacingView)
        {
            rp->NextVR = rp->CurVR - 1;
            if ((s32)rp->NextVR < 0)
                rp->NextVR = polygon->NumVertices - 1;
        }
        else
        {
            rp->NextVR = rp->CurVR + 1;
            if (rp->NextVR >= polygon->NumVertices)
                rp->NextVR = 0;
        }
    }

    rp->XR = rp->SlopeR.Setup(PXI(rp,polygon,rp->CurVR), PXI(rp,polygon,rp->NextVR),
                              PYI(rp,polygon,rp->CurVR), PYI(rp,polygon,rp->NextVR),
                              PWI(rp,polygon,rp->CurVR), PWI(rp,polygon,rp->NextVR), y, polygon->WBuffer);
#if defined(LITEV_SOFT3D_GRADIENT) || defined(LITEV_SOFT3D_EDGENEON)
    SnapshotEdgeR(rp);
#endif
#ifdef LITEV_SOFT3D_EDGEHOIST
    SnapshotEdgeHoistR(rp);
#endif
}

void SoftRenderer3D::SetupPolygon(SoftRenderer3D::RendererPolygon* rp, Polygon* polygon) const
{
    u32 nverts = polygon->NumVertices;

    u32 vtop = polygon->VTop, vbot = polygon->VBottom;
    s32 ytop = polygon->YTop, ybot = polygon->YBottom;

    rp->PolyData = polygon;

    rp->CurVL = vtop;
    rp->CurVR = vtop;

    if (polygon->FacingView)
    {
        rp->NextVL = rp->CurVL + 1;
        if (rp->NextVL >= nverts) rp->NextVL = 0;
        rp->NextVR = rp->CurVR - 1;
        if ((s32)rp->NextVR < 0) rp->NextVR = nverts - 1;
    }
    else
    {
        rp->NextVL = rp->CurVL - 1;
        if ((s32)rp->NextVL < 0) rp->NextVL = nverts - 1;
        rp->NextVR = rp->CurVR + 1;
        if (rp->NextVR >= nverts) rp->NextVR = 0;
    }

    if (ybot == ytop)
    {
        vtop = 0; vbot = 0;
        int i;

        i = 1;
        if (PXI(rp,polygon,i) < PXI(rp,polygon,vtop)) vtop = i;
        if (PXI(rp,polygon,i) > PXI(rp,polygon,vbot)) vbot = i;

        i = nverts - 1;
        if (PXI(rp,polygon,i) < PXI(rp,polygon,vtop)) vtop = i;
        if (PXI(rp,polygon,i) > PXI(rp,polygon,vbot)) vbot = i;

        rp->CurVL = vtop; rp->NextVL = vtop;
        rp->CurVR = vbot; rp->NextVR = vbot;

        rp->XL = rp->SlopeL.SetupDummy(PXI(rp,polygon,rp->CurVL), polygon->WBuffer);
        rp->XR = rp->SlopeR.SetupDummy(PXI(rp,polygon,rp->CurVR), polygon->WBuffer);
#if defined(LITEV_SOFT3D_GRADIENT) || defined(LITEV_SOFT3D_EDGENEON)
        // Flat poly (YTop==YBottom): edges are set up via SetupDummy here (not via
        // SetupPolygon*Edge), so snapshot both edges directly. Cur==Next for each edge,
        // so the endpoints are degenerate -- Interpolate returns the Cur value (xdiff==0),
        // identical to reading polygon->FinalW[..] / Vertices[..] as the OFF path does.
        SnapshotEdgeL(rp);
        SnapshotEdgeR(rp);
#endif
#ifdef LITEV_SOFT3D_EDGEHOIST
        // Flat poly: Cur==Next per edge, so the hoisted endpoints are degenerate (X1/Y1 ==
        // Cur, W0==W1 etc.) -- identical to the OFF path reading the compact fields live.
        SnapshotEdgeHoistL(rp);
        SnapshotEdgeHoistR(rp);
#endif
    }
    else
    {
        SetupPolygonLeftEdge(rp, ytop);
        SetupPolygonRightEdge(rp, ytop);
    }
}

void SoftRenderer3D::RenderShadowMaskScanline(RendererPolygon* rp, s32 y)
{
#if defined(LITEV_SOFT3D_BANDTILE)
    BT_SHADOW_FB();
#elif defined(LITEV_SOFT3D_PIPELINE2)
    P2_SHADOW_RENDER();
#endif
    Polygon* polygon = rp->PolyData;

    u32 polyattr = (polygon->Attr & 0x3F008000);
    if (!polygon->FacingView) polyattr |= (1<<4);

    u32 polyalpha = (polygon->Attr >> 16) & 0x1F;
    bool wireframe = (polyalpha == 0);

    bool (*fnDepthTest)(s32 dstz, s32 z, u32 dstattr);
    if (polygon->Attr & (1<<14))
        fnDepthTest = polygon->WBuffer ? DepthTest_Equal_W : DepthTest_Equal_Z;
    else if (polygon->FacingView)
        fnDepthTest = DepthTest_LessThan_FrontFacing;
    else
        fnDepthTest = DepthTest_LessThan;

#ifdef LITEV_SOFT3D_HANDNEON
    int f_dtmode;
    if (polygon->Attr & (1<<14))      f_dtmode = polygon->WBuffer ? 3 : 2;
    else if (polygon->FacingView)     f_dtmode = 1;
    else                              f_dtmode = 0;
    (void)fnDepthTest;
#endif

    if (!PrevIsShadowMask)
        memset(&StencilBuffer[256 * (y&0x1)], 0, 256);

    PrevIsShadowMask = true;

    if (polygon->YTop != polygon->YBottom)
    {
        if (y >= EHY_L(rp,polygon) && rp->CurVL != polygon->VBottom)
        {
            SetupPolygonLeftEdge(rp, y);
        }

        if (y >= EHY_R(rp,polygon) && rp->CurVR != polygon->VBottom)
        {
            SetupPolygonRightEdge(rp, y);
        }
    }

#ifdef LITEV_SOFT3D_BANDED
    // Banded raster: this scanline belongs to another band. The edge walk (above)
    // and the per-scanline slope Step (below) MUST still run so the incremental
    // state is correct when we reach our own rows, but we write nothing here.
    // The memset + PrevIsShadowMask bookkeeping above also always runs so the
    // stencil group state matches the single-threaded sequence.
    if (y < BandY0 || y >= BandY1)
    {
        rp->XL = rp->SlopeL.Step();
        rp->XR = rp->SlopeR.Step();
        return;
    }
#endif

    Vertex *vlcur, *vlnext, *vrcur, *vrnext;
#ifdef LITEV_SOFT3D_COMPACTVTX
    u32 ivln, ivrn;   // compact indices for vlnext/vrnext (filledge FinalPosition[0])
#endif
#ifdef LITEV_SOFT3D_EDGEHOIST
    const RendererPolygon::EdgeHoist *ehS, *ehE;   // span-start / span-end edge snapshots
#endif
    s32 xstart, xend;
    bool l_filledge, r_filledge;
    s32 l_edgelen, r_edgelen;
    s32 l_edgecov, r_edgecov;
    Interpolator<1>* interp_start;
    Interpolator<1>* interp_end;

    xstart = rp->XL;
    xend = rp->XR;

    s32 wl = rp->SlopeL.Interp.Interpolate(EHWL0(rp,polygon), EHWL1(rp,polygon));
    s32 wr = rp->SlopeR.Interp.Interpolate(EHWR0(rp,polygon), EHWR1(rp,polygon));

    s32 zl = rp->SlopeL.Interp.InterpolateZ(EHZL0(rp,polygon), EHZL1(rp,polygon));
    s32 zr = rp->SlopeR.Interp.InterpolateZ(EHZR0(rp,polygon), EHZR1(rp,polygon));

    // right vertical edges are pushed 1px to the left as long as either:
    // the left edge slope is not 0, or the span is not 0 pixels wide, and it is not at the leftmost pixel of the screen
    if (rp->SlopeR.Increment==0 && (rp->SlopeL.Increment!=0 || xstart != xend) && (xend != 0))
        xend--;

    // if the left and right edges are swapped, render backwards.
    if (xstart > xend)
    {
        vlcur = polygon->Vertices[rp->CurVR];
        vlnext = polygon->Vertices[rp->NextVR];
        vrcur = polygon->Vertices[rp->CurVL];
        vrnext = polygon->Vertices[rp->NextVL];
#ifdef LITEV_SOFT3D_COMPACTVTX
        ivln = rp->NextVR; ivrn = rp->NextVL;
        (void)vlcur; (void)vlnext; (void)vrcur; (void)vrnext;
#endif
#ifdef LITEV_SOFT3D_EDGEHOIST
        ehS = &rp->EHR; ehE = &rp->EHL;   // swapped: span start = right edge
        (void)ivln; (void)ivrn;
#endif

        interp_start = &rp->SlopeR.Interp;
        interp_end = &rp->SlopeL.Interp;

        rp->SlopeR.EdgeParams<true>(&l_edgelen, &l_edgecov);
        rp->SlopeL.EdgeParams<true>(&r_edgelen, &r_edgecov);

        std::swap(xstart, xend);
        std::swap(wl, wr);
        std::swap(zl, zr);

        // CHECKME: edge fill rules for swapped opaque shadow mask polygons
        if ((GPU3D.RenderDispCnt & ((1<<4)|(1<<5))) || ((polyalpha < 31) && (GPU3D.RenderDispCnt & (1<<3))) || wireframe)
        {
            l_filledge = true;
            r_filledge = true;
        }
        else
        {
            l_filledge = (rp->SlopeR.Negative || !rp->SlopeR.XMajor)
                || (y == polygon->YBottom-1) && rp->SlopeR.XMajor && EH_FXNE;
            r_filledge = (!rp->SlopeL.Negative && rp->SlopeL.XMajor)
                || (!(rp->SlopeL.Negative && rp->SlopeL.XMajor) && rp->SlopeR.Increment==0)
                || (y == polygon->YBottom-1) && rp->SlopeL.XMajor && EH_FXNE;
        }
    }
    else
    {
        vlcur = polygon->Vertices[rp->CurVL];
        vlnext = polygon->Vertices[rp->NextVL];
        vrcur = polygon->Vertices[rp->CurVR];
        vrnext = polygon->Vertices[rp->NextVR];
#ifdef LITEV_SOFT3D_COMPACTVTX
        ivln = rp->NextVL; ivrn = rp->NextVR;
        (void)vlcur; (void)vlnext; (void)vrcur; (void)vrnext;
#endif
#ifdef LITEV_SOFT3D_EDGEHOIST
        ehS = &rp->EHL; ehE = &rp->EHR;   // unswapped: span start = left edge
        (void)ivln; (void)ivrn;
#endif

        interp_start = &rp->SlopeL.Interp;
        interp_end = &rp->SlopeR.Interp;

        rp->SlopeL.EdgeParams<false>(&l_edgelen, &l_edgecov);
        rp->SlopeR.EdgeParams<false>(&r_edgelen, &r_edgecov);

        // CHECKME: edge fill rules for unswapped opaque shadow mask polygons
        if ((GPU3D.RenderDispCnt & ((1<<4)|(1<<5))) || ((polyalpha < 31) && (GPU3D.RenderDispCnt & (1<<3))) || wireframe)
        {
            l_filledge = true;
            r_filledge = true;
        }
        else
        {
            l_filledge = ((rp->SlopeL.Negative || !rp->SlopeL.XMajor)
                || (y == polygon->YBottom-1) && rp->SlopeL.XMajor && EH_FXNE)
                || (rp->SlopeL.Increment == rp->SlopeR.Increment) && (xstart+l_edgelen == xend+1);
            r_filledge = (!rp->SlopeR.Negative && rp->SlopeR.XMajor) || (rp->SlopeR.Increment==0)
                || (y == polygon->YBottom-1) && rp->SlopeR.XMajor && EH_FXNE;
        }
    }

    // color/texcoord attributes aren't needed for shadow masks
    // all the pixels are guaranteed to have the same alpha
    // even if a texture is used (decal blending is used for shadows)
    // similarly, we can perform alpha test early (checkme)

    if (wireframe) polyalpha = 31;
    if (polyalpha <= GPU3D.RenderAlphaRef) return;

    // in wireframe mode, there are special rules for equal Z (TODO)

    int yedge = 0;
    if (y == polygon->YTop)           yedge = 0x4;
    else if (y == polygon->YBottom-1) yedge = 0x8;
    int edge;

    s32 x = xstart;
    Interpolator<0> interpX(xstart, xend+1, wl, wr, polygon->WBuffer);

    if (x < 0) x = 0;
    s32 xlimit;

    // for shadow masks: set stencil bits where the depth test fails.
    // draw nothing.

    // part 1: left edge
    edge = yedge | 0x1;
    xlimit = xstart+l_edgelen;
    if (xlimit > xend+1) xlimit = xend+1;
    if (xlimit > 256) xlimit = 256;

    if (!l_filledge) x = xlimit;
    else
    for (; x < xlimit; x++)
    {
        u32 pixeladdr = FirstPixelOffset + (y*RowStride) + x;

        interpX.SetX(x);

        s32 z = interpX.InterpolateZ(zl, zr);
        u32 dstattr = AttrBuffer[pixeladdr];

        if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
            StencilBuffer[256*(y&0x1) + x] = 1;

        if (dstattr & 0xF)
        {
            pixeladdr += UnderOffset;
            if (!DTEST(DepthBuffer[pixeladdr], z, AttrBuffer[pixeladdr]))
                StencilBuffer[256*(y&0x1) + x] |= 0x2;
        }
    }

    // part 2: polygon inside
    edge = yedge;
    xlimit = xend-r_edgelen+1;
    if (xlimit > xend+1) xlimit = xend+1;
    if (xlimit > 256) xlimit = 256;
    if (wireframe && !edge) x = std::max(x, xlimit);
    else for (; x < xlimit; x++)
    {
        u32 pixeladdr = FirstPixelOffset + (y*RowStride) + x;

        interpX.SetX(x);

        s32 z = interpX.InterpolateZ(zl, zr);
        u32 dstattr = AttrBuffer[pixeladdr];

        if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
            StencilBuffer[256*(y&0x1) + x] = 1;

        if (dstattr & 0xF)
        {
            pixeladdr += UnderOffset;
            if (!DTEST(DepthBuffer[pixeladdr], z, AttrBuffer[pixeladdr]))
                StencilBuffer[256*(y&0x1) + x] |= 0x2;
        }
    }

    // part 3: right edge
    edge = yedge | 0x2;
    xlimit = xend+1;
    if (xlimit > 256) xlimit = 256;

    if (r_filledge)
    for (; x < xlimit; x++)
    {
        u32 pixeladdr = FirstPixelOffset + (y*RowStride) + x;

        interpX.SetX(x);

        s32 z = interpX.InterpolateZ(zl, zr);
        u32 dstattr = AttrBuffer[pixeladdr];

        if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
            StencilBuffer[256*(y&0x1) + x] = 1;

        if (dstattr & 0xF)
        {
            pixeladdr += UnderOffset;
            if (!DTEST(DepthBuffer[pixeladdr], z, AttrBuffer[pixeladdr]))
                StencilBuffer[256*(y&0x1) + x] |= 0x2;
        }
    }

    rp->XL = rp->SlopeL.Step();
    rp->XR = rp->SlopeR.Step();
}

#ifdef LITEV_SOFT3D_FAST
// Subaffine span interpolation (FPS-first, approximate — NOT bit-exact).
// Instead of a perspective-correct divide (Interpolator::SetX -> num/den) every
// pixel, we anchor the true perspective-correct attributes (z,r,g,b,s,t) only
// every SA_SUB pixels (2 divides per block) and LINEARLY step 16.16 fixed-point
// accumulators in between. The per-pixel depth test, stencil/shadow, RenderPixel,
// alpha test and plot are untouched — only attribute interpolation is approximate.
#define SA_SUB   8
#define SA_FRAC  16
// Refresh-or-step the accumulators and produce z for the depth test. Steps every
// pixel (even those the depth/stencil test will 'continue' past) so the linear
// walk stays aligned with x. xlimit is this span segment's exclusive upper bound.
#define SA_STEP_Z() \
    s32 z; \
    { \
        if (sa_rem == 0) \
        { \
            interpX.SetX(x); \
            sa_z = (s64)interpX.InterpolateZ(zl, zr) << SA_FRAC; \
            sa_r = (s64)interpX.Interpolate(rl, rr) << SA_FRAC; \
            sa_g = (s64)interpX.Interpolate(gl, gr) << SA_FRAC; \
            sa_b = (s64)interpX.Interpolate(bl, br) << SA_FRAC; \
            sa_s = (s64)interpX.Interpolate(sl, sr) << SA_FRAC; \
            sa_t = (s64)interpX.Interpolate(tl, tr) << SA_FRAC; \
            s32 sa_xn = x + SA_SUB; \
            if (sa_xn > xlimit - 1) sa_xn = xlimit - 1; \
            s32 sa_span = sa_xn - x; \
            if (sa_span < 1) \
            { \
                sa_dz = sa_dr = sa_dg = sa_db = sa_ds = sa_dt = 0; \
                sa_rem = 1; \
            } \
            else \
            { \
                interpX.SetX(sa_xn); \
                sa_dz = (((s64)interpX.InterpolateZ(zl, zr) << SA_FRAC) - sa_z) / sa_span; \
                sa_dr = (((s64)interpX.Interpolate(rl, rr) << SA_FRAC) - sa_r) / sa_span; \
                sa_dg = (((s64)interpX.Interpolate(gl, gr) << SA_FRAC) - sa_g) / sa_span; \
                sa_db = (((s64)interpX.Interpolate(bl, br) << SA_FRAC) - sa_b) / sa_span; \
                sa_ds = (((s64)interpX.Interpolate(sl, sr) << SA_FRAC) - sa_s) / sa_span; \
                sa_dt = (((s64)interpX.Interpolate(tl, tr) << SA_FRAC) - sa_t) / sa_span; \
                sa_rem = sa_span; \
            } \
        } \
        else \
        { \
            sa_z += sa_dz; sa_r += sa_dr; sa_g += sa_dg; \
            sa_b += sa_db; sa_s += sa_ds; sa_t += sa_dt; \
        } \
        sa_rem--; \
        z = (s32)(sa_z >> SA_FRAC); \
    }
#define SA_LOAD_RGBST() \
    u32 vr = (u32)(sa_r >> SA_FRAC); \
    u32 vg = (u32)(sa_g >> SA_FRAC); \
    u32 vb = (u32)(sa_b >> SA_FRAC); \
    s16 s = (s16)(sa_s >> SA_FRAC); \
    s16 t = (s16)(sa_t >> SA_FRAC);
#endif

#if defined(LITEV_SOFT3D_FAST) && defined(LITEV_SOFT3D_GRADIENT)
// Stage 2 (LITEV_SOFT3D_GRADIENT): sub-affine DDA of the edge attributes along Y.
// Mirrors the shipped SA_STEP_Z span code but per-edge and per-scanline: anchor the true
// perspective value (via the edge interpolator) every SA_SUB_Y rows, linearly step 16.16
// accumulators in between. Handles W + colour/texcoord (index 0..5); Z stays on the exact
// InterpolateZ path (Z drives the depth test, so it must not be approximated). APPROXIMATE.
// e.rem is reset to 0 at every vertex-crossing (SnapshotEdge*), forcing a fresh anchor per
// segment; e.segYBot clamps the look-ahead to the segment bottom so the step never
// extrapolates past the Next vertex.
#define SA_SUB_Y 8   // edge re-anchor interval in scanlines (tunable)
void SoftRenderer3D::StepEdgeAttrsDDA(RendererPolygon::EdgeEndpoints& e,
                                      const Interpolator<1>& interp, s32 y, s32* out) const
{
    if (e.rem == 0)
    {
        // Anchor: true perspective value at the current row (uses interp's live yfactor).
        for (int i = 0; i < 6; i++)
            e.acc[i] = (s64)interp.Interpolate(e.v0[i], e.v1[i]) << SA_FRAC;

        s32 anchorY = y + SA_SUB_Y;
        if (anchorY > e.segYBot) anchorY = e.segYBot;
        s32 span = anchorY - y;
        if (span < 1)
        {
            for (int i = 0; i < 6; i++) e.dv[i] = 0;
            e.rem = 1;
        }
        else
        {
            // Look-ahead anchor at row anchorY via a COPY (must not disturb the persistent
            // per-edge interpolator, which is stepped once per row by Slope::Step).
            Interpolator<1> tmp = interp;
            tmp.SetX(anchorY);
            for (int i = 0; i < 6; i++)
                e.dv[i] = (((s64)tmp.Interpolate(e.v0[i], e.v1[i]) << SA_FRAC) - e.acc[i]) / span;
            e.rem = span;
        }
    }
    else
    {
        for (int i = 0; i < 6; i++) e.acc[i] += e.dv[i];
    }
    e.rem--;
    for (int i = 0; i < 6; i++) out[i] = (s32)(e.acc[i] >> SA_FRAC);
}
#endif

void SoftRenderer3D::RenderPolygonScanline(RendererPolygon* rp, s32 y)
{
#if defined(LITEV_SOFT3D_BANDTILE)
    BT_SHADOW_FB();
#elif defined(LITEV_SOFT3D_PIPELINE2)
    P2_SHADOW_RENDER();
#endif
    Polygon* polygon = rp->PolyData;

#ifndef LITEV_SOFT3D_FAST
    u32 polyattr = (polygon->Attr & 0x3F008000);
    if (!polygon->FacingView) polyattr |= (1<<4);

    u32 polyalpha = (polygon->Attr >> 16) & 0x1F;
    bool wireframe = (polyalpha == 0);

    bool (*fnDepthTest)(s32 dstz, s32 z, u32 dstattr);
    if (polygon->Attr & (1<<14))
        fnDepthTest = polygon->WBuffer ? DepthTest_Equal_W : DepthTest_Equal_Z;
    else if (polygon->FacingView)
        fnDepthTest = DepthTest_LessThan_FrontFacing;
    else
        fnDepthTest = DepthTest_LessThan;
#endif

    PrevIsShadowMask = false;

    if (polygon->YTop != polygon->YBottom)
    {
        if (y >= EHY_L(rp,polygon) && rp->CurVL != polygon->VBottom)
        {
            SetupPolygonLeftEdge(rp, y);
        }

        if (y >= EHY_R(rp,polygon) && rp->CurVR != polygon->VBottom)
        {
            SetupPolygonRightEdge(rp, y);
        }
    }

#ifdef LITEV_SOFT3D_BANDED
    // Banded raster: not our row. Advance the incremental edge state (Step) but
    // write no spans. See RenderShadowMaskScanline for the rationale.
    if (y < BandY0 || y >= BandY1)
    {
#if defined(LITEV_SOFT3D_FAST) && defined(LITEV_SOFT3D_GRADIENT) && !defined(LITEV_SOFT3D_EDGENEON)
        // The GRADIENT DDA is incremental, so it must advance on EVERY row (even the
        // out-of-band rows this band only walks for edge state), or it desyncs from y
        // before the band's first written row. Step it here (discard the output); the
        // interp still holds row-y's yfactor since Slope::Step runs just below. EDGENEON
        // is stateless (recomputed from yfactor per written row), so it needs nothing here.
        s32 ddaDumpL[6], ddaDumpR[6];
        StepEdgeAttrsDDA(rp->EdgeL, rp->SlopeL.Interp, y, ddaDumpL);
        StepEdgeAttrsDDA(rp->EdgeR, rp->SlopeR.Interp, y, ddaDumpR);
#endif
        rp->XL = rp->SlopeL.Step();
        rp->XR = rp->SlopeR.Step();
        return;
    }
#endif

#ifdef LITEV_SOFT3D_FAST
    // Lever 1: compute the per-scanline polygon constants only for in-band rows
    // (the out-of-band Step-only path above never touches them).
    u32 polyattr = (polygon->Attr & 0x3F008000);
    if (!polygon->FacingView) polyattr |= (1<<4);

    u32 polyalpha = (polygon->Attr >> 16) & 0x1F;
    bool wireframe = (polyalpha == 0);

    bool (*fnDepthTest)(s32 dstz, s32 z, u32 dstattr);
    if (polygon->Attr & (1<<14))
        fnDepthTest = polygon->WBuffer ? DepthTest_Equal_W : DepthTest_Equal_Z;
    else if (polygon->FacingView)
        fnDepthTest = DepthTest_LessThan_FrontFacing;
    else
        fnDepthTest = DepthTest_LessThan;
#endif

#ifdef LITEV_SOFT3D_HANDNEON
    // Per-polygon depth-test mode for the inlined DTEST (see DepthTestInline).
    int f_dtmode;
    if (polygon->Attr & (1<<14))      f_dtmode = polygon->WBuffer ? 3 : 2;
    else if (polygon->FacingView)     f_dtmode = 1;
    else                              f_dtmode = 0;
    (void)fnDepthTest;
#endif

    Vertex *vlcur, *vlnext, *vrcur, *vrnext;
#ifdef LITEV_SOFT3D_COMPACTVTX
    // Compact indices paralleling vlcur/vlnext/vrcur/vrnext (span cur/next, both edges).
    u32 ivlc, ivln, ivrc, ivrn;
#endif
#ifdef LITEV_SOFT3D_EDGEHOIST
    const RendererPolygon::EdgeHoist *ehS, *ehE;   // span-start / span-end edge snapshots
#endif
    s32 xstart, xend;
    bool l_filledge, r_filledge;
    s32 l_edgelen, r_edgelen;
    s32 l_edgecov, r_edgecov;
    Interpolator<1>* interp_start;
    Interpolator<1>* interp_end;
#if defined(LITEV_SOFT3D_FAST) && (defined(LITEV_SOFT3D_EDGENEON) || defined(LITEV_SOFT3D_GRADIENT))
    // Per physical edge, the 6 interpolated attrs {W,R,G,B,S,T} (index 0=W). Computed once
    // here from the endpoint snapshot, then wired to the span start/end below via
    // edgeSwapped (which mirrors the interp_start/interp_end swap). Z is NOT in this set --
    // it stays on the exact InterpolateZ path (it drives the depth test).
    s32 attrL[6], attrR[6];
    bool edgeSwapped = false;
#endif

    xstart = rp->XL;
    xend = rp->XR;

#if defined(LITEV_SOFT3D_FAST) && (defined(LITEV_SOFT3D_EDGENEON) || defined(LITEV_SOFT3D_GRADIENT))
#if defined(LITEV_SOFT3D_EDGENEON)
    // Variant 1: EXACT NEON batch of the per-edge perspective interpolation (byte-identical
    // to 6 scalar Interpolate calls; same yfactor, SIMD-packed). Reuses the endpoint cache.
    rp->SlopeL.Interp.InterpolateBatch(rp->EdgeL.v0, rp->EdgeL.v1, attrL, 6);
    rp->SlopeR.Interp.InterpolateBatch(rp->EdgeR.v0, rp->EdgeR.v1, attrR, 6);
#else
    // Variant 2 (LITEV_SOFT3D_GRADIENT): approximate sub-affine DDA along Y for W + colour/
    // texcoord. Z below stays exact.
    StepEdgeAttrsDDA(rp->EdgeL, rp->SlopeL.Interp, y, attrL);
    StepEdgeAttrsDDA(rp->EdgeR, rp->SlopeR.Interp, y, attrR);
#endif
    s32 wl = attrL[0];
    s32 wr = attrR[0];
    s32 zl = rp->SlopeL.Interp.InterpolateZ(rp->EdgeL.z0, rp->EdgeL.z1);
    s32 zr = rp->SlopeR.Interp.InterpolateZ(rp->EdgeR.z0, rp->EdgeR.z1);
#else
    s32 wl = rp->SlopeL.Interp.Interpolate(EHWL0(rp,polygon), EHWL1(rp,polygon));
    s32 wr = rp->SlopeR.Interp.Interpolate(EHWR0(rp,polygon), EHWR1(rp,polygon));

    s32 zl = rp->SlopeL.Interp.InterpolateZ(EHZL0(rp,polygon), EHZL1(rp,polygon));
    s32 zr = rp->SlopeR.Interp.InterpolateZ(EHZR0(rp,polygon), EHZR1(rp,polygon));
#endif

    // right vertical edges are pushed 1px to the left as long as either:
    // the left edge slope is not 0, or the span is not 0 pixels wide, and it is not at the leftmost pixel of the screen
    if (rp->SlopeR.Increment==0 && (rp->SlopeL.Increment!=0 || xstart != xend) && (xend != 0))
        xend--;

    // if the left and right edges are swapped, render backwards.
    // on hardware, swapped edges seem to break edge length calculation,
    // causing X-major edges to be rendered wrong when filled,
    // and resulting in buggy looking anti-aliasing on X-major edges

    if (xstart > xend)
    {
        vlcur = polygon->Vertices[rp->CurVR];
        vlnext = polygon->Vertices[rp->NextVR];
        vrcur = polygon->Vertices[rp->CurVL];
        vrnext = polygon->Vertices[rp->NextVL];
#ifdef LITEV_SOFT3D_COMPACTVTX
        ivlc = rp->CurVR; ivln = rp->NextVR; ivrc = rp->CurVL; ivrn = rp->NextVL;
        (void)vlcur; (void)vlnext; (void)vrcur; (void)vrnext;
#endif
#ifdef LITEV_SOFT3D_EDGEHOIST
        ehS = &rp->EHR; ehE = &rp->EHL;   // swapped: span start = right edge
        (void)ivlc; (void)ivln; (void)ivrc; (void)ivrn;
#endif

        interp_start = &rp->SlopeR.Interp;
        interp_end = &rp->SlopeL.Interp;
#if defined(LITEV_SOFT3D_FAST) && (defined(LITEV_SOFT3D_EDGENEON) || defined(LITEV_SOFT3D_GRADIENT))
        // swapped: span start = right edge, span end = left edge (mirrors interp_start/
        // interp_end + the vlcur/vrcur reassignment above).
        edgeSwapped = true;
#endif

        rp->SlopeR.EdgeParams<true>(&l_edgelen, &l_edgecov);
        rp->SlopeL.EdgeParams<true>(&r_edgelen, &r_edgecov);

        std::swap(xstart, xend);
        std::swap(wl, wr);
        std::swap(zl, zr);

        // edge fill rules for swapped opaque edges:
        // * right edge is filled if slope > 1, or if the left edge = 0, but is never filled if it is < -1
        // * left edge is filled if slope <= 1
        // * the bottom-most pixel of negative x-major slopes are filled if they are next to a flat bottom edge
        // edges are always filled if antialiasing/edgemarking are enabled,
        // if the pixels are translucent and alpha blending is enabled, or if the polygon is wireframe
        // checkme: do swapped line polygons exist?
        if ((GPU3D.RenderDispCnt & ((1<<4)|(1<<5))) || ((polyalpha < 31) && (GPU3D.RenderDispCnt & (1<<3))) || wireframe)
        {
            l_filledge = true;
            r_filledge = true;
        }
        else
        {
            l_filledge = (rp->SlopeR.Negative || !rp->SlopeR.XMajor)
                || (y == polygon->YBottom-1) && rp->SlopeR.XMajor && EH_FXNE;
            r_filledge = (!rp->SlopeL.Negative && rp->SlopeL.XMajor)
                || (!(rp->SlopeL.Negative && rp->SlopeL.XMajor) && rp->SlopeR.Increment==0)
                || (y == polygon->YBottom-1) && rp->SlopeL.XMajor && EH_FXNE;
        }
    }
    else
    {
        vlcur = polygon->Vertices[rp->CurVL];
        vlnext = polygon->Vertices[rp->NextVL];
        vrcur = polygon->Vertices[rp->CurVR];
        vrnext = polygon->Vertices[rp->NextVR];
#ifdef LITEV_SOFT3D_COMPACTVTX
        ivlc = rp->CurVL; ivln = rp->NextVL; ivrc = rp->CurVR; ivrn = rp->NextVR;
        (void)vlcur; (void)vlnext; (void)vrcur; (void)vrnext;
#endif
#ifdef LITEV_SOFT3D_EDGEHOIST
        ehS = &rp->EHL; ehE = &rp->EHR;   // unswapped: span start = left edge
        (void)ivlc; (void)ivln; (void)ivrc; (void)ivrn;
#endif

        interp_start = &rp->SlopeL.Interp;
        interp_end = &rp->SlopeR.Interp;
#if defined(LITEV_SOFT3D_FAST) && (defined(LITEV_SOFT3D_EDGENEON) || defined(LITEV_SOFT3D_GRADIENT))
        // unswapped: span start = left edge, span end = right edge.
        edgeSwapped = false;
#endif

        rp->SlopeL.EdgeParams<false>(&l_edgelen, &l_edgecov);
        rp->SlopeR.EdgeParams<false>(&r_edgelen, &r_edgecov);

        // edge fill rules for unswapped opaque edges:
        // * right edge is filled if slope > 1
        // * left edge is filled if slope <= 1
        // * edges with slope = 0 are always filled
        // * the bottom-most pixel of negative x-major slopes are filled if they are next to a flat bottom edge
        // * edges are filled if both sides are identical and fully overlapping
        // edges are always filled if antialiasing/edgemarking are enabled,
        // if the pixels are translucent and alpha blending is enabled, or if the polygon is wireframe
        if ((GPU3D.RenderDispCnt & ((1<<4)|(1<<5))) || ((polyalpha < 31) && (GPU3D.RenderDispCnt & (1<<3))) || wireframe)
        {
            l_filledge = true;
            r_filledge = true;
        }
        else
        {
            l_filledge = ((rp->SlopeL.Negative || !rp->SlopeL.XMajor)
                || (y == polygon->YBottom-1) && rp->SlopeL.XMajor && EH_FXNE)
                || (rp->SlopeL.Increment == rp->SlopeR.Increment) && (xstart+l_edgelen == xend+1);
            r_filledge = (!rp->SlopeR.Negative && rp->SlopeR.XMajor) || (rp->SlopeR.Increment==0)
                || (y == polygon->YBottom-1) && rp->SlopeR.XMajor && EH_FXNE;
        }
    }

    // interpolate attributes along Y

#if defined(LITEV_SOFT3D_FAST) && (defined(LITEV_SOFT3D_EDGENEON) || defined(LITEV_SOFT3D_GRADIENT))
    // Wire the per-edge attrs computed above (NEON-exact or DDA-approx) to the span
    // start/end. attrL/attrR are keyed to the PHYSICAL left/right edges; edgeSwapped picks
    // which one is the span start, exactly mirroring interp_start/interp_end + the vlcur/
    // vrcur reassignment (start=left, end=right when unswapped; reversed when swapped).
    const s32* aS = edgeSwapped ? attrR : attrL;   // span-start edge attrs
    const s32* aE = edgeSwapped ? attrL : attrR;   // span-end   edge attrs
    s32 rl = aS[1], gl = aS[2], bl = aS[3], sl = aS[4], tl = aS[5];
    s32 rr = aE[1], gr = aE[2], br = aE[3], sr = aE[4], tr = aE[5];
    // interp_start/interp_end feed only the baseline per-attr Interpolate below; here the
    // per-edge attrs came from the NEON batch / DDA, so the pointers are unused this path.
    (void)interp_start; (void)interp_end;
#else
#ifdef LITEV_SOFT3D_EDGEHOIST
    // Colour/texcoord endpoints from the hot per-edge snapshot (span-start ehS / span-end
    // ehE), matching the interp_start/interp_end + vlcur/vrcur swap. Byte-identical to the
    // compact/vertex reads below (verbatim values; 0=Cur, 1=Next; S/T sign-extend as before).
    s32 rl = interp_start->Interpolate(ehS->R0, ehS->R1);
    s32 gl = interp_start->Interpolate(ehS->G0, ehS->G1);
    s32 bl = interp_start->Interpolate(ehS->B0, ehS->B1);

    s32 sl = interp_start->Interpolate(ehS->S0, ehS->S1);
    s32 tl = interp_start->Interpolate(ehS->T0, ehS->T1);

    s32 rr = interp_end->Interpolate(ehE->R0, ehE->R1);
    s32 gr = interp_end->Interpolate(ehE->G0, ehE->G1);
    s32 br = interp_end->Interpolate(ehE->B0, ehE->B1);

    s32 sr = interp_end->Interpolate(ehE->S0, ehE->S1);
    s32 tr = interp_end->Interpolate(ehE->T0, ehE->T1);
#else
    s32 rl = interp_start->Interpolate(VC0(rp,vlcur,ivlc), VC0(rp,vlnext,ivln));
    s32 gl = interp_start->Interpolate(VC1(rp,vlcur,ivlc), VC1(rp,vlnext,ivln));
    s32 bl = interp_start->Interpolate(VC2(rp,vlcur,ivlc), VC2(rp,vlnext,ivln));

    s32 sl = interp_start->Interpolate(VT0(rp,vlcur,ivlc), VT0(rp,vlnext,ivln));
    s32 tl = interp_start->Interpolate(VT1(rp,vlcur,ivlc), VT1(rp,vlnext,ivln));

    s32 rr = interp_end->Interpolate(VC0(rp,vrcur,ivrc), VC0(rp,vrnext,ivrn));
    s32 gr = interp_end->Interpolate(VC1(rp,vrcur,ivrc), VC1(rp,vrnext,ivrn));
    s32 br = interp_end->Interpolate(VC2(rp,vrcur,ivrc), VC2(rp,vrnext,ivrn));

    s32 sr = interp_end->Interpolate(VT0(rp,vrcur,ivrc), VT0(rp,vrnext,ivrn));
    s32 tr = interp_end->Interpolate(VT1(rp,vrcur,ivrc), VT1(rp,vrnext,ivrn));
#endif
#endif

    // in wireframe mode, there are special rules for equal Z (TODO)

    int yedge = 0;
    if (y == polygon->YTop)           yedge = 0x4;
    else if (y == polygon->YBottom-1) yedge = 0x8;
    int edge;

    s32 x = xstart;
    Interpolator<0> interpX(xstart, xend+1, wl, wr, polygon->WBuffer);

    if (x < 0) x = 0;
    s32 xlimit;

    s32 xcov = 0;

#ifdef LITEV_SOFT3D_FAST
    // subaffine span accumulators (16.16 fixed point) + per-pixel deltas
    s64 sa_z = 0, sa_r = 0, sa_g = 0, sa_b = 0, sa_s = 0, sa_t = 0;
    s64 sa_dz = 0, sa_dr = 0, sa_dg = 0, sa_db = 0, sa_ds = 0, sa_dt = 0;
    s32 sa_rem = 0;

    // Lever 2: hoist RenderPixel's per-POLYGON invariants (blend mode, toon /
    // highlight mode, texture-enable, palette + toon table pointers) out of the
    // per-pixel loop. RenderPixel re-derived all of these from polygon->Attr /
    // polygon->TexParam / RenderDispCnt on EVERY texel; they are constant for the
    // whole scanline. shadeFast captures them once and only varies s,t + vertex
    // colour per pixel. Result is exact vs RenderPixel (still calls TextureLookup
    // for the texel fetch, which is memory-bound and not hoistable).
    const u32  f_attr      = polygon->Attr;
    const u32  f_blendmode = (f_attr >> 4) & 0x3;
    const u32  f_dispcnt   = GPU3D.RenderDispCnt;
    const bool f_toon      = (f_blendmode == 2);
    const bool f_highlight = f_toon && (f_dispcnt & (1<<1));
    const bool f_decal     = (f_blendmode & 0x1) != 0;
    const u32  f_texparam  = polygon->TexParam;
    const u32  f_texfmt    = (f_texparam >> 26) & 0x7;
    const bool f_texEnable = (f_dispcnt & 0x1) && (f_texfmt != 0);
    const u32  f_texpal    = polygon->TexPalette;
    const u16* f_toontbl   = GPU3D.RenderToonTable;
    const u32  f_polyalpha = polyalpha;
    const bool f_wireframe = wireframe;

    // Decode-once texture cache: decode this polygon's texture (WxH) ONCE for the
    // whole band here (a hit costs a short slot scan; a miss decodes once and is
    // reused across every scanline this band renders of every poly sharing the
    // texture). shadeFast then samples with a plain array read (no per-texel format
    // branch / palette / VRAM read). Nullptr => too large to cache; fall back.
    const u16* f_texcache = nullptr;
    s32 f_texW = 0, f_texH = 0;
#ifdef LITEV_SOFT3D_TEX1B
    // TEX1B: f_texcache is the u16 palette for palette formats (index with f_tex1b[texAddr]),
    // or the u16 direct/4x4 arena (f_tex1b == null). Captured by shadeFast/shade4 below.
    const u8* f_tex1b = nullptr;
    if (f_texEnable)
        f_texcache = ResolveTexCache(f_texparam, f_texpal, &f_texW, &f_texH, &f_tex1b);
#else
    if (f_texEnable)
        f_texcache = ResolveTexCache(f_texparam, f_texpal, &f_texW, &f_texH);
#endif

    // Common wrap mode: repeat on both axes, no flip. Then the texel address is a
    // pure masked (si = (s>>4)&(W-1)) — no branches — so it vectorizes cleanly.
    // W,H are always powers of two, so the mask is exact for negative coords too.
    const bool f_wrap_simple =
        (f_texparam & (1<<16)) && (f_texparam & (1<<17)) &&
        !(f_texparam & (1<<18)) && !(f_texparam & (1<<19));

    // Wrap/flip/clamp (s,t) fixed-point tex coords to a decoded-arena texel index.
    // Shared by the current-pixel fetch and the look-ahead prefetch. Branchy but
    // cheap integer ALU that overlaps the outstanding texel cache miss.
    auto texAddr = [&](s16 s, s16 t) -> u32
    {
        s32 si = s >> 4;
        s32 ti = t >> 4;

        if (f_texparam & (1<<16))
        {
            if (f_texparam & (1<<18))
            {
                if (si & f_texW) si = (f_texW-1) - (si & (f_texW-1));
                else             si = (si & (f_texW-1));
            }
            else
                si &= f_texW-1;
        }
        else
        {
            if (si < 0) si = 0;
            else if (si >= f_texW) si = f_texW-1;
        }

        if (f_texparam & (1<<17))
        {
            if (f_texparam & (1<<19))
            {
                if (ti & f_texH) ti = (f_texH-1) - (ti & (f_texH-1));
                else             ti = (ti & (f_texH-1));
            }
            else
                ti &= f_texH-1;
        }
        else
        {
            if (ti < 0) ti = 0;
            else if (ti >= f_texH) ti = f_texH-1;
        }

        return (u32)ti * (u32)f_texW + (u32)si;
    };

    auto shadeFast = [&](u32 vr, u32 vg, u32 vb, s16 s, s16 t) -> u32
    {
        u8 r, g, b, a;

        if (f_toon)
        {
            if (f_highlight) { vg = vr; vb = vr; }
            else
            {
                u16 tc = f_toontbl[vr >> 1];
                vr = (tc << 1) & 0x3E; if (vr) vr++;
                vg = (tc >> 4) & 0x3E; if (vg) vg++;
                vb = (tc >> 9) & 0x3E; if (vb) vb++;
            }
        }

        if (f_texEnable)
        {
            u16 tcolor; u8 talpha;
            if (f_texcache)
            {
#ifdef LITEV_SOFT3D_TEX1B
                // palette fmt: 1-byte index (halved arena) -> L1-hot palette; else 2B direct.
                u16 packed = f_tex1b ? f_texcache[f_tex1b[texAddr(s, t)]]
                                     : f_texcache[texAddr(s, t)];
#else
                u16 packed = f_texcache[texAddr(s, t)];
#endif
                tcolor = (u16)(packed & 0x7FFF);
                talpha = (packed & 0x8000) ? 31 : 0;
            }
            else
            {
                TextureLookup(f_texparam, f_texpal, s, t, &tcolor, &talpha);
            }

            u8 tr = (tcolor << 1) & 0x3E; if (tr) tr++;
            u8 tg = (tcolor >> 4) & 0x3E; if (tg) tg++;
            u8 tb = (tcolor >> 9) & 0x3E; if (tb) tb++;

            if (f_decal)
            {
                if (talpha == 0)       { r = vr; g = vg; b = vb; }
                else if (talpha == 31) { r = tr; g = tg; b = tb; }
                else
                {
                    r = ((tr * talpha) + (vr * (31-talpha))) >> 5;
                    g = ((tg * talpha) + (vg * (31-talpha))) >> 5;
                    b = ((tb * talpha) + (vb * (31-talpha))) >> 5;
                }
                a = f_polyalpha;
            }
            else
            {
                r = ((tr+1) * (vr+1) - 1) >> 6;
                g = ((tg+1) * (vg+1) - 1) >> 6;
                b = ((tb+1) * (vb+1) - 1) >> 6;
                a = ((talpha+1) * (f_polyalpha+1) - 1) >> 5;
            }
        }
        else
        {
            r = vr; g = vg; b = vb; a = f_polyalpha;
        }

        if (f_highlight)
        {
            u16 tc = f_toontbl[vr >> 1];
            u8 hr = (tc << 1) & 0x3E; if (hr) hr++;
            u8 hg = (tc >> 4) & 0x3E; if (hg) hg++;
            u8 hb = (tc >> 9) & 0x3E; if (hb) hb++;

            int rr = r + hr; if (rr > 63) rr = 63; r = (u8)rr;
            int gg = g + hg; if (gg > 63) gg = 63; g = (u8)gg;
            int bb = b + hb; if (bb > 63) bb = 63; b = (u8)bb;
        }

        if (f_wireframe) a = 31;

        return r | (g << 8) | (b << 16) | ((u32)a << 24);
    };

#ifdef LITEV_SOFT3D_NEON
    // Batched 4-wide shade for the pure-modulate decode-once-texcache path
    // (!decal !toon !highlight !wireframe, textured). Gathers the 4 scattered
    // texels with 4 back-to-back scalar loads (decoupling load-use latency: the
    // loads pipeline instead of each blocking its own modulate) then NEON-
    // modulates all four RGBA channels of all four pixels at once. Bit-identical
    // to shadeFast for this case (same integer ops, just SIMD-packed). out[] gets
    // the ABGR words in pixel order.
    auto shade4 = [&](const s16* bs, const s16* bt,
                      const u16* bvr, const u16* bvg, const u16* bvb,
                      u32* out)
    {
        u32 addr[4];
        if (f_wrap_simple)
        {
            // branchless masked wrap for all 4 lanes at once
            int32x4_t vs = vshrq_n_s32(vmovl_s16(vld1_s16(bs)), 4);
            int32x4_t vt = vshrq_n_s32(vmovl_s16(vld1_s16(bt)), 4);
            int32x4_t si = vandq_s32(vs, vdupq_n_s32(f_texW - 1));
            int32x4_t ti = vandq_s32(vt, vdupq_n_s32(f_texH - 1));
            int32x4_t a  = vmlaq_s32(si, ti, vdupq_n_s32(f_texW)); // si + ti*W
            vst1q_u32(addr, vreinterpretq_u32_s32(a));
        }
        else
        {
            addr[0] = texAddr(bs[0], bt[0]);
            addr[1] = texAddr(bs[1], bt[1]);
            addr[2] = texAddr(bs[2], bt[2]);
            addr[3] = texAddr(bs[3], bt[3]);
        }

        u16 packed[4];
#ifdef LITEV_SOFT3D_TEX1B
        if (f_tex1b)
        {
            // Gather 4 one-byte indices from the halved arena, then 4 L1-hot palette reads.
            // Bit-identical to the 2B gather; the WIN is the halved arena (fewer L2 misses),
            // not the lookup. (A NEON TBL over the palette is a possible later refinement.)
            packed[0] = f_texcache[f_tex1b[addr[0]]];
            packed[1] = f_texcache[f_tex1b[addr[1]]];
            packed[2] = f_texcache[f_tex1b[addr[2]]];
            packed[3] = f_texcache[f_tex1b[addr[3]]];
        }
        else
#endif
        {
        packed[0] = f_texcache[addr[0]];
        packed[1] = f_texcache[addr[1]];
        packed[2] = f_texcache[addr[2]];
        packed[3] = f_texcache[addr[3]];
        }

        uint16x4_t vpacked = vld1_u16(packed);
        uint16x4_t one     = vdup_n_u16(1);
        uint16x4_t c3e     = vdup_n_u16(0x3E);

        uint16x4_t vtcolor = vand_u16(vpacked, vdup_n_u16(0x7FFF));
        // talpha = (bit15 ? 31 : 0)
        uint16x4_t vtalpha = vmul_u16(vshr_n_u16(vpacked, 15), vdup_n_u16(31));

        // 5-bit channel -> 6-bit (x<<1, then +1 iff nonzero)
        uint16x4_t tr = vand_u16(vshl_n_u16(vtcolor, 1), c3e);
        uint16x4_t tg = vand_u16(vshr_n_u16(vtcolor, 4), c3e);
        uint16x4_t tb = vand_u16(vshr_n_u16(vtcolor, 9), c3e);
        tr = vadd_u16(tr, vmin_u16(tr, one));
        tg = vadd_u16(tg, vmin_u16(tg, one));
        tb = vadd_u16(tb, vmin_u16(tb, one));

        uint16x4_t vr = vld1_u16(bvr);
        uint16x4_t vg = vld1_u16(bvg);
        uint16x4_t vb = vld1_u16(bvb);

        // r = ((tr+1)*(vr+1) - 1) >> 6  (all lanes fit in u16: max 64*64-1 = 4095)
        uint16x4_t rr = vshr_n_u16(vsub_u16(vmul_u16(vadd_u16(tr, one), vadd_u16(vr, one)), one), 6);
        uint16x4_t gg = vshr_n_u16(vsub_u16(vmul_u16(vadd_u16(tg, one), vadd_u16(vg, one)), one), 6);
        uint16x4_t bb = vshr_n_u16(vsub_u16(vmul_u16(vadd_u16(tb, one), vadd_u16(vb, one)), one), 6);
        // a = ((talpha+1)*(polyalpha+1) - 1) >> 5
        uint16x4_t pa1 = vdup_n_u16((u16)(f_polyalpha + 1));
        uint16x4_t aa  = vshr_n_u16(vsub_u16(vmul_u16(vadd_u16(vtalpha, one), pa1), one), 5);

        // pack ABGR: r | g<<8 | b<<16 | a<<24
        uint32x4_t col = vorrq_u32(
            vorrq_u32(vmovl_u16(rr), vshlq_n_u32(vmovl_u16(gg), 8)),
            vorrq_u32(vshlq_n_u32(vmovl_u16(bb), 16), vshlq_n_u32(vmovl_u16(aa), 24)));
        vst1q_u32(out, col);
    };

    // Plot tail for part-2 interior pixels (edge == yedge). Mirrors the scalar
    // interior plot exactly; used by the batched path after shade4.
    auto plot2 = [&](u32 pixeladdr, s32 z, u32 dstattr, u32 color)
    {
        u8 alpha = color >> 24;
        if (alpha <= GPU3D.RenderAlphaRef) return;

        if (alpha == 31)
        {
            u32 attr = polyattr | edge;
            if ((GPU3D.RenderDispCnt & (1<<4)) && (attr & 0xF))
            {
                attr |= (0x1F << 8);
                if (PA_IS_TOP(pixeladdr))
                {
                    ColorBuffer[pixeladdr+UnderOffset] = ColorBuffer[pixeladdr];
                    DepthBuffer[pixeladdr+UnderOffset] = DepthBuffer[pixeladdr];
                    AttrBuffer[pixeladdr+UnderOffset] = AttrBuffer[pixeladdr];
                }
            }
            DepthBuffer[pixeladdr] = z;
            ColorBuffer[pixeladdr] = color;
            AttrBuffer[pixeladdr] = attr;
        }
        else
        {
            if (!(polygon->Attr & (1<<11))) z = -1;
            PlotTranslucentPixel(pixeladdr, color, z, polyattr, polygon->IsShadow);
            if ((dstattr & 0xF) && (PA_IS_TOP(pixeladdr)))
                PlotTranslucentPixel(pixeladdr+UnderOffset, color, z, polyattr, polygon->IsShadow);
        }
    };

#ifdef LITEV_SOFT3D_AAFOLD
    // Plot tail for the folded AA-EDGE batched path (part 1 / part 3). Mirrors the SCALAR
    // edge plot EXACTLY (per-pixel xcov coverage, push-down, translucent) so the batched-
    // shade edge is byte-identical to the scalar edge loop. `rightSide` picks the part-1
    // (cov = xcov>>5) vs part-3 (cov = 0x1F-(xcov>>5)) coverage formula; it is loop-invariant
    // per edge so the branch hoists out of the 4-wide replay. `edgeflag` is part1's
    // (yedge|0x1) / part3's (yedge|0x2); `edgecov` is l_edgecov / r_edgecov; `xcovref` is the
    // shared per-scanline xcov accumulator (advanced only for opaque survivors, in x-order).
    auto plotEdge = [&](u32 pixeladdr, s32 z, u32 dstattr, u32 color,
                        u32 edgeflag, s32 edgecov, s32& xcovref, bool rightSide)
    {
        u8 alpha = color >> 24;
        if (alpha <= GPU3D.RenderAlphaRef) return;

        if (alpha == 31)
        {
            u32 attr = polyattr | edgeflag;
            if (GPU3D.RenderDispCnt & (1<<4))
            {
                s32 cov = edgecov;
                if (cov & (1<<31))
                {
                    if (!rightSide) { cov = xcovref >> 5;          if (cov > 31) cov = 31; }
                    else            { cov = 0x1F - (xcovref >> 5); if (cov < 0)  cov = 0;  }
                    xcovref += (edgecov & 0x3FF);
                }
                attr |= (cov << 8);

                // push old pixel down if needed
                if (PA_IS_TOP(pixeladdr))
                {
                    ColorBuffer[pixeladdr+UnderOffset] = ColorBuffer[pixeladdr];
                    DepthBuffer[pixeladdr+UnderOffset] = DepthBuffer[pixeladdr];
                    AttrBuffer[pixeladdr+UnderOffset] = AttrBuffer[pixeladdr];
                }
            }

            DepthBuffer[pixeladdr] = z;
            ColorBuffer[pixeladdr] = color;
            AttrBuffer[pixeladdr] = attr;
        }
        else
        {
            if (!(polygon->Attr & (1<<11))) z = -1;
            PlotTranslucentPixel(pixeladdr, color, z, polyattr, polygon->IsShadow);
            if ((dstattr & 0xF) && (PA_IS_TOP(pixeladdr)))
                PlotTranslucentPixel(pixeladdr+UnderOffset, color, z, polyattr, polygon->IsShadow);
        }
    };
#endif

#ifdef LITEV_SOFT3D_INTERPNEON
    // 4-wide NEON of the diffuse SUBAFFINE INTERPOLATION RAMP (the per-pixel s64
    // accumulator step that SA_STEP_Z / SA_LOAD_RGBST do scalar). The perspective-
    // correct anchor divide every SA_SUB px stays scalar; ONLY the LINEAR step
    // between anchors is vectorized. Fills w<=4 consecutive pixels' z + vr/vg/vb
    // + s/t from the current segment accumulators, then advances them by w steps.
    //   z    -> int64x2 pairs (vaddq via compound-literal, reaches ~2^40)
    //   rgbst-> int32x4      (the linear ramp value fits 32-bit between anchors)
    // Multiplier {1,2,3,4}: the scalar path steps BEFORE reading, so the value at
    // pixel x is base + 1*step (base = accumulator for pixel x-1). Bit-close to the
    // scalar ramp (identical for z; for rgb/st the s32 truncation matches whenever
    // the value fits 32-bit, which the between-anchor linear ramp does). Caller
    // guarantees sa_rem >= w, so all w pixels are step (non-anchor) pixels.
    s32 sa_z4[4]; u32 sa_r4[4], sa_g4[4], sa_b4[4]; s16 sa_s4[4], sa_t4[4];
    auto sa_ramp = [&](int w)
    {
        // z: (sa_z + {1,2,3,4}*sa_dz) >> 16, s64 then narrowed to s32
        int64x2_t z01 = { sa_z + sa_dz,     sa_z + 2*sa_dz };
        int64x2_t z23 = { sa_z + 3*sa_dz,   sa_z + 4*sa_dz };
        int32x4_t zv  = vcombine_s32(vmovn_s64(vshrq_n_s64(z01, SA_FRAC)),
                                     vmovn_s64(vshrq_n_s64(z23, SA_FRAC)));
        vst1q_s32(sa_z4, zv);

        const int32x4_t kmul = { 1, 2, 3, 4 };
        // r/g/b/s/t: (base + kmul*step) >> 16, all s32 (linear ramp fits 32-bit).
        int32x4_t rv = vshrq_n_s32(vmlaq_s32(vdupq_n_s32((s32)sa_r), kmul, vdupq_n_s32((s32)sa_dr)), SA_FRAC);
        int32x4_t gv = vshrq_n_s32(vmlaq_s32(vdupq_n_s32((s32)sa_g), kmul, vdupq_n_s32((s32)sa_dg)), SA_FRAC);
        int32x4_t bv = vshrq_n_s32(vmlaq_s32(vdupq_n_s32((s32)sa_b), kmul, vdupq_n_s32((s32)sa_db)), SA_FRAC);
        int32x4_t sv = vshrq_n_s32(vmlaq_s32(vdupq_n_s32((s32)sa_s), kmul, vdupq_n_s32((s32)sa_ds)), SA_FRAC);
        int32x4_t tv = vshrq_n_s32(vmlaq_s32(vdupq_n_s32((s32)sa_t), kmul, vdupq_n_s32((s32)sa_dt)), SA_FRAC);
        vst1q_u32(sa_r4, vreinterpretq_u32_s32(rv));
        vst1q_u32(sa_g4, vreinterpretq_u32_s32(gv));
        vst1q_u32(sa_b4, vreinterpretq_u32_s32(bv));
        vst1_s16(sa_s4, vmovn_s32(sv));
        vst1_s16(sa_t4, vmovn_s32(tv));

        sa_z += (s64)w * sa_dz; sa_r += (s64)w * sa_dr; sa_g += (s64)w * sa_dg;
        sa_b += (s64)w * sa_db; sa_s += (s64)w * sa_ds; sa_t += (s64)w * sa_dt;
        sa_rem -= w;
    };
#endif
#endif
#endif

    // part 1: left edge
    edge = yedge | 0x1;
    xlimit = xstart+l_edgelen;
    if (xlimit > xend+1) xlimit = xend+1;
    if (xlimit > 256) xlimit = 256;
    if (l_edgecov & (1<<31))
    {
        xcov = (l_edgecov >> 12) & 0x3FF;
        if (xcov == 0x3FF) xcov = 0;
    }

    if (!l_filledge) x = xlimit;
    else {
#ifdef LITEV_SOFT3D_FAST
    sa_rem = 0;
#endif
#if defined(LITEV_SOFT3D_AAFOLD) && defined(LITEV_SOFT3D_NEON)
    // Folded AA edge: for the pure-modulate case (same gate as the interior batch), and only
    // when the edge is wide enough to fill a batch, route the edge pixels through the SHARED
    // branchless shade4 + shared plotEdge instead of the scalar shadeFast + inline plot. The
    // scalar SA interpolation (SA_STEP_Z/SA_LOAD_RGBST) is UNCHANGED, only the shade is
    // batched, so z/vr/vg/vb/s/t are bit-identical and shade4==shadeFast for this case.
    // Depth survivors are buffered and plotted in x-order (columns are framebuffer-independent
    // within a scanline; xcov accumulates only for opaque survivors, in order) -> byte-exact.
    if (f_texcache && !f_decal && !f_toon && !f_highlight && !f_wireframe && !polygon->IsShadow
        && (xlimit - x) >= 4)
    {
        s16 bs[4], bt[4]; u16 bvr[4], bvg[4], bvb[4];
        u32 bpaddr[4], bdstattr[4]; s32 bz[4]; u32 bcolor[4];
        int nb = 0;
        for (; x < xlimit; x++)
        {
            u32 pixeladdr = FirstPixelOffset + (y*RowStride) + x;
            u32 dstattr = AttrBuffer[pixeladdr];

            SA_STEP_Z();

            if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
            {
                if (!(dstattr & 0xF) || PA_IS_UNDER(pixeladdr)) continue;
                pixeladdr += UnderOffset;
                dstattr = AttrBuffer[pixeladdr];
                if (!DTEST(DepthBuffer[pixeladdr], z, dstattr)) continue;
            }

            SA_LOAD_RGBST();

            bs[nb] = s; bt[nb] = t;
            bvr[nb] = (u16)(vr>>3); bvg[nb] = (u16)(vg>>3); bvb[nb] = (u16)(vb>>3);
            bpaddr[nb] = pixeladdr; bdstattr[nb] = dstattr; bz[nb] = z;
            if (++nb == 4)
            {
                shade4(bs, bt, bvr, bvg, bvb, bcolor);
                for (int k = 0; k < 4; k++)
                    plotEdge(bpaddr[k], bz[k], bdstattr[k], bcolor[k], edge, l_edgecov, xcov, false);
                nb = 0;
            }
        }
        for (int i = 0; i < nb; i++)
            plotEdge(bpaddr[i], bz[i], bdstattr[i],
                     shadeFast(bvr[i], bvg[i], bvb[i], bs[i], bt[i]), edge, l_edgecov, xcov, false);
    }
    else
#endif
    for (; x < xlimit; x++)
    {
        u32 pixeladdr = FirstPixelOffset + (y*RowStride) + x;
        u32 dstattr = AttrBuffer[pixeladdr];

        // check stencil buffer for shadows
        if (polygon->IsShadow)
        {
            u8 stencil = StencilBuffer[256*(y&0x1) + x];
            if (!stencil)
                continue;
            if (!(stencil & 0x1))
                pixeladdr += UnderOffset;
            if (!(stencil & 0x2))
                dstattr &= ~0xF; // quick way to prevent drawing the shadow under antialiased edges
        }

#ifdef LITEV_SOFT3D_FAST
        SA_STEP_Z();
#else
        interpX.SetX(x);

        s32 z = interpX.InterpolateZ(zl, zr);
#endif

        // if depth test against the topmost pixel fails, test
        // against the pixel underneath
        if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
        {
            if (!(dstattr & 0xF) || PA_IS_UNDER(pixeladdr)) continue;

            pixeladdr += UnderOffset;
            dstattr = AttrBuffer[pixeladdr];
            if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
                continue;
        }

#ifdef LITEV_SOFT3D_FAST
        SA_LOAD_RGBST();
#else
        u32 vr = interpX.Interpolate(rl, rr);
        u32 vg = interpX.Interpolate(gl, gr);
        u32 vb = interpX.Interpolate(bl, br);

        s16 s = interpX.Interpolate(sl, sr);
        s16 t = interpX.Interpolate(tl, tr);
#endif

#ifdef LITEV_SOFT3D_FAST
        u32 color = shadeFast(vr>>3, vg>>3, vb>>3, s, t);
#else
        u32 color = RenderPixel(polygon, vr>>3, vg>>3, vb>>3, s, t);
#endif
        u8 alpha = color >> 24;

        // alpha test
        if (alpha <= GPU3D.RenderAlphaRef) continue;

        if (alpha == 31)
        {
            u32 attr = polyattr | edge;

            if (GPU3D.RenderDispCnt & (1<<4))
            {
                // anti-aliasing: all edges are rendered

                // calculate coverage
                s32 cov = l_edgecov;
                if (cov & (1<<31))
                {
                    cov = xcov >> 5;
                    if (cov > 31) cov = 31;
                    xcov += (l_edgecov & 0x3FF);
                }
                attr |= (cov << 8);

                // push old pixel down if needed
                if (PA_IS_TOP(pixeladdr))
                {
                    ColorBuffer[pixeladdr+UnderOffset] = ColorBuffer[pixeladdr];
                    DepthBuffer[pixeladdr+UnderOffset] = DepthBuffer[pixeladdr];
                    AttrBuffer[pixeladdr+UnderOffset] = AttrBuffer[pixeladdr];
                }
            }

            DepthBuffer[pixeladdr] = z;
            ColorBuffer[pixeladdr] = color;
            AttrBuffer[pixeladdr] = attr;
        }
        else
        {
            if (!(polygon->Attr & (1<<11))) z = -1;
            PlotTranslucentPixel(pixeladdr, color, z, polyattr, polygon->IsShadow);

            // blend with bottom pixel too, if needed
            if ((dstattr & 0xF) && (PA_IS_TOP(pixeladdr)))
                PlotTranslucentPixel(pixeladdr+UnderOffset, color, z, polyattr, polygon->IsShadow);
        }
    }
    }

    // part 2: polygon inside
    edge = yedge;
    xlimit = xend-r_edgelen+1;
    if (xlimit > xend+1) xlimit = xend+1;
    if (xlimit > 256) xlimit = 256;

    if (wireframe && !edge) x = std::max(x, xlimit);
    else {
#ifdef LITEV_SOFT3D_FAST
    sa_rem = 0;
#endif
#ifdef LITEV_SOFT3D_NEON
    // Batched interior fast path: pure-modulate textured, non-shadow polys (the
    // dominant textured-fill case). Per-pixel depth test + pixeladdr stay scalar;
    // survivors are buffered and flushed 4 at a time through shade4 (batched
    // gather + NEON modulate) then plotted scalar. Bit-identical to the scalar
    // path below. Everything else (shadow / decal / toon / highlight / untextured
    // / no-cache) falls through to the scalar loop.
    if (f_texcache && !f_decal && !f_toon && !f_highlight && !f_wireframe && !polygon->IsShadow)
    {
        s16 bs[4], bt[4];
        u16 bvr[4], bvg[4], bvb[4];
        u32 bpaddr[4], bdstattr[4];
        s32 bz[4];
        u32 bcolor[4];
        int nb = 0;

#ifdef LITEV_SOFT3D_INTERPNEON
        // NEON-ramp interior: compute up to 4 consecutive pixels' z + attributes in
        // one pass (sa_ramp) whenever we are inside a linear segment (sa_rem>=2),
        // then run the scalar per-pixel depth test + survivor buffering off the
        // precomputed arrays. Anchor pixels (sa_rem==0) and 1-px tails fall to the
        // scalar SA_STEP_Z / SA_LOAD_RGBST path (which also refreshes the anchors).
        while (x < xlimit)
        {
            int avail = (int)(xlimit - x);
            int w = (sa_rem < avail) ? sa_rem : avail;
            if (w > 4) w = 4;

            if (w >= 2)
            {
                sa_ramp(w);
                for (int k = 0; k < w; k++, x++)
                {
                    u32 pixeladdr = FirstPixelOffset + (y*RowStride) + x;
                    u32 dstattr = AttrBuffer[pixeladdr];
                    s32 z = sa_z4[k];

#if defined(LITEV_SOFT3D_HANDNEON) && defined(LITEV_SOFT3D_DTEST_BRANCHLESS)
                    // Branchless survivor compaction: compute pass (no data-dependent depth
                    // branch), buffer slot nb UNCONDITIONALLY, then commit via nb += pass. A
                    // failed pixel's slot is overwritten by the next iteration -> never
                    // shaded. The exact 2-level under-retry stays behind the edge-only
                    // (predictable, biased-false) `dstattr & 0xF` test.
                    u32 pass = DepthTestMask(f_dtmode, DepthBuffer[pixeladdr], z, dstattr);
                    if (dstattr & 0xF)
                    {
                        if (!pass && PA_IS_TOP(pixeladdr))
                        {
                            pixeladdr += UnderOffset;
                            dstattr = AttrBuffer[pixeladdr];
                            pass = DepthTestMask(f_dtmode, DepthBuffer[pixeladdr], z, dstattr);
                        }
                    }

                    bs[nb] = sa_s4[k]; bt[nb] = sa_t4[k];
                    bvr[nb] = (u16)(sa_r4[k]>>3); bvg[nb] = (u16)(sa_g4[k]>>3); bvb[nb] = (u16)(sa_b4[k]>>3);
                    bpaddr[nb] = pixeladdr; bdstattr[nb] = dstattr; bz[nb] = z;
                    nb += pass;
                    if (nb == 4)
                    {
                        shade4(bs, bt, bvr, bvg, bvb, bcolor);
                        plot2(bpaddr[0], bz[0], bdstattr[0], bcolor[0]);
                        plot2(bpaddr[1], bz[1], bdstattr[1], bcolor[1]);
                        plot2(bpaddr[2], bz[2], bdstattr[2], bcolor[2]);
                        plot2(bpaddr[3], bz[3], bdstattr[3], bcolor[3]);
                        nb = 0;
                    }
#else
                    if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
                    {
                        if (!(dstattr & 0xF) || PA_IS_UNDER(pixeladdr)) continue;
                        pixeladdr += UnderOffset;
                        dstattr = AttrBuffer[pixeladdr];
                        if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
                            continue;
                    }

                    bs[nb] = sa_s4[k]; bt[nb] = sa_t4[k];
                    bvr[nb] = (u16)(sa_r4[k]>>3); bvg[nb] = (u16)(sa_g4[k]>>3); bvb[nb] = (u16)(sa_b4[k]>>3);
                    bpaddr[nb] = pixeladdr; bdstattr[nb] = dstattr; bz[nb] = z;
                    if (++nb == 4)
                    {
                        shade4(bs, bt, bvr, bvg, bvb, bcolor);
                        plot2(bpaddr[0], bz[0], bdstattr[0], bcolor[0]);
                        plot2(bpaddr[1], bz[1], bdstattr[1], bcolor[1]);
                        plot2(bpaddr[2], bz[2], bdstattr[2], bcolor[2]);
                        plot2(bpaddr[3], bz[3], bdstattr[3], bcolor[3]);
                        nb = 0;
                    }
#endif
                }
            }
            else
            {
                u32 pixeladdr = FirstPixelOffset + (y*RowStride) + x;
                u32 dstattr = AttrBuffer[pixeladdr];

                SA_STEP_Z();

#if defined(LITEV_SOFT3D_HANDNEON) && defined(LITEV_SOFT3D_DTEST_BRANCHLESS)
                u32 pass = DepthTestMask(f_dtmode, DepthBuffer[pixeladdr], z, dstattr);
                if (dstattr & 0xF)
                {
                    if (!pass && PA_IS_TOP(pixeladdr))
                    {
                        pixeladdr += UnderOffset;
                        dstattr = AttrBuffer[pixeladdr];
                        pass = DepthTestMask(f_dtmode, DepthBuffer[pixeladdr], z, dstattr);
                    }
                }

                SA_LOAD_RGBST();

                bs[nb] = s; bt[nb] = t;
                bvr[nb] = (u16)(vr>>3); bvg[nb] = (u16)(vg>>3); bvb[nb] = (u16)(vb>>3);
                bpaddr[nb] = pixeladdr; bdstattr[nb] = dstattr; bz[nb] = z;
                nb += pass;
                if (nb == 4)
                {
                    shade4(bs, bt, bvr, bvg, bvb, bcolor);
                    plot2(bpaddr[0], bz[0], bdstattr[0], bcolor[0]);
                    plot2(bpaddr[1], bz[1], bdstattr[1], bcolor[1]);
                    plot2(bpaddr[2], bz[2], bdstattr[2], bcolor[2]);
                    plot2(bpaddr[3], bz[3], bdstattr[3], bcolor[3]);
                    nb = 0;
                }
                x++;
#else
                if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
                {
                    if (!(dstattr & 0xF) || PA_IS_UNDER(pixeladdr)) { x++; continue; }
                    pixeladdr += UnderOffset;
                    dstattr = AttrBuffer[pixeladdr];
                    if (!DTEST(DepthBuffer[pixeladdr], z, dstattr)) { x++; continue; }
                }

                SA_LOAD_RGBST();

                bs[nb] = s; bt[nb] = t;
                bvr[nb] = (u16)(vr>>3); bvg[nb] = (u16)(vg>>3); bvb[nb] = (u16)(vb>>3);
                bpaddr[nb] = pixeladdr; bdstattr[nb] = dstattr; bz[nb] = z;
                if (++nb == 4)
                {
                    shade4(bs, bt, bvr, bvg, bvb, bcolor);
                    plot2(bpaddr[0], bz[0], bdstattr[0], bcolor[0]);
                    plot2(bpaddr[1], bz[1], bdstattr[1], bcolor[1]);
                    plot2(bpaddr[2], bz[2], bdstattr[2], bcolor[2]);
                    plot2(bpaddr[3], bz[3], bdstattr[3], bcolor[3]);
                    nb = 0;
                }
                x++;
#endif
            }
        }
#else
        for (; x < xlimit; x++)
        {
            u32 pixeladdr = FirstPixelOffset + (y*RowStride) + x;
            u32 dstattr = AttrBuffer[pixeladdr];

            SA_STEP_Z();

#if defined(LITEV_SOFT3D_HANDNEON) && defined(LITEV_SOFT3D_DTEST_BRANCHLESS)
            u32 pass = DepthTestMask(f_dtmode, DepthBuffer[pixeladdr], z, dstattr);
            if (dstattr & 0xF)
            {
                if (!pass && PA_IS_TOP(pixeladdr))
                {
                    pixeladdr += UnderOffset;
                    dstattr = AttrBuffer[pixeladdr];
                    pass = DepthTestMask(f_dtmode, DepthBuffer[pixeladdr], z, dstattr);
                }
            }

            SA_LOAD_RGBST();

            bs[nb] = s; bt[nb] = t;
            bvr[nb] = (u16)(vr>>3); bvg[nb] = (u16)(vg>>3); bvb[nb] = (u16)(vb>>3);
            bpaddr[nb] = pixeladdr; bdstattr[nb] = dstattr; bz[nb] = z;
            nb += pass;
            if (nb == 4)
            {
                shade4(bs, bt, bvr, bvg, bvb, bcolor);
                plot2(bpaddr[0], bz[0], bdstattr[0], bcolor[0]);
                plot2(bpaddr[1], bz[1], bdstattr[1], bcolor[1]);
                plot2(bpaddr[2], bz[2], bdstattr[2], bcolor[2]);
                plot2(bpaddr[3], bz[3], bdstattr[3], bcolor[3]);
                nb = 0;
            }
#else
            if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
            {
                if (!(dstattr & 0xF) || PA_IS_UNDER(pixeladdr)) continue;
                pixeladdr += UnderOffset;
                dstattr = AttrBuffer[pixeladdr];
                if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
                    continue;
            }

            SA_LOAD_RGBST();

            bs[nb] = s; bt[nb] = t;
            bvr[nb] = (u16)(vr>>3); bvg[nb] = (u16)(vg>>3); bvb[nb] = (u16)(vb>>3);
            bpaddr[nb] = pixeladdr; bdstattr[nb] = dstattr; bz[nb] = z;
            if (++nb == 4)
            {
                shade4(bs, bt, bvr, bvg, bvb, bcolor);
                plot2(bpaddr[0], bz[0], bdstattr[0], bcolor[0]);
                plot2(bpaddr[1], bz[1], bdstattr[1], bcolor[1]);
                plot2(bpaddr[2], bz[2], bdstattr[2], bcolor[2]);
                plot2(bpaddr[3], bz[3], bdstattr[3], bcolor[3]);
                nb = 0;
            }
#endif
        }
#endif
        // flush remainder (<4) via the scalar shade
        for (int i = 0; i < nb; i++)
            plot2(bpaddr[i], bz[i], bdstattr[i], shadeFast(bvr[i], bvg[i], bvb[i], bs[i], bt[i]));
    }
    else
#endif
    for (; x < xlimit; x++)
    {
        u32 pixeladdr = FirstPixelOffset + (y*RowStride) + x;
        u32 dstattr = AttrBuffer[pixeladdr];

        // check stencil buffer for shadows
        if (polygon->IsShadow)
        {
            u8 stencil = StencilBuffer[256*(y&0x1) + x];
            if (!stencil)
                continue;
            if (!(stencil & 0x1))
                pixeladdr += UnderOffset;
            if (!(stencil & 0x2))
                dstattr &= ~0xF; // quick way to prevent drawing the shadow under antialiased edges
        }

#ifdef LITEV_SOFT3D_FAST
        SA_STEP_Z();
#else
        interpX.SetX(x);

        s32 z = interpX.InterpolateZ(zl, zr);
#endif

        // if depth test against the topmost pixel fails, test
        // against the pixel underneath
        if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
        {
            if (!(dstattr & 0xF) || PA_IS_UNDER(pixeladdr)) continue;

            pixeladdr += UnderOffset;
            dstattr = AttrBuffer[pixeladdr];
            if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
                continue;
        }

#ifdef LITEV_SOFT3D_FAST
        SA_LOAD_RGBST();
#else
        u32 vr = interpX.Interpolate(rl, rr);
        u32 vg = interpX.Interpolate(gl, gr);
        u32 vb = interpX.Interpolate(bl, br);

        s16 s = interpX.Interpolate(sl, sr);
        s16 t = interpX.Interpolate(tl, tr);
#endif

#ifdef LITEV_SOFT3D_FAST
        u32 color = shadeFast(vr>>3, vg>>3, vb>>3, s, t);
#else
        u32 color = RenderPixel(polygon, vr>>3, vg>>3, vb>>3, s, t);
#endif
        u8 alpha = color >> 24;

        // alpha test
        if (alpha <= GPU3D.RenderAlphaRef) continue;

        if (alpha == 31)
        {
            u32 attr = polyattr | edge;

            if ((GPU3D.RenderDispCnt & (1<<4)) && (attr & 0xF))
            {
                // anti-aliasing: all edges are rendered

                // set coverage to avoid black lines from anti-aliasing
                attr |= (0x1F << 8);

                // push old pixel down if needed
                if (PA_IS_TOP(pixeladdr))
                {
                    ColorBuffer[pixeladdr+UnderOffset] = ColorBuffer[pixeladdr];
                    DepthBuffer[pixeladdr+UnderOffset] = DepthBuffer[pixeladdr];
                    AttrBuffer[pixeladdr+UnderOffset] = AttrBuffer[pixeladdr];
                }
            }

            DepthBuffer[pixeladdr] = z;
            ColorBuffer[pixeladdr] = color;
            AttrBuffer[pixeladdr] = attr;
        }
        else
        {
            if (!(polygon->Attr & (1<<11))) z = -1;
            PlotTranslucentPixel(pixeladdr, color, z, polyattr, polygon->IsShadow);

            // blend with bottom pixel too, if needed
            if ((dstattr & 0xF) && (PA_IS_TOP(pixeladdr)))
                PlotTranslucentPixel(pixeladdr+UnderOffset, color, z, polyattr, polygon->IsShadow);
        }
    }
    }

    // part 3: right edge
    edge = yedge | 0x2;
    xlimit = xend+1;
    if (xlimit > 256) xlimit = 256;
    if (r_edgecov & (1<<31))
    {
        xcov = (r_edgecov >> 12) & 0x3FF;
        if (xcov == 0x3FF) xcov = 0;
    }

    if (r_filledge) {
#ifdef LITEV_SOFT3D_FAST
    sa_rem = 0;
#endif
#if defined(LITEV_SOFT3D_AAFOLD) && defined(LITEV_SOFT3D_NEON)
    // Folded AA edge (right side): same as part 1 but with the right-edge coverage formula
    // (rightSide=true -> cov = 0x1F - (xcov>>5)) and edge flag (yedge|0x2). Byte-exact.
    if (f_texcache && !f_decal && !f_toon && !f_highlight && !f_wireframe && !polygon->IsShadow
        && (xlimit - x) >= 4)
    {
        s16 bs[4], bt[4]; u16 bvr[4], bvg[4], bvb[4];
        u32 bpaddr[4], bdstattr[4]; s32 bz[4]; u32 bcolor[4];
        int nb = 0;
        for (; x < xlimit; x++)
        {
            u32 pixeladdr = FirstPixelOffset + (y*RowStride) + x;
            u32 dstattr = AttrBuffer[pixeladdr];

            SA_STEP_Z();

            if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
            {
                if (!(dstattr & 0xF) || PA_IS_UNDER(pixeladdr)) continue;
                pixeladdr += UnderOffset;
                dstattr = AttrBuffer[pixeladdr];
                if (!DTEST(DepthBuffer[pixeladdr], z, dstattr)) continue;
            }

            SA_LOAD_RGBST();

            bs[nb] = s; bt[nb] = t;
            bvr[nb] = (u16)(vr>>3); bvg[nb] = (u16)(vg>>3); bvb[nb] = (u16)(vb>>3);
            bpaddr[nb] = pixeladdr; bdstattr[nb] = dstattr; bz[nb] = z;
            if (++nb == 4)
            {
                shade4(bs, bt, bvr, bvg, bvb, bcolor);
                for (int k = 0; k < 4; k++)
                    plotEdge(bpaddr[k], bz[k], bdstattr[k], bcolor[k], edge, r_edgecov, xcov, true);
                nb = 0;
            }
        }
        for (int i = 0; i < nb; i++)
            plotEdge(bpaddr[i], bz[i], bdstattr[i],
                     shadeFast(bvr[i], bvg[i], bvb[i], bs[i], bt[i]), edge, r_edgecov, xcov, true);
    }
    else
#endif
    for (; x < xlimit; x++)
    {
        u32 pixeladdr = FirstPixelOffset + (y*RowStride) + x;
        u32 dstattr = AttrBuffer[pixeladdr];

        // check stencil buffer for shadows
        if (polygon->IsShadow)
        {
            u8 stencil = StencilBuffer[256*(y&0x1) + x];
            if (!stencil)
                continue;
            if (!(stencil & 0x1))
                pixeladdr += UnderOffset;
            if (!(stencil & 0x2))
                dstattr &= ~0xF; // quick way to prevent drawing the shadow under antialiased edges
        }

#ifdef LITEV_SOFT3D_FAST
        SA_STEP_Z();
#else
        interpX.SetX(x);

        s32 z = interpX.InterpolateZ(zl, zr);
#endif

        // if depth test against the topmost pixel fails, test
        // against the pixel underneath
        if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
        {
            if (!(dstattr & 0xF) || PA_IS_UNDER(pixeladdr)) continue;

            pixeladdr += UnderOffset;
            dstattr = AttrBuffer[pixeladdr];
            if (!DTEST(DepthBuffer[pixeladdr], z, dstattr))
                continue;
        }

#ifdef LITEV_SOFT3D_FAST
        SA_LOAD_RGBST();
#else
        u32 vr = interpX.Interpolate(rl, rr);
        u32 vg = interpX.Interpolate(gl, gr);
        u32 vb = interpX.Interpolate(bl, br);

        s16 s = interpX.Interpolate(sl, sr);
        s16 t = interpX.Interpolate(tl, tr);
#endif

#ifdef LITEV_SOFT3D_FAST
        u32 color = shadeFast(vr>>3, vg>>3, vb>>3, s, t);
#else
        u32 color = RenderPixel(polygon, vr>>3, vg>>3, vb>>3, s, t);
#endif
        u8 alpha = color >> 24;

        // alpha test
        if (alpha <= GPU3D.RenderAlphaRef) continue;

        if (alpha == 31)
        {
            u32 attr = polyattr | edge;

            if (GPU3D.RenderDispCnt & (1<<4))
            {
                // anti-aliasing: all edges are rendered

                // calculate coverage
                s32 cov = r_edgecov;
                if (cov & (1<<31))
                {
                    cov = 0x1F - (xcov >> 5);
                    if (cov < 0) cov = 0;
                    xcov += (r_edgecov & 0x3FF);
                }
                attr |= (cov << 8);

                // push old pixel down if needed
                if (PA_IS_TOP(pixeladdr))
                {
                    ColorBuffer[pixeladdr+UnderOffset] = ColorBuffer[pixeladdr];
                    DepthBuffer[pixeladdr+UnderOffset] = DepthBuffer[pixeladdr];
                    AttrBuffer[pixeladdr+UnderOffset] = AttrBuffer[pixeladdr];
                }
            }

            DepthBuffer[pixeladdr] = z;
            ColorBuffer[pixeladdr] = color;
            AttrBuffer[pixeladdr] = attr;
        }
        else
        {
            if (!(polygon->Attr & (1<<11))) z = -1;
            PlotTranslucentPixel(pixeladdr, color, z, polyattr, polygon->IsShadow);

            // blend with bottom pixel too, if needed
            if ((dstattr & 0xF) && (PA_IS_TOP(pixeladdr)))
                PlotTranslucentPixel(pixeladdr+UnderOffset, color, z, polyattr, polygon->IsShadow);
        }
    }
    }

    rp->XL = rp->SlopeL.Step();
    rp->XR = rp->SlopeR.Step();
}

#ifdef LITEV_SOFT3D_FAST
#undef SA_SUB
#undef SA_FRAC
#undef SA_STEP_Z
#undef SA_LOAD_RGBST
#endif

void SoftRenderer3D::RenderScanline(s32 y, int npolys)
{
    for (int i = 0; i < npolys; i++)
    {
        RendererPolygon* rp = &PolygonList[i];
        Polygon* polygon = rp->PolyData;

        if (y >= polygon->YTop && (y < polygon->YBottom || (y == polygon->YTop && polygon->YBottom == polygon->YTop)))
        {
            if (polygon->IsShadowMask)
                RenderShadowMaskScanline(rp, y);
            else
                RenderPolygonScanline(rp, y);
        }
    }
}

#ifdef LITEV_SOFT3D_FAST
// Bin PolygonList[0..npolys) indices by YTop (counting sort). Because we fill in
// ascending index order, each per-scanline bucket stays sorted by polygon index
// (== draw order). Polys with YTop>=192 land in the never-walked bucket 192 (they
// cover no on-screen scanline); YTop<0 is clamped to 0 (renders from the top row).
void SoftRenderer3D::AETBuild(int npolys)
{
    s32 cnt[193];
    for (int k = 0; k <= 192; k++) cnt[k] = 0;
    for (int i = 0; i < npolys; i++)
    {
        s32 yt = PolygonList[i].PolyData->YTop;
        if (yt < 0) yt = 0; else if (yt > 192) yt = 192;
        cnt[yt]++;
    }
    s32 acc = 0;
    for (int k = 0; k <= 192; k++) { AET_BucketStart[k] = acc; acc += cnt[k]; }
    s32 fill[193];
    for (int k = 0; k <= 192; k++) fill[k] = AET_BucketStart[k];
    for (int i = 0; i < npolys; i++)
    {
        s32 yt = PolygonList[i].PolyData->YTop;
        if (yt < 0) yt = 0; else if (yt > 192) yt = 192;
        AET_Bucket[fill[yt]++] = i;
    }
}

// Advance the active list from the previous scanline to scanline y:
//   1. drop polys whose YBottom<=y (in place, order preserved),
//   2. merge in polys entering at this row (YTop==y, already index-sorted).
// The active list stays sorted by polygon index so RenderActiveList renders in
// draw order. A polygon covers exactly [YTop, YBottom) (flat polys YTop==YBottom
// enter and render only at y==YTop, then drop the next row) — identical coverage
// to RenderScanline's old per-poly Y-range test.
int SoftRenderer3D::AETAdvance(int nActive, s32 y)
{
    int w = 0;
    for (int r = 0; r < nActive; r++)
    {
        int idx = AET_Active[r];
        if (PolygonList[idx].PolyData->YBottom > y) AET_Active[w++] = idx;
    }
    nActive = w;

    s32 bs = AET_BucketStart[y];
    s32 be = AET_BucketStart[y + 1];
    int b = be - bs;
    if (b > 0)
    {
        // In-place back-merge: existing active + entering bucket (both ascending,
        // disjoint since a poly enters exactly once at its YTop).
        int i = nActive - 1;
        int k = be - 1;
        int wpos = nActive + b - 1;
        while (k >= bs)
        {
            if (i >= 0 && AET_Active[i] > AET_Bucket[k])
                AET_Active[wpos--] = AET_Active[i--];
            else
                AET_Active[wpos--] = AET_Bucket[k--];
        }
        nActive += b;
    }
    return nActive;
}

void SoftRenderer3D::RenderActiveList(s32 y, int nActive)
{
    for (int k = 0; k < nActive; k++)
    {
        RendererPolygon* rp = &PolygonList[AET_Active[k]];
        if (rp->PolyData->IsShadowMask)
            RenderShadowMaskScanline(rp, y);
        else
            RenderPolygonScanline(rp, y);
    }
}
#endif

u32 SoftRenderer3D::CalculateFogDensity(u32 pixeladdr) const
{
#ifdef LITEV_SOFT3D_PIPELINE2
    // Called from ScanlineFinalPass's fog path -> must read the SAME bank it uses
    // (consumer bank under OVERLAP, producer bank otherwise -- compile-time).
  #ifdef LITEV_SOFT3D_OVERLAP
    const u32* const DepthBuffer = this->DepthBuffer + P2ConsumeBank;
  #else
    const u32* const DepthBuffer = this->DepthBuffer + P2RenderBank;
  #endif
#endif
    u32 z = DepthBuffer[pixeladdr];
    u32 densityid, densityfrac;

    if (z < GPU3D.RenderFogOffset)
    {
        densityid = 0;
        densityfrac = 0;
    }
    else
    {
        // technically: Z difference is shifted right by two, then shifted left by fog shift
        // then bit 0-16 are the fractional part and bit 17-31 are the density index
        // on hardware, the final value can overflow the 32-bit range with a shift big enough,
        // causing fog to 'wrap around' and accidentally apply to larger Z ranges

        z -= GPU3D.RenderFogOffset;
        z = (z >> 2) << GPU3D.RenderFogShift;

        densityid = z >> 17;
        if (densityid >= 32)
        {
            densityid = 32;
            densityfrac = 0;
        }
        else
            densityfrac = z & 0x1FFFF;
    }

    // checkme (may be too precise?)
    u32 density =
        ((GPU3D.RenderFogDensityTable[densityid] * (0x20000-densityfrac)) +
         (GPU3D.RenderFogDensityTable[densityid+1] * densityfrac)) >> 17;
    if (density >= 127) density = 128;

    return density;
}

void SoftRenderer3D::ScanlineFinalPass(s32 y)
{
#ifdef LITEV_SOFT3D_PIPELINE2
    // The final pass runs on the CONSUMER side (2D GetLine) only under OVERLAP; otherwise it
    // runs on the PRODUCER side (band phase / STREAM / non-threaded). Pick the matching bank
    // -- compile-time, since which side runs it is fixed by whether OVERLAP is compiled.
    // Under depth-1 both banks are the same frame anyway.
  #ifdef LITEV_SOFT3D_OVERLAP
    P2_SHADOW_CONSUME();
  #else
    P2_SHADOW_RENDER();
  #endif
#endif
    // to consider:
    // clearing all polygon fog flags if the master flag isn't set?
    // merging all final pass loops into one?

    if (GPU3D.RenderDispCnt & (1<<5))
    {
        // edge marking
        // only applied to topmost pixels

        for (int x = 0; x < 256; x++)
        {
            u32 pixeladdr = FirstPixelOffset + (y*RowStride) + x;

            u32 attr = AttrBuffer[pixeladdr];
            if (!(attr & 0xF)) continue;

            u32 polyid = attr >> 24; // opaque polygon IDs are used for edgemarking
            u32 z = DepthBuffer[pixeladdr];

            if (((polyid != (AttrBuffer[pixeladdr-1] >> 24)) && (z < DepthBuffer[pixeladdr-1])) ||
                ((polyid != (AttrBuffer[pixeladdr+1] >> 24)) && (z < DepthBuffer[pixeladdr+1])) ||
                ((polyid != (AttrBuffer[pixeladdr-RowStride] >> 24)) && (z < DepthBuffer[pixeladdr-RowStride])) ||
                ((polyid != (AttrBuffer[pixeladdr+RowStride] >> 24)) && (z < DepthBuffer[pixeladdr+RowStride])))
            {
                u16 edgecolor = GPU3D.RenderEdgeTable[polyid >> 3];
                u32 edgeR = (edgecolor << 1) & 0x3E; if (edgeR) edgeR++;
                u32 edgeG = (edgecolor >> 4) & 0x3E; if (edgeG) edgeG++;
                u32 edgeB = (edgecolor >> 9) & 0x3E; if (edgeB) edgeB++;

                ColorBuffer[pixeladdr] = edgeR | (edgeG << 8) | (edgeB << 16) | (ColorBuffer[pixeladdr] & 0xFF000000);

                // break antialiasing coverage (checkme)
                AttrBuffer[pixeladdr] = (AttrBuffer[pixeladdr] & 0xFFFFE0FF) | 0x00001000;
            }
        }
    }

    if (GPU3D.RenderDispCnt & (1<<7))
    {
        // fog

        // hardware testing shows that the fog step is 0x80000>>SHIFT
        // basically, the depth values used in GBAtek need to be
        // multiplied by 0x200 to match Z-buffer values

        // fog is applied to the topmost two pixels, which is required for
        // proper antialiasing

        // TODO: check the 'fog alpha glitch with small Z' GBAtek talks about

        bool fogcolor = !(GPU3D.RenderDispCnt & (1<<6));

        u32 fogR = (GPU3D.RenderFogColor << 1) & 0x3E; if (fogR) fogR++;
        u32 fogG = (GPU3D.RenderFogColor >> 4) & 0x3E; if (fogG) fogG++;
        u32 fogB = (GPU3D.RenderFogColor >> 9) & 0x3E; if (fogB) fogB++;
        u32 fogA = (GPU3D.RenderFogColor >> 16) & 0x1F;

        for (int x = 0; x < 256; x++)
        {
            u32 pixeladdr = FirstPixelOffset + (y*RowStride) + x;
            u32 density, srccolor, srcR, srcG, srcB, srcA;

            u32 attr = AttrBuffer[pixeladdr];
            if (attr & (1<<15))
            {
                density = CalculateFogDensity(pixeladdr);

                srccolor = ColorBuffer[pixeladdr];
                srcR = srccolor & 0x3F;
                srcG = (srccolor >> 8) & 0x3F;
                srcB = (srccolor >> 16) & 0x3F;
                srcA = (srccolor >> 24) & 0x1F;

                if (fogcolor)
                {
                    srcR = ((fogR * density) + (srcR * (128-density))) >> 7;
                    srcG = ((fogG * density) + (srcG * (128-density))) >> 7;
                    srcB = ((fogB * density) + (srcB * (128-density))) >> 7;
                }

                srcA = ((fogA * density) + (srcA * (128-density))) >> 7;

                ColorBuffer[pixeladdr] = srcR | (srcG << 8) | (srcB << 16) | (srcA << 24);
            }

            // fog for lower pixel
            // TODO: make this code nicer, but avoid using a loop

            if (!(attr & 0xF)) continue;
            pixeladdr += UnderOffset;

            attr = AttrBuffer[pixeladdr];
            if (!(attr & (1<<15))) continue;

            density = CalculateFogDensity(pixeladdr);

            srccolor = ColorBuffer[pixeladdr];
            srcR = srccolor & 0x3F;
            srcG = (srccolor >> 8) & 0x3F;
            srcB = (srccolor >> 16) & 0x3F;
            srcA = (srccolor >> 24) & 0x1F;

            if (fogcolor)
            {
                srcR = ((fogR * density) + (srcR * (128-density))) >> 7;
                srcG = ((fogG * density) + (srcG * (128-density))) >> 7;
                srcB = ((fogB * density) + (srcB * (128-density))) >> 7;
            }

            srcA = ((fogA * density) + (srcA * (128-density))) >> 7;

            ColorBuffer[pixeladdr] = srcR | (srcG << 8) | (srcB << 16) | (srcA << 24);
        }
    }

    if (GPU3D.RenderDispCnt & (1<<4))
    {
        // anti-aliasing

        // edges were flagged and their coverages calculated during rendering
        // this is where such edge pixels are blended with the pixels underneath

        for (int x = 0; x < 256; x++)
        {
            u32 pixeladdr = FirstPixelOffset + (y*RowStride) + x;

            u32 attr = AttrBuffer[pixeladdr];
            if (!(attr & 0xF)) continue;

            u32 coverage = (attr >> 8) & 0x1F;
            if (coverage == 0x1F) continue;

            if (coverage == 0)
            {
                ColorBuffer[pixeladdr] = ColorBuffer[pixeladdr+UnderOffset];
                continue;
            }

            u32 topcolor = ColorBuffer[pixeladdr];
            u32 topR = topcolor & 0x3F;
            u32 topG = (topcolor >> 8) & 0x3F;
            u32 topB = (topcolor >> 16) & 0x3F;
            u32 topA = (topcolor >> 24) & 0x1F;

            u32 botcolor = ColorBuffer[pixeladdr+UnderOffset];
            u32 botR = botcolor & 0x3F;
            u32 botG = (botcolor >> 8) & 0x3F;
            u32 botB = (botcolor >> 16) & 0x3F;
            u32 botA = (botcolor >> 24) & 0x1F;

            coverage++;

            // only blend color if the bottom pixel isn't fully transparent
            if (botA > 0)
            {
                topR = ((topR * coverage) + (botR * (32-coverage))) >> 5;
                topG = ((topG * coverage) + (botG * (32-coverage))) >> 5;
                topB = ((topB * coverage) + (botB * (32-coverage))) >> 5;
            }

            // alpha is always blended
            topA = ((topA * coverage) + (botA * (32-coverage))) >> 5;

            ColorBuffer[pixeladdr] = topR | (topG << 8) | (topB << 16) | (topA << 24);
        }
    }
}

void SoftRenderer3D::ClearBuffers()
{
#ifdef LITEV_SOFT3D_PIPELINE2
    // Clear the bank the raster is about to write (producer side).
    P2_SHADOW_RENDER();
#endif
    u32 clearz = ((GPU3D.RenderClearAttr2 & 0x7FFF) * 0x200) + 0x1FF;
    u32 polyid = GPU3D.RenderClearAttr1 & 0x3F000000; // this sets the opaque polygonID

    // fill screen borders for edge marking

    for (int x = 0; x < ScanlineWidth; x++)
    {
        ColorBuffer[x] = 0;
        DepthBuffer[x] = clearz;
        AttrBuffer[x] = polyid;
    }

    for (int x = RowStride; x < RowStride*193; x+=RowStride)
    {
        ColorBuffer[x] = 0;
        DepthBuffer[x] = clearz;
        AttrBuffer[x] = polyid;
        ColorBuffer[x+257] = 0;
        DepthBuffer[x+257] = clearz;
        AttrBuffer[x+257] = polyid;
    }

    for (int x = RowStride*193; x < RowStride*193 + ScanlineWidth; x++)
    {
        ColorBuffer[x] = 0;
        DepthBuffer[x] = clearz;
        AttrBuffer[x] = polyid;
    }

    // clear the screen

    if (GPU3D.RenderDispCnt & (1<<14))
    {
        u8 xoff = (GPU3D.RenderClearAttr2 >> 16) & 0xFF;
        u8 yoff = (GPU3D.RenderClearAttr2 >> 24) & 0xFF;

        for (int y = 0; y < RowStride*192; y+=RowStride)
        {
            for (int x = 0; x < 256; x++)
            {
                u16 val2 = GPU.ReadVRAMFlat_Texture<u16>(0x40000 + (yoff << 9) + (xoff << 1));
                u16 val3 = GPU.ReadVRAMFlat_Texture<u16>(0x60000 + (yoff << 9) + (xoff << 1));

                // TODO: confirm color conversion
                u32 r = (val2 << 1) & 0x3E; if (r) r++;
                u32 g = (val2 >> 4) & 0x3E; if (g) g++;
                u32 b = (val2 >> 9) & 0x3E; if (b) b++;
                u32 a = (val2 & 0x8000) ? 0x1F000000 : 0;
                u32 color = r | (g << 8) | (b << 16) | a;

                u32 z = ((val3 & 0x7FFF) * 0x200) + 0x1FF;

                u32 pixeladdr = FirstPixelOffset + y + x;
                ColorBuffer[pixeladdr] = color;
                DepthBuffer[pixeladdr] = z;
                AttrBuffer[pixeladdr] = polyid | (val3 & 0x8000);

                xoff++;
            }

            yoff++;
        }
    }
    else
    {
        // TODO: confirm color conversion
        u32 r = (GPU3D.RenderClearAttr1 << 1) & 0x3E; if (r) r++;
        u32 g = (GPU3D.RenderClearAttr1 >> 4) & 0x3E; if (g) g++;
        u32 b = (GPU3D.RenderClearAttr1 >> 9) & 0x3E; if (b) b++;
        u32 a = (GPU3D.RenderClearAttr1 >> 16) & 0x1F;
        u32 color = r | (g << 8) | (b << 16) | (a << 24);

        polyid |= (GPU3D.RenderClearAttr1 & 0x8000);

        for (int y = 0; y < RowStride*192; y+=RowStride)
        {
            for (int x = 0; x < 256; x++)
            {
                u32 pixeladdr = FirstPixelOffset + y + x;
                ColorBuffer[pixeladdr] = color;
                DepthBuffer[pixeladdr] = clearz;
                AttrBuffer[pixeladdr] = polyid;
            }
        }
    }
}

#ifdef LITEV_SOFT3D_BANDTILE
// Copy the already-cleared (by ClearBuffers) real TOP-slot on-screen rows [cy0,cy1) into the
// tile, so the tile carries the correct clear colour/depth/attr (uniform OR clear-image --
// both handled by copying the real cleared buffer) before the raster read-modify-writes it.
// The under-slot is NOT copied in (ClearBuffers never clears it; it is only read after an AA
// push-down writes it within the same chunk, matching the non-tiled path). On-screen cols are
// x in [0,255] -> buffer offset FirstPixelOffset + y*SW + [0..255].
void SoftRenderer3D::BandTileCopyIn(s32 cy0, s32 cy1)
{
    for (s32 y = cy0; y < cy1; y++)
    {
        s32 real = FirstPixelOffset + y * ScanlineWidth;
        s32 tile = FirstPixelOffset + (y - cy0) * ScanlineWidth;
        memcpy(&BtTileColor[tile], &ColorBuffer[real], 256 * sizeof(u32));
        memcpy(&BtTileDepth[tile], &DepthBuffer[real], 256 * sizeof(u32));
        memcpy(&BtTileAttr [tile], &AttrBuffer [real], 256 * sizeof(u32));
    }
}

// Copy the rastered tile rows [cy0,cy1) out to the real framebuffer -- BOTH AA slots (the
// final pass reads the under slot for edge pixels). Real under slot is at +BufferSize; the
// tile under slot is at +BtTileSlot.
void SoftRenderer3D::BandTileCopyOut(s32 cy0, s32 cy1)
{
    for (s32 y = cy0; y < cy1; y++)
    {
        s32 real = FirstPixelOffset + y * ScanlineWidth;
        s32 tile = FirstPixelOffset + (y - cy0) * ScanlineWidth;
        memcpy(&ColorBuffer[real], &BtTileColor[tile], 256 * sizeof(u32));
        memcpy(&DepthBuffer[real], &BtTileDepth[tile], 256 * sizeof(u32));
        memcpy(&AttrBuffer [real], &BtTileAttr [tile], 256 * sizeof(u32));
        memcpy(&ColorBuffer[real + BufferSize], &BtTileColor[tile + BtTileSlot], 256 * sizeof(u32));
        memcpy(&DepthBuffer[real + BufferSize], &BtTileDepth[tile + BtTileSlot], 256 * sizeof(u32));
        memcpy(&AttrBuffer [real + BufferSize], &BtTileAttr [tile + BtTileSlot], 256 * sizeof(u32));
    }
}
#endif

#ifdef LITEV_SOFT3D_BANDED
// One band worker: sets up ALL polygons into this thread's own PolygonList, then
// walks every scanline 0..191 to keep the incremental edge/stencil state correct,
// but only writes ColorBuffer/DepthBuffer/AttrBuffer for its own [y0,y1) rows
// (the y-range gate lives in RenderPolygonScanline / RenderShadowMaskScanline).
// Scanline writes across bands are disjoint, so no shared-write race.
void SoftRenderer3D::RenderBand(Polygon** polygons, int npolys, s32 y0, s32 y1, int bandidx)
{
    BandY0 = y0;
    BandY1 = y1;

#ifdef LITEV_SOFT3D_BANDTILE
    // Default the raster shadow to the REAL buffers (no-op) on this band thread; the
    // FAST chunk loop below re-points them at the cache-resident tile per chunk.
    BtCB = ColorBuffer; BtDB = DepthBuffer; BtAB = AttrBuffer;
    BtSlot = BufferSize; BtTileY0 = 0;
#endif

#ifdef LITEV_SOFT3D_FAST
    // Aim this band thread at its own (persistent) texture cache. Drop last frame's
    // decoded textures only if the texture/palette VRAM changed (else reuse them
    // across frames). Arena memory is always retained.
    CurTexCache = &TexCaches[bandidx];
    if (TexCacheDirty)
    {
        CurTexCache->Used = 0;
        CurTexCache->Count = 0;
    }
#endif

    int j = 0;
    for (int i = 0; i < npolys; i++)
    {
        if (polygons[i]->Degenerate) continue;
#ifdef LITEV_SOFT3D_FAST
        // Lever 3: a band only writes [y0, y1) and its edge/stencil state is
        // thread-local (never shared with other bands), so a polygon that never
        // touches [y0, y1) contributes nothing to this band. Dropping it here
        // shrinks the O(polys * scanlines) per-scanline Y-range scan in
        // RenderScanline (the dominant RenderBand self-cost), and each band ends
        // up owning only ~its share of the polygons.
        {
            s32 pt = polygons[i]->YTop;
            s32 pb = polygons[i]->YBottom;
            bool relevant = (pt == pb) ? (pt >= y0 && pt < y1)   // flat: renders only at YTop
                                       : (pt < y1 && pb > y0);   // spans [YTop, YBottom)
            if (!relevant) continue;
        }
#endif
#ifdef LITEV_SOFT3D_COMPACTVTX
        // Point this poly's RendererPolygon at its block in the shared compact arena
        // (built once by RenderPolygons before this band worker woke), then set it up.
        RendererPolygon* rp = &PolygonList[j++];
        rp->CompactV = &CompactArena[CompactBase[i]];
        SetupPolygon(rp, polygons[i]);
#else
        SetupPolygon(&PolygonList[j++], polygons[i]);
#endif
    }

#ifdef LITEV_SOFT3D_FAST
    // Lever 1: the band only writes [BandY0, BandY1). Rows below BandY0 are still
    // walked to keep the incremental edge state exact (Step-only, see the early
    // gate in RenderPolygonScanline), but rows at/after BandY1 are never used by
    // this band, so stop there instead of re-walking [BandY1, 192).
    // AET (Lever 2): instead of re-scanning all j polygons every scanline for the
    // Y-range test, maintain an active list. A poly enters at its YTop (fresh edge
    // state) and is walked every row of [YTop, YBottom) — so polys starting above
    // this band still get their Step-only fast-forward to BandY0, and in-band polys
    // start fresh at their YTop, exactly as before.
    AETBuild(j);
    int nActive = 0;
#ifdef LITEV_SOFT3D_BANDTILE
    // Cache-resident tiling: retarget the raster to the tile and process [y0,y1) in
    // BtChunkRows chunks (raster chunk into tile -> copy chunk out -> next). Rows < y0 are
    // Step-only (RenderPolygonScanline's BANDED early-out returns before any framebuffer
    // access), so they never touch the tile. Row-ready flags for the 2D consumer are set
    // AFTER copy-out, since the consumer's final pass runs on the real framebuffer.
    BtCB = BtTileColor; BtDB = BtTileDepth; BtAB = BtTileAttr; BtSlot = BtTileSlot;
    s32 chunkStart = y0;
    bool tileActive = false;
    for (s32 y = 0; y < y1; y++)
    {
        if (y >= y0 && !tileActive)
        {
            chunkStart = y;
            BtTileY0 = chunkStart;
            s32 cend = std::min(chunkStart + BtChunkRows, y1);
            BandTileCopyIn(chunkStart, cend);     // seed the tile with the cleared rows
            tileActive = true;
        }
        nActive = AETAdvance(nActive, y);
        RenderActiveList(y, nActive);             // in-band rows read-modify-write the tile
        if (tileActive)
        {
            s32 cend = std::min(chunkStart + BtChunkRows, y1);
            if (y + 1 == cend)
            {
                BandTileCopyOut(chunkStart, cend); // flush the finished chunk to the real fb
#ifdef LITEV_SOFT3D_STREAM
                BandRowProgress[bandidx].store(cend, std::memory_order_release);
#endif
#ifdef LITEV_SOFT3D_OVERLAP
                for (s32 ry = chunkStart; ry < cend; ry++)
                    RowRastered[BandRasterParity][ry].store(1, std::memory_order_release);
#endif
                tileActive = false;
            }
        }
    }
#else
    for (s32 y = 0; y < y1; y++)
    {
        nActive = AETAdvance(nActive, y);
        RenderActiveList(y, nActive);
#ifdef LITEV_SOFT3D_STREAM
        // Rows below y0 are only walked for edge state -- this band WRITES [y0,y1).
        // Publish the watermark so the streaming releaser can hand row y to the 2D.
        if (y >= y0)
            BandRowProgress[bandidx].store(y + 1, std::memory_order_release);
#endif
#ifdef LITEV_SOFT3D_OVERLAP
        // Flag row y ready (release) so the 2D consumer's WaitRowRasteredP sees it.
        if (y >= y0)
            RowRastered[BandRasterParity][y].store(1, std::memory_order_release);
#endif
    }
#endif
#else
    for (s32 y = 0; y < 192; y++)
    {
        RenderScanline(y, j);
#ifdef LITEV_SOFT3D_STREAM
        if (y >= y0)
            BandRowProgress[bandidx].store(y + 1, std::memory_order_release);
#endif
#ifdef LITEV_SOFT3D_OVERLAP
        if (y >= y0)
            RowRastered[BandRasterParity][y].store(1, std::memory_order_release);
#endif
    }
#endif
}

#ifdef LITEV_SOFT3D_STREAM
// Spin until row `row` has been rasterized by the band that owns it. Bands raster
// contiguous ranges [BandRasterBnd[b], BandRasterBnd[b+1]), so locate the owner and
// wait on its watermark. Short spin (the producer is running concurrently on another
// core and is typically only microseconds away), then yield so we never burn a core.
void SoftRenderer3D::WaitRowRastered(s32 row, int nb)
{
    int b = 0;
    while (b < nb - 1 && row >= BandRasterBnd[b + 1]) b++;

    int spins = 0;
    while (BandRowProgress[b].load(std::memory_order_acquire) <= row)
    {
        if (++spins < 256)
            __builtin_arm_yield();
        else
            std::this_thread::yield();
    }
}
#endif

#ifdef LITEV_SOFT3D_OVERLAP
// EMU thread, at the RenderFrame kick: hand this frame a fresh parity slot. Toggling
// before resetting means the slot we clear is NOT the one an in-flight consumer of the
// previous frame is still reading (it reads the opposite parity). For an identical frame
// (no bands run) mark every row ready up-front and record that the consumer must NOT
// re-run the final pass (the buffers are already final from the previous real frame).
void SoftRenderer3D::OverlapKickReset(bool fresh)
{
    LastKickParity ^= 1;
    const int p = LastKickParity;
    RasterFresh[p] = fresh;
    const u8 init = fresh ? 0 : 1;
    for (int y = 0; y < 256; y++)
        RowRastered[p][y].store(init, std::memory_order_relaxed);
    // The Sema_RenderStart post (bands) and the AsyncStart post (consumer) that follow
    // this call on the emu thread publish these stores with release ordering.
}

// CONSUMER (2D async) thread: spin until row `row` of parity `par` is rastered. The
// producing band is running concurrently on another core and is typically only
// microseconds ahead, so spin briefly (yield-hint) then deschedule so we never burn the
// shared emu core. Rows past 191 clamp to the guard border (always ready).
void SoftRenderer3D::WaitRowRasteredP(s32 row, int par)
{
    if (row > 191) row = 191;
    int spins = 0;
    while (!RowRastered[par][row].load(std::memory_order_acquire))
    {
        if (++spins < 256)
            __builtin_arm_yield();
        else
            std::this_thread::yield();
    }
}
#endif
#endif

#ifdef LITEV_SOFT3D_BANDED
// One persistent band worker: a fixed thread, so its thread_local band state
// (PolygonList / StencilBuffer / TexCaches slot via CurTexCache / AET scratch /
// BandY0 / BandY1) is created once and reused across frames (no per-frame realloc).
// Loops: wait my start-sema -> run this frame's job (raster OR final pass) -> post
// my done-sema. Exits when BandPoolRunning is cleared and it's woken by a start.
void SoftRenderer3D::BandWorkerFunc(int idx)
{
    litevPinRenderThread();
#ifdef LITEV_SOFTPROF
    { char nm[16]; snprintf(nm, sizeof(nm), "s3d-band%d", idx); LSP_NAME(nm); }
#endif
    for (;;)
    {
        Platform::Semaphore_Wait(BandStartSema[idx]);
        if (!BandPoolRunning.load(std::memory_order_relaxed))
            return;

        const double _t0 = LitevSP_Now();
        if (BandPhase == 0)
        {
            // Phase 0: rasterize this band's rows (bandidx = idx maps to TexCaches[idx]).
            RenderBand(BandPolygons, BandNumPolys,
                       BandRasterBnd[idx], BandRasterBnd[idx + 1], idx);
            // record this band's cost for the next frame's rebalance (own slot: no race)
            BandLastMs[idx] = LitevSP_Now() - _t0;
            LSP_ADD(S3DBand[idx], BandLastMs[idx]);
        }
        else
        {
            // Phase 1: final pass over this band's rows. Runs only after the raster
            // barrier, so neighbour-row reads (y-1 / y+1) see finished rows.
            for (s32 y = BandFinalBnd[idx]; y < BandFinalBnd[idx + 1]; y++)
                ScanlineFinalPass(y);
            LSP_ADD(S3DFinal[idx], LitevSP_Now() - _t0);
        }

        Platform::Semaphore_Post(BandDoneSema[idx]);
    }
}

// Spawn the pool once. NB from LITEV_BANDS (default 3, clamped 1..8).
// Sweet-spot RETUNED for the 3-raster-core layout (render pinned {0,1,2}, emu core 3):
// during the dominant 3D-raster phase the coordinator + async-2D threads are BLOCKED on
// semaphores (0 cores), so NB=3 band workers fill all 3 raster cores exactly. Cooled
// interleaved headless soft3dfast (Shrek race, fs0, PIN_RENDER=ON 3-core): NB=2 49fps ->
// NB=3 58fps (+18%); NB=4 slightly regresses (57-58, 4 workers oversubscribe 3 cores).
// (Was 2, sweet-spot for the OLD 2-core render layout.)
void SoftRenderer3D::EnsureBandPool()
{
    if (BandPoolRunning.load(std::memory_order_relaxed)) return;

    const char* e = getenv("LITEV_BANDS");
#ifdef LITEV_RENDER_4CORE
    // 4 raster cores available (see litevPinRenderThread): one band per core.
    int n = e ? atoi(e) : 4;
#else
    int n = e ? atoi(e) : 3;
#endif
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    BandPoolNB = n;

    const char* bf = getenv("LITEV_BAND_FINAL");
    BandFinalBanded = !(bf && bf[0] == '0');

    // Adaptive band load balancing (default ON; LITEV_BAND_BALANCE=0 restores the
    // equal-line partition for A/B). Output-neutral either way.
    const char* bb = getenv("LITEV_BAND_BALANCE");
    BandBalance = !(bb && bb[0] == '0');
    BandBndInit = false;
    for (int b = 0; b < 8; b++) { BandLastMs[b] = 0.0; BandEwmaMs[b] = 0.0; }

    for (int b = 0; b < n; b++)
    {
        BandStartSema[b] = Platform::Semaphore_Create();
        BandDoneSema[b]  = Platform::Semaphore_Create();
    }
    BandPoolRunning = true;
    for (int b = 0; b < n; b++)
        BandThreads[b] = Platform::Thread_Create([this, b]() { BandWorkerFunc(b); });
}

// Clean shutdown: clear the running flag, wake every worker so it observes it and
// returns, join, then free the semaphores.
void SoftRenderer3D::ShutdownBandPool()
{
    if (!BandPoolRunning.load(std::memory_order_relaxed)) return;

    BandPoolRunning = false;
    for (int b = 0; b < BandPoolNB; b++)
        Platform::Semaphore_Post(BandStartSema[b]);
    for (int b = 0; b < BandPoolNB; b++)
    {
        Platform::Thread_Wait(BandThreads[b]);
        Platform::Thread_Free(BandThreads[b]);
        BandThreads[b] = nullptr;
    }
    for (int b = 0; b < BandPoolNB; b++)
    {
        Platform::Semaphore_Free(BandStartSema[b]);
        Platform::Semaphore_Free(BandDoneSema[b]);
        BandStartSema[b] = nullptr;
        BandDoneSema[b]  = nullptr;
    }
    BandPoolNB = 0;
}
#endif

#ifdef LITEV_SOFT3D_BANDED
// Recompute BandRasterBnd[0..nb] so that each band's MEASURED raster time converges
// to the mean. Runs on the 3D render thread, before the workers are woken.
//
// Model: attribute the previous frame's band cost uniformly across the rows that band
// owned (piecewise-constant per-row density), then re-cut [0,192) at equal-cost
// prefixes. Iterating this each frame converges on equal band times even though the
// true per-row cost is not uniform (the fixed edge-state fast-forward is folded into
// the density and the feedback loop absorbs the model error).
//
// Output is unaffected: bands write disjoint rows and each fast-forwards its edge
// state from row 0, so any partition renders the same framebuffer.
void SoftRenderer3D::RebalanceBands(int nb)
{
    if (!BandBalance || nb < 2)
    {
        for (int b = 0; b <= nb; b++) BandRasterBnd[b] = (192 * b) / nb;
        return;
    }

    if (!BandBndInit)
    {
        for (int b = 0; b <= nb; b++) BandRasterBnd[b] = (192 * b) / nb;
        BandBndInit = true;
        return;
    }

    // EWMA the measured band costs (scene load changes gradually; damp frame noise).
    double total = 0.0;
    for (int b = 0; b < nb; b++)
    {
        if (BandLastMs[b] > 0.0)
            BandEwmaMs[b] = (BandEwmaMs[b] <= 0.0) ? BandLastMs[b]
                                                   : (0.75 * BandEwmaMs[b] + 0.25 * BandLastMs[b]);
        total += BandEwmaMs[b];
    }
    if (total <= 0.0) return;

    // Per-row cost density from the CURRENT partition.
    double dens[192];
    for (int b = 0; b < nb; b++)
    {
        s32 y0 = BandRasterBnd[b], y1 = BandRasterBnd[b + 1];
        s32 rows = y1 - y0;
        if (rows <= 0) continue;
        double d = BandEwmaMs[b] / (double)rows;
        for (s32 y = y0; y < y1; y++) dens[y] = d;
    }

    // Re-cut at equal-cost prefixes.
    const double target = total / (double)nb;
    s32 nbnd[9];
    nbnd[0] = 0;
    int cut = 1;
    double acc = 0.0;
    for (s32 y = 0; y < 192 && cut < nb; y++)
    {
        acc += dens[y];
        if (acc >= target * (double)cut)
            nbnd[cut++] = y + 1;
    }
    while (cut <= nb) nbnd[cut++] = 192;
    nbnd[nb] = 192;

    // Enforce monotonic, >=MINROWS-per-band (keeps every worker useful and bounds the
    // per-band fixed overhead from dominating).
    const s32 MINROWS = 8;
    for (int b = 1; b < nb; b++)
    {
        if (nbnd[b] < nbnd[b - 1] + MINROWS) nbnd[b] = nbnd[b - 1] + MINROWS;
    }
    for (int b = nb - 1; b >= 1; b--)
    {
        if (nbnd[b] > nbnd[b + 1] - MINROWS) nbnd[b] = nbnd[b + 1] - MINROWS;
        if (nbnd[b] < 0) nbnd[b] = 0;
    }

    for (int b = 0; b <= nb; b++) BandRasterBnd[b] = nbnd[b];
}
#endif

void SoftRenderer3D::RenderPolygons(bool threaded, Polygon** polygons, int npolys)
{
#ifdef LITEV_SOFT3D_BANDTILE
    // The non-banded raster path below (and any synchronous frame) runs on THIS thread and
    // does not set up a tile; point the raster shadow at the real buffers so it is a no-op.
    // (The banded path re-inits these per band thread inside RenderBand.)
    BtCB = ColorBuffer; BtDB = DepthBuffer; BtAB = AttrBuffer;
    BtSlot = BufferSize; BtTileY0 = 0;
#endif
    // DIAGNOSTIC (throwaway): LITEV_SKIP3D skips the raster but still posts the 192
    // scanline semaphores so the emu thread's per-scanline GetLine doesn't hang.
    // Isolates the single-threaded 3D-raster cost from the rest of the frame.
    static const bool _skip3d = getenv("LITEV_SKIP3D") != nullptr;
    if (_skip3d)
    {
        if (threaded)
#ifdef LITEV_SOFT3D_OVERLAP
            // Overlap mode reads RowRastered, not Sema_ScanlineCount: mark all rows ready
            // so the consumer's GetLine doesn't hang (the final pass runs on garbage, but
            // LITEV_SKIP3D is a throwaway raster-cost isolation diagnostic).
            for (int k = 0; k < 192; k++)
                RowRastered[BandRasterParity][k].store(1, std::memory_order_release);
#else
            for (int k = 0; k < 192; k++) Platform::Semaphore_Post(Sema_ScanlineCount);
#endif
        return;
    }

#ifdef LITEV_SOFT3D_BANDED
    if (threaded)
    {
        // Parallel banded 3D raster. ClearBuffers() has already run on the render
        // thread. Split the 192 scanlines into N contiguous bands; each band walks
        // all scanlines (edge state) but only rasterizes its own rows.
        // PERSISTENT band-worker pool (DraStic-style): NB workers are spawned ONCE
        // (below), not per frame. Band count is tunable (LITEV_BANDS, default 3).
        // With the render threads pinned to 3 cores {0,1,2} (emu on core 3), NB=3 is
        // the sweet spot: during the raster phase the coordinator + async-2D threads
        // are blocked on semaphores, so 3 band workers fill all 3 raster cores. NB=4
        // oversubscribes (4 workers, 3 cores) and slightly regresses. The old code
        // spawned/joined std::threads every frame here, which added spawn cost +
        // scheduler contention while the emu thread waited at the GetLine barrier.
        EnsureBandPool();
        const int NB = BandPoolNB;
#ifdef LITEV_SOFTPROF
        LitevSP::S.NB3D.store(NB, std::memory_order_relaxed);
#endif

        // Publish this frame's raster args, then wake all workers and wait them out.
        const double _tr0 = LSP_NOW();
        // Adaptive row partition (equal-line bands are ~2x imbalanced on a real scene).
        RebalanceBands(NB);
        BandPolygons = polygons;
        BandNumPolys = npolys;
        BandPhase = 0;
#ifdef LITEV_SOFT3D_COMPACTVTX
        // Build the compact vertex buffer ONCE here (render/coordinator thread), before
        // any band worker is woken. The scattered fat-vertex cold-touch is paid a single
        // time (not once per band); the band StartSema post/wait publishes it. Phase 2
        // (final pass) reuses the same PolygonList without re-touching vertices, so the
        // build is not repeated for BandPhase==1.
        BuildCompactVtx(polygons, npolys);
#endif
#ifdef LITEV_SOFT3D_OVERLAP
        // Consumer-driven overlap: the bands flag rows into RowRastered[BandRasterParity]
        // (already reset for this parity by the emu at the kick, and latched into
        // BandRasterParity at the render-thread wake). Wake the bands and wait out the
        // raster barrier -- so FinishRendering's Sema_RenderDone still means the whole
        // frame is rastered before its single-buffered ColorBuffer can be reused -- but
        // run NO final pass and post NO scanline semaphores here: the 2D consumer
        // (GetLine) runs ScanlineFinalPass itself as it reaches each row, so the 2D
        // composite pipelines behind the bands instead of sitting blocked for the whole
        // raster. The final pass is bit-identical: GetLine walks rows in order and waits
        // rows y and y+1 rastered, exactly the neighbours the barriered final pass sees.
        for (int b = 0; b < NB; b++) Platform::Semaphore_Post(BandStartSema[b]);
        for (int b = 0; b < NB; b++) Platform::Semaphore_Wait(BandDoneSema[b]);
        LSP_ADD(S3DRaster, LSP_NOW() - _tr0);
        return;
#elif defined(LITEV_SOFT3D_STREAM)
        // Reset the row watermarks BEFORE waking the workers.
        for (int b = 0; b < NB; b++)
            BandRowProgress[b].store(BandRasterBnd[b], std::memory_order_relaxed);
        for (int b = 0; b < NB; b++) Platform::Semaphore_Post(BandStartSema[b]);

        // STREAM: do NOT wait out the raster barrier. Release each scanline to the 2D
        // consumer (GetLine) the moment it -- and its forward neighbour, which
        // ScanlineFinalPass reads -- have been rastered. Walking y in order also
        // guarantees row y-1 is already done (we waited on it last iteration), so the
        // final pass sees exactly the same neighbours as the barriered path => output
        // is bit-identical. The 2D composite now pipelines behind the bands instead of
        // sitting BLOCKED for the entire raster (8.7ms/frame measured).
        for (s32 y = 0; y < 192; y++)
        {
            WaitRowRastered(y < 191 ? y + 1 : 191, NB);
            ScanlineFinalPass(y);
            Platform::Semaphore_Post(Sema_ScanlineCount);
        }
        // Every band has passed its last row by construction; join them.
        for (int b = 0; b < NB; b++) Platform::Semaphore_Wait(BandDoneSema[b]);
        LSP_ADD(S3DRaster, LSP_NOW() - _tr0);
        return;
#else
        for (int b = 0; b < NB; b++) Platform::Semaphore_Post(BandStartSema[b]);
        for (int b = 0; b < NB; b++) Platform::Semaphore_Wait(BandDoneSema[b]);
        LSP_ADD(S3DRaster, LSP_NOW() - _tr0);
        // --- raster barrier: every band's rows are now fully written ---
#endif

        // Phase 2: the per-scanline final pass (edge marking / fog / anti-aliasing)
        // reads neighbouring scanlines, so it must run only after ALL bands have
        // finished rasterizing. Historically this was serial because banding it via
        // per-frame thread spawn REGRESSED (spawn cost > the ~3.5ms savings). With
        // the persistent pool that spawn cost is gone, so band the final pass across
        // the SAME workers (each does its own disjoint [y0,y1) rows; neighbour reads
        // are safe post-barrier), then release all 192 scanlines to the emu thread.
        if (BandFinalBanded)
        {
            const double _tf0 = LSP_NOW();
            for (int b = 0; b <= NB; b++) BandFinalBnd[b] = (192 * b) / NB;
            BandPhase = 1;
            for (int b = 0; b < NB; b++) Platform::Semaphore_Post(BandStartSema[b]);
            for (int b = 0; b < NB; b++) Platform::Semaphore_Wait(BandDoneSema[b]);
            LSP_ADD(S3DFinalWall, LSP_NOW() - _tf0);
            for (int k = 0; k < 192; k++) Platform::Semaphore_Post(Sema_ScanlineCount);
        }
        else
        {
            // Fallback (LITEV_BAND_FINAL=0): serial final pass, posting each scanline
            // as it completes so the emu thread's GetLine can overlap the tail.
            for (s32 y = 0; y < 192; y++)
            {
                ScanlineFinalPass(y);
                Platform::Semaphore_Post(Sema_ScanlineCount);
            }
        }
        return;
    }
#endif

#ifdef LITEV_SOFT3D_FAST
    // New frame (non-banded path): use band slot 0; drop textures only if VRAM changed.
    CurTexCache = &TexCaches[0];
    if (TexCacheDirty)
    {
        CurTexCache->Used = 0;
        CurTexCache->Count = 0;
    }
#endif

#ifdef LITEV_SOFT3D_COMPACTVTX
    // Synchronous single-threaded path: build the compact buffer once, up front.
    BuildCompactVtx(polygons, npolys);
#endif
    int j = 0;
    for (int i = 0; i < npolys; i++)
    {
        if (polygons[i]->Degenerate) continue;
#ifdef LITEV_SOFT3D_COMPACTVTX
        RendererPolygon* rp = &PolygonList[j++];
        rp->CompactV = &CompactArena[CompactBase[i]];
        SetupPolygon(rp, polygons[i]);
#else
        SetupPolygon(&PolygonList[j++], polygons[i]);
#endif
    }

#ifdef LITEV_SOFT3D_FAST
    // AET (Lever 2): drive the scanline loop from an active list instead of the
    // O(npolys) per-scanline Y-range re-scan. See AETBuild/AETAdvance.
    AETBuild(j);
    int nActive = 0;
    nActive = AETAdvance(nActive, 0);
    RenderActiveList(0, nActive);

    for (s32 y = 1; y < 192; y++)
    {
        nActive = AETAdvance(nActive, y);
        RenderActiveList(y, nActive);
        ScanlineFinalPass(y-1);

        if (threaded)
            // Notify the main thread that we're done with a scanline.
            Platform::Semaphore_Post(Sema_ScanlineCount);
    }
#else
    RenderScanline(0, j);

    for (s32 y = 1; y < 192; y++)
    {
        RenderScanline(y, j);
        ScanlineFinalPass(y-1);

        if (threaded)
            // Notify the main thread that we're done with a scanline.
            Platform::Semaphore_Post(Sema_ScanlineCount);
    }
#endif

    ScanlineFinalPass(191);

    if (threaded)
        // If this renderer is threaded, notify the main thread that we're done with the frame.
        Platform::Semaphore_Post(Sema_ScanlineCount);
}

void SoftRenderer3D::FinishRendering()
{
    if (RenderThreadRunning.load(std::memory_order_relaxed) && !GPU3D.AbortFrame)
    {
        // THE emu-thread render barrier: blocks until the 3D render thread has
        // finished the WHOLE frame (clear + banded raster + final pass).
        const double _t0 = LSP_NOW();
#ifdef LITEV_SOFTPROF
        {
            LitevSP::S.NFinish.fetch_add(1, std::memory_order_relaxed);
            double post = LitevSP::S.T3DPost.load(std::memory_order_relaxed);
            if (post > 0.0) LSP_ADD(EmuWindow, _t0 - post);
        }
#endif
        Platform::Semaphore_Wait(Sema_RenderDone);
        LSP_ADD(Emu3DBarrier, LSP_NOW() - _t0);
    }
}

#ifdef LITEV_SOFT3D_ASYNC
// True if the derived dirty set has any bit set (i.e. MakeVRAMFlat_* would WRITE).
template <typename BF>
static inline bool LitevAnyDirty(const BF& bf)
{
    for (u32 i = 0; i < BF::DataLength; i++)
        if (bf.Data[i]) return true;
    return false;
}
#endif

void SoftRenderer3D::RenderFrame()
{
    auto textureDirty = GPU.VRAMDirty_Texture.DeriveState(GPU.VRAMMap_Texture, GPU);
    auto texPalDirty = GPU.VRAMDirty_TexPal.DeriveState(GPU.VRAMMap_TexPal, GPU);

#ifdef LITEV_SOFT3D_ASYNC
    // The MakeVRAMFlat_* calls below WRITE the flat texture/palette buffers that an
    // in-flight async raster is still reading. Barrier only when there is actually
    // something to write — a frame that dirtied no texture VRAM writes nothing, so
    // the raster is free to keep running.
    if (RenderThreadRunning.load(std::memory_order_relaxed)
        && (LitevAnyDirty(textureDirty) || LitevAnyDirty(texPalDirty)))
        FinishRendering();
#endif

    bool textureChanged = GPU.MakeVRAMFlat_TextureCoherent(textureDirty);
    bool texPalChanged = GPU.MakeVRAMFlat_TexPalCoherent(texPalDirty);

#ifdef LITEV_SOFT3D_FAST
    // Decode-once cache is valid across frames while the texture/palette VRAM is
    // unchanged (decoded texels are a pure function of that VRAM). Only invalidate
    // when it actually changed, so a static scene decodes each texture just once and
    // amortizes it over many frames (per-frame full re-decode was a net loss).
    TexCacheDirty = textureChanged || texPalChanged;
#endif

    FrameIdentical = !(textureChanged || texPalChanged) && GPU3D.RenderFrameIdentical;

    if (RenderThreadRunning.load(std::memory_order_relaxed))
    {
#ifdef LITEV_SOFT3D_PIPELINE2
        // Assign this frame's 3D-plane bank parity (emu), BEFORE the Sema_RenderStart post
        // (which publishes it to the render thread's P2RenderBank latch) and before this
        // frame's later AsyncStart post (which publishes P2ConsumeBank to the 2D consumer).
        // Toggle ONLY on a real (non-identical) frame: a FrameIdentical frame does NOT
        // raster, so its consumer must keep reading the bank the last REAL frame wrote (unlike
        // OVERLAP, whose single ColorBuffer holds that data regardless of parity, our banks
        // are per-frame -- toggling here would point the consumer at a stale bank).
        if (!FrameIdentical)
        {
            // Part 3: cycle the 3D-plane bank parity mod-3 (see GPU3D_Soft.h -- the 3D
            // raster leads its 2D consumer by ~1 frame, so a 2-bank scheme would reuse a
            // bank still being consumed under depth-2). Byte-identical at depth-1.
            P2KickParity = (P2KickParity + 1) % 3;
#ifdef LITEV_RENDER_THREAD
            // Part 1 (flat-VRAM parity snapshot, 3D side): the just-made-coherent flat
            // texture VRAM (MakeVRAMFlat_Texture/TexPalCoherent above) is snapshotted into
            // this frame's texture-shadow bank of the R4 A/B (mod-2) shadow. The 3D raster
            // reads it via GPU.ReadVRAMFlat_Texture (redirected at the render-thread wake
            // below), so under depth-2 the emu's next-frame coherence writes the OTHER
            // texture bank and cannot corrupt the bytes the in-flight raster is reading.
            // ~640 KB/frame (~0.1 ms). The texture shadow is 2-bank (serial 3D raster, <=1
            // outstanding) -> key on P2KickParity & 1, independent of the mod-3 plane bank.
            // Byte-identical at depth-1 (shadow == live: no mutation during the barriered
            // render). Reuses the proven R4 STEP-2 double buffer (GPU.h).
            GPU.SnapshotTexShadow(P2KickParity & 1);
#endif
        }
#endif
#ifdef LITEV_SOFT3D_OVERLAP
        // Assign + reset this frame's parity slot on the EMU thread, BEFORE the
        // Sema_RenderStart post below (which publishes it to the render thread) and
        // before this frame's later AsyncStart post (which publishes it to the 2D
        // consumer). Resetting the freshly-toggled slot cannot disturb the opposite
        // slot an in-flight consumer of the previous frame is still reading.
        OverlapKickReset(!FrameIdentical);
#endif
#ifdef LITEV_SOFTPROF
        LitevSP::S.T3DPost.store(LitevSP::NowMs(), std::memory_order_relaxed);
        LitevSP::S.NPost.fetch_add(1, std::memory_order_relaxed);
#endif
        // "Render thread, you're up! Get moving."
        Platform::Semaphore_Post(Sema_RenderStart);
    }
    else if (!FrameIdentical)
    {
#ifdef LITEV_SOFT3D_PIPELINE2
        // Non-threaded (synchronous) path: everything on bank 0 (raster + GetLine, same
        // thread, one frame live). Keep the parity/banks pinned to 0 for consistency, and
        // read the LIVE flat texture VRAM (no shadow indirection -- synchronous, coherent).
        P2KickParity = 0; P2RenderParity = 0; P2RenderBank = 0; P2ConsumeBank = 0;
#ifdef LITEV_RENDER_THREAD
        GPU.SetTexReadShadow(false, 0);
#endif
#endif
        ClearBuffers();
        RenderPolygons(false, &GPU3D.RenderPolygonRAM[0], GPU3D.RenderNumPolygons);
    }
}

void SoftRenderer3D::RestartFrame()
{
    SetupRenderThread();
    EnableRenderThread();
}

void SoftRenderer3D::RenderThreadFunc()
{
    litevPinRenderThread();
    LSP_NAME("s3d-rt");
    for (;;)
    {
        // Wait for a notice from the main thread to start rendering (or to stop entirely).
        Platform::Semaphore_Wait(Sema_RenderStart);
        if (!RenderThreadRunning) return;
#ifdef LITEV_SOFT3D_PIPELINE2
        // Latch the 3D-plane bank this raster (+ its ClearBuffers + producer-side final
        // pass) writes, the instant we wake -- before the emu can kick the next frame
        // (FIFO-paced by Sema_RenderStart; <=1 raster outstanding, so P2KickParity is still
        // ours). The bands/ClearBuffers/final pass read P2RenderBank; the start-sema post
        // publishes it (release) to the band workers (acquire).
        P2RenderParity = P2KickParity;
        P2RenderBank = P2RenderParity * P2BankStride;
#ifdef LITEV_RENDER_THREAD
        // Part 1: point GPU.ReadVRAMFlat_Texture/TexPal at THIS raster's flat-texture shadow
        // bank (snapshotted at the kick). The 3D sampler + clear-image read reads the frozen
        // VCount-215 bytes; the emu's next-frame SnapshotTexShadow writes the OTHER bank. The
        // start-sema post publishes this pointer (release) to the band workers (acquire).
        // Texture shadow is mod-2 (serial raster) -> & 1.
        GPU.SetTexReadShadow(true, P2RenderParity & 1);
#endif
#endif
#ifdef LITEV_SOFT3D_OVERLAP
        // Latch this raster's parity slot the instant we wake -- before the emu can kick
        // the next frame (a full ~16ms away; kicks are FIFO-paced by Sema_RenderStart and
        // this pipeline keeps <=1 raster outstanding, so LastKickParity is still ours).
        // The bands read BandRasterParity when RenderPolygons posts their start-semas.
        BandRasterParity = LastKickParity;
#endif
#ifdef LITEV_SOFTPROF
        {
            double now = LitevSP::NowMs();
            LitevSP::S.T3DStart.store(now, std::memory_order_relaxed);
            double post = LitevSP::S.T3DPost.load(std::memory_order_relaxed);
            if (post > 0.0) LSP_ADD(S3DWake, now - post);
        }
#endif

        // Protect the GPU state from the main thread.
        // Some melonDS frontends (though not ours)
        // will repeatedly save or load states;
        // if they do so while the render thread is busy here,
        // the ensuing race conditions may cause a crash
        // (since some of the GPU state includes pointers).
        RenderThreadRendering = true;
        if (FrameIdentical)
        { // If no rendering is needed, just say we're done.
#ifdef LITEV_SOFTPROF
            LitevSP::S.NIdent.fetch_add(1, std::memory_order_relaxed);
#endif
#ifdef LITEV_SOFT3D_OVERLAP
            // Overlap mode uses RowRastered, not Sema_ScanlineCount. The emu already
            // marked every row of this parity ready (init=1) at the kick, so the
            // consumer's GetLine returns immediately with no final pass (RasterFresh
            // is false: the buffers are still final from the previous real frame).
#else
            Platform::Semaphore_Post(Sema_ScanlineCount, 192);
#endif
        }
        else
        {
#ifdef LITEV_SOFTPROF
            LitevSP::S.NRender.fetch_add(1, std::memory_order_relaxed);
#endif
            const double _tc0 = LSP_NOW();
            ClearBuffers();
            LSP_ADD(S3DClear, LSP_NOW() - _tc0);
            RenderPolygons(true, &GPU3D.RenderPolygonRAM[0], GPU3D.RenderNumPolygons);
        }

#ifdef LITEV_SOFTPROF
        {
            double post = LitevSP::S.T3DPost.load(std::memory_order_relaxed);
            if (post > 0.0) LSP_ADD(S3DTotal, LitevSP::NowMs() - post);
        }
#endif
        // Tell the main thread that we're done rendering
        // and that it's safe to access the GPU state again.
        Platform::Semaphore_Post(Sema_RenderDone);

        RenderThreadRendering = false;
    }
}

u32* SoftRenderer3D::GetLine(int line)
{
    if (GPU3D.AbortFrame)
    {
        // TODO this isn't accurate
        memset(ScrolledLine, 0, sizeof(ScrolledLine));
        return ScrolledLine;
    }

    if (RenderThreadRunning.load(std::memory_order_relaxed))
    {
        if (line < 192)
        {
#ifdef LITEV_SOFT3D_OVERLAP
            // Consumer-driven overlap: instead of blocking on one big semaphore posted
            // after the whole raster+final, wait only until THIS row and its forward
            // neighbour are rastered, run the final pass here, and return -- so the 2D
            // composite pipelines behind the bands. This thread is pinned to the emu's
            // idle core 3; the bands keep {0,1,2}. Called strictly in order (0..191) by
            // the single-band async 2D thread, so row line-1 was rastered + final-passed
            // on the previous call -- ScanlineFinalPass's line-1/line/line+1 neighbours
            // are all ready and the output is bit-identical to the barriered final pass
            // (the per-row final pass has no cross-row write dependency: it preserves the
            // polyid bits its neighbour test reads and never touches neighbour depth).
            const int cp = ConsumeParity;
            WaitRowRasteredP(line, cp);
            if (line < 191) WaitRowRasteredP(line + 1, cp);
            if (RasterFresh[cp])
                ScanlineFinalPass(line);
#else
            // We need a scanline, so let's wait for the render thread to finish it.
            // (both threads process scanlines from top-to-bottom,
            // so we don't need to wait for a specific row)
            Platform::Semaphore_Wait(Sema_ScanlineCount);
#endif
        }
    }

#ifdef LITEV_SOFT3D_PIPELINE2
    // Consumer: read the bank the emu latched for this frame at VBlank (P2ConsumeBank).
    u32* rawline = &ColorBuffer[P2ConsumeBank + (line * RowStride) + FirstPixelOffset];
#else
    u32* rawline = &ColorBuffer[(line * RowStride) + FirstPixelOffset];
#endif
    u16 xpos = GPU3D.RenderXPos;
    if (xpos == 0)
        return rawline;

    // apply X scroll

    if (xpos & 0x100)
    {
        int i = 0, j = xpos;
        for (; j < 512; i++, j++)
            ScrolledLine[i] = 0;
        for (j = 0; i < 256; i++, j++)
            ScrolledLine[i] = rawline[j];
    }
    else
    {
        int i = 0, j = xpos;
        for (; j < 256; i++, j++)
            ScrolledLine[i] = rawline[j];
        for (; i < 256; i++)
            ScrolledLine[i] = 0;
    }

    return ScrolledLine;
}

}
