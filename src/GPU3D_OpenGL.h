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

#pragma once

#ifdef OGLRENDERER_ENABLED
#include "GPU3D.h"
#include "OpenGLSupport.h"
#include "GPU3D_TexcacheOpenGL.h"
#include "NonStupidBitfield.h"

namespace melonDS
{
class GLRenderer;

class GLRenderer3D : public Renderer3D
{
public:
    GLRenderer3D(melonDS::GPU3D& gpu3D, GLRenderer& parent) noexcept;
    ~GLRenderer3D() override;
    bool Init() override;
    void Reset() override;

    void SetRenderSettings(int scale, bool betterpolygons) noexcept;
    void SetBetterPolygons(bool betterpolygons) noexcept;
    void SetScaleFactor(int scale) noexcept;
    [[nodiscard]] bool GetBetterPolygons() const noexcept { return BetterPolygons; }
    [[nodiscard]] int GetScaleFactor() const noexcept { return ScaleFactor; }

    void RenderFrame() override;
#ifdef LITEV_RENDER_THREAD
    // R4 Stage 1b (recipe §2): deferred 3D-raster split. PrepareDeferred3D runs the
    // CPU-side texcache coherence + early-out at VCount 215; RenderFrameBody issues
    // the GL raster (reading the VCount-215 texture-VRAM shadow) at SubmitFrame.
    bool PrepareDeferred3D(u8& clrBitmapDirtyOut);
    void RenderFrameBody(u8 clrBitmapDirty);
    // R4 STEP C: RenderFrameBody split so the deferred ReplayLog can consume the
    // frame's geometry + texcache (RenderFrameBodyGeometry — reads RenderPolygonRAM
    // and calls Texcache.GetTexture, producing render-private PolygonList/VBOs;
    // touches no OutputTex3D) BEFORE the 2D replay and then release the geometry bank
    // early, while the OutputTex3D-writing half (RenderFrameBodyRaster — clear +
    // RenderSceneChunk) runs at the Render3D record after the 2D composites have read
    // the previous frame's 3D. Because GetTexture runs in the pre-release Geometry
    // phase, the texcache Cache is never touched concurrently by the emu thread.
    void RenderFrameBodyGeometry();
    void RenderFrameBodyRaster(u8 clrBitmapDirty);
#else
    void RenderFrameBody(u8 clrBitmapDirty);
#endif
    u32* GetLine(int line) override;

private:
    GLRenderer& Parent;

    // GL version requirements
    // * texelFetch: 3.0 (GLSL 1.30)     (3.2/1.50 for MS)
    // * UBO: 3.1

    struct RendererPolygon
    {
        Polygon* PolyData;

        // r4-fix: render-private snapshot of the Polygon fields the RASTER phase
        // (RenderSceneChunk) reads. PolyData points into emu-owned RenderPolygonRAM,
        // which the emu thread overwrites for frame N+1 during the overlap window (after
        // the early bank release). SetupPolygon copies these at geometry time (before the
        // release) so the raster never dereferences a mutated Polygon. Flag-OFF: the
        // snapshot equals the live value (taken in the same synchronous call chain), so
        // the rendered output is byte-identical.
        u32  PolyAttr;
        bool PolyTranslucent;
        bool PolyIsShadowMask;
        bool PolyIsShadow;

        u32 NumIndices;
        u32 IndicesOffset;
        GLuint PrimType;

        u32 NumEdgeIndices;
        u32 EdgeIndicesOffset;

        u32 RenderKey;

        GLuint TexID;
        u32 TexRepeat;
    };

    //GLCompositor CurGLCompositor;
    RendererPolygon PolygonList[2048] {};

    bool TexEnable;
    TexcacheOpenGL Texcache;

    bool BuildRenderShader(bool wbuffer);
    void UseRenderShader(bool wbuffer);
    void SetupPolygon(RendererPolygon* rp, Polygon* polygon) const;
    u32* SetupVertex(const Polygon* poly, int vid, const Vertex* vtx, u32 vtxattr, u32 texlayer, u32* vptr) const;
    void BuildPolygons(RendererPolygon* polygons, int npolys, int captureinfo[16]);
    void SetupPolygonTexture(const RendererPolygon* poly) const;
    int RenderSinglePolygon(int i) const;
    int RenderPolygonBatch(int i) const;
    int RenderPolygonEdgeBatch(int i) const;
    void RenderSceneChunk(int y, int h);


    enum
    {
        RenderMode_Opaque = 0,
        RenderMode_Translucent,
        RenderMode_ShadowMask,
    };


    GLuint ClearShaderPlain {};
    GLuint ClearShaderBitmap {};

    GLuint RenderShader[2] {};
    GLint RenderModeULoc = 0;
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

#ifdef LITEV_RENDER_THREAD
    // R4 r4-fix: the raster half (RenderFrameBodyRaster/RenderSceneChunk) runs AFTER the
    // early bank release, concurrently with the emu thread building frame N+1. It must
    // NOT read live GPU3D.RenderNumPolygons / RenderPolygonRAM (both unbanked — emu N+1
    // overwrites them). RenderFrameBodyGeometry snapshots the per-frame W-buffer flag
    // here (from RenderPolygonRAM[0], read before the release) and the raster reads it.
    bool RenderWBuffer {};
#endif

    GLuint ClearVertexBufferID = 0, ClearVertexArrayID {};
    GLint ClearUniformLoc[4] {};

    GLint ClearBitmapULoc[2] {};
    GLuint ClearBitmapTex[2];
    u32* ClearBitmap[2];
    u8 ClearBitmapDirty;

    // vertex buffer
    // * XYZW: 4x16bit
    // * RGBA: 4x8bit
    // * ST: 2x16bit
    // * polygon data: 3x32bit (polygon/texture attributes)
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

    int ScaleFactor {};
    bool BetterPolygons {};
    int ScreenW {}, ScreenH {};

    GLuint ColorBufferTex {}, DepthBufferTex {}, AttrBufferTex {};

    GLuint MainFramebuffer {};
};
}
#endif