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

#include <thread>
#include "NDS.h"
#include "GPU_Soft.h"
#include "GPU_ColorOp.h"

#if defined(LITEV_NEON_RENDERER) && defined(__aarch64__)
#include "GPU2D_NEON.h"
#endif

namespace melonDS
{

SoftRenderer::SoftRenderer(melonDS::NDS& nds)
    : Renderer(nds.GPU)
{
    const size_t len = 256 * 192;
    Framebuffer[0][0] = new u32[len];
    Framebuffer[0][1] = new u32[len];
    Framebuffer[1][0] = new u32[len];
    Framebuffer[1][1] = new u32[len];
    BackBuffer = 0;

    Rend2D_A = std::make_unique<SoftRenderer2D>(GPU.GPU2D_A, *this);
    Rend2D_B = std::make_unique<SoftRenderer2D>(GPU.GPU2D_B, *this);
    Rend3D = std::make_unique<SoftRenderer3D>(GPU.GPU3D, *this);
}

SoftRenderer::~SoftRenderer()
{
    delete[] Framebuffer[0][0];
    delete[] Framebuffer[0][1];
    delete[] Framebuffer[1][0];
    delete[] Framebuffer[1][1];
}

void SoftRenderer::Reset()
{
    const size_t len = 256 * 192 * sizeof(u32);
    memset(Framebuffer[0][0], 0, len);
    memset(Framebuffer[0][1], 0, len);
    memset(Framebuffer[1][0], 0, len);
    memset(Framebuffer[1][1], 0, len);

    Rend2D_A->Reset();
    Rend2D_B->Reset();
    Rend3D->Reset();
}

void SoftRenderer::Stop()
{
    // clear framebuffers to black
    const size_t len = 256 * 192 * sizeof(u32);
    memset(Framebuffer[0][0], 0, len);
    memset(Framebuffer[0][1], 0, len);
    memset(Framebuffer[1][0], 0, len);
    memset(Framebuffer[1][1], 0, len);
}


void SoftRenderer::PreSavestate()
{
    auto rend3d = dynamic_cast<SoftRenderer3D*>(Rend3D.get());
    if (rend3d->IsThreaded())
        rend3d->SetupRenderThread();
}

void SoftRenderer::PostSavestate()
{
    auto rend3d = dynamic_cast<SoftRenderer3D*>(Rend3D.get());
    if (rend3d->IsThreaded())
        rend3d->EnableRenderThread();
}


void SoftRenderer::SetRenderSettings(RendererSettings& settings)
{
    auto rend3d = dynamic_cast<SoftRenderer3D*>(Rend3D.get());
    rend3d->SetThreaded(settings.Threaded);
}


void SoftRenderer::DrawScanline(u32 line)
{
#ifdef LITEV_SOFT2D_THREADED
    // Deferred (DraStic-model): snapshot this line's per-scanline state and defer the
    // whole raster+composite to RenderDeferredFrame() at VBlank. Index by the dst line
    // (fb position); the snapshot captures the current (VCount) register state, which
    // matches the inline path when VCount==line (the normal case).
    if (line < 192)
    {
        u32 vline = GPU.VCount;
        if (vline < 192)
        {
            // Consume this line's 3D output NOW (in lockstep with the threaded-3D
            // render thread's per-scanline semaphore) and copy it; the deferred 2D
            // batch reads the copy at VBlank.
            u32* l3d = Rend3D->GetLine(vline);
            memcpy(Snap3D[line], l3d, 256 * sizeof(u32));
        }
        static_cast<SoftRenderer2D*>(Rend2D_A.get())->SnapshotLineState(line);
        static_cast<SoftRenderer2D*>(Rend2D_B.get())->SnapshotLineState(line);
        SnapshotCompositeLine(line);
        S2DDeferActive = true;
        return;
    }
    // line >= 192 falls through to the original inline out-of-range path.
#endif
    u32 *dstA, *dstB;
    u32 dstoffset = 256 * line;
    if (GPU.ScreenSwap)
    {
        dstA = &Framebuffer[BackBuffer][0][dstoffset];
        dstB = &Framebuffer[BackBuffer][1][dstoffset];
    }
    else
    {
        dstA = &Framebuffer[BackBuffer][1][dstoffset];
        dstB = &Framebuffer[BackBuffer][0][dstoffset];
    }

    // the position used for drawing operations is based on VCOUNT
    line = GPU.VCount;
    if (line < 192)
    {
        // retrieve 3D output
        Output3D = Rend3D->GetLine(line);

        // draw BG/OBJ layers
        Rend2D_A->DrawScanline(line);
        Rend2D_B->DrawScanline(line);

        // draw the final screen output
        DrawScanlineA(line, dstA, Output2D[0], GPU.GPU2D_A.DispCnt, GPU.MasterBrightnessA);
        DrawScanlineB(line, dstB, Output2D[1], GPU.GPU2D_B.DispCnt, GPU.MasterBrightnessB);

        // perform display capture if enabled
        if (GPU.CaptureEnable)
            DoCapture(line, Output2D[0], Output3D);
    }
    else
    {
        // if scanlines outside VCOUNT range 0..191 were to be visible, fill them white
        // this may happen if VCOUNT is written to during active display
        // the actual hardware behavior depends on the screen model, and suggests that
        // no video signal is output for such scanlines

        for (int i = 0; i < 256; i++)
        {
            dstA[i] = 0x3F3F3F;
            dstB[i] = 0x3F3F3F;
        }
    }

    if (GPU.ScreensEnabled)
    {
        // expand the color from 6-bit to 8-bit
        ExpandColor(dstA);
        ExpandColor(dstB);
    }
    else
    {
        // if the screens are disabled: fill the framebuffer black
        for (int i = 0; i < 256; i++)
        {
            dstA[i] = 0xFF000000;
            dstB[i] = 0xFF000000;
        }
    }
}

void SoftRenderer::DrawSprites(u32 line)
{
#ifdef LITEV_SOFT2D_THREADED
    // Deferred: snapshot the OBJ state at this (one-line-ahead) moment; the actual
    // sprite raster runs in RenderDeferredFrame at VBlank.
    if (line < 192)
    {
        static_cast<SoftRenderer2D*>(Rend2D_A.get())->SnapshotSprState(line);
        static_cast<SoftRenderer2D*>(Rend2D_B.get())->SnapshotSprState(line);
        return;
    }
#endif
    Rend2D_A->DrawSprites(line);
    Rend2D_B->DrawSprites(line);
}

#ifdef LITEV_SOFT2D_THREADED
void SoftRenderer::SnapshotCompositeLine(u32 line)
{
    FrameLineSnap& f = FrameSnap[line];
    f.DispCntA = GPU.GPU2D_A.DispCnt;
    f.DispCntB = GPU.GPU2D_B.DispCnt;
    f.MasterBrightnessA = GPU.MasterBrightnessA;
    f.MasterBrightnessB = GPU.MasterBrightnessB;
    f.ScreenSwap = GPU.ScreenSwap;
    f.ScreensEnabled = GPU.ScreensEnabled;
    f.CaptureEnable = GPU.CaptureEnable;
    f.Valid = 1;
}

// The whole frame's 2D raster + final composite, run once at VBlank off the
// per-scanline critical path. Milestone 1: single-thread, reusing the inline
// draw functions after restoring each line's snapshot. Bit-exact-gated vs the
// per-scanline inline path before band-threading.
// Lazily create the per-band private render contexts (once).
void SoftRenderer::InitBands()
{
    for (int b = 0; b < S2D_NBANDS; b++)
    {
        for (int e = 0; e < 2; e++)
        {
            S2DBands[b].unit[e] = std::make_unique<GPU2D>((u32)e, GPU);
            S2DBands[b].rend[e] = std::make_unique<SoftRenderer2D>(*S2DBands[b].unit[e], *this);
            static_cast<SoftRenderer2D*>(S2DBands[b].rend[e].get())->Reset();
        }
    }
    S2DBandsInit = true;
}

// Render a disjoint line range [y0,y1) for BOTH engines into BandOut2D, using this
// band's PRIVATE units/renderers, reading the shared main snapshots. Runs on a
// helper thread; no shared mutable render state with the other bands.
void SoftRenderer::RenderBand(int bi, u32 y0, u32 y1)
{
    auto* mainA = static_cast<SoftRenderer2D*>(Rend2D_A.get());
    auto* mainB = static_cast<SoftRenderer2D*>(Rend2D_B.get());
    auto* rA = static_cast<SoftRenderer2D*>(S2DBands[bi].rend[0].get());
    auto* rB = static_cast<SoftRenderer2D*>(S2DBands[bi].rend[1].get());

    for (u32 line = y0; line < y1; line++)
    {
        FrameLineSnap& f = FrameSnap[line];
        if (!f.Valid) continue;

        // --- BG/OBJ raster into this band's per-engine line buffers ---
        rA->Cur3DLine = Snap3D[line];
        rA->CurOAM = GPU.OAM;
        rA->DrawSpritesDeferred(mainA->SprSnap[line], line);
        rA->DrawScanlineDeferred(mainA->LineSnap[line], line, BandOut2D[0][line]);

        rB->Cur3DLine = Snap3D[line];
        rB->CurOAM = GPU.OAM;
        rB->DrawSpritesDeferred(mainB->SprSnap[line], line);
        rB->DrawScanlineDeferred(mainB->LineSnap[line], line, BandOut2D[1][line]);

        // --- final composite + capture + expand into the framebuffer (banded too) ---
        u32 dstoffset = 256 * line;
        u32 *dstA, *dstB;
        if (f.ScreenSwap)
        {
            dstA = &Framebuffer[BackBuffer][0][dstoffset];
            dstB = &Framebuffer[BackBuffer][1][dstoffset];
        }
        else
        {
            dstA = &Framebuffer[BackBuffer][1][dstoffset];
            dstB = &Framebuffer[BackBuffer][0][dstoffset];
        }

        DrawScanlineA(line, dstA, BandOut2D[0][line], f.DispCntA, f.MasterBrightnessA);
        DrawScanlineB(line, dstB, BandOut2D[1][line], f.DispCntB, f.MasterBrightnessB);

        if (f.CaptureEnable)
            DoCapture(line, BandOut2D[0][line], Snap3D[line]);

        if (f.ScreensEnabled)
        {
            ExpandColor(dstA);
            ExpandColor(dstB);
        }
        else
        {
            for (int i = 0; i < 256; i++) { dstA[i] = 0xFF000000; dstB[i] = 0xFF000000; }
        }

        f.Valid = 0;
    }
}

void SoftRenderer::RenderDeferredFrame()
{
    if (!S2DBandsInit) InitBands();

    auto* r2a = static_cast<SoftRenderer2D*>(Rend2D_A.get());
    auto* r2b = static_cast<SoftRenderer2D*>(Rend2D_B.get());

    // Once-per-frame VRAM coherence (sequential, before the parallel section).
    r2a->SyncVRAM_BG(); r2a->SyncVRAM_OBJ();
    r2b->SyncVRAM_BG(); r2b->SyncVRAM_OBJ();

    // Seed each band's private units with the frame-level register state (per-line
    // varying fields are overridden by LoadLineState inside the deferred draws).
    for (int b = 0; b < S2D_NBANDS; b++)
    {
        S2DBands[b].unit[0]->CopyRenderState(GPU.GPU2D_A);
        S2DBands[b].unit[1]->CopyRenderState(GPU.GPU2D_B);
    }

    // N-way banded raster across idle cores. Emu thread renders band 0; helpers do
    // the rest. (Emu is blocked here, so its core + the idle 3D thread's core are
    // available.)
    const u32 rows = 192 / S2D_NBANDS;
    std::thread helpers[S2D_NBANDS - 1];
    for (int b = 1; b < S2D_NBANDS; b++)
    {
        u32 y0 = (u32)b * rows;
        u32 y1 = (b == S2D_NBANDS - 1) ? 192 : y0 + rows;
        helpers[b-1] = std::thread([this, b, y0, y1]{ RenderBand(b, y0, y1); });
    }
    RenderBand(0, 0, rows);
    for (int b = 0; b < S2D_NBANDS - 1; b++) helpers[b].join();

    // Composite + capture + expand now happen INSIDE each band (per line), so the
    // whole 2D pipeline is off the sequential emu-thread path.
    S2DDeferActive = false;
}
#endif

void SoftRenderer::DrawScanlineA(u32 line, u32* dst, const u32* src2d, u32 dispcnt, u16 mbright)
{
    switch ((dispcnt >> 16) & 0x3)
    {
    case 0: // screen off
        {
            for (int i = 0; i < 256; i++)
                dst[i] = 0x3F3F3F;
        }
        return;

    case 1: // regular display
        {
            for (int i = 0; i < 256; i+=2)
                *(u64*)&dst[i] = *(u64*)&src2d[i];
        }
        break;

    case 2: // VRAM display
        {
            u32 vrambank = (dispcnt >> 18) & 0x3;
            if (GPU.VRAMMap_LCDC & (1<<vrambank))
            {
                u16* vram = (u16*)GPU.VRAM[vrambank];
                vram = &vram[line * 256];

                for (int i = 0; i < 256; i++)
                {
                    u16 color = vram[i];
                    u8 r = (color & 0x001F) << 1;
                    u8 g = (color & 0x03E0) >> 4;
                    u8 b = (color & 0x7C00) >> 9;

                    dst[i] = r | (g << 8) | (b << 16);
                }
            }
            else
            {
                for (int i = 0; i < 256; i++)
                    dst[i] = 0;
            }
        }
        break;

    case 3: // FIFO display
        {
            for (int i = 0; i < 256; i++)
            {
                u16 color = GPU.DispFIFOBuffer[i];
                u8 r = (color & 0x001F) << 1;
                u8 g = (color & 0x03E0) >> 4;
                u8 b = (color & 0x7C00) >> 9;

                dst[i] = r | (g << 8) | (b << 16);
            }
        }
        break;
    }

    ApplyMasterBrightness(mbright, dst);
}

void SoftRenderer::DrawScanlineB(u32 line, u32* dst, const u32* src2d, u32 dispcnt, u16 mbright)
{
    switch ((dispcnt >> 16) & 0x1)
    {
    case 0: // screen off
        {
            for (int i = 0; i < 256; i++)
                dst[i] = 0xFF3F3F3F;
        }
        return;

    case 1: // regular display
        {
            for (int i = 0; i < 256; i+=2)
                *(u64*)&dst[i] = *(u64*)&src2d[i];
        }
        break;
    }

    ApplyMasterBrightness(mbright, dst);
}

void SoftRenderer::DoCapture(u32 line, const u32* srcA2d, const u32* src3d)
{
    u32 captureCnt = GPU.CaptureCnt;

    u32 width, height;
    u32 sz = (captureCnt >> 20) & 0x3;
    if (sz == 0)
    {
        width = 128;
        height = 128;
    }
    else
    {
        width = 256;
        height = 64 * sz;
    }

    if (line >= height)
        return;

    u32 dstvram = (captureCnt >> 16) & 0x3;
    if (!(GPU.VRAMMap_LCDC & (1<<dstvram)))
        return;

    u16* dst = (u16*)GPU.VRAM[dstvram];
    u32 dstaddr = (((captureCnt >> 18) & 0x3) << 14) + (line * width);
    dst += (dstaddr & 0xFFFF);

    const u32* srcA;
    if (captureCnt & (1<<24))
        srcA = src3d;
    else
        srcA = srcA2d;

    u16* srcB = nullptr;
    if (captureCnt & (1<<25))
        srcB = GPU.DispFIFOBuffer;
    else
    {
        u32 dispcnt = GPU.GPU2D_A.DispCnt;
        u32 srcvram = (dispcnt >> 18) & 0x3;
        if (GPU.VRAMMap_LCDC & (1<<srcvram))
        {
            srcB = (u16*)GPU.VRAM[srcvram];

            u32 offset = line * 256;
            if (((dispcnt >> 16) & 0x3) != 2)
                offset += (((captureCnt >> 26) & 0x3) << 14);

            srcB += (offset & 0xFFFF);
        }
    }

    static_assert(VRAMDirtyGranularity == 512);
    GPU.VRAMDirty[dstvram][(dstaddr * 2) / VRAMDirtyGranularity] = true;

    switch ((captureCnt >> 29) & 0x3)
    {
    case 0: // source A
        {
            for (u32 i = 0; i < width; i++)
            {
                u32 val = srcA[i];

                u32 r = (val >> 1) & 0x1F;
                u32 g = (val >> 9) & 0x1F;
                u32 b = (val >> 17) & 0x1F;
                u32 a = ((val >> 24) != 0) ? 0x8000 : 0;

                dst[i] = r | (g << 5) | (b << 10) | a;
            }
        }
        break;

    case 1: // source B
        {
            if (srcB)
            {
                for (u32 i = 0; i < width; i++)
                    dst[i] = srcB[i];
            }
            else
            {
                for (u32 i = 0; i < width; i++)
                    dst[i] = 0;
            }
        }
        break;

    case 2: // sources A+B
    case 3:
        {
            u32 eva = captureCnt & 0x1F;
            u32 evb = (captureCnt >> 8) & 0x1F;

            // checkme
            if (eva > 16) eva = 16;
            if (evb > 16) evb = 16;

            if (srcB)
            {
                for (u32 i = 0; i < width; i++)
                {
                    u32 val = srcA[i];

                    u32 rA = (val >> 1) & 0x1F;
                    u32 gA = (val >> 9) & 0x1F;
                    u32 bA = (val >> 17) & 0x1F;
                    u32 aA = ((val >> 24) != 0) ? 1 : 0;

                    val = srcB[i];

                    u32 rB = val & 0x1F;
                    u32 gB = (val >> 5) & 0x1F;
                    u32 bB = (val >> 10) & 0x1F;
                    u32 aB = val >> 15;

                    u32 rD = ((rA * aA * eva) + (rB * aB * evb) + 8) >> 4;
                    u32 gD = ((gA * aA * eva) + (gB * aB * evb) + 8) >> 4;
                    u32 bD = ((bA * aA * eva) + (bB * aB * evb) + 8) >> 4;
                    u32 aD = (eva>0 ? aA : 0) | (evb>0 ? aB : 0);

                    if (rD > 0x1F) rD = 0x1F;
                    if (gD > 0x1F) gD = 0x1F;
                    if (bD > 0x1F) bD = 0x1F;

                    dst[i] = rD | (gD << 5) | (bD << 10) | (aD << 15);
                }
            }
            else
            {
                for (u32 i = 0; i < width; i++)
                {
                    u32 val = srcA[i];

                    u32 rA = (val >> 1) & 0x1F;
                    u32 gA = (val >> 9) & 0x1F;
                    u32 bA = (val >> 17) & 0x1F;
                    u32 aA = ((val >> 24) != 0) ? 1 : 0;

                    u32 rD = ((rA * aA * eva) + 8) >> 4;
                    u32 gD = ((gA * aA * eva) + 8) >> 4;
                    u32 bD = ((bA * aA * eva) + 8) >> 4;
                    u32 aD = (eva>0 ? aA : 0);

                    dst[i] = rD | (gD << 5) | (bD << 10) | (aD << 15);
                }
            }
        }
        break;
    }
}

void SoftRenderer::ApplyMasterBrightness(u16 regval, u32* dst)
{
    u16 mode = regval >> 14;
    if (mode == 1)
    {
        // up
        u32 factor = regval & 0x1F;
        if (factor > 16) factor = 16;

#if defined(LITEV_NEON_RENDERER) && defined(__aarch64__)
        GPU2DNeon::BrightnessUp(dst, 256, factor);
#else
        for (int i = 0; i < 256; i++)
            dst[i] = ColorBrightnessUp(dst[i], factor, 0x0);
#endif
    }
    else if (mode == 2)
    {
        // down
        u32 factor = regval & 0x1F;
        if (factor > 16) factor = 16;

#if defined(LITEV_NEON_RENDERER) && defined(__aarch64__)
        GPU2DNeon::BrightnessDown(dst, 256, factor);
#else
        for (int i = 0; i < 256; i++)
            dst[i] = ColorBrightnessDown(dst[i], factor, 0xF);
#endif
    }
}

void SoftRenderer::ExpandColor(u32* dst)
{
    // convert to 32-bit BGRA
    // note: 32-bit RGBA would be more straightforward, but
    // BGRA seems to be more compatible (Direct2D soft, cairo...)
#if defined(LITEV_NEON_RENDERER) && defined(__aarch64__)
    GPU2DNeon::ConvertToBGRA(dst, 256);
#else
    for (int i = 0; i < 256; i+=2)
    {
        u64 c = *(u64*)&dst[i];

        u64 r = (c << 18) & 0xFC000000FC0000;
        u64 g = (c << 2) & 0xFC000000FC00;
        u64 b = (c >> 14) & 0xFC000000FC;
        c = r | g | b;

        *(u64*)&dst[i] = c | ((c & 0x00C0C0C000C0C0C0) >> 6) | 0xFF000000FF000000;
    }
#endif
}


bool SoftRenderer::GetFramebuffers(void** top, void** bottom)
{
    int frontbuf = BackBuffer ^ 1;
    *top = Framebuffer[frontbuf][0];
    *bottom = Framebuffer[frontbuf][1];
    return true;
}

}
