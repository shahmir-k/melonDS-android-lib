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

#ifndef GPU_OPENGL_H
#define GPU_OPENGL_H

#include "OpenGLSupport.h"
#include "GPU.h"
#include "GPU2D_OpenGL.h"
#include "GPU3D_OpenGL.h"
#include "GPU3D_Compute.h"
#ifdef LITEV_RENDER_THREAD
#include "GPU_RenderLog.h"
#endif

namespace melonDS
{

class GLRenderer : public Renderer
{
public:
    GLRenderer(melonDS::NDS& nds, bool compute);
    ~GLRenderer() override;
    bool Init() override;
    void Reset() override;
    void Stop() override;

    void PostSavestate() override;

    void SetRenderSettings(RendererSettings& settings) override;

    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;

    void VBlank() override;
    void VBlankEnd() override;

    void AllocCapture(u32 bank, u32 start, u32 len) override;
    void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete) override;

    bool GetFramebuffers(void** top, void** bottom) override;

    bool NeedsShaderCompile() override;
    void ShaderCompileStep(int& current, int& count) override;

#ifdef LITEV_RENDER_THREAD
    // --- R4 render-thread offload seam (docs/r4-render-thread-design.md) ---
    // Exported phase entry points the app tranche drives. See the definitions
    // in GPU_OpenGL.cpp for the precise contract and the documented boundary at
    // which this tranche stops (packet materialization + VRAM/palette shadow).
    void SetDeferredSubmit(bool enable) override;
    bool IsDeferredSubmit() const override { return DeferSubmit; }
    void SetRIRMode(bool enable) override { RIRMode = enable; }
    u64 GetRIRReplayCount() const override { return RIRReplayCount; }
    u64 GetRIRInlineGL() const override { return RIRInlineGL; }
    void SubmitFrame() override;
    void SwapBuffers() override;
#endif

private:
    friend class GLRenderer2D;
    friend class GLRenderer3D;
    friend class ComputeRenderer3D;

    bool IsCompute;

    int ScaleFactor;
    int ScreenW, ScreenH;

    GLuint RectVtxBuffer;
    GLuint RectVtxArray;

    GLuint OutputTex3D;
    GLuint OutputTex2D[2];

    struct sFinalPassConfig
    {
        u32 uScreenSwap[192];
        u32 uScaleFactor;
        u32 uAuxLayer;
        u32 uDispModeA;
        u32 uDispModeB;
        u32 uBrightModeA;
        u32 uBrightModeB;
        u32 uBrightFactorA;
        u32 uBrightFactorB;
        float uAuxColorFactor;
        u32 __pad0[3];
    } FinalPassConfig;

    GLuint FPShader;
    GLuint FPConfigUBO;

    GLuint FPVertexBufferID;
    GLuint FPVertexArrayID;

    GLuint AuxInputTex;                 // aux input (VRAM and mainmem FIFO)

    // texture/fb for display capture VRAM input
    GLuint CaptureVRAMTex;
    GLuint CaptureVRAMFB;

    GLuint FPOutputTex[2];               // final output
    GLuint FPOutputFB[2];

    struct sCaptureConfig
    {
        float uInvCaptureSize[2];
        u32 uSrcALayer;
        u32 uSrcBLayer;
        u32 uSrcBOffset;
        u32 uDstMode;
        u32 uBlendFactors[2];
        float uSrcAOffset[192];
        float uSrcBColorFactor;
        u32 __pad0[3];
    } CaptureConfig;

    GLuint CaptureShader;
    GLuint CaptureConfigUBO;

    GLuint CaptureVtxBuffer;
    GLuint CaptureVtxArray;

    GLuint CaptureOutput256FB[4];
    GLuint CaptureOutput256Tex;
    GLuint CaptureOutput128FB[16];
    GLuint CaptureOutput128Tex;

    GLuint CapDownShader;
    GLint CapDownInputLayerULoc;

    GLuint CaptureSyncFB;
    GLuint CaptureSyncTex;

