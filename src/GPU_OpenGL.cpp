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

#include <string.h>
#include <chrono>
#include "NDS.h"
#include "GPU_OpenGL.h"
// R3: GL per-frame call counters. Must come AFTER the GL headers above so the
// wrapping macros can #undef/redefine the (GLES_Compat) glTex* entry points.
#include "LiteProfileGL.h"
// R3: GL redundant-state diet. Must come AFTER LiteProfileGL.h so it can take
// over the state-changing entry points (see LiteGLStateCache.h). Inert unless
// LITEV_GL_STATE_CACHE && __ANDROID__.
#include "LiteGLStateCache.h"
#if LITEV_PROFILE && defined(__ANDROID__)
#include <android/log.h>
#endif

namespace melonDS
{
using Platform::Log;
using Platform::LogLevel;

#if LITEV_PROFILE && defined(__ANDROID__)
// R3: emit one "LITEV_GL:" logcat line per 60 rendered frames from the core
// renderer itself (least-invasive route: the app glue's LITEV_PROF profiler
// does not read the core LiteProfile counters). Values are averaged per frame
// over the 60-frame window. Each GL counter is read-and-reset (exchange) here
// at end-of-frame so no external per-frame Reset() is required on Android.
static void LiteVGLFrameReport()
{
    using namespace melonDS::LiteProfile;
    static uint64_t s_draws = 0, s_binds = 0, s_progs = 0, s_unif = 0, s_texp = 0;
    static uint64_t s_bufb = 0, s_bufd = 0, s_fbo = 0, s_texu = 0, s_bytes = 0;
    static uint32_t s_n = 0;

    s_draws += g_Frame.GLDrawCalls.exchange(0, std::memory_order_relaxed);
    s_binds += g_Frame.GLTexBinds.exchange(0, std::memory_order_relaxed);
    s_progs += g_Frame.GLProgramSwitches.exchange(0, std::memory_order_relaxed);
    s_unif  += g_Frame.GLUniformCalls.exchange(0, std::memory_order_relaxed);
    s_texp  += g_Frame.GLTexParamCalls.exchange(0, std::memory_order_relaxed);
    s_bufb  += g_Frame.GLBufferBinds.exchange(0, std::memory_order_relaxed);
    s_bufd  += g_Frame.GLBufferUploads.exchange(0, std::memory_order_relaxed);
    s_fbo   += g_Frame.GLFramebufferBinds.exchange(0, std::memory_order_relaxed);
    s_texu  += g_Frame.GLTexUploads.exchange(0, std::memory_order_relaxed);
    s_bytes += g_Frame.GLUploadBytes.exchange(0, std::memory_order_relaxed);
    s_n++;

    if (s_n >= 60)
    {
        __android_log_print(ANDROID_LOG_INFO, "LITEV_GL",
            "60f avg/frame: draws=%llu binds=%llu progs=%llu uniforms=%llu "
            "texparam=%llu bufbind=%llu bufdata=%llu fbo=%llu texup=%llu uploadKB=%llu",
            (unsigned long long)(s_draws / s_n),
            (unsigned long long)(s_binds / s_n),
            (unsigned long long)(s_progs / s_n),
            (unsigned long long)(s_unif  / s_n),
            (unsigned long long)(s_texp  / s_n),
            (unsigned long long)(s_bufb  / s_n),
            (unsigned long long)(s_bufd  / s_n),
            (unsigned long long)(s_fbo   / s_n),
            (unsigned long long)(s_texu  / s_n),
            (unsigned long long)((s_bytes / s_n) / 1024));
        s_draws = s_binds = s_progs = s_unif = s_texp = 0;
        s_bufb = s_bufd = s_fbo = s_texu = s_bytes = 0;
        s_n = 0;
    }
}
#endif

#include "OpenGL_shaders/FinalPassVS.h"
#include "OpenGL_shaders/FinalPassFS.h"
#include "OpenGL_shaders/CaptureVS.h"
#include "OpenGL_shaders/CaptureFS.h"
#include "OpenGL_shaders/CaptureDownscaleVS.h"
#include "OpenGL_shaders/CaptureDownscaleFS.h"


GLRenderer::GLRenderer(melonDS::NDS& nds, bool compute)
    : Renderer(nds.GPU)
{
    AuxInputBuffer[0] = new u16[256 * 256];
    AuxInputBuffer[1] = new u16[256 * 192];

    Rend2D_A = std::make_unique<GLRenderer2D>(GPU.GPU2D_A, *this);
    Rend2D_B = std::make_unique<GLRenderer2D>(GPU.GPU2D_B, *this);

    // TODO, eventually: figure out a nicer way to support different 3D renderers?
    IsCompute = compute;
    if (IsCompute)
        Rend3D = std::make_unique<ComputeRenderer3D>(GPU.GPU3D, *this);
    else
        Rend3D = std::make_unique<GLRenderer3D>(GPU.GPU3D, *this);

    ScaleFactor = 0;
}

#define glTexParams(target, wrap) \
    glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap); \
    glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap); \
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST); \
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

