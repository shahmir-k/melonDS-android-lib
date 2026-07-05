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

#ifndef GPU_RENDERLOG_H
#define GPU_RENDERLOG_H

// ===========================================================================
// R4 Stage A — per-frame GL command log (docs/r4-full-split-recipe.md §1).
//
// During RunFrame, every GL-issuing op in the OpenGL renderer appends a record
// here, deep-copying the exact bytes/config it would have used AT THAT MOMENT
// into the arena. At SubmitFrame() the log is replayed in order, re-issuing the
// GL with the snapshotted bytes. Because the replayed GL command stream is
// byte-identical to the inline stream (same ops, same bytes, same order), the
// output is bit-exact (recipe §1) — the same guarantee the current 3D-output
// snapshot relies on, generalized to the whole frame.
//
// This header is the load-bearing substrate: the arena + typed record headers
// that every call-site conversion (recipe §1.2) writes into and SubmitFrame()
// reads back. It is compiled only under LITEV_RENDER_THREAD and has no effect
// unless the renderer's DeferSubmit is set AND the frame is not capture-active
// (recipe §3 / design §5.1 Tier 1).
//
// Storage model: two preallocated RenderLog instances live in GLRenderer (A/B,
// the depth-1 double buffer, design §4.2). Each holds a fixed record array plus
// a byte arena for variable-size payloads (config snapshots, palette copies,
// aux buffers). VRAM is NOT copied per-op into the arena; UploadBGVRAM/
// UploadOBJVRAM/Render3D read the Stage-B VRAM/palette shadow (recipe §2). Reset
// at StartFrame rewinds both cursors to zero — no per-frame allocation.
// ===========================================================================

#include "types.h"
#include <cstring>
#include <cstddef>

namespace melonDS
{

enum class GLOp : u8
{
    UploadBGVRAM,       // BG VRAM glTexSubImage2D span (Stage-B shadow ref)
    UploadOBJVRAM,      // OBJ VRAM glTexSubImage2D (Stage-B shadow ref)
    UploadPalBG,        // BG palette upload: payload = TempPalBuffer copy (256*(1+64) u16)
    UploadPalOBJ,       // OBJ palette upload: payload = TempPalBuffer copy (256*(1+16) u16)
    PrerenderLayer,     // per-BG-layer prerender: payload = LayerConfig snapshot; i0 = layer
    PrerenderSprites,   // sprite prerender: payload = SpriteConfig snap + SpritePreVtxData; i0 = NumSprites
    RenderSpritesSpan,  // sprite span raster: SpriteScanlineConfig span + SpriteConfig snap
    Composite2D,        // per-engine composite span: ScanlineConfig span + CompositorConfig + LayerConfig
    Render3D,           // bank-gated 3D raster; reads RenderPolygonRAM + Stage-B texture VRAM (recipe §2)
    FinalPassSpan,      // final-pass composite: FinalPassConfig snap + aux buffers + regs
    Capture,            // display capture span — Tier-1 forces synchronous, so it takes NO log (recipe §3)
};

// Fixed-size record header. Variable payload (config/palette/aux blobs) lives in
// the arena at [PayloadOff, PayloadOff+PayloadLen). Scalar op parameters that fit
// go in the inline fields to avoid tiny arena allocations.
struct GLLogRecord
{
    GLOp Op;
    u8   Engine;        // 0 = engine A / 2D-A, 1 = engine B; ignored for FinalPass/Render3D
    s32  YStart;        // span start (scanline) — uploads reuse as row-start
    s32  YEnd;          // span end (scanline)   — uploads reuse as row-count
    s32  I0;            // op-specific scalar (layer index, NumSprites, vramcap, ...)
    s32  I1;            // op-specific scalar
    u32  PayloadOff;    // byte offset into arena, or 0 if none
    u32  PayloadLen;    // payload byte length, or 0
};

class RenderLog
{
public:
    // Worst-case arena (recipe §1.1 / design §3): ~84 KB double-buffered configs
    // + palettes + aux buffers. Composite2D snapshots ScanlineConfig spans
    // (≤192 lines × 160 B ≈ 30 KB) and can appear multiple times per frame when
    // register state changes mid-frame, so size generously. VRAM is NOT copied
    // here (Stage-B shadow). 4 MB arena covers pathological many-span frames.
    static constexpr u32 ArenaSize   = 4 * 1024 * 1024;
    static constexpr u32 MaxRecords  = 4096;

    RenderLog()
    {
        Arena = new u8[ArenaSize];
        Reset();
    }

    ~RenderLog()
    {
        delete[] Arena;
    }

    RenderLog(const RenderLog&) = delete;
    RenderLog& operator=(const RenderLog&) = delete;

    // Rewind for a new frame. O(1) — no per-frame allocation.
    void Reset()
    {
        NumRecords = 0;
        ArenaUsed = 0;
        Overflow = false;
    }

    bool IsEmpty() const { return NumRecords == 0; }

    // Append a record with no payload. Returns a pointer to the record so the
    // caller can fill inline fields, or nullptr on overflow (records dropped —
    // Overflow flag set so SubmitFrame can fall back to synchronous).
    GLLogRecord* Append(GLOp op)
    {
        if (NumRecords >= MaxRecords) { Overflow = true; return nullptr; }
        GLLogRecord* r = &Records[NumRecords++];
        r->Op = op;
        r->Engine = 0;
        r->YStart = 0;
        r->YEnd = 0;
        r->I0 = 0;
        r->I1 = 0;
        r->PayloadOff = 0;
        r->PayloadLen = 0;
        return r;
    }

    // Append a record and reserve `len` bytes of arena for its payload, copying
    // `src` in (src may be null to reserve uninitialized). Returns the record;
    // the payload pointer is Payload(*r). nullptr on overflow.
    GLLogRecord* AppendWithPayload(GLOp op, const void* src, u32 len)
    {
        GLLogRecord* r = Append(op);
        if (!r) return nullptr;
        if (len)
        {
            if (ArenaUsed + len > ArenaSize) { Overflow = true; NumRecords--; return nullptr; }
            r->PayloadOff = ArenaUsed;
            r->PayloadLen = len;
            if (src) memcpy(&Arena[ArenaUsed], src, len);
            ArenaUsed += len;
            // keep the arena 8-byte aligned for typed payload casts
            ArenaUsed = (ArenaUsed + 7u) & ~7u;
        }
        return r;
    }

    // Payload accessor for replay.
    u8*       Payload(const GLLogRecord& r)       { return r.PayloadLen ? &Arena[r.PayloadOff] : nullptr; }
    const u8* Payload(const GLLogRecord& r) const { return r.PayloadLen ? &Arena[r.PayloadOff] : nullptr; }

    u32 Count() const { return NumRecords; }
    const GLLogRecord& At(u32 i) const { return Records[i]; }
    GLLogRecord&       At(u32 i)       { return Records[i]; }

    bool DidOverflow() const { return Overflow; }
    u32  BytesUsed() const { return ArenaUsed; }

private:
    GLLogRecord Records[MaxRecords];
    u8*  Arena = nullptr;
    u32  NumRecords = 0;
    u32  ArenaUsed = 0;
    bool Overflow = false;
};

}

#endif // GPU_RENDERLOG_H
