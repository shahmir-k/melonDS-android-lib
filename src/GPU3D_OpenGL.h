/*
    Copyright 2016-2025 melonDS team

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

#ifdef OGLRENDERER_ENABLED
#include "GPU3D.h"
#include "GPU_OpenGL.h"
#include "OpenGLSupport.h"
#include <array>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace melonDS
{
class GPU;

class GLRenderer : public Renderer3D
{
public:
    ~GLRenderer() override;
    void Reset(GPU& gpu) override;

    void SetRenderSettings(bool betterpolygons, int scale) noexcept;
    void SetBetterPolygons(bool betterpolygons) noexcept;
    void SetScaleFactor(int scale) noexcept;
    [[nodiscard]] bool GetBetterPolygons() const noexcept { return BetterPolygons; }
    [[nodiscard]] int GetScaleFactor() const noexcept { return ScaleFactor; }

    void VCount144(GPU& gpu) override {};
    void RenderFrame(GPU& gpu) override;
    void Stop(const GPU& gpu) override;
    u32* GetLine(int line) override;

    void SetupAccelFrame() override;
    void PrepareCaptureFrame() override;
    void PrepareFrame(const GPU3D& gpu3d) override;
    void Blit(const GPU& gpu) override;

    void SetOutputTexture(int buffer, u32 texture) override;
    void BindOutputTexture(int buffer) override;

    static std::unique_ptr<GLRenderer> New() noexcept;
private:
    // Used by New()
    GLRenderer(GLCompositor&& compositor) noexcept;

    // GL version requirements
    // * texelFetch: 3.0 (GLSL 1.30)     (3.2/1.50 for MS)
    // * UBO: 3.1

    struct RendererPolygon
    {
        Polygon* PolyData;

        u32 NumIndices;
        u32 IndicesOffset;
        GLuint PrimType;

        u32 NumEdgeIndices;
        u32 EdgeIndicesOffset;

        u32 RenderKey;
    };

    struct PreparedFrameData
    {
        std::array<RendererPolygon, 2048> PolygonList {};
        std::array<u32, 10240 * 7> VertexBuffer {};
        std::array<u16, 2048 * 40> IndexBuffer {};
        u32 NumVertices = 0;
        u32 NumIndices = 0;
        u32 NumEdgeIndices = 0;
        int NumFinalPolys = 0;
        int NumOpaqueFinalPolys = -1;
        u64 FrameToken = 0;
    };

    struct PrepareJob
    {
        const GPU3D* GPU = nullptr;
        u64 FrameToken = 0;
        int ScaleFactor = 1;
        bool BetterPolygons = false;
    };

    GLCompositor CurGLCompositor;
    RendererPolygon PolygonList[2048] {};
    PreparedFrameData PreparedFrames[2] {};

    bool BuildRenderShader(u32 flags, const std::string& vs, const std::string& fs);
    void UseRenderShader(u32 flags);
    static void SetupPolygon(RendererPolygon* rp, Polygon* polygon) noexcept;
    static u32* SetupVertex(int scaleFactor, const Polygon* poly, int vid, const Vertex* vtx, u32 vtxattr, u32* vptr) noexcept;
    static void BuildPolygons(PreparedFrameData& frame, int scaleFactor, bool betterPolygons, RendererPolygon* polygons, int npolys);
    void BuildPreparedFrame(PreparedFrameData& frame, const GPU3D& gpu3d, int scaleFactor, bool betterPolygons);
    void PrepareThreadMain();
    const PreparedFrameData* ResolvePreparedFrame(u64 frameToken);
    int RenderSinglePolygon(int i) const;
    int RenderPolygonBatch(int i) const;
    int RenderPolygonEdgeBatch(int i) const;
    void RenderSceneChunk(const GPU3D& gpu3d, int y, int h);

    enum
    {
        RenderFlag_WBuffer     = 0x01,
        RenderFlag_Trans       = 0x02,
        RenderFlag_ShadowMask  = 0x04,
        RenderFlag_Edge        = 0x08,
    };


    GLuint ClearShaderPlain {};

    GLuint RenderShader[16] {};
    GLuint CurShaderID = -1;

    GLuint FinalPassEdgeShader {};
    GLuint FinalPassFogShader {};

    // std140 compliant structure
    struct
    {
        float uScreenSize[2];       // vec2       0 / 2
        u32 uDispCnt;               // int        2 / 1
        u32 __pad0;
        float uToonColors[32][4];   // vec4[32]   4 / 128
        float uEdgeColors[8][4];    // vec4[8]    132 / 32
        float uFogColor[4];         // vec4       164 / 4
        float uFogDensity[34][4];   // float[34]  168 / 136
        u32 uFogOffset;             // int        304 / 1
        u32 uFogShift;              // int        305 / 1
        u32 _pad1[2];               // int        306 / 2
    } ShaderConfig {};

    GLuint ShaderConfigUBO {};
    int NumFinalPolys {}, NumOpaqueFinalPolys {};

    GLuint ClearVertexBufferID = 0, ClearVertexArrayID {};
    GLint ClearUniformLoc[4] {};

    // vertex buffer
    // * XYZW: 4x16bit
    // * RGBA: 4x8bit
    // * ST: 2x16bit
    // * polygon data: 3x32bit (polygon attrib, texture VRAM offset, (texture attrib (low 16 bit), texture palette (high 16 bit))
    //
    // polygon attributes:
    // * bit4-7, 11, 14-15, 24-29: POLYGON_ATTR
    // * bit16-20: Z shift
    // * bit8: front-facing (?)
    // * bit9: W-buffering (?)

    GLuint VertexBufferID {};
    u32 VertexBuffer[10240 * 7] {};
    u32 NumVertices {};

    GLuint VertexArrayID {};
    GLuint IndexBufferID {};
    u16 IndexBuffer[2048 * 40] {};
    u32 NumIndices {}, NumEdgeIndices {};

    const u32 EdgeIndicesOffset = 2048 * 30;
    static constexpr u32 AsyncEdgeIndicesOffset = 2048 * 30;

    GLuint TexMemID {};
    GLuint TexPalMemID {};
    u16 TexPalUploadBuffer[1024 * 48] {};

    int ScaleFactor {};
    bool BetterPolygons {};
    int ScreenW {}, ScreenH {};

    GLuint ColorBufferTex {}, DepthBufferTex {}, AttrBufferTex {};
    GLuint DownScaleBufferTex {};
    GLuint PixelbufferID {};

    GLuint MainFramebuffer {}, DownscaleFramebuffer {};
    u32 Framebuffer[256*192] {};

    std::thread PrepareThread;
    std::mutex PrepareMutex;
    std::condition_variable PrepareCV;
    PrepareJob PendingPrepareJob {};
    bool PrepareJobPending = false;
    bool PrepareThreadStop = false;
    int PrepareWriteIndex = 0;
    int PreparedReadIndex = -1;
    const PreparedFrameData* ActivePreparedFrame = nullptr;
};
}
#endif