bool GLRenderer::Init()
{
    assert(glEnable != nullptr);

    // R3: fresh GL context -> the state-cache shadow must start empty so its
    // first assumptions match the real (default) context state.
    LITEV_GL_RESET_STATE_CACHE();

    GLint uniloc;

    // compile shaders

    if (!OpenGL::CompileVertexFragmentProgram(FPShader,
                                              kFinalPassVS, kFinalPassFS,
                                              "2DFinalPassShader",
                                              {{"vPosition", 0}},
                                              {{"oTopColor", 0}, {"oBottomColor", 1}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(CaptureShader,
                                              kCaptureVS, kCaptureFS,
                                              "2DCaptureShader",
                                              {{"vPosition", 0}, {"vTexcoord", 1}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(CapDownShader,
                                              kCaptureDownscaleVS, kCaptureDownscaleFS,
                                              "2DCaptureDownscaleShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    // vertex buffers

    const float rectvertices[2*2*3] = {
            0, 1,   1, 0,   1, 1,
            0, 1,   0, 0,   1, 0
    };

    glGenBuffers(1, &RectVtxBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, RectVtxBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rectvertices), rectvertices, GL_STATIC_DRAW);

    glGenVertexArrays(1, &RectVtxArray);
    glBindVertexArray(RectVtxArray);
    glEnableVertexAttribArray(0); // position
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    float vertices[12][2];
#define SETVERTEX(i, x, y) \
    vertices[i][0] = x; \
    vertices[i][1] = y;

    SETVERTEX(0, -1, 1);
    SETVERTEX(1, 1, -1);
    SETVERTEX(2, 1, 1);
    SETVERTEX(3, -1, 1);
    SETVERTEX(4, -1, -1);
    SETVERTEX(5, 1, -1);

#undef SETVERTEX

    // final pass vertex data: 2x position, 2x texcoord
    glGenBuffers(1, &FPVertexBufferID);
    glBindBuffer(GL_ARRAY_BUFFER, FPVertexBufferID);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices[0], GL_STATIC_DRAW);

    glGenVertexArrays(1, &FPVertexArrayID);
    glBindVertexArray(FPVertexArrayID);
    glEnableVertexAttribArray(0); // position
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glGenFramebuffers(2, &FPOutputFB[0]);

    // capture vertex data: 2x position, 2x texcoord
    glGenBuffers(1, &CaptureVtxBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, CaptureVtxBuffer);
    glBufferData(GL_ARRAY_BUFFER, 2 * 6 * 4 * sizeof(u16), nullptr, GL_STREAM_DRAW);

    glGenVertexArrays(1, &CaptureVtxArray);
    glBindVertexArray(CaptureVtxArray);
    glEnableVertexAttribArray(0); // position
    glVertexAttribIPointer(0, 2, GL_SHORT, 4 * sizeof(u16), (void*)0);
    glEnableVertexAttribArray(1); // texcoord
    glVertexAttribIPointer(1, 2, GL_SHORT, 4 * sizeof(u16), (void*)(2 * sizeof(u16)));

    // textures / framebuffers

    glGenTextures(1, &AuxInputTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, AuxInputTex);
    glTexParams(GL_TEXTURE_2D_ARRAY, GL_REPEAT);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB5_A1, 256, 256, 2, 0, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, nullptr);

    glGenTextures(1, &CaptureVRAMTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureVRAMTex);
    glTexParams(GL_TEXTURE_2D_ARRAY, GL_REPEAT);
    glGenFramebuffers(1, &CaptureVRAMFB);

    glGenTextures(2, FPOutputTex);
    for (int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D_ARRAY, FPOutputTex[i]);
        glTexParams(GL_TEXTURE_2D_ARRAY, GL_CLAMP_TO_EDGE);
    }

    glGenTextures(1, &CaptureOutput256Tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureOutput256Tex);
    glTexParams(GL_TEXTURE_2D_ARRAY, GL_REPEAT);
    glGenFramebuffers(4, CaptureOutput256FB);

    glGenTextures(1, &CaptureOutput128Tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureOutput128Tex);
    glTexParams(GL_TEXTURE_2D_ARRAY, GL_REPEAT);
    glGenFramebuffers(16, CaptureOutput128FB);

    glGenTextures(1, &CaptureSyncTex);
    glBindTexture(GL_TEXTURE_2D, CaptureSyncTex);
    glTexParams(GL_TEXTURE_2D, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, 256, 256, 0, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, nullptr);

    glGenFramebuffers(1, &CaptureSyncFB);
    glBindFramebuffer(GL_FRAMEBUFFER, CaptureSyncFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureSyncTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    // UBOs

    glGenBuffers(1, &FPConfigUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, FPConfigUBO);
    static_assert((sizeof(sFinalPassConfig) & 15) == 0);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(sFinalPassConfig), nullptr, GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 30, FPConfigUBO);

    glGenBuffers(1, &CaptureConfigUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, CaptureConfigUBO);
    static_assert((sizeof(sCaptureConfig) & 15) == 0);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(sCaptureConfig), nullptr, GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 31, CaptureConfigUBO);

    // shader config

    glUseProgram(FPShader);

    uniloc = glGetUniformLocation(FPShader, "MainInputTexA");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(FPShader, "MainInputTexB");
    glUniform1i(uniloc, 1);
    uniloc = glGetUniformLocation(FPShader, "AuxInputTex");
    glUniform1i(uniloc, 2);

    uniloc = glGetUniformBlockIndex(FPShader, "ubFinalPassConfig");
    glUniformBlockBinding(FPShader, uniloc, 30);


    glUseProgram(CaptureShader);

    uniloc = glGetUniformLocation(CaptureShader, "InputTexA");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(CaptureShader, "InputTexB");
    glUniform1i(uniloc, 1);

    uniloc = glGetUniformBlockIndex(CaptureShader, "ubCaptureConfig");
    glUniformBlockBinding(CaptureShader, uniloc, 31);


    glUseProgram(CapDownShader);

    uniloc = glGetUniformLocation(CapDownShader, "InputTex");
    glUniform1i(uniloc, 0);

    CapDownInputLayerULoc = glGetUniformLocation(CapDownShader, "uInputLayer");


    auto rend2DA = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    if (!rend2DA->InitShaders()) return false;
    auto rend2DB = dynamic_cast<GLRenderer2D*>(Rend2D_B.get());
    if (!rend2DB->InitShaders(*rend2DA)) return false;

    if (!Rend2D_A->Init()) return false;
    if (!Rend2D_B->Init()) return false;
    if (!Rend3D->Init()) return false;

#ifdef LITEV_RENDER_THREAD
    // R4 deferred-submit: shadow of the 3D color output + its blit FBOs. The
    // texture is sized in SetScaleFactor (matching OutputTex3D = RGBA8,
    // ScreenW x ScreenH). The read FBO gets OutputTex3D attached at blit time.
    glGenTextures(1, &SubmitShadow3DTex);
    glBindTexture(GL_TEXTURE_2D, SubmitShadow3DTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &SubmitShadow3DFB);
    glBindFramebuffer(GL_FRAMEBUFFER, SubmitShadow3DFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, SubmitShadow3DTex, 0);
    glGenFramebuffers(1, &SubmitShadow3DReadFB);
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

GLRenderer::~GLRenderer()
{
    glDeleteProgram(FPShader);
    glDeleteProgram(CaptureShader);
    glDeleteProgram(CapDownShader);

    glDeleteBuffers(1, &RectVtxBuffer);
    glDeleteVertexArrays(1, &RectVtxArray);

    glDeleteBuffers(1, &FPVertexBufferID);
    glDeleteVertexArrays(1, &FPVertexArrayID);

    glDeleteBuffers(1, &CaptureVtxBuffer);
    glDeleteVertexArrays(1, &CaptureVtxArray);

    glDeleteFramebuffers(2, FPOutputFB);
    glDeleteTextures(1, &AuxInputTex);
    glDeleteTextures(1, &CaptureVRAMTex);
    glDeleteTextures(2, FPOutputTex);

    delete[] AuxInputBuffer[0];
    delete[] AuxInputBuffer[1];

    glDeleteTextures(1, &CaptureOutput256Tex);
    glDeleteFramebuffers(4, CaptureOutput256FB);
    glDeleteTextures(1, &CaptureOutput128Tex);
    glDeleteFramebuffers(16, CaptureOutput128FB);
    glDeleteTextures(1, &CaptureSyncTex);
    glDeleteFramebuffers(1, &CaptureSyncFB);

    glDeleteBuffers(1, &FPConfigUBO);
    glDeleteBuffers(1, &CaptureConfigUBO);

#ifdef LITEV_RENDER_THREAD
    glDeleteTextures(1, &SubmitShadow3DTex);
    glDeleteFramebuffers(1, &SubmitShadow3DFB);
    glDeleteFramebuffers(1, &SubmitShadow3DReadFB);
#endif

    auto rend2D = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    rend2D->DeleteShaders();
}

void GLRenderer::Reset()
{
    // R3: renderer reset may follow a context reset / savestate load; drop any
    // stale state-cache assumptions.
    LITEV_GL_RESET_STATE_CACHE();

    memset(&FinalPassConfig, 0, sizeof(FinalPassConfig));
    memset(&CaptureConfig, 0, sizeof(CaptureConfig));

    AuxUsageMask = 0;

    DispCntA = 0;
    DispCntB = 0;
    MasterBrightnessA = 0;
    MasterBrightnessB = 0;
    CaptureCnt = 0;

    NeedPartialRender = false;
    LastLine = 0;
    LastCapLine = 0;
    Aux0VRAMCap = -1;

#ifdef LITEV_RENDER_THREAD
    // Drain protocol (design §5.2): reset/savestate-load discards any
    // half-captured deferred frame so no stale composite/log is replayed.
    SubmitPending = false;
    SubmitReplaying = false;
    DeferReplay = false;
    RenderLogA.Reset();
    RenderLogB.Reset();
#endif

    Rend2D_A->Reset();
    Rend2D_B->Reset();
    Rend3D->Reset();
}

void GLRenderer::Stop()
{
    // TODO clear buffers
    // TODO: do we even need this anymore?
}

void GLRenderer::PostSavestate()
{
    Reset();

    auto rend2D = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    rend2D->PostSavestate();
    rend2D = dynamic_cast<GLRenderer2D*>(Rend2D_B.get());
    rend2D->PostSavestate();
}


void GLRenderer::SetRenderSettings(RendererSettings& settings)
{
    SetScaleFactor(settings.ScaleFactor);

    auto rend2d = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    rend2d->SetScaleFactor(settings.ScaleFactor);

    rend2d = dynamic_cast<GLRenderer2D*>(Rend2D_B.get());
    rend2d->SetScaleFactor(settings.ScaleFactor);

    if (IsCompute)
    {
        auto rend3d = dynamic_cast<ComputeRenderer3D *>(Rend3D.get());
        rend3d->SetRenderSettings(settings.ScaleFactor, settings.HiresCoordinates);
    }
    else
    {
        auto rend3d = dynamic_cast<GLRenderer3D *>(Rend3D.get());
        rend3d->SetRenderSettings(settings.ScaleFactor, settings.BetterPolygons);
    }
}


void GLRenderer::SetScaleFactor(int scale)
{
    if (scale == ScaleFactor)
        return;

    ScaleFactor = scale;
    ScreenW = 256 * scale;
    ScreenH = 192 * scale;

    const GLenum fbassign2[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};

    glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureOutput256Tex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, 256*ScaleFactor, 256*ScaleFactor, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    for (int i = 0; i < 4; i++)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, CaptureOutput256FB[i]);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureOutput256Tex, 0, i);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureOutput128Tex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, 128*ScaleFactor, 128*ScaleFactor, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    for (int i = 0; i < 16; i++)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, CaptureOutput128FB[i]);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureOutput128Tex, 0, i);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureVRAMTex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, 256*ScaleFactor, 256*ScaleFactor, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindFramebuffer(GL_FRAMEBUFFER, CaptureVRAMFB);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureVRAMTex, 0, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    for (int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D_ARRAY, FPOutputTex[i]);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, ScreenW, ScreenH, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, FPOutputFB[i]);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, FPOutputTex[i], 0, 0);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, FPOutputTex[i], 0, 1);
        glDrawBuffers(2, fbassign2);
    }

