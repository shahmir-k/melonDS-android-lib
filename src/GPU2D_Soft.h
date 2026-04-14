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

#include "GPU2D.h"

namespace melonDS
{
class GPU;

namespace GPU2D
{

class SoftRenderer : public Renderer2D
{
public:
    SoftRenderer(melonDS::GPU& gpu);
    ~SoftRenderer() override {}

    void DrawScanline(u32 line, Unit* unit) override;
    void DrawSprites(u32 line, Unit* unit) override;
    void VBlankEnd(Unit* unitA, Unit* unitB) override;
private:
    melonDS::GPU& GPU;
    alignas(8) u32 BGOBJLine[256*3];
    u32* _3DLine;

    alignas(8) u8 WindowMask[256];

    alignas(8) u32 OBJLine[2][256];
    alignas(8) u8 OBJWindow[2][256];

    struct TextBGFrameCache
    {
        u32 StateGeneration = 0;
        u32 BGStamp = 0;
        u32 BGExtPalStamp = 0;
        u32 PaletteStamp = 0;
        u32 DispCnt = 0;
        u16 BGCnt = 0;
        u16 BGXPos = 0;
        u16 BGYPos = 0;
        u8 ValidLines[192] {};
        alignas(64) u32 Pixels[192][256] {};
    };

    struct TextBGTileRowCache
    {
        u32 BGStamp = 0;
        u32 BGExtPalStamp = 0;
        u32 PaletteStamp = 0;
        u32 DispKey = 0;
        u8 Valid = 0;
        u8 ValidSlots[4096] {};
        u64 Keys[4096] {};
        alignas(64) u32 Pixels[4096][8] {};
    };

    struct ComposedLineCache
    {
        u32 Epoch = 0;
        u32 StateGeneration = 0;
        u32 BGStamp = 0;
        u32 BGExtPalStamp = 0;
        u32 OBJStamp = 0;
        u32 OBJExtPalStamp = 0;
        u32 PaletteStamp = 0;
        u32 OAMStamp = 0;
        u32 DispCnt = 0;
        u32 BlendKey = 0;
        u32 RendererFlags = 0;
        u32 NumSprites = 0;
        u16 BGCnt[4] {};
        u16 BGXPos[4] {};
        u16 BGYPos[4] {};
        s32 BGXRefInternal[2] {};
        s32 BGYRefInternal[2] {};
        u8 Valid = 0;
        alignas(64) u32 Pixels[256*3] {};
    };

    struct SpriteLineBinCache
    {
        u32 StateGeneration = 0;
        u32 OAMStamp = 0;
        u32 DispCnt = 0;
        u8 Valid = 0;
        u8 Counts[192][4] {};
        u8 SpriteNums[192][4][128] {};
    };

    struct PreparedFrameState
    {
        struct PreparedTextBGState
        {
            u16 BGCnt = 0;
            u16 BGXPos = 0;
            u16 BGYPos = 0;
            u32 TileDispKey = 0;
        };

        u32 Epoch = 0;
        u32 StateGeneration = 0;
        u32 BGStamp = 0;
        u32 BGExtPalStamp = 0;
        u32 OBJStamp = 0;
        u32 OBJExtPalStamp = 0;
        u32 PaletteStamp = 0;
        u32 OAMStamp = 0;
        u32 DispCnt = 0;
        u32 BlendKey = 0;
        u32 RendererFlags = 0;
        u8 ComposedLineCacheEligible = 0;
        u8 Valid = 0;
        PreparedTextBGState TextBG[4] {};
    };

    u32 NumSprites[2] {};
    u32 SyncedBGStamp[2] {};
    u32 SyncedBGExtPalStamp[2] {};
    u32 SyncedOBJStamp[2] {};
    u32 SyncedOBJExtPalStamp[2] {};
    bool LineUsesAccelAux = false;
    bool ForceSpriteDraw = false;
    bool SpriteLineSkipped[2][192] {};
    TextBGFrameCache TextBGCache[2][4] {};
    TextBGTileRowCache TextBGTileCache[2][4] {};
    ComposedLineCache LineCache[2][192] {};
    SpriteLineBinCache SpriteBins[2] {};
    PreparedFrameState PreparedFrames[2] {};

    u8* CurBGXMosaicTable;
    array2d<u8, 16, 256> MosaicTable = []() constexpr
    {
        array2d<u8, 16, 256> table {};
        // initialize mosaic table
        for (int m = 0; m < 16; m++)
        {
            for (int x = 0; x < 256; x++)
            {
                int offset = x % (m+1);
                table[m][x] = offset;
            }
        }

        return table;
    }();

    static constexpr u32 ColorBlend4(u32 val1, u32 val2, u32 eva, u32 evb) noexcept
    {
        u32 r =  (((val1 & 0x00003F) * eva) + ((val2 & 0x00003F) * evb) + 0x000008) >> 4;
        u32 g = ((((val1 & 0x003F00) * eva) + ((val2 & 0x003F00) * evb) + 0x000800) >> 4) & 0x007F00;
        u32 b = ((((val1 & 0x3F0000) * eva) + ((val2 & 0x3F0000) * evb) + 0x080000) >> 4) & 0x7F0000;

        if (r > 0x00003F) r = 0x00003F;
        if (g > 0x003F00) g = 0x003F00;
        if (b > 0x3F0000) b = 0x3F0000;

        return r | g | b | 0xFF000000;
    }

    static constexpr u32 ColorBlend5(u32 val1, u32 val2) noexcept
    {
        u32 eva = ((val1 >> 24) & 0x1F) + 1;
        u32 evb = 32 - eva;

        if (eva == 32) return val1;

        u32 r =  (((val1 & 0x00003F) * eva) + ((val2 & 0x00003F) * evb) + 0x000010) >> 5;
        u32 g = ((((val1 & 0x003F00) * eva) + ((val2 & 0x003F00) * evb) + 0x001000) >> 5) & 0x007F00;
        u32 b = ((((val1 & 0x3F0000) * eva) + ((val2 & 0x3F0000) * evb) + 0x100000) >> 5) & 0x7F0000;

        if (r > 0x00003F) r = 0x00003F;
        if (g > 0x003F00) g = 0x003F00;
        if (b > 0x3F0000) b = 0x3F0000;

        return r | g | b | 0xFF000000;
    }

    static constexpr u32 ColorBrightnessUp(u32 val, u32 factor, u32 bias) noexcept
    {
        u32 rb = val & 0x3F003F;
        u32 g = val & 0x003F00;

        rb += (((((0x3F003F - rb) * factor) + (bias*0x010001)) >> 4) & 0x3F003F);
        g +=  (((((0x003F00 - g ) * factor) + (bias*0x000100)) >> 4) & 0x003F00);

        return rb | g | 0xFF000000;
    }

    static constexpr u32 ColorBrightnessDown(u32 val, u32 factor, u32 bias) noexcept
    {
        u32 rb = val & 0x3F003F;
        u32 g = val & 0x003F00;

        rb -= ((((rb * factor) + (bias*0x010001)) >> 4) & 0x3F003F);
        g -=  ((((g  * factor) + (bias*0x000100)) >> 4) & 0x003F00);

        return rb | g | 0xFF000000;
    }
    template<bool windowMaskFull> u32 ColorComposite(int i, u32 val1, u32 val2) const;

    template<u32 bgmode> void DrawScanlineBGMode(u32 line);
    void DrawScanlineBGMode6(u32 line);
    void DrawScanlineBGMode7(u32 line);
    void DrawScanline_BGOBJ(u32 line);
    bool CanUseComposedLineCache(u32 line);
    u32 ComposedLineRendererFlags() const;
    bool ComposedLineCacheMatches(u32 line, bool compareNumSprites = true);
    bool TryReplayComposedLineCache(u32 line);
    void StoreComposedLineCache(u32 line, const s32 bgXRefInternal[2], const s32 bgYRefInternal[2]);
    void AdvanceCachedAffineState();

    static void DrawPixel_Normal(u32* dst, u16 color, u32 flag);
    static void DrawPixel_Accel(u32* dst, u16 color, u32 flag);

    typedef void (*DrawPixel)(u32* dst, u16 color, u32 flag);
    template<DrawPixel drawPixel> static void ApplyCachedBGPixel(u32* dst, u32 pixel);
    bool PrepareTextBGFrameCacheLine(u32 line, u32 bgnum, u16 bgcnt, TextBGFrameCache*& cache, u32*& cacheLine);
    bool PrepareTextBGTileRowCache(u32 bgnum, TextBGTileRowCache*& cache);
    static u32 TextBGTileRowCacheSlot(u64 key);
    void DecodeTextBGTileRow(TextBGTileRowCache& cache, u32 slot, u64 key, u8* bgvram, u32 bgvrammask, u32 pixelsaddr, bool hflip, bool color256, const u16* pal, u32 bgFlag);
    template<DrawPixel drawPixel> void ApplyTextBGTileRow(TextBGTileRowCache* tileCache, u64 key, u8* bgvram, u32 bgvrammask, u32 pixelsaddr, bool hflip, bool color256, const u16* pal, u32 bgFlag, u32 tilePos, u32 count, u32 dstPos, u32* cacheLine);
    const PreparedFrameState& GetPreparedFrameState();
    bool PrepareSpriteLineBins();
    template<bool windowMaskFull> void DrawBG_3D();
    template<bool mosaic, bool windowMaskFull, DrawPixel drawPixel> void DrawBG_Text(u32 line, u32 bgnum);
    template<bool mosaic, DrawPixel drawPixel> void DrawBG_Affine(u32 line, u32 bgnum);
    template<bool mosaic, DrawPixel drawPixel> void DrawBG_Extended(u32 line, u32 bgnum);
    template<bool mosaic, DrawPixel drawPixel> void DrawBG_Large(u32 line);

    void ApplySpriteMosaicX();
    template<DrawPixel drawPixel>
    void InterleaveSprites(u32 prio);
    template<bool window> void DrawSprite_Rotscale(u32 num, u32 boundwidth, u32 boundheight, u32 width, u32 height, s32 xpos, s32 ypos);
    template<bool window> void DrawSprite_Normal(u32 num, u32 width, u32 height, s32 xpos, s32 ypos);

    void DoCapture(u32 line, u32 width);
};

}

}
