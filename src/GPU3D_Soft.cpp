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
#include <stdio.h>
#include <string.h>
#if defined(LITEV_SOFT3D_FAST) && (defined(__ARM_NEON) || defined(__aarch64__))
#include <arm_neon.h>
#define LITEV_SOFT3D_NEON 1
#endif
#include "NDS.h"
#include "GPU.h"

namespace melonDS
{

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
    memset(ColorBuffer, 0, BufferSize * 2 * 4);
    memset(DepthBuffer, 0, BufferSize * 2 * 4);
    memset(AttrBuffer, 0, BufferSize * 2 * 4);

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

void SoftRenderer3D::SetupPolygonLeftEdge(SoftRenderer3D::RendererPolygon* rp, s32 y) const
{
    Polygon* polygon = rp->PolyData;

    while (y >= polygon->Vertices[rp->NextVL]->FinalPosition[1] && rp->CurVL != polygon->VBottom)
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

    rp->XL = rp->SlopeL.Setup(polygon->Vertices[rp->CurVL]->FinalPosition[0], polygon->Vertices[rp->NextVL]->FinalPosition[0],
                              polygon->Vertices[rp->CurVL]->FinalPosition[1], polygon->Vertices[rp->NextVL]->FinalPosition[1],
                              polygon->FinalW[rp->CurVL], polygon->FinalW[rp->NextVL], y, polygon->WBuffer);
}

void SoftRenderer3D::SetupPolygonRightEdge(SoftRenderer3D::RendererPolygon* rp, s32 y) const
{
    Polygon* polygon = rp->PolyData;

    while (y >= polygon->Vertices[rp->NextVR]->FinalPosition[1] && rp->CurVR != polygon->VBottom)
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

    rp->XR = rp->SlopeR.Setup(polygon->Vertices[rp->CurVR]->FinalPosition[0], polygon->Vertices[rp->NextVR]->FinalPosition[0],
                              polygon->Vertices[rp->CurVR]->FinalPosition[1], polygon->Vertices[rp->NextVR]->FinalPosition[1],
                              polygon->FinalW[rp->CurVR], polygon->FinalW[rp->NextVR], y, polygon->WBuffer);
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
        if (polygon->Vertices[i]->FinalPosition[0] < polygon->Vertices[vtop]->FinalPosition[0]) vtop = i;
        if (polygon->Vertices[i]->FinalPosition[0] > polygon->Vertices[vbot]->FinalPosition[0]) vbot = i;

        i = nverts - 1;
        if (polygon->Vertices[i]->FinalPosition[0] < polygon->Vertices[vtop]->FinalPosition[0]) vtop = i;
        if (polygon->Vertices[i]->FinalPosition[0] > polygon->Vertices[vbot]->FinalPosition[0]) vbot = i;

        rp->CurVL = vtop; rp->NextVL = vtop;
        rp->CurVR = vbot; rp->NextVR = vbot;

        rp->XL = rp->SlopeL.SetupDummy(polygon->Vertices[rp->CurVL]->FinalPosition[0], polygon->WBuffer);
        rp->XR = rp->SlopeR.SetupDummy(polygon->Vertices[rp->CurVR]->FinalPosition[0], polygon->WBuffer);
    }
    else
    {
        SetupPolygonLeftEdge(rp, ytop);
        SetupPolygonRightEdge(rp, ytop);
    }
}