#ifdef LITEV_RENDER_THREAD
    // R4 deferred-submit: size the 3D-output shadow to match OutputTex3D.
    if (SubmitShadow3DTex)
    {
        glBindTexture(GL_TEXTURE_2D, SubmitShadow3DTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


void GLRenderer::DrawScanline(u32 line)
{
    u32 dispcnt_a_diff = DispCntA ^ GPU.GPU2D_A.DispCnt;
    u32 dispcnt_b_diff = DispCntB ^ GPU.GPU2D_B.DispCnt;
    u32 capturecnt_diff = CaptureCnt ^ GPU.CaptureCnt;

    bool need_render = false;
    bool need_capture = false;

    if (dispcnt_a_diff & 0xF0000)
        need_render = true;
    else if (dispcnt_b_diff & 0x10000)
        need_render = true;
    else if (MasterBrightnessA != GPU.MasterBrightnessA ||
             MasterBrightnessB != GPU.MasterBrightnessB)
        need_render = true;

    if (GPU.CaptureEnable && (capturecnt_diff & 0x7FFFFFFF))
    {
        need_render = true;
        need_capture = true;
    }

    NeedPartialRender = need_render;
    Rend2D_A->DrawScanline(line);
    Rend2D_B->DrawScanline(line);

    if (need_render && (line > 0))
    {
#ifdef LITEV_RENDER_THREAD
        if (RIRMode || DeferReplay) RIRRecordFinalPass(LastLine, line);
        else
#endif
            RenderScreen(LastLine, line);
        LastLine = line;
    }

    if (need_capture && (line > 0))
    {
        DoCapture(LastCapLine, line);
        LastCapLine = line;
    }

    DispCntA = GPU.GPU2D_A.DispCnt;
    DispCntB = GPU.GPU2D_B.DispCnt;
    MasterBrightnessA = GPU.MasterBrightnessA;
    MasterBrightnessB = GPU.MasterBrightnessB;
    CaptureCnt = GPU.CaptureCnt;

    FinalPassConfig.uScreenSwap[line] = GPU.ScreenSwap;

    u32 dispcnt = GPU.GPU2D_A.DispCnt;
    u32 dispmode = (dispcnt >> 16) & 0x3;
    u32 capcnt = GPU.CaptureCnt;
    u32 capsel = (capcnt >> 29) & 0x3;
    u32 capA = (capcnt >> 24) & 0x1;
    u32 capB = (capcnt >> 25) & 0x1;
    bool checkcap = GPU.CaptureEnable && (capsel != 0);

    if (GPU.CaptureEnable && (capsel != 1))
    {
        if (capA == 0)
            CaptureConfig.uSrcAOffset[line] = 0;
        else
        {
            int xpos = GPU.GPU3D.GetRenderXPos() & 0x1FF;
            xpos -= ((xpos & 0x100) << 1);
            CaptureConfig.uSrcAOffset[line] = (float)xpos / 256.f;
        }
    }

    if ((dispmode == 2) || (checkcap && (capB == 0)))
    {
        AuxUsageMask |= (1<<0);

        u32 vrambank = (dispcnt >> 18) & 0x3;
        u32 vramoffset = line * 256;
        u32 outoffset = line * 256;
        if (dispmode != 2)
        {
            u32 yoff = ((capcnt >> 26) & 0x3) << 14;
            vramoffset += yoff;
            outoffset += yoff;
        }

        vramoffset &= 0xFFFF;
        outoffset &= 0xFFFF;

        u16* adst = &AuxInputBuffer[0][outoffset];

        if (GPU.VRAMMap_LCDC & (1<<vrambank))
        {
            u16* vram = (u16*)GPU.VRAM[vrambank];

            for (int i = 0; i < 256; i++)
            {
                adst[i] = vram[vramoffset];
                vramoffset++;
            }
        }
        else
        {
            for (int i = 0; i < 256; i++)
            {
                adst[i] = 0;
            }
        }
    }

    if ((dispmode == 3) || (checkcap && (capB == 1)))
    {
        AuxUsageMask |= (1<<1);

        u16* adst = &AuxInputBuffer[1][line * 256];
        for (int i = 0; i < 256; i++)
        {
            adst[i] = GPU.DispFIFOBuffer[i];
        }
    }
}

void GLRenderer::DrawSprites(u32 line)
{
    Rend2D_A->DrawSprites(line);
    Rend2D_B->DrawSprites(line);
}


void GLRenderer::RenderScreen(int ystart, int yend)
{
    int backbuf = BackBuffer;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, FPOutputFB[backbuf]);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, ScreenW, ScreenH);

    // TODO: adjust incoming vertices instead of doing this?
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, ystart * ScaleFactor, ScreenW, (yend-ystart) * ScaleFactor);

    int vramcap = -1;
    if (AuxUsageMask & (1<<0))
    {
        u32 vrambank = (DispCntA >> 18) & 0x3;
        if (GPU.VRAMMap_LCDC & (1<<vrambank))
            vramcap = GPU.GetCaptureBlock_LCDC(vrambank << 17);
    }
    Aux0VRAMCap = vramcap;

    if (!GPU.ScreensEnabled)
    {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    else
    {
        glUseProgram(FPShader);

        FinalPassConfig.uScaleFactor = ScaleFactor;
        FinalPassConfig.uDispModeA = (DispCntA >> 16) & 0x3;
        FinalPassConfig.uDispModeB = (DispCntB >> 16) & 0x1;
        FinalPassConfig.uBrightModeA = (MasterBrightnessA >> 14) & 0x3;
        FinalPassConfig.uBrightModeB = (MasterBrightnessB >> 14) & 0x3;
        FinalPassConfig.uBrightFactorA = std::min(MasterBrightnessA & 0x1F, 16);
        FinalPassConfig.uBrightFactorB = std::min(MasterBrightnessB & 0x1F, 16);

        if (AuxUsageMask)
        {
            glBindTexture(GL_TEXTURE_2D_ARRAY, AuxInputTex);
            if ((AuxUsageMask & (1<<0)) && (vramcap == -1))
            {
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, 256, 256, 1, GL_RGBA,
                                GL_UNSIGNED_SHORT_1_5_5_5_REV, AuxInputBuffer[0]);
            }
            if (AuxUsageMask & (1<<1))
            {
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 1, 256, 192, 1, GL_RGBA,
                                GL_UNSIGNED_SHORT_1_5_5_5_REV, AuxInputBuffer[1]);
            }
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, OutputTex2D[0]);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, OutputTex2D[1]);

        glActiveTexture(GL_TEXTURE2);
        u32 modeA = (DispCntA >> 16) & 0x3;
        if ((modeA == 2) && (vramcap != -1))
        {
            glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureOutput256Tex);
            FinalPassConfig.uAuxLayer = vramcap >> 2;
            FinalPassConfig.uAuxColorFactor = 63.75f;
        }
        else if (modeA >= 2)
        {
            glBindTexture(GL_TEXTURE_2D_ARRAY, AuxInputTex);
            FinalPassConfig.uAuxLayer = (modeA - 2);
            FinalPassConfig.uAuxColorFactor = 62.f;
        }

        glBindBuffer(GL_UNIFORM_BUFFER, FPConfigUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(FinalPassConfig), &FinalPassConfig);

        glBindBuffer(GL_ARRAY_BUFFER, FPVertexBufferID);
        glBindVertexArray(FPVertexArrayID);
        glDrawArrays(GL_TRIANGLES, 0, 2*3);
    }

    glDisable(GL_SCISSOR_TEST);
}

