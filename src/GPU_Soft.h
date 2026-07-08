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

#ifndef GPU_SOFT_H
#define GPU_SOFT_H

#include "GPU.h"
#include "GPU2D_Soft.h"
#include "GPU3D_Soft.h"

namespace melonDS
{

class SoftRenderer : public Renderer
{
public:
    explicit SoftRenderer(melonDS::NDS& nds);
    ~SoftRenderer() override;
    bool Init() override { return true; }
    void Reset() override;
    void Stop() override;

    void PreSavestate() override;
    void PostSavestate() override;

    void SetRenderSettings(RendererSettings& settings) override;

    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;

#ifdef LITEV_SOFT2D_THREADED
    void VBlank() override { if (S2DDeferActive) RenderDeferredFrame(); }
#else
    void VBlank() override {};
#endif
    void VBlankEnd() override {};

    void AllocCapture(u32 bank, u32 start, u32 len) override {};
    void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete) override {};

    bool GetFramebuffers(void** top, void** bottom) override;

private:
    friend class SoftRenderer2D;
    friend class SoftRenderer3D;

    u32* Framebuffer[2][2];

    u32* Output3D;
    alignas(8) u32 Output2D[2][256];

#ifdef LITEV_SOFT2D_THREADED
    // Deferred (DraStic-model) software 2D: snapshot the final-composite per-scanline
    // state on the emu thread; the whole frame's raster+composite runs at VBlank, off
    // the per-scanline critical path (later banded across helper threads).
    struct FrameLineSnap
    {
        u32 DispCntA, DispCntB;
        u16 MasterBrightnessA, MasterBrightnessB;
        u8  ScreenSwap;
        u8  ScreensEnabled;
        u8  CaptureEnable;
        u8  Valid;
    };
    FrameLineSnap FrameSnap[192];
    // 3D output copied per line DURING the visible period, keeping the threaded-3D
    // GetLine semaphore consumption in lockstep with the render thread (the deferred
    // 2D batch at VBlank then reads these copies instead of re-calling GetLine, which
    // would race the 3D render thread's frame schedule).
    alignas(8) u32 Snap3D[192][256];
    // Full-frame per-engine 2D output, so engine A and engine B (independent GPU2D
    // units + SoftRenderer2D instances + buffers) can render in parallel before the
    // sequential composite reads both. (M2 step 1: 2-way A||B; later: line bands.)
    alignas(8) u32 BandOut2D[2][192][256];
    bool S2DDeferActive = false;   // set per-frame: no capture/edge → safe to defer
    void SnapshotCompositeLine(u32 line);
    void RenderDeferredFrame();    // called at VBlank

    // N-way banded raster (DraStic model): each band renders a disjoint line range
    // for BOTH engines using PRIVATE GPU2D units (seeded from the main frame state
    // via CopyRenderState, then per-line snapshot overrides) + private scanline temp
    // buffers, so all bands run concurrently on idle cores with no shared mutable
    // render state. They read the shared read-only snapshots + shared VRAM (emu is
    // blocked during the batch).
    static constexpr int S2D_NBANDS = 4;
    struct S2DBand
    {
        std::unique_ptr<GPU2D> unit[2];
        std::unique_ptr<Renderer2D> rend[2];   // SoftRenderer2D bound to unit[]
    };
    S2DBand S2DBands[S2D_NBANDS];
    bool S2DBandsInit = false;
    void InitBands();
    void RenderBand(int bi, u32 y0, u32 y1);
#endif

    void DrawScanlineA(u32 line, u32* dst, const u32* src2d, u32 dispcnt, u16 mbright);
    void DrawScanlineB(u32 line, u32* dst, const u32* src2d, u32 dispcnt, u16 mbright);

    void DoCapture(u32 line, const u32* srcA2d, const u32* src3d);

    void ApplyMasterBrightness(u16 regval, u32* dst);
    void ExpandColor(u32* dst);
};

}

#endif // GPU_SOFT_H