void SoftRenderer3D::RenderShadowMaskScanline(RendererPolygon* rp, s32 y)
{
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

    if (!PrevIsShadowMask)
        memset(&StencilBuffer[256 * (y&0x1)], 0, 256);

    PrevIsShadowMask = true;

    if (polygon->YTop != polygon->YBottom)
    {
        if (y >= polygon->Vertices[rp->NextVL]->FinalPosition[1] && rp->CurVL != polygon->VBottom)
        {
            SetupPolygonLeftEdge(rp, y);
        }

        if (y >= polygon->Vertices[rp->NextVR]->FinalPosition[1] && rp->CurVR != polygon->VBottom)
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
    s32 xstart, xend;
    bool l_filledge, r_filledge;
    s32 l_edgelen, r_edgelen;
    s32 l_edgecov, r_edgecov;
    Interpolator<1>* interp_start;
    Interpolator<1>* interp_end;

    xstart = rp->XL;
    xend = rp->XR;

    s32 wl = rp->SlopeL.Interp.Interpolate(polygon->FinalW[rp->CurVL], polygon->FinalW[rp->NextVL]);
    s32 wr = rp->SlopeR.Interp.Interpolate(polygon->FinalW[rp->CurVR], polygon->FinalW[rp->NextVR]);

    s32 zl = rp->SlopeL.Interp.InterpolateZ(polygon->FinalZ[rp->CurVL], polygon->FinalZ[rp->NextVL]);
    s32 zr = rp->SlopeR.Interp.InterpolateZ(polygon->FinalZ[rp->CurVR], polygon->FinalZ[rp->NextVR]);

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
                || (y == polygon->YBottom-1) && rp->SlopeR.XMajor && (vlnext->FinalPosition[0] != vrnext->FinalPosition[0]);
            r_filledge = (!rp->SlopeL.Negative && rp->SlopeL.XMajor)
                || (!(rp->SlopeL.Negative && rp->SlopeL.XMajor) && rp->SlopeR.Increment==0)
                || (y == polygon->YBottom-1) && rp->SlopeL.XMajor && (vlnext->FinalPosition[0] != vrnext->FinalPosition[0]);
        }
    }
    else
    {
        vlcur = polygon->Vertices[rp->CurVL];
        vlnext = polygon->Vertices[rp->NextVL];
        vrcur = polygon->Vertices[rp->CurVR];
        vrnext = polygon->Vertices[rp->NextVR];

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
                || (y == polygon->YBottom-1) && rp->SlopeL.XMajor && (vlnext->FinalPosition[0] != vrnext->FinalPosition[0]))
                || (rp->SlopeL.Increment == rp->SlopeR.Increment) && (xstart+l_edgelen == xend+1);
            r_filledge = (!rp->SlopeR.Negative && rp->SlopeR.XMajor) || (rp->SlopeR.Increment==0)
                || (y == polygon->YBottom-1) && rp->SlopeR.XMajor && (vlnext->FinalPosition[0] != vrnext->FinalPosition[0]);
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
        u32 pixeladdr = FirstPixelOffset + (y*ScanlineWidth) + x;

        interpX.SetX(x);

        s32 z = interpX.InterpolateZ(zl, zr);
        u32 dstattr = AttrBuffer[pixeladdr];

        if (!fnDepthTest(DepthBuffer[pixeladdr], z, dstattr))
            StencilBuffer[256*(y&0x1) + x] = 1;

        if (dstattr & 0xF)
        {
            pixeladdr += BufferSize;
            if (!fnDepthTest(DepthBuffer[pixeladdr], z, AttrBuffer[pixeladdr]))
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
        u32 pixeladdr = FirstPixelOffset + (y*ScanlineWidth) + x;

        interpX.SetX(x);

        s32 z = interpX.InterpolateZ(zl, zr);
        u32 dstattr = AttrBuffer[pixeladdr];

        if (!fnDepthTest(DepthBuffer[pixeladdr], z, dstattr))
            StencilBuffer[256*(y&0x1) + x] = 1;

        if (dstattr & 0xF)
        {
            pixeladdr += BufferSize;
            if (!fnDepthTest(DepthBuffer[pixeladdr], z, AttrBuffer[pixeladdr]))
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
        u32 pixeladdr = FirstPixelOffset + (y*ScanlineWidth) + x;

        interpX.SetX(x);

        s32 z = interpX.InterpolateZ(zl, zr);
        u32 dstattr = AttrBuffer[pixeladdr];

        if (!fnDepthTest(DepthBuffer[pixeladdr], z, dstattr))
            StencilBuffer[256*(y&0x1) + x] = 1;

        if (dstattr & 0xF)
        {
            pixeladdr += BufferSize;
            if (!fnDepthTest(DepthBuffer[pixeladdr], z, AttrBuffer[pixeladdr]))
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

void SoftRenderer3D::RenderPolygonScanline(RendererPolygon* rp, s32 y)
{
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
        if (y >= polygon->Vertices[rp->NextVL]->FinalPosition[1] && rp->CurVL != polygon->VBottom)
        {
            SetupPolygonLeftEdge(rp, y);
        }

        if (y >= polygon->Vertices[rp->NextVR]->FinalPosition[1] && rp->CurVR != polygon->VBottom)
        {
            SetupPolygonRightEdge(rp, y);
        }
    }

#ifdef LITEV_SOFT3D_BANDED
    // Banded raster: not our row. Advance the incremental edge state (Step) but
    // write no spans. See RenderShadowMaskScanline for the rationale.
    if (y < BandY0 || y >= BandY1)
    {
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

    Vertex *vlcur, *vlnext, *vrcur, *vrnext;
    s32 xstart, xend;
    bool l_filledge, r_filledge;
    s32 l_edgelen, r_edgelen;
    s32 l_edgecov, r_edgecov;
    Interpolator<1>* interp_start;
    Interpolator<1>* interp_end;

    xstart = rp->XL;
    xend = rp->XR;

    s32 wl = rp->SlopeL.Interp.Interpolate(polygon->FinalW[rp->CurVL], polygon->FinalW[rp->NextVL]);
    s32 wr = rp->SlopeR.Interp.Interpolate(polygon->FinalW[rp->CurVR], polygon->FinalW[rp->NextVR]);

    s32 zl = rp->SlopeL.Interp.InterpolateZ(polygon->FinalZ[rp->CurVL], polygon->FinalZ[rp->NextVL]);
    s32 zr = rp->SlopeR.Interp.InterpolateZ(polygon->FinalZ[rp->CurVR], polygon->FinalZ[rp->NextVR]);

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

        interp_start = &rp->SlopeR.Interp;
        interp_end = &rp->SlopeL.Interp;

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
                || (y == polygon->YBottom-1) && rp->SlopeR.XMajor && (vlnext->FinalPosition[0] != vrnext->FinalPosition[0]);
            r_filledge = (!rp->SlopeL.Negative && rp->SlopeL.XMajor)
                || (!(rp->SlopeL.Negative && rp->SlopeL.XMajor) && rp->SlopeR.Increment==0)
                || (y == polygon->YBottom-1) && rp->SlopeL.XMajor && (vlnext->FinalPosition[0] != vrnext->FinalPosition[0]);
        }
    }
    else
    {
        vlcur = polygon->Vertices[rp->CurVL];
        vlnext = polygon->Vertices[rp->NextVL];
        vrcur = polygon->Vertices[rp->CurVR];
        vrnext = polygon->Vertices[rp->NextVR];

        interp_start = &rp->SlopeL.Interp;
        interp_end = &rp->SlopeR.Interp;

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
                || (y == polygon->YBottom-1) && rp->SlopeL.XMajor && (vlnext->FinalPosition[0] != vrnext->FinalPosition[0]))
                || (rp->SlopeL.Increment == rp->SlopeR.Increment) && (xstart+l_edgelen == xend+1);
            r_filledge = (!rp->SlopeR.Negative && rp->SlopeR.XMajor) || (rp->SlopeR.Increment==0)
                || (y == polygon->YBottom-1) && rp->SlopeR.XMajor && (vlnext->FinalPosition[0] != vrnext->FinalPosition[0]);
        }
    }

    // interpolate attributes along Y

    s32 rl = interp_start->Interpolate(vlcur->FinalColor[0], vlnext->FinalColor[0]);
    s32 gl = interp_start->Interpolate(vlcur->FinalColor[1], vlnext->FinalColor[1]);
    s32 bl = interp_start->Interpolate(vlcur->FinalColor[2], vlnext->FinalColor[2]);

    s32 sl = interp_start->Interpolate(vlcur->TexCoords[0], vlnext->TexCoords[0]);
    s32 tl = interp_start->Interpolate(vlcur->TexCoords[1], vlnext->TexCoords[1]);

    s32 rr = interp_end->Interpolate(vrcur->FinalColor[0], vrnext->FinalColor[0]);
    s32 gr = interp_end->Interpolate(vrcur->FinalColor[1], vrnext->FinalColor[1]);
    s32 br = interp_end->Interpolate(vrcur->FinalColor[2], vrnext->FinalColor[2]);

    s32 sr = interp_end->Interpolate(vrcur->TexCoords[0], vrnext->TexCoords[0]);
    s32 tr = interp_end->Interpolate(vrcur->TexCoords[1], vrnext->TexCoords[1]);

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
    if (f_texEnable)
        f_texcache = ResolveTexCache(f_texparam, f_texpal, &f_texW, &f_texH);

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
                u16 packed = f_texcache[texAddr(s, t)];
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
        packed[0] = f_texcache[addr[0]];
        packed[1] = f_texcache[addr[1]];
        packed[2] = f_texcache[addr[2]];
        packed[3] = f_texcache[addr[3]];

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
                if (pixeladdr < BufferSize)
                {
                    ColorBuffer[pixeladdr+BufferSize] = ColorBuffer[pixeladdr];
                    DepthBuffer[pixeladdr+BufferSize] = DepthBuffer[pixeladdr];
                    AttrBuffer[pixeladdr+BufferSize] = AttrBuffer[pixeladdr];
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
            if ((dstattr & 0xF) && (pixeladdr < BufferSize))
                PlotTranslucentPixel(pixeladdr+BufferSize, color, z, polyattr, polygon->IsShadow);
        }
    };
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
    for (; x < xlimit; x++)
    {
        u32 pixeladdr = FirstPixelOffset + (y*ScanlineWidth) + x;
        u32 dstattr = AttrBuffer[pixeladdr];

        // check stencil buffer for shadows
        if (polygon->IsShadow)
        {
            u8 stencil = StencilBuffer[256*(y&0x1) + x];
            if (!stencil)
                continue;
            if (!(stencil & 0x1))
                pixeladdr += BufferSize;
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
        if (!fnDepthTest(DepthBuffer[pixeladdr], z, dstattr))
        {
            if (!(dstattr & 0xF) || pixeladdr >= BufferSize) continue;

            pixeladdr += BufferSize;
            dstattr = AttrBuffer[pixeladdr];
            if (!fnDepthTest(DepthBuffer[pixeladdr], z, dstattr))
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
                if (pixeladdr < BufferSize)
                {
                    ColorBuffer[pixeladdr+BufferSize] = ColorBuffer[pixeladdr];
                    DepthBuffer[pixeladdr+BufferSize] = DepthBuffer[pixeladdr];
                    AttrBuffer[pixeladdr+BufferSize] = AttrBuffer[pixeladdr];
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
            if ((dstattr & 0xF) && (pixeladdr < BufferSize))
                PlotTranslucentPixel(pixeladdr+BufferSize, color, z, polyattr, polygon->IsShadow);
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

        for (; x < xlimit; x++)
        {
            u32 pixeladdr = FirstPixelOffset + (y*ScanlineWidth) + x;
            u32 dstattr = AttrBuffer[pixeladdr];

            SA_STEP_Z();

            if (!fnDepthTest(DepthBuffer[pixeladdr], z, dstattr))
            {
                if (!(dstattr & 0xF) || pixeladdr >= BufferSize) continue;
                pixeladdr += BufferSize;
                dstattr = AttrBuffer[pixeladdr];
                if (!fnDepthTest(DepthBuffer[pixeladdr], z, dstattr))
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
        }
        // flush remainder (<4) via the scalar shade
        for (int i = 0; i < nb; i++)
            plot2(bpaddr[i], bz[i], bdstattr[i], shadeFast(bvr[i], bvg[i], bvb[i], bs[i], bt[i]));
    }
    else
#endif
    for (; x < xlimit; x++)
    {
        u32 pixeladdr = FirstPixelOffset + (y*ScanlineWidth) + x;
        u32 dstattr = AttrBuffer[pixeladdr];

        // check stencil buffer for shadows
        if (polygon->IsShadow)
        {
            u8 stencil = StencilBuffer[256*(y&0x1) + x];
            if (!stencil)
                continue;
            if (!(stencil & 0x1))
                pixeladdr += BufferSize;
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
        if (!fnDepthTest(DepthBuffer[pixeladdr], z, dstattr))
        {
            if (!(dstattr & 0xF) || pixeladdr >= BufferSize) continue;

            pixeladdr += BufferSize;
            dstattr = AttrBuffer[pixeladdr];
            if (!fnDepthTest(DepthBuffer[pixeladdr], z, dstattr))
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
                if (pixeladdr < BufferSize)
                {
                    ColorBuffer[pixeladdr+BufferSize] = ColorBuffer[pixeladdr];
                    DepthBuffer[pixeladdr+BufferSize] = DepthBuffer[pixeladdr];
                    AttrBuffer[pixeladdr+BufferSize] = AttrBuffer[pixeladdr];
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
            if ((dstattr & 0xF) && (pixeladdr < BufferSize))
                PlotTranslucentPixel(pixeladdr+BufferSize, color, z, polyattr, polygon->IsShadow);
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
    for (; x < xlimit; x++)
    {
        u32 pixeladdr = FirstPixelOffset + (y*ScanlineWidth) + x;
        u32 dstattr = AttrBuffer[pixeladdr];

        // check stencil buffer for shadows
        if (polygon->IsShadow)
        {
            u8 stencil = StencilBuffer[256*(y&0x1) + x];
            if (!stencil)
                continue;
            if (!(stencil & 0x1))
                pixeladdr += BufferSize;
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
        if (!fnDepthTest(DepthBuffer[pixeladdr], z, dstattr))
        {
            if (!(dstattr & 0xF) || pixeladdr >= BufferSize) continue;

            pixeladdr += BufferSize;
            dstattr = AttrBuffer[pixeladdr];
            if (!fnDepthTest(DepthBuffer[pixeladdr], z, dstattr))
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
                if (pixeladdr < BufferSize)
                {
                    ColorBuffer[pixeladdr+BufferSize] = ColorBuffer[pixeladdr];
                    DepthBuffer[pixeladdr+BufferSize] = DepthBuffer[pixeladdr];
                    AttrBuffer[pixeladdr+BufferSize] = AttrBuffer[pixeladdr];
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
            if ((dstattr & 0xF) && (pixeladdr < BufferSize))
                PlotTranslucentPixel(pixeladdr+BufferSize, color, z, polyattr, polygon->IsShadow);
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
    // to consider:
    // clearing all polygon fog flags if the master flag isn't set?
    // merging all final pass loops into one?

    if (GPU3D.RenderDispCnt & (1<<5))
    {
        // edge marking
        // only applied to topmost pixels

        for (int x = 0; x < 256; x++)
        {
            u32 pixeladdr = FirstPixelOffset + (y*ScanlineWidth) + x;

            u32 attr = AttrBuffer[pixeladdr];
            if (!(attr & 0xF)) continue;

            u32 polyid = attr >> 24; // opaque polygon IDs are used for edgemarking
            u32 z = DepthBuffer[pixeladdr];

            if (((polyid != (AttrBuffer[pixeladdr-1] >> 24)) && (z < DepthBuffer[pixeladdr-1])) ||
                ((polyid != (AttrBuffer[pixeladdr+1] >> 24)) && (z < DepthBuffer[pixeladdr+1])) ||
                ((polyid != (AttrBuffer[pixeladdr-ScanlineWidth] >> 24)) && (z < DepthBuffer[pixeladdr-ScanlineWidth])) ||
                ((polyid != (AttrBuffer[pixeladdr+ScanlineWidth] >> 24)) && (z < DepthBuffer[pixeladdr+ScanlineWidth])))
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
            u32 pixeladdr = FirstPixelOffset + (y*ScanlineWidth) + x;
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
            pixeladdr += BufferSize;

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
            u32 pixeladdr = FirstPixelOffset + (y*ScanlineWidth) + x;

            u32 attr = AttrBuffer[pixeladdr];
            if (!(attr & 0xF)) continue;

            u32 coverage = (attr >> 8) & 0x1F;
            if (coverage == 0x1F) continue;

            if (coverage == 0)
            {
                ColorBuffer[pixeladdr] = ColorBuffer[pixeladdr+BufferSize];
                continue;
            }

            u32 topcolor = ColorBuffer[pixeladdr];
            u32 topR = topcolor & 0x3F;
            u32 topG = (topcolor >> 8) & 0x3F;
            u32 topB = (topcolor >> 16) & 0x3F;
            u32 topA = (topcolor >> 24) & 0x1F;

            u32 botcolor = ColorBuffer[pixeladdr+BufferSize];
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
    u32 clearz = ((GPU3D.RenderClearAttr2 & 0x7FFF) * 0x200) + 0x1FF;
    u32 polyid = GPU3D.RenderClearAttr1 & 0x3F000000; // this sets the opaque polygonID

    // fill screen borders for edge marking

    for (int x = 0; x < ScanlineWidth; x++)
    {
        ColorBuffer[x] = 0;
        DepthBuffer[x] = clearz;
        AttrBuffer[x] = polyid;
    }

    for (int x = ScanlineWidth; x < ScanlineWidth*193; x+=ScanlineWidth)
    {
        ColorBuffer[x] = 0;
        DepthBuffer[x] = clearz;
        AttrBuffer[x] = polyid;
        ColorBuffer[x+257] = 0;
        DepthBuffer[x+257] = clearz;
        AttrBuffer[x+257] = polyid;
    }

    for (int x = ScanlineWidth*193; x < ScanlineWidth*194; x++)
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

        for (int y = 0; y < ScanlineWidth*192; y+=ScanlineWidth)
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

        for (int y = 0; y < ScanlineWidth*192; y+=ScanlineWidth)
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
        SetupPolygon(&PolygonList[j++], polygons[i]);
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
    for (s32 y = 0; y < y1; y++)
    {
        nActive = AETAdvance(nActive, y);
        RenderActiveList(y, nActive);
    }
#else
    for (s32 y = 0; y < 192; y++)
        RenderScanline(y, j);
#endif
}
#endif

void SoftRenderer3D::RenderPolygons(bool threaded, Polygon** polygons, int npolys)
{
    // DIAGNOSTIC (throwaway): LITEV_SKIP3D skips the raster but still posts the 192
    // scanline semaphores so the emu thread's per-scanline GetLine doesn't hang.
    // Isolates the single-threaded 3D-raster cost from the rest of the frame.
    static const bool _skip3d = getenv("LITEV_SKIP3D") != nullptr;
    if (_skip3d)
    {
        if (threaded)
            for (int k = 0; k < 192; k++) Platform::Semaphore_Post(Sema_ScanlineCount);
        return;
    }

#ifdef LITEV_SOFT3D_BANDED
    if (threaded)
    {
        // Parallel banded 3D raster. ClearBuffers() has already run on the render
        // thread. Split the 192 scanlines into N contiguous bands; each band walks
        // all scanlines (edge state) but only rasterizes its own rows.
        // Band count is tunable at runtime (LITEV_BANDS, default 2) so the sweet
        // spot vs the emu thread's core contention can be found without rebuilding.
        // On the 4-core (all-A55) target NB=2 is the sweet spot: the emu JIT thread
        // plus the threaded 2D renderer already occupy the other cores, so NB>=3
        // oversubscribes and regresses (measured). See the render-thread analysis.
        static const int NB = []{
            const char* e = getenv("LITEV_BANDS");
            int n = e ? atoi(e) : 2;
            if (n < 1) n = 1;
            if (n > 8) n = 8;
            return n;
        }();
        s32 bnd[8 + 1];
        for (int b = 0; b <= NB; b++) bnd[b] = (192 * b) / NB;

        std::thread workers[8];
        for (int b = 0; b < NB; b++)
            workers[b] = std::thread(&SoftRenderer3D::RenderBand, this,
                                     polygons, npolys, bnd[b], bnd[b + 1], b);
        for (int b = 0; b < NB; b++)
            workers[b].join();

        // Phase 2: the per-scanline final pass (edge marking / fog / anti-aliasing)
        // reads neighbouring scanlines, so it must run only after ALL bands have
        // finished rasterizing. It is only ~4% of the 3D cost (measured: ~3.5ms vs
        // ~90ms raster), so it is NOT the cause of the flat NB scaling and is not
        // worth parallelizing -- banding it was measured to REGRESS fps because the
        // per-frame thread spawn/join cost exceeds the tiny savings. Run it serially
        // top-to-bottom and post each scanline as it completes, so the emu thread's
        // GetLine compositing can overlap the remaining final-pass rows.
        for (s32 y = 0; y < 192; y++)
        {
            ScanlineFinalPass(y);
            Platform::Semaphore_Post(Sema_ScanlineCount);
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

    int j = 0;
    for (int i = 0; i < npolys; i++)
    {
        if (polygons[i]->Degenerate) continue;
        SetupPolygon(&PolygonList[j++], polygons[i]);
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
        Platform::Semaphore_Wait(Sema_RenderDone);
}

void SoftRenderer3D::RenderFrame()
{
    auto textureDirty = GPU.VRAMDirty_Texture.DeriveState(GPU.VRAMMap_Texture, GPU);
    auto texPalDirty = GPU.VRAMDirty_TexPal.DeriveState(GPU.VRAMMap_TexPal, GPU);

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
        // "Render thread, you're up! Get moving."
        Platform::Semaphore_Post(Sema_RenderStart);
    }
    else if (!FrameIdentical)
    {
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
    for (;;)
    {
        // Wait for a notice from the main thread to start rendering (or to stop entirely).
        Platform::Semaphore_Wait(Sema_RenderStart);
        if (!RenderThreadRunning) return;

        // Protect the GPU state from the main thread.
        // Some melonDS frontends (though not ours)
        // will repeatedly save or load states;
        // if they do so while the render thread is busy here,
        // the ensuing race conditions may cause a crash
        // (since some of the GPU state includes pointers).
        RenderThreadRendering = true;
        if (FrameIdentical)
        { // If no rendering is needed, just say we're done.
            Platform::Semaphore_Post(Sema_ScanlineCount, 192);
        }
        else
        {
            ClearBuffers();
            RenderPolygons(true, &GPU3D.RenderPolygonRAM[0], GPU3D.RenderNumPolygons);
        }

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
            // We need a scanline, so let's wait for the render thread to finish it.
            // (both threads process scanlines from top-to-bottom,
            // so we don't need to wait for a specific row)
            Platform::Semaphore_Wait(Sema_ScanlineCount);
    }

    u32* rawline = &ColorBuffer[(line * ScanlineWidth) + FirstPixelOffset];
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