void GLRenderer::VBlank()
{
#ifdef LITEV_RENDER_THREAD
    // R4 step-2 capture/submit split (single-thread this tranche). When deferred
    // submission is selected and the frame does NOT use display capture (the one
    // feedback edge, §5.1 Tier 1 -> synchronous), do not issue the 2D final
    // composite now. Snapshot the 3D color output (the single GL texture the next
    // frame's Start3DRendering at VCount 215 would overwrite before SubmitFrame
    // runs) and mark the composite pending; SubmitFrame() replays it after
    // RunFrame. SwapBuffers() is deferred in lockstep so the replayed composite
    // still targets THIS frame's back buffer. Every other input the deferred
    // composite reads (the per-engine composite outputs, prerendered layer/sprite
    // textures, config UBOs, aux buffers) is not mutated between here and
    // SubmitFrame() during single-thread operation (no DrawScanline runs at
    // VCount >= 192; DrawSprites(0) at VCount 262 only re-uploads OBJ VRAM and
    // marks state dirty for the NEXT frame's prerender). Result: byte-identical
    // to the inline path, verified by on-device screenshot parity.
    if (DeferReplay)
    {
        // R4 STEP 1 — zero-GL RunFrame. Phase 2 full deferred submit. Since Stage 1b
        // the 3D raster is itself deferred and (recorded at VCount 215) replays LAST
        // in the log, AFTER these VBlank 2D composites. So at SubmitFrame the 2D
        // composites read the live OutputTex3D still holding the PREVIOUS frame's 3D
        // — identical to the inline path — and the VBlank-point 3D snapshot blit
        // (Submit_Snapshot3D) is redundant and REMOVED. With it gone this DeferReplay
        // VBlank issues NO GL: the whole RunFrame is now GL-free, which is what lets
        // the render thread own OutputTex3D (and all GL) exclusively. RECORD the
        // VBlank-span 2D work (per-engine sprite raster + composite, and the final
        // pass) into the log; no GL is issued. The whole log replays at SubmitFrame().
        Rend2D_A->VBlank();                 // records RenderSpritesSpan + Composite2D
        Rend2D_B->VBlank();
        RIRRecordFinalPass(LastLine, 192);  // records FinalPassSpan
        LastLine = 0;
        LastCapLine = 0;
        SubmitPending = true;
        return;
    }

    // Legacy narrow deferral (final composite only) — retained for the RIR bring-up
    // path where DeferSubmit is set without full deferred replay. DeferReplay above
    // supersedes it whenever the renderer is in Phase-2 deferred mode.
    if (DeferSubmit && !RIRMode && !GPU.CaptureActiveThisFrame)
    {
        Submit_Snapshot3D();
        SubmitPending = true;
        return;
    }
#endif

    VBlankSubmit();
}