    u16* AuxInputBuffer[2];
    u8 AuxUsageMask;

    u32 DispCntA, DispCntB;
    u16 MasterBrightnessA, MasterBrightnessB;
    u32 CaptureCnt;

    bool NeedPartialRender;
    int LastLine;
    int LastCapLine;
    int Aux0VRAMCap;

#ifdef LITEV_RENDER_THREAD
    // R4: deferred-submit mode selected by the app glue at emu start. When set
    // (and the frame is not capture-active), GL submission is meant to move to
    // the render thread's SubmitFrame() phase. See SubmitFrame() for the
    // boundary this tranche stops at. Default false -> submission inline in
    // RunFrame, byte-identical to flag-OFF.
    bool DeferSubmit = false;

    // R4 step-2 (this tranche): the 2D final-composite phase is deferred out of
    // RunFrame into SubmitFrame(). SubmitPending marks a frame whose VBlank
    // composite + buffer swap were deferred; SubmitReplaying is true only while
    // SubmitFrame() replays them (so GLRenderer2D::RenderScreen reads the
    // snapshotted 3D output instead of the live one). See SubmitFrame() for the
    // full contract and the 3D-output coupling this resolves.
    bool SubmitPending = false;
    bool SubmitReplaying = false;
    // Snapshot of the 3D color output (OutputTex3D) taken at the VBlank point,
    // before the next frame's Start3DRendering (VCount 215) overwrites the single
    // OutputTex3D. The deferred composite reads this shadow so its output is
    // byte-identical to the inline path.
    GLuint SubmitShadow3DTex = 0;
    GLuint SubmitShadow3DFB = 0;      // draw FBO: shadow attached
    GLuint SubmitShadow3DReadFB = 0;  // read FBO: OutputTex3D attached at blit time
    void Submit_Snapshot3D();

    // R4 Stage A — the per-frame GL command log (recipe §1). Two instances form
    // the depth-1 A/B double buffer (design §4.2); LogBuild points at the one the
    // current frame records into. Populated during RunFrame by the converted call
    // sites (recipe §1.2) and replayed by SubmitFrame(). Reset at StartFrame.
    // NOTE: call-site conversion (recipe §1.2) + Stage-B VRAM shadow (recipe §2)
    // are the remaining work; until every op is converted, LogBuild stays empty
    // and the existing final-composite-deferral path (Submit*Pending) is used.
    RenderLog RenderLogA;
    RenderLog RenderLogB;
    RenderLog* LogBuild = &RenderLogA;
    int LogBuildBank = 0;
    void StartFrameLog() override;   // called from GPU::StartFrame under DeferSubmit

    // R4 RIR (Record-and-Immediately-Replay, recipe §8). When RIRMode is set, the
    // converted per-scanline call sites (recipe §1.2) append a snapshot record to
    // LogBuild and IMMEDIATELY replay it (re-issue the GL from the snapshot) at the
    // same call site. Bit-exact by construction — same GL, same state, same moment —
    // while proving the record/replay plumbing one call site at a time. Phase 2
    // moves the replay to a real render thread; the record + replay bodies are the
    // same code. RIRReplayCount/RIRInlineGL are the counter proof: in RIR mode every
    // converted site goes through replay (RIRInlineGL stays 0; only arena overflow
    // would force an inline fallback). Independent of DeferSubmit.
    bool RIRMode = false;
    u64 RIRReplayCount = 0;   // converted sites that recorded + replayed
    u64 RIRInlineGL = 0;      // converted sites forced inline (overflow) — want 0
#endif

    // The 2D final-composite GL body (per-engine composite + final pass +
    // display capture). Called inline from VBlank() on the synchronous path and,
    // under LITEV_RENDER_THREAD deferred mode, from SubmitFrame() during replay.
    void VBlankSubmit();

    void SetScaleFactor(int scale);

    void RenderScreen(int ystart, int yend);
    void DoCapture(int ystart, int yend);
    void DownscaleCapture(int width, int height, int layer);
};

}

#endif // GPU_OPENGL_H