void GLRenderer::VBlankSubmit()
{
    Rend2D_A->VBlank();
    Rend2D_B->VBlank();

#ifdef LITEV_RENDER_THREAD
    if (RIRMode) RIRRecordFinalPass(LastLine, 192);
    else
#endif
        RenderScreen(LastLine, 192);

    if (GPU.CaptureEnable)
        DoCapture(LastCapLine, 192);

    LastLine = 0;
    LastCapLine = 0;

#if LITEV_PROFILE && defined(__ANDROID__)
    // R3: once-per-frame hook — VBlank runs exactly once per rendered frame and
    // GL context is current here (RenderScreen above submits the compositor
    // draws). Emits the LITEV_GL logcat line every 60 frames.
    LiteVGLFrameReport();
#endif

    // R3 state-cache: VBlank is the end of this frame's core GL work and runs
    // exactly once per frame. Resetting the shadow HERE means no bind assumption
    // survives into the app glue's blit/present or into the next frame — so even
    // if the app touches GL between frames, the next frame's first binds always
    // issue. This is the staleness guard that makes the cache transparent.
    LITEV_GL_RESET_STATE_CACHE();
}

void GLRenderer::VBlankEnd()
{
    AuxUsageMask = 0;
}


void GLRenderer::DoCapture(int ystart, int yend)
{
    u32 dispcnt = DispCntA;
    u32 capcnt = CaptureCnt;
    u32 dispmode = (dispcnt >> 16) & 0x3;
    u32 srcA = (capcnt >> 24) & 0x1;
    u32 srcB = (capcnt >> 25) & 0x1;
    u32 srcBblock = (dispcnt >> 18) & 0x3;
    u32 srcBoffset = (dispmode == 2) ? 0 : ((capcnt >> 26) & 0x3);
    u32 dstblock = (capcnt >> 16) & 0x3;
    u32 dstoffset = (capcnt >> 18) & 0x3;
    u32 capsize = (capcnt >> 20) & 0x3;
    u32 dstmode = (capcnt >> 29) & 0x3;
    u32 eva = std::min(capcnt & 0x1F, 16u);
    u32 evb = std::min((capcnt >> 8) & 0x1F, 16u);

    // determine the region we're going to capture to

    int dstwidth, dstheight;

    if (capsize == 0)
    {
        dstwidth = 128;
        dstheight = 128;
    }
    else
    {
        dstwidth = 256;
        dstheight = 64 * capsize;
    }

    if (ystart >= dstheight)
        return;
    if (yend > dstheight)
        yend = dstheight;

    glUseProgram(CaptureShader);

    GLuint inputA;
    if (srcA)
        inputA = OutputTex3D;
    else
        inputA = OutputTex2D[0];

    bool useSrcB = (dstmode == 1) || (dstmode == 2 && evb > 0);

    GLuint inputB = AuxInputTex;
    u32 layerB = srcB;
    CaptureConfig.uSrcBColorFactor = 248.f;

    if (useSrcB && (Aux0VRAMCap != -1))
    {
        // hi-res VRAM
        if (dstblock == srcBblock)
        {
            // we are reading from the same block we are capturing to
            // on hardware, it would read the old VRAM contents, then write new stuff
            // but we can't do that with OpenGL
            // so we need to blit it to a temporary framebuffer

            int blitY0 = (srcBoffset * 64) + ystart;
            int blitY1 = (srcBoffset * 64) + yend;

            if (dstoffset != srcBoffset)
                Log(LogLevel::Error, "GPU_OpenGL: MISMATCHED VRAM OFFSETS ON SAME BANK!!! bank=%d src=%d dst=%d\n",
                       dstblock, srcBoffset, dstoffset);

            glBindFramebuffer(GL_READ_FRAMEBUFFER, CaptureOutput256FB[srcBblock]);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, CaptureVRAMFB);

            if (blitY1 > 256)
            {
                // wraparound
                glBlitFramebuffer(0, blitY0*ScaleFactor, 256*ScaleFactor, 256*ScaleFactor,
                                  0, blitY0*ScaleFactor, 256*ScaleFactor, 256*ScaleFactor,
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glBlitFramebuffer(0, 0, 256*ScaleFactor, (blitY1-256)*ScaleFactor,
                                  0, 0, 256*ScaleFactor, (blitY1-256)*ScaleFactor,
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }
            else
            {
                // straightforward
                glBlitFramebuffer(0, blitY0*ScaleFactor, 256*ScaleFactor, blitY1*ScaleFactor,
                                  0, blitY0*ScaleFactor, 256*ScaleFactor, blitY1*ScaleFactor,
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }

            inputB = CaptureVRAMTex;
            layerB = 0;
        }
        else
        {
            // if it's a different bank, we can just use it as-is
            inputB = CaptureOutput256Tex;
            layerB = srcBblock;
        }

        CaptureConfig.uSrcBColorFactor = 255.f;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    if (capsize == 0)
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, CaptureOutput128FB[(dstblock << 2) | dstoffset]);
        glViewport(0, 0, 128*ScaleFactor, 128*ScaleFactor);
    }
    else
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, CaptureOutput256FB[dstblock]);
        glViewport(0, 0, 256*ScaleFactor, 256*ScaleFactor);
    }

    CaptureConfig.uInvCaptureSize[0] = 1.f / (float)dstwidth;
    CaptureConfig.uInvCaptureSize[1] = 1.f / (float)dstheight;

    CaptureConfig.uSrcALayer = srcA;

    if (srcB == 0)
        CaptureConfig.uSrcBOffset = 64 * srcBoffset;
    else
        CaptureConfig.uSrcBOffset = 0;

    CaptureConfig.uSrcBLayer = layerB;

    CaptureConfig.uDstMode = dstmode;
    CaptureConfig.uBlendFactors[0] = eva;
    CaptureConfig.uBlendFactors[1] = evb;

    glBindBuffer(GL_UNIFORM_BUFFER, CaptureConfigUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CaptureConfig), &CaptureConfig);

    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputA);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, inputB);

    u16 vtxbuf[12 * 4];
    u16* vptr = vtxbuf;
    int numvtx;

    // y0/y1 = coordinates in destination buffer
    // t0/t1 = coordinates in source buffers
    if (capsize == 0) dstoffset = 0;
    int y0 = (dstoffset * 64) + ystart;
    int y1 = (dstoffset * 64) + yend;
    int t0 = ystart;
    int t1 = yend;

    int bufferheight = (capsize == 0) ? 128 : 256;
    if (y1 > bufferheight)
    {
        // wraparound
        int y2 = bufferheight;
        int t2 = t0 + (y2 - y0);
        *vptr++ = 0;        *vptr++ = y2; *vptr++ = 0;         *vptr++ = t2;
        *vptr++ = dstwidth; *vptr++ = y0; *vptr++ = dstwidth;  *vptr++ = t0;
        *vptr++ = dstwidth; *vptr++ = y2; *vptr++ = dstwidth;  *vptr++ = t2;
        *vptr++ = 0;        *vptr++ = y2; *vptr++ = 0;         *vptr++ = t2;
        *vptr++ = 0;        *vptr++ = y0; *vptr++ = 0;         *vptr++ = t0;
        *vptr++ = dstwidth; *vptr++ = y0; *vptr++ = dstwidth;  *vptr++ = t0;

        y2 = y1 - bufferheight;
        *vptr++ = 0;        *vptr++ = y2; *vptr++ = 0;         *vptr++ = t1;
        *vptr++ = dstwidth; *vptr++ = 0;  *vptr++ = dstwidth;  *vptr++ = t2;
        *vptr++ = dstwidth; *vptr++ = y2; *vptr++ = dstwidth;  *vptr++ = t1;
        *vptr++ = 0;        *vptr++ = y2; *vptr++ = 0;         *vptr++ = t1;
        *vptr++ = 0;        *vptr++ = 0;  *vptr++ = 0;         *vptr++ = t2;
        *vptr++ = dstwidth; *vptr++ = 0;  *vptr++ = dstwidth;  *vptr++ = t2;

        numvtx = 12;
    }
    else
    {
        // straightforward
        *vptr++ = 0;        *vptr++ = y1; *vptr++ = 0;         *vptr++ = t1;
        *vptr++ = dstwidth; *vptr++ = y0; *vptr++ = dstwidth;  *vptr++ = t0;
        *vptr++ = dstwidth; *vptr++ = y1; *vptr++ = dstwidth;  *vptr++ = t1;
        *vptr++ = 0;        *vptr++ = y1; *vptr++ = 0;         *vptr++ = t1;
        *vptr++ = 0;        *vptr++ = y0; *vptr++ = 0;         *vptr++ = t0;
        *vptr++ = dstwidth; *vptr++ = y0; *vptr++ = dstwidth;  *vptr++ = t0;

        numvtx = 6;
    }

    glBindBuffer(GL_ARRAY_BUFFER, CaptureVtxBuffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, numvtx * 4 * sizeof(u16), vtxbuf);

    glBindVertexArray(CaptureVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, numvtx);
}


void GLRenderer::AllocCapture(u32 bank, u32 start, u32 len)
{
    auto rend2D = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    rend2D->LayerConfigDirty = true;
    rend2D->SpriteConfigDirty = true;
    rend2D = dynamic_cast<GLRenderer2D*>(Rend2D_B.get());
    rend2D->LayerConfigDirty = true;
    rend2D->SpriteConfigDirty = true;
}

void GLRenderer::DownscaleCapture(int width, int height, int layer)
{
    // downscale a hi-res capture buffer to 1x IR, and convert to RGBA5551
    // we need to do this with a shader so we can accurately downscale color components

    glUseProgram(CapDownShader);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, CaptureSyncFB);

    glViewport(0, 0, width, height);

    glActiveTexture(GL_TEXTURE0);
    if (width == 128)
        glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureOutput128Tex);
    else
        glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureOutput256Tex);
    glUniform1i(CapDownInputLayerULoc, layer);

    glBindBuffer(GL_ARRAY_BUFFER, RectVtxBuffer);
    glBindVertexArray(RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);
}

void GLRenderer::SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete)
{
    if (!complete)
        Log(LogLevel::Error, "GPU_OpenGL: !!! READING VRAM AS IT IS BEING CAPTURED TO\n");

    u8* vram = GPU.VRAM[bank];

    glDisable(GL_DITHER);

    if (len == 0) // 128x128
    {
        DownscaleCapture(128, 128, (bank<<2) | start);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, CaptureSyncFB);

        glReadPixels(0, 0, 128, 128,
                     GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, &vram[start * 64 * 512]);

        for (u32 j = start * 64; j < (start+1) * 64; j++)
            GPU.VRAMDirty[bank][j] = true;
    }
    else
    {
        DownscaleCapture(256, 256, bank);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, CaptureSyncFB);

        u32 pos = start;
        for (u32 i = 0; i < len;)
        {
            u32 end = pos + len;
            if (end > 4)
                end = 4;

            glReadPixels(0, pos * 64, 256, (end - pos) * 64,
                         GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, &vram[pos * 64 * 512]);

            for (u32 j = pos * 64; j < end * 64; j++)
                GPU.VRAMDirty[bank][j] = true;

            i += (end - pos);
            pos += (end - pos);
            pos &= 3;
        }
    }
}


bool GLRenderer::GetFramebuffers(void** top, void** bottom)
{
    // since we use an array texture, we only need one of the pointer fields
    int frontbuf = BackBuffer ^ 1;
    *top = &FPOutputTex[frontbuf];
    *bottom = nullptr;
    return false;
}


bool GLRenderer::NeedsShaderCompile()
{
    return Rend3D->NeedsShaderCompile();
}

void GLRenderer::ShaderCompileStep(int& current, int& count)
{
    return Rend3D->ShaderCompileStep(current, count);
}


#ifdef LITEV_RENDER_THREAD
// ===========================================================================
// R4 render-thread offload seam (docs/r4-render-thread-design.md §3.2, §8).
//
// This tranche implements the FINAL-COMPOSITE phase of the capture/submit split
// (design step 2), single-threaded: SubmitFrame() is called on the emulation
// thread immediately after RunFrame. It proves the split is byte-correct before
// a real render thread is introduced.
//
// What is deferred out of RunFrame into SubmitFrame():
//   * the 2D final composite of the frame's VBlank span (both engine composites
//     GLRenderer2D::VBlank/RenderScreen + the final pass GLRenderer::RenderScreen
//     + display-capture DoCapture), and
//   * the back-buffer swap (SwapBuffers).
//
// The coupling that makes this non-trivial (design boundary note item 3): the
// 2D compositor reads the 3D color output OutputTex3D, which is a SINGLE GL
// texture (GLRenderer3D ColorBufferTex) that the NEXT frame's Start3DRendering
// (GPU.cpp VCount 215, still inside the same RunFrame) overwrites BEFORE
// SubmitFrame runs. We resolve it by snapshotting OutputTex3D into a shadow
// texture at the VBlank point (Submit_Snapshot3D) and having the deferred
// composite read the shadow (GLRenderer2D::RenderScreen, gated by
// SubmitReplaying). The mid-frame partial composites (issued inline during
// DrawScanline at VCount < 192) are unchanged; only the VBlank span is deferred,
// and it reads exactly the pixels the inline path would have read.
//
// Capture-active frames (GPU.CaptureActiveThisFrame, §5.1 Tier 1) never defer:
// they run the synchronous VBlankSubmit() inline, so the capture feedback edge
// (SyncVRAMCapture writing rendered pixels back into traced VRAM) is untouched.
//
// STILL REMAINING for the threaded design (a later tranche, NOT this one): the
// per-span 2D config packet (LayerConfig/CompositorConfig snapshots, items
// 5-8/12) and the VRAM/palette shadow flat mirror (items 10-11). Those are only
// needed once RunFrame N+1 can mutate live VRAM/registers WHILE submission runs
// concurrently. In single-thread operation submission completes before RunFrame
// N+1 begins, so the live config/VRAM the inline prerenders/uploads already used
// during THIS frame are still valid at SubmitFrame; only the 3D-output overwrite
// (above) crosses the boundary, and the shadow handles it.
// ===========================================================================

void GLRenderer::SetDeferredSubmit(bool enable)
{
    DeferSubmit = enable;
}

void GLRenderer::StartFrameLog()
{
    // Rewind the build-bank log for a new frame. LogBuild alternates A/B so the
    // depth-1 queue (design §4.2) can, in the threaded stage, have the render
    // thread replay bank r while the emu thread fills bank 1-r. Single-thread
    // this tranche: SubmitFrame consumes the same bank immediately, so the flip
    // is harmless and keeps the A/B plumbing exercised.
    // R4 STEP 2: alternate the build bank every frame so the A/B replay-read state
    // (RenderLog arena, texture-VRAM shadow, render-register snapshot) actually
    // ping-pongs. Single-thread it is harmless (SubmitFrame reads the same bank back
    // immediately); under the render thread it is what keeps emu frame N+1's writes
    // off the bank the render thread is reading for frame N.
    LogBuildBank ^= 1;
    LogBuild = LogBuildBank ? &RenderLogB : &RenderLogA;
    LogBuild->Reset();

    // Phase 2: decide the deferred replay mode for this frame. RIR bring-up mode
    // (record + immediate replay) takes precedence and is NOT deferred. A
    // capture-active frame (Tier 1) records no log and runs synchronously.
    DeferReplay = !RIRMode && !GPU.CaptureActiveThisFrame;
    ShadowCopyNs = 0;
    ShadowCopyBytes = 0;
}

void GLRenderer::Submit_Snapshot3D()
{
    // Copy the just-finished 3D color output into the shadow so the deferred
    // composite can read it after the next Start3DRendering overwrites the live
    // OutputTex3D. glBlitFramebuffer (proven available; used by DoCapture).
    glBindFramebuffer(GL_READ_FRAMEBUFFER, SubmitShadow3DReadFB);
    glFramebufferTexture(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, OutputTex3D, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, SubmitShadow3DFB);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glBlitFramebuffer(0, 0, ScreenW, ScreenH,
                      0, 0, ScreenW, ScreenH,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

void GLRenderer::SwapBuffers()
{
    // Deferred frames swap only after the deferred composite has been replayed
    // (in SubmitFrame). This keeps the deferred VBlank composite targeting the
    // same back buffer the inline mid-frame composites already wrote to.
    if (SubmitPending)
        return;
    BackBuffer ^= 1;
}

void GLRenderer::Start3DRendering()
{
    if (RIRMode)
    {
        // RIR (recipe §8): route the 3D raster through the log. Payload-less —
        // GLRenderer3D::RenderFrame reads live RenderPolygonRAM + texture VRAM;
        // under IMMEDIATE replay these are unchanged (same moment, VCount 215), so
        // read-live is bit-exact. (The DEFERRED / Phase-2 path needs the Stage-B
        // texture-VRAM shadow to be bit-exact — recipe §2 — but RIR does not.)
        GLLogRecord* rec = LogBuild->Append(GLOp::Render3D);
        if (rec) { Rend3D->RenderFrame(); RIRReplayCount++; }
        else     { RIRInlineGL++; Rend3D->RenderFrame(); }
        LogBuild->Reset();
    }
#ifdef LITEV_RENDER_THREAD
    else if (DeferReplay)
    {
        // R4 Stage 1b (recipe §2): DEFER the 3D raster itself. Run only the CPU-side
        // texcache coherence NOW (VCount 215) so the VRAM dirty state is consumed at
        // the correct moment and the flat texture mirrors reflect 215-state; snapshot
        // that into the Stage-B shadow; record a Render3D op. The GL raster is replayed
        // at SubmitFrame (ReplayLog) reading the shadow — so RunFrame issues no 3D GL,
        // and the deferred raster is bit-exact with the inline VCount-215 one even
        // though the CPU mutates texture VRAM during 215->262.
        GLRenderer3D* r3d = static_cast<GLRenderer3D*>(Rend3D.get());
        u8 clrBitmapDirty = 0;
        if (r3d->PrepareDeferred3D(clrBitmapDirty))
        {
            // R4 STEP 2: snapshot the texture-VRAM AND the small render registers into
            // the build bank so the deferred raster replay reads a render-thread-owned
            // copy. Timed into the Stage-B copy-cost accounting (kill-criterion <1ms).
            auto ts0 = std::chrono::steady_clock::now();
            GPU.SnapshotTexShadow(LogBuildBank);
            GPU.GPU3D.SnapshotRenderRegs3D(LogBuildBank);
            auto ts1 = std::chrono::steady_clock::now();
            ShadowCopyNs += (u64) std::chrono::duration_cast<std::chrono::nanoseconds>(ts1 - ts0).count();
            ShadowCopyBytes += sizeof(GPU.VRAMFlat_Texture) + sizeof(GPU.VRAMFlat_TexPal)
                             + sizeof(RenderRegs3D);
            GLLogRecord* rec = LogBuild->Append(GLOp::Render3D);
            if (rec) { rec->I0 = clrBitmapDirty; RIRReplayCount++; }
            else
            {
                // Record overflow: raster inline now (bit-exact, live 215 VRAM/regs) so
                // the frame is never dropped — it just forgoes the offload this frame.
                // The just-snapshotted bank holds the live registers, so point the read
                // there; texture reads stay live (SetTexReadShadow default off).
                RIRInlineGL++;
                GPU.GPU3D.SetRenderRegs3DReadBank(LogBuildBank);
                r3d->RenderFrameBody(clrBitmapDirty);
            }
        }
        // else: RenderFrameIdentical — nothing to raster, OutputTex3D unchanged.
    }
#endif
    else
    {
#if LITEV_PROFILE && defined(__ANDROID__)
        // Measure the inline 3D raster cost (recipe §6 go/pivot). This is the GL
        // the deferred RunFrame still emits — the piece a full render-thread split
        // would additionally offload. Reported every 60 calls; static content so no
        // reset needed here.
        auto t0 = std::chrono::steady_clock::now();
        Rend3D->RenderFrame();
        auto t1 = std::chrono::steady_clock::now();
        static double acc = 0; static int n3 = 0;
        acc += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / 1e6;
        if (++n3 >= 60)
        {
            __android_log_print(ANDROID_LOG_INFO, "LITEV_3D",
                "60f: inline_3d_raster=%.3fms/frame", acc / 60.0);
            acc = 0; n3 = 0;
        }
#else
        Rend3D->RenderFrame();
#endif
    }
}

void GLRenderer::RIRRecordFinalPass(int ystart, int yend)
{
    // RIR (recipe §8): snapshot the final-pass config + registers + both aux input
    // buffers into the log, then replay RenderScreen from the snapshot immediately.
    // vramcap / GPU.ScreensEnabled etc. RenderScreen reads live are valid because
    // replay is immediate (same moment). Bit-exact by construction.
    const u32 aux0Bytes = 256 * 256 * sizeof(u16);
    const u32 aux1Bytes = 256 * 192 * sizeof(u16);
    const u32 len = sizeof(RIRFinalPassHdr) + aux0Bytes + aux1Bytes;

    GLLogRecord* rec = LogBuild->AppendWithPayload(GLOp::FinalPassSpan, nullptr, len);
    if (rec)
    {
        rec->YStart = ystart;
        rec->YEnd = yend;

        u8* p = LogBuild->Payload(*rec);
        RIRFinalPassHdr h;
        h.FPC = FinalPassConfig;
        h.DispCntA = DispCntA; h.DispCntB = DispCntB;
        h.MasterBrightnessA = MasterBrightnessA; h.MasterBrightnessB = MasterBrightnessB;
        h.AuxUsageMask = AuxUsageMask;
        memcpy(p, &h, sizeof(h));
        memcpy(p + sizeof(h), AuxInputBuffer[0], aux0Bytes);
        memcpy(p + sizeof(h) + aux0Bytes, AuxInputBuffer[1], aux1Bytes);

        RIRReplayCount++;
        if (RIRMode)
        {
            ReplayFinalPass(*rec);   // immediate replay (bring-up)
            LogBuild->Reset();
        }
        // else deferred: ReplayLog() replays this record at SubmitFrame.
    }
    else
    {
        // Overflow. In RIR mode a synchronous inline final pass is bit-exact
        // (same moment). In deferred mode the composites feeding it are still
        // unreplayed in the log, so an inline pass would composite garbage —
        // rely on arena sizing to keep this at zero (monitored by RIRInlineGL).
        RIRInlineGL++;
        if (RIRMode) { RenderScreen(ystart, yend); LogBuild->Reset(); }
    }
}

// Replay one FinalPassSpan record: restore the register/config state the final
// pass reads, then run RenderScreen. Shared by RIR immediate replay and the
// deferred ReplayLog() drain.
void GLRenderer::ReplayFinalPass(const GLLogRecord& r)
{
    const u32 aux0Bytes = 256 * 256 * sizeof(u16);
    const u8* p = LogBuild->Payload(r);
    RIRFinalPassHdr hr;
    memcpy(&hr, p, sizeof(hr));
    FinalPassConfig = hr.FPC;
    DispCntA = hr.DispCntA; DispCntB = hr.DispCntB;
    MasterBrightnessA = hr.MasterBrightnessA; MasterBrightnessB = hr.MasterBrightnessB;
    AuxUsageMask = (u8) hr.AuxUsageMask;
    memcpy(AuxInputBuffer[0], p + sizeof(hr), aux0Bytes);
    memcpy(AuxInputBuffer[1], p + sizeof(hr) + aux0Bytes, 256 * 192 * sizeof(u16));
    RenderScreen(r.YStart, r.YEnd);
}

// Replay the entire deferred log in record (timeline) order. Each 2D op routes to
// its owning engine's RIRReplay; FinalPassSpan to ReplayFinalPass. Render3D is not
// recorded in deferred mode (the raster stays inline at VCount 215); any stray
// Render3D record is skipped. Called from SubmitFrame() with SubmitReplaying set.
void GLRenderer::ReplayLog()
{
    const u32 n = LogBuild->Count();
    for (u32 i = 0; i < n; i++)
    {
        const GLLogRecord& r = LogBuild->At(i);
        switch (r.Op)
        {
        case GLOp::FinalPassSpan:
            ReplayFinalPass(r);
            break;
        case GLOp::Render3D:
        {
            // R4 Stage 1b (recipe §2): replay the deferred 3D raster. Point the
            // texture-VRAM reads at the VCount-215 shadow, issue the GL raster body,
            // then restore. Recorded after the VBlank 2D records, so it runs last in
            // the log — the 2D composites above have already read OutputTex3D holding
            // the PREVIOUS frame's 3D (what they want); this raster now writes THIS
            // frame's 3D into OutputTex3D for the next frame. clrBitmapDirty was
            // captured at prepare time into I0.
            GLRenderer3D* r3d = static_cast<GLRenderer3D*>(Rend3D.get());
            // R4 STEP 2: read the texture-VRAM shadow AND the render-register snapshot
            // from the replay bank the emu thread published — race-free against emu
            // frame N+1 recording into the other bank.
            GPU.SetTexReadShadow(true, LogReplayBank);
            GPU.GPU3D.SetRenderRegs3DReadBank(LogReplayBank);
            r3d->RenderFrameBody((u8) r.I0);
            GPU.SetTexReadShadow(false, 0);
            break;
        }
        default:
            // Rend2D_{A,B} are unique_ptr<Renderer2D>; under the GL renderer they
            // are always GLRenderer2D (created in Init). RIRReplay is GL-specific.
            static_cast<GLRenderer2D*>((r.Engine ? Rend2D_B : Rend2D_A).get())->RIRReplay(r);
            break;
        }
    }
}

void GLRenderer::SubmitFrame()
{
    if (!SubmitPending)
        return;

    SubmitPending = false;

    // R4 STEP 2 (single-thread this tranche): replay the bank the frame was just
    // recorded into. Under the render thread (STEP 3) the packet carries the bank the
    // emu thread published and this is set from it instead.
    LogReplayBank = LogBuildBank;

    SubmitReplaying = true;
    if (DeferReplay)
        ReplayLog();       // Phase 2: drain the whole frame's GL command log
    else
        VBlankSubmit();    // legacy narrow deferral (final composite only)
    SubmitReplaying = false;

    if (DeferReplay)
    {
#if LITEV_PROFILE && defined(__ANDROID__)
        LiteVGLFrameReport();
        {
            // Stage-B copy-cost report (recipe §2 kill-criterion #1). Medians over
            // 60 submits, in ms/frame, plus KB/frame moved into the log arena.
            static u64 accNs = 0, accBytes = 0; static int nrep = 0;
            accNs += ShadowCopyNs; accBytes += ShadowCopyBytes; nrep++;
            if (nrep >= 60)
            {
                __android_log_print(ANDROID_LOG_INFO, "LITEV_SHADOW",
                    "60f: vram_shadow_copy=%.3fms/frame  %llu KB/frame  (records=%u overflow=%d)",
                    (accNs / 60.0) / 1e6,
                    (unsigned long long)(accBytes / 60 / 1024),
                    LogBuild->Count(), LogBuild->DidOverflow() ? 1 : 0);
                accNs = 0; accBytes = 0; nrep = 0;
            }
        }
#endif
        LITEV_GL_RESET_STATE_CACHE();
    }

    // Perform the swap that SwapBuffers() deferred, so GetFramebuffers() returns
    // the frame just composited.
    BackBuffer ^= 1;
}
#endif

}
