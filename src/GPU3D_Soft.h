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

#include "GPU.h"
#include "GPU3D.h"
#include "Platform.h"
#include <thread>
#include <atomic>
#ifdef LITEV_SOFT3D_EDGENEON
#include <arm_neon.h>   // InterpolateBatch (EXACT NEON edge interp) is inline in this header
#endif

namespace melonDS
{
class SoftRenderer;

class SoftRenderer3D : public Renderer3D
{
public:
    SoftRenderer3D(melonDS::GPU3D& gpu3D, SoftRenderer& parent) noexcept;
    ~SoftRenderer3D() override;
    void Reset() override;

    void SetThreaded(bool threaded) noexcept;
    [[nodiscard]] bool IsThreaded() const noexcept { return Threaded; }

    void RenderFrame() override;
    void FinishRendering() override;
    void RestartFrame() override;

    u32* GetLine(int line) override;

#ifdef LITEV_SOFT3D_OVERLAP
    // EMU thread, called from SoftRenderer::VBlank (sibling 2D class): latch the parity
    // slot the async 2D consumer (GetLine) will read this frame. Public so the 2D
    // renderer can reach it; touches only this class's own parity fields.
    void OverlapLatchConsume() { ConsumeParity = LastKickParity; }
#endif
#ifdef LITEV_SOFT3D_PIPELINE2
    // EMU thread, called from SoftRenderer::VBlank: latch which 3D bank the async 2D
    // consumer (GetLine / consumer-side final pass) will read this frame. Ordered to the
    // consumer by the AsyncStart post that follows in VBlank. Under depth-1 this equals the
    // bank the render thread rastered (same frame parity).
    void Pipeline2LatchConsume() { P2ConsumeBank = P2KickParity * P2BankStride; }
    // The parity of the 3D bank the 2D consumer of THIS frame must read (== the parity 3D-N
    // was kicked/rastered with). Public so SoftRenderer::VBlank can capture it into the
    // depth-2 ring for the frame's 2D consumer. See the confirmation note in VBlank: at
    // VBlank(N) P2KickParity is exactly 3D-N's parity (toggled at frame N's VCount-215
    // RenderFrame, next toggle not until N+1's VCount 215). Identical to what
    // Pipeline2LatchConsume uses at depth-1.
    int Pipeline2CurrentConsumeParity() const { return P2KickParity; }
    // Depth-2: set the 3D consume bank from a captured parity (called by the async 2D thread
    // when it pops a ring frame). Public so SoftRenderer can reach it cross-class.
    void Pipeline2SetConsumeParity(int par) { P2ConsumeBank = par * P2BankStride; }
#endif

    void SetupRenderThread();
    void EnableRenderThread();
    void StopRenderThread();

private:
    SoftRenderer& Parent;

    friend void GPU3D::DoSavestate(Savestate* file) noexcept;

    // Notes on the interpolator:
    //
    // This is a theory on how the DS hardware interpolates values. It matches hardware output
    // in the tests I did, but the hardware may be doing it differently. You never know.
    //
    // Assuming you want to perspective-correctly interpolate a variable named A across two points
    // in a typical rasterizer, you would calculate A/W and 1/W at each point, interpolate linearly,
    // then divide A/W by 1/W to recover the correct A value.
    //
    // The DS GPU approximates interpolation by calculating a perspective-correct interpolation
    // between 0 and 1, then using the result as a factor to linearly interpolate the actual
    // vertex attributes. The factor has 9 bits of precision when interpolating along Y and
    // 8 bits along X.
    //
    // There's a special path for when the two W values are equal: it directly does linear
    // interpolation, avoiding precision loss from the aforementioned approximation.
    // Which is desirable when using the GPU to draw 2D graphics.

    template<int dir>
    class Interpolator
    {
    public:
        constexpr Interpolator() {}
        constexpr Interpolator(s32 x0, s32 x1, s32 w0, s32 w1, bool wbuffer)
        {
            Setup(x0, x1, w0, w1, wbuffer);
        }

        constexpr void Setup(s32 x0, s32 x1, s32 w0, s32 w1, bool wbuffer)
        {
            this->x0 = x0;
            this->x1 = x1;
            this->xdiff = x1 - x0;
            this->wbuffer = wbuffer;

            // calculate reciprocal for Z interpolation
            // TODO eventually: use a faster reciprocal function?
            if (this->xdiff != 0)
                this->xrecip_z = (1<<22) / this->xdiff;
            else
                this->xrecip_z = 0;

            // linear mode is used if both W values are equal and have
            // low-order bits cleared (0-6 along X, 1-6 along Y)
            u32 mask = dir ? 0x7E : 0x7F;
            if ((w0 == w1) && !(w0 & mask) && !(w1 & mask))
                this->linear = true;
            else
                this->linear = false;

            if (dir)
            {
                // along Y

                this->w0n = w0 >> 1;
                this->w0d = (w0 + ((w0 & ~w1) & 1)) >> 1;
                this->w1d = w1 >> 1;

                this->shift = 9;
            }
            else
            {
                // along X

                this->w0n = w0;
                this->w0d = w0;
                this->w1d = w1;

                this->shift = 8;
            }
        }

        constexpr void SetX(s32 x)
        {
            x -= x0;
            this->x = x;
            if ((xdiff != 0) && ((!linear) || wbuffer))
            {
                u32 num = (x * w0n) << shift;
                u32 den = (x * w0d) + ((xdiff-x) * w1d);

                // this seems to be a proper division on hardware :/
                // I haven't been able to find cases that produce imperfect output
                if (den == 0) yfactor = 0;
                else          yfactor = num / den;
            }
        }

        constexpr s32 Interpolate(s32 y0, s32 y1) const
        {
            if (xdiff == 0 || y0 == y1) return y0;

            if (!linear)
            {
                // perspective-correct approx. interpolation
                if (y0 < y1)
                    return y0 + (((y1-y0) * yfactor) >> shift);
                else
                    return y1 + (((y0-y1) * ((1<<shift)-yfactor)) >> shift);
            }
            else
            {
                // linear interpolation
                if (y0 < y1)
                    return y0 + (s64)(y1-y0) * x / xdiff;
                else
                    return y1 + (s64)(y0-y1) * (xdiff - x) / xdiff;
            }
        }

        constexpr s32 InterpolateZ(s32 z0, s32 z1) const
        {
            if (xdiff == 0 || z0 == z1) return z0;

            if (wbuffer)
            {
                // W-buffering: perspective-correct approx. interpolation
                if (z0 < z1)
                    return z0 + (((s64)(z1-z0) * yfactor) >> shift);
                else
                    return z1 + (((s64)(z0-z1) * ((1<<shift)-yfactor)) >> shift);
            }
            else
            {
                // Z-buffering: linear interpolation
                // still doesn't quite match hardware...
                s32 base = 0, disp = 0, factor = 0;

                if (z0 < z1)
                {
                    base = z0;
                    disp = z1 - z0;
                    factor = x;
                }
                else
                {
                    base = z1;
                    disp = z0 - z1,
                    factor = xdiff - x;
                }

                if (dir)
                {
                    int shift = 0;
                    while (disp > 0x3FF)
                    {
                        disp >>= 1;
                        shift++;
                    }

                    return base + ((((s64)disp * factor * xrecip_z) >> 22) << shift);
                }
                else
                {
                    disp >>= 9;
                    return base + (((s64)disp * factor * xrecip_z) >> 13);
                }
            }
        }

#ifdef LITEV_SOFT3D_EDGENEON
        // EXACT NEON batch of Interpolate() over n independent attributes that all share
        // this interpolator's yfactor/shift: out[i] == Interpolate(y0v[i], y1v[i]) for
        // every i, byte-identical to the scalar path (same integer ops, SIMD-packed).
        // The perspective (!linear) case is vectorised 4 lanes/iter; the rare linear /
        // xdiff==0 cases fall back to scalar Interpolate. Used for the per-scanline EDGE
        // interpolation (Interpolator<1>), which is otherwise scalar and dependency-
        // stalled on the in-order A55.
        void InterpolateBatch(const s32* y0v, const s32* y1v, s32* out, int n) const
        {
            if (linear || xdiff == 0)
            {
                for (int i = 0; i < n; i++) out[i] = Interpolate(y0v[i], y1v[i]);
                return;
            }
            // shift is always (dir ? 9 : 8); use the compile-time value so the logical
            // right-shift count is a constant (required by vshrq_n_u32). The scalar path
            // does the multiply in u32 (mod 2^32) and a LOGICAL >> shift, then adds to the
            // base -- reproduced here exactly (vmulq_s32 keeps the same low 32 bits; the
            // wrap on overflow matches because both are mod 2^32).
            constexpr int SH = dir ? 9 : 8;
            const int32x4_t vyf   = vdupq_n_s32((s32)yfactor);
            const int32x4_t vcomp = vdupq_n_s32((1 << SH) - (s32)yfactor);
            int i = 0;
            for (; i + 4 <= n; i += 4)
            {
                int32x4_t a0 = vld1q_s32(y0v + i);
                int32x4_t a1 = vld1q_s32(y1v + i);
                int32x4_t dpos = vsubq_s32(a1, a0);   // y1 - y0
                int32x4_t dneg = vsubq_s32(a0, a1);   // y0 - y1
                // pathA (y0<y1):  y0 + ((u32)(dpos * yfactor)          >> SH)
                uint32x4_t pa = vshrq_n_u32(vreinterpretq_u32_s32(vmulq_s32(dpos, vyf)), SH);
                int32x4_t  rA = vaddq_s32(a0, vreinterpretq_s32_u32(pa));
                // pathB (y0>=y1): y1 + ((u32)(dneg * (2^SH - yfactor)) >> SH)
                uint32x4_t pb = vshrq_n_u32(vreinterpretq_u32_s32(vmulq_s32(dneg, vcomp)), SH);
                int32x4_t  rB = vaddq_s32(a1, vreinterpretq_s32_u32(pb));
                uint32x4_t lt = vcltq_s32(a0, a1);    // y0 < y1
                vst1q_s32(out + i, vbslq_s32(lt, rA, rB));
            }
            for (; i < n; i++) out[i] = Interpolate(y0v[i], y1v[i]);
        }
#endif

    private:
        s32 x0, x1, xdiff, x;

        int shift;
        bool linear;
        bool wbuffer;

        s32 xrecip_z;
        s32 w0n, w0d, w1d;

        u32 yfactor;
    };


    template<int side>
    class Slope
    {
    public:
        constexpr Slope() {}

        constexpr s32 SetupDummy(s32 x0, bool wbuffer)
        {
            dx = 0;

            this->x0 = x0;
            this->xmin = x0;
            this->xmax = x0;

            Increment = 0;
            XMajor = false;

            Interp.Setup(0, 0, 0, 0, wbuffer);
            Interp.SetX(0);

            xcov_incr = 0;

            return x0;
        }

        constexpr s32 Setup(s32 x0, s32 x1, s32 y0, s32 y1, s32 w0, s32 w1, s32 y, bool wbuffer)
        {
            this->x0 = x0;
            this->y = y;

            if (x1 > x0)
            {
                this->xmin = x0;
                this->xmax = x1-1;
                this->Negative = false;
            }
            else if (x1 < x0)
            {
                this->xmin = x1;
                this->xmax = x0-1;
                this->Negative = true;
            }
            else
            {
                this->xmin = x0;
                this->xmax = this->xmin;
                this->Negative = false;
            }

            xlen = xmax+1 - xmin;
            ylen = y1 - y0;

            // slope increment has a 18-bit fractional part
            // note: for some reason, x/y isn't calculated directly,
            // instead, 1/y is calculated and then multiplied by x
            // TODO: this is still not perfect (see for example x=169 y=33)
            if (ylen == 0)
                Increment = 0;
            else if (ylen == xlen && xlen != 1)
                Increment = 0x40000;
            else
            {
                s32 yrecip = (1<<18) / ylen;
                Increment = (x1-x0) * yrecip;
                if (Increment < 0) Increment = -Increment;
            }

            XMajor = (Increment > 0x40000);

            if constexpr (side)
            {
                // right

                if (XMajor)              dx = Negative ? (0x20000 + 0x40000) : (Increment - 0x20000);
                else if (Increment != 0) dx = Negative ? 0x40000 : 0;
                else                     dx = 0;
            }
            else
            {
                // left

                if (XMajor)              dx = Negative ? ((Increment - 0x20000) + 0x40000) : 0x20000;
                else if (Increment != 0) dx = Negative ? 0x40000 : 0;
                else                     dx = 0;
            }

            dx += (y - y0) * Increment;

            s32 x = XVal();

            int interpoffset = (Increment >= 0x40000) && (side ^ Negative);
            Interp.Setup(y0-interpoffset, y1-interpoffset, w0, w1, wbuffer);
            Interp.SetX(y);

            // used for calculating AA coverage
            if (XMajor) xcov_incr = (ylen << 10) / xlen;

            return x;
        }

        constexpr s32 Step()
        {
            dx += Increment;
            y++;

            s32 x = XVal();
            Interp.SetX(y);
            return x;
        }

        constexpr s32 XVal() const
        {
            s32 ret = 0;
            if (Negative) ret = x0 - (dx >> 18);
            else          ret = x0 + (dx >> 18);

            if (ret < xmin) ret = xmin;
            else if (ret > xmax) ret = xmax;
            return ret;
        }

        template<bool swapped>
        constexpr void EdgeParams_XMajor(s32* length, s32* coverage) const
        {
            // only do length calc for right side when swapped as it's
            // only needed for aa calcs, as actual line spans are broken
            if constexpr (!swapped || side)
            {
                if (side ^ Negative)
                    *length = (dx >> 18) - ((dx-Increment) >> 18);
                else
                    *length = ((dx+Increment) >> 18) - (dx >> 18);
            }

            // for X-major edges, we return the coverage
            // for the first pixel, and the increment for
            // further pixels on the same scanline
            s32 startx = dx >> 18;
            if (Negative) startx = xlen - startx;
            if (side)     startx = startx - *length + 1;

            s32 startcov = (((startx << 10) + 0x1FF) * ylen) / xlen;
            *coverage = (1<<31) | ((startcov & 0x3FF) << 12) | (xcov_incr & 0x3FF);

            if constexpr (swapped) *length = 1;
        }

        template<bool swapped>
        constexpr void EdgeParams_YMajor(s32* length, s32* coverage) const
        {
            *length = 1;

            if (Increment == 0)
            {
                // for some reason vertical edges' aa values
                // are inverted too when the edges are swapped
                if constexpr (swapped)
                    *coverage = 0;
                else
                    *coverage = 31;
            }
            else
            {
                s32 cov = ((dx >> 9) + (Increment >> 10)) >> 4;
                if ((cov >> 5) != (dx >> 18)) cov = 31;
                cov &= 0x1F;
                if constexpr (swapped)
                {
                    if (side ^ Negative) cov = 0x1F - cov;
                }
                else
                {
                    if (!(side ^ Negative)) cov = 0x1F - cov;
                }

                *coverage = cov;
            }
        }

        template<bool swapped>
        constexpr void EdgeParams(s32* length, s32* coverage) const
        {
            if (XMajor)
                return EdgeParams_XMajor<swapped>(length, coverage);
            else
                return EdgeParams_YMajor<swapped>(length, coverage);
        }

        s32 Increment;
        bool Negative;
        bool XMajor;
        Interpolator<1> Interp;

    private:
        s32 x0, xmin, xmax;
        s32 xlen, ylen;
        s32 dx;
        s32 y;

        s32 xcov_incr;
        s32 ycoverage, ycov_incr;
    };

    u32 AlphaBlend(u32 srccolor, u32 dstcolor, u32 alpha) const noexcept;

#ifdef LITEV_SOFT3D_COMPACTVTX
    // DraStic-style compact, contiguous, direct-indexed raster vertex. Holds ONLY the
    // fields the software raster reads per scanline, in a tightly packed 32-byte record
    // (2 per 64B cache line, vs the shared 64B `Vertex` = 1/line chased through a pointer
    // array). Values are copied verbatim from the fat structs so the raster is byte-
    // identical: X/Y = Vertex::FinalPosition[0/1], W/Z = Polygon::FinalW/FinalZ[i],
    // R/G/B = Vertex::FinalColor[0..2] (kept s32 so no range assumption is needed),
    // S/T = Vertex::TexCoords[0/1] (kept s16 so the s16->s32 sign-extension the scalar
    // Interpolate does is reproduced exactly). Indexed by the polygon's own vertex index
    // [0,NumVertices) -- the SAME index the raster uses for Vertices[i]/FinalW[i].
    struct CompactVtx
    {
        s32 X, Y;      // FinalPosition[0], FinalPosition[1]
        s32 W, Z;      // Polygon::FinalW[i], Polygon::FinalZ[i]
        s32 R, G, B;   // FinalColor[0..2]
        s16 S, T;      // TexCoords[0..1]
    };                 // 7*s32 + 2*s16 = 32 bytes
#endif

    struct RendererPolygon
    {
        Polygon* PolyData;
#ifdef LITEV_SOFT3D_COMPACTVTX
        // Base of THIS polygon's contiguous compact-vertex block in CompactArena (built
        // once per frame by BuildCompactVtx, before the band workers wake). Set at the
        // SetupPolygon call site from the polygon's global index. Read-only during raster.
        const CompactVtx* CompactV;
#endif
#ifdef LITEV_SOFT3D_EDGEHOIST
        // Per-EDGE endpoint snapshot, taken ONCE at edge-setup (SnapshotEdgeHoistL/R, from
        // SetupPolygon*Edge + SetupPolygon's flat branch) -- the endpoints only change at a
        // vertex crossing. Every per-scanline raster read (crossing-check Y1, filledge X1,
        // W/Z, colour/texcoord) reads from HERE instead of dereferencing the compact arena,
        // so the hot loop touches only the contiguous rp. 0 = Cur vertex, 1 = Next vertex.
        // Values are verbatim copies of the compact fields (byte-identical): Y1 = Next
        // FinalPosition[1], X1 = Next FinalPosition[0], W/Z = Polygon FinalW/FinalZ, R/G/B =
        // FinalColor (s32), S/T = TexCoords (s16, so Interpolate's s16->s32 promotion matches).
        struct EdgeHoist
        {
            s32 Y1;                        // Next FinalPosition[1] (per-scanline crossing check)
            s32 X1;                        // Next FinalPosition[0] (filledge)
            s32 W0, W1;                    // FinalW  (wl/wr interp)
            s32 Z0, Z1;                    // FinalZ  (zl/zr interp)
            s32 R0, R1, G0, G1, B0, B1;    // FinalColor (colour interp)
            s16 S0, S1, T0, T1;            // TexCoords  (texcoord interp)
        };
        EdgeHoist EHL, EHR;                // left edge (SlopeL/CurVL..) / right edge (SlopeR/CurVR..)
#endif

        Slope<0> SlopeL;
        Slope<1> SlopeR;
        s32 XL, XR;
        u32 CurVL, CurVR;
        u32 NextVL, NextVR;

#if defined(LITEV_SOFT3D_GRADIENT) || defined(LITEV_SOFT3D_EDGENEON)
        // Per-edge Cur/Next vertex-attribute snapshot, taken ONCE per vertex-crossing
        // (SnapshotEdgeL/R, from SetupPolygon*Edge + SetupPolygon's flat branch). The two
        // edge vertices only change on a crossing, so between crossings these are the
        // invariant endpoints of the per-scanline edge interpolation. Both raster variants
        // consume this cache as their lane/DDA inputs:
        //   * LITEV_SOFT3D_EDGENEON  -- NEON-vectorize the EXACT edge Interpolate (byte-
        //                               identical), 6 attrs/edge sharing the edge yfactor.
        //   * LITEV_SOFT3D_GRADIENT  -- sub-affine DDA along Y (approximate) for the same
        //                               6 attrs (W + colour/texcoord); Z stays exact.
        // Attr index order: 0=W, 1=R, 2=G, 3=B, 4=S(texcoord), 5=T. Texcoords are stored
        // sign-extended to s32 (matches the s16->s32 promotion the scalar Interpolate did).
        // Z is kept out of the batch/DDA (exact InterpolateZ) and stored separately.
        struct EdgeEndpoints
        {
            s32 v0[6];   // Cur  vertex: W, R, G, B, S, T
            s32 v1[6];   // Next vertex: W, R, G, B, S, T
            s32 z0, z1;  // polygon->FinalZ[Cur/Next] (exact path, not batched/DDA'd)
#ifdef LITEV_SOFT3D_GRADIENT
            // Sub-affine DDA state (Stage 2, GRADIENT only). 16.16 fixed point.
            s64 acc[6];  // current accumulator for W,R,G,B,S,T
            s64 dv[6];   // per-row step between anchors
            s32 rem;     // rows until next re-anchor (0 => anchor this row); reset on crossing
            s32 segYBot; // this segment's bottom scanline (Next vertex Y) -- anchor clamp
#endif
        };
        EdgeEndpoints EdgeL, EdgeR;
#endif
    };

#ifdef LITEV_SOFT3D_BANDED
    // Banded software 3D raster: the incremental per-polygon edge state and the
    // shadow stencil are PER-BAND (each band thread walks all scanlines to keep the
    // edge walk correct but only writes its own [BandY0,BandY1) rows). Making these
    // static thread_local gives every band std::thread its own copy while the
    // unqualified member references in the scanline methods keep resolving to them.
    static thread_local RendererPolygon PolygonList[2048];
    // Per-band scanline write window [BandY0, BandY1). Default full-frame so the
    // non-banded (threaded==false / main-thread) path is unchanged.
    static thread_local s32 BandY0;
    static thread_local s32 BandY1;
    void RenderBand(Polygon** polygons, int npolys, s32 y0, s32 y1, int bandidx);

    // Persistent band-worker pool (DraStic-style, teardown doc 07 T4/T5/T6).
    // NB worker threads are spawned ONCE (first threaded RenderPolygons) and each
    // loops {wait its start-sema; do its job; post its done-sema}. Replaces the
    // per-frame std::thread spawn/join, which churned + oversubscribed the 4-core
    // target. Two job phases per frame, separated by a full done-barrier: phase 0
    // rasters the band's [BandRasterBnd[b],BandRasterBnd[b+1]) rows, phase 1 runs
    // ScanlineFinalPass over its [BandFinalBnd[b],BandFinalBnd[b+1]) rows (safe once
    // all raster bands have joined, since the final pass reads neighbour rows).
    void EnsureBandPool();
    void ShutdownBandPool();
    void BandWorkerFunc(int idx);
    int  BandPoolNB = 0;                       // 0 => pool not yet created
    std::atomic_bool BandPoolRunning { false };
    bool BandFinalBanded = true;              // LITEV_BAND_FINAL=0 => serial final pass
    Platform::Thread* BandThreads[8] = {};
    Platform::Semaphore* BandStartSema[8] = {};
    Platform::Semaphore* BandDoneSema[8] = {};
    // Per-frame args the workers read (published before posting the start semas;
    // the semaphore post/wait pair provides the release/acquire ordering).
    Polygon** BandPolygons = nullptr;
    int BandNumPolys = 0;
    int BandPhase = 0;                         // 0 = raster, 1 = final pass
    s32 BandRasterBnd[9] = {};
    s32 BandFinalBnd[9] = {};

#ifdef LITEV_SOFT3D_STREAM
    // LITEV_SOFT3D_STREAM: the 2D composite was fully SERIALIZED behind the 3D raster
    // -- the old path waited the whole raster barrier, ran the final pass, then dumped
    // all 192 Sema_ScanlineCount posts at once, so the 2D (~9.3ms of work) could not
    // start until raster(~14.7ms)+final(~1.1ms) were done. Measured: 2D sat BLOCKED
    // 8.7ms/frame. Bands raster CONTIGUOUS row ranges top-to-bottom, so band 0's rows
    // are ready long before the last band finishes.
    //
    // Each band publishes a row watermark; the render thread then streams
    // finalPass(y) -> release(y) in order as soon as rows y and y+1 are rastered, so
    // the 2D pipelines behind the bands. Render wall: 14.7+9.3+1.1 -> max(14.7, 9.3).
    // BandRowProgress[b] = "first row of band b NOT yet rastered" (starts at its y0).
    std::atomic<s32> BandRowProgress[8];
    // Block until guest row `row` has been rastered by whichever band owns it.
    void WaitRowRastered(s32 row, int nb);
#endif

#ifdef LITEV_SOFT3D_OVERLAP
    // Consumer-driven 2D/3D overlap (see CMakeLists LITEV_SOFT3D_OVERLAP). The 2D async
    // thread's GetLine(y) waits until rows y and y+1 are rastered, runs ScanlineFinalPass
    // itself, then returns the composited line -- so the 2D pipelines behind the bands
    // instead of blocking for the whole raster. No producer-side releaser, no extra
    // thread (unlike the reverted STREAM lever).
    //
    // DOUBLE-BUFFERED by frame parity: under SOFT3D_ASYNC two frames are in flight (the
    // consumer of frame N overlaps the raster kick of N+1) over a SINGLE-buffered
    // ColorBuffer, so a single shared progress array would be bulk-reset by N+1 while N
    // still reads it. RowRastered[parity][row]=1 once that row is rastered; each frame
    // owns its own parity slot, reset by the EMU thread at the RenderFrame kick (ordered
    // before both the bands via Sema_RenderStart and the consumer via AsyncStart).
    std::atomic<u8> RowRastered[2][256];
    // Parity the BANDS write this frame (set by the render thread from LastKickParity
    // before posting the band start-semas; the sema pair provides the ordering).
    int BandRasterParity = 0;
    // Parity the CONSUMER reads (latched by the emu thread in SoftRenderer::VBlank from
    // LastKickParity, ordered to the consumer by the AsyncStart post/wait).
    int ConsumeParity = 0;
    // Parity assigned to the raster kicked at the last RenderFrame (emu thread only).
    int LastKickParity = 0;
    // Per-parity: did this frame do a FRESH raster (=> the consumer must run the final
    // pass), or was it FrameIdentical (buffers already final from the previous frame =>
    // consumer must NOT re-run edge/fog, which would double-apply)?
    bool RasterFresh[2] = { false, false };
    // EMU thread, at the RenderFrame kick: toggle parity, reset the new parity's row
    // flags (or mark all-ready for an identical frame), and record fresh-ness.
    void OverlapKickReset(bool fresh);
    // (OverlapLatchConsume is declared public above -- it is called cross-class from
    // the 2D SoftRenderer, which cannot reach a private member.)
    // CONSUMER (2D async) thread: block until row `row` of parity `par` is rastered.
    void WaitRowRasteredP(s32 row, int par);
#endif

    // ---- adaptive band load balancing (LITEV_BAND_BALANCE, default on) ----
    // Equal-line bands are badly imbalanced on a real scene: on the Shrek race the
    // measured per-band raster times were 22.8 / 19.2 / 11.7 ms (band 0 does ~2x
    // band 2), so the raster WALL is set by the slowest band, ~27% above the
    // perfectly-balanced ideal. Rebalance the row partition every frame from the
    // previous frame's measured band times (EWMA), converging on equal band times.
    //
    // BIT-EXACT BY CONSTRUCTION: a band fast-forwards its incremental edge/stencil
    // state from row 0 regardless of where its write window starts, and bands write
    // disjoint rows. So ANY partition of [0,192) produces an identical framebuffer;
    // only the wall time changes. (FBHASH-gated anyway.)
    bool BandBalance = true;
    bool BandBndInit = false;
    double BandLastMs[8] = {};                 // band i writes its own slot (no race)
    double BandEwmaMs[8] = {};
    void RebalanceBands(int nb);
#else
    RendererPolygon PolygonList[2048];
#endif
    void TextureLookup(u32 texparam, u32 texpal, s16 s, s16 t, u16* color, u8* alpha) const;
    // Integer texel fetch + palette lookup for one already-in-range (s,t) (no wrap).
    // Shared by TextureLookup (per-pixel exact path) and the decode-once cache.
    void DecodeTexel(u32 texparam, u32 texpal, s32 s, s32 t, u16* color, u8* alpha) const;
#ifdef LITEV_SOFT3D_FAST
    // Decode-once texture cache (FPS-first, DraStic-style). Each unique
    // (TexParam,TexPalette) texture is decoded ONCE into a flat arena of packed
    // texels packed as RGBA5551 (u16: RGB555 color in bits 0-14 | opaque flag in
    // bit 15), then sampled per pixel with a plain array read instead of re-deriving
    // format/palette/VRAM base for every texel. u16 (not u32) keeps the decoded
    // footprint small so the per-pixel read stays cache-friendly. Alpha is stored as
    // 1 bit: EXACT for all binary-alpha formats (2/4/16/256-color, compressed,
    // direct); the two graded-alpha formats (A3I5/A5I3) are approximated to 0/31
    // (FPS-first, approximation OK). One cache PER BAND INDEX (bands run concurrently
    // on disjoint indices, so no race); the arena persists across frames (band
    // threads are respawned each frame, so a thread_local arena would leak+realloc
    // per frame). CurTexCache is a thread_local pointer the active band/thread aims
    // at its own TexCaches[] slot. Reset only when texture/palette VRAM changed.
    // ResolveTexCache returns the decoded base pointer (decoding on a miss), or
    // nullptr if the texture is too big to cache (caller falls back to TextureLookup).
    static constexpr u32 TexCacheArenaTexels = 1u << 21; // 2M texels (4MB u16) per band
    static constexpr u32 TexCacheSlots = 512;
    static constexpr int TexCacheMaxBands = 8;
#ifdef LITEV_SOFT3D_TEX1B
    // Byte-addressed arena under TEX1B (same 4MB cap): palette-format textures store a
    // 1-byte index per texel + a small decoded u16 palette (both in the arena); direct/4x4
    // store 2-byte u16 exactly as before. ElemSize=1 => palette path (Offset=index base,
    // PalOffset=palette base); ElemSize=2 => direct/4x4 (Offset=u16 base, no palette).
    static constexpr u32 TexCacheArenaBytes = TexCacheArenaTexels * 2;
    struct TexCacheEntry { u32 Param, Pal, Offset; s32 W, H; u32 PalOffset; u8 ElemSize; };
    struct TexCacheState
    {
        u8*  Arena = nullptr;
        u32  Used = 0;   // BYTES used
        u32  Count = 0;
        TexCacheEntry Entries[TexCacheSlots];
    };
#else
    struct TexCacheEntry { u32 Param, Pal, Offset; s32 W, H; };
    struct TexCacheState
    {
        u16* Arena = nullptr;
        u32  Used = 0;
        u32  Count = 0;
        TexCacheEntry Entries[TexCacheSlots];
    };
#endif
    TexCacheState TexCaches[TexCacheMaxBands];
    static thread_local TexCacheState* CurTexCache;
    // Set in RenderFrame: texture/palette VRAM changed this frame => drop caches.
    bool TexCacheDirty = true;
#ifdef LITEV_SOFT3D_TEX1B
    // Returns f_texcache: the u16 PALETTE for palette formats (index it with *out1b[texAddr]),
    // or the u16 direct/4x4 arena for fmt 5/7 (*out1b set null). nullptr => fall back.
    const u16* ResolveTexCache(u32 texparam, u32 texpal, s32* outW, s32* outH, const u8** out1b);
#else
    const u16* ResolveTexCache(u32 texparam, u32 texpal, s32* outW, s32* outH);
#endif
#endif
    u32 RenderPixel(const Polygon* polygon, u8 vr, u8 vg, u8 vb, s16 s, s16 t) const;
    void PlotTranslucentPixel(u32 pixeladdr, u32 color, u32 z, u32 polyattr, u32 shadow);
    void SetupPolygonLeftEdge(RendererPolygon* rp, s32 y) const;
    void SetupPolygonRightEdge(RendererPolygon* rp, s32 y) const;
#ifdef LITEV_SOFT3D_EDGEHOIST
    // Snapshot the left/right edge's current Cur/Next endpoint attributes into rp->EHL/EHR
    // (read from the already-built compact arena). Called at every edge (re)setup.
    void SnapshotEdgeHoistL(RendererPolygon* rp) const;
    void SnapshotEdgeHoistR(RendererPolygon* rp) const;
#endif
#if defined(LITEV_SOFT3D_GRADIENT) || defined(LITEV_SOFT3D_EDGENEON)
    // Snapshot each edge's Cur/Next vertex attributes into rp->EdgeL/EdgeR. Called
    // wherever CurVL/NextVL (L) or CurVR/NextVR (R) are (re)assigned. Under GRADIENT it
    // also resets the DDA (rem=0) and records the segment bottom.
    void SnapshotEdgeL(RendererPolygon* rp) const;
    void SnapshotEdgeR(RendererPolygon* rp) const;
#endif
#if defined(LITEV_SOFT3D_FAST) && defined(LITEV_SOFT3D_GRADIENT)
    // Sub-affine DDA (Stage 2, approximate): advance edge e by one scanline, writing the
    // 6 attrs (W,R,G,B,S,T) to out[6]. Z is not handled here (exact path). interp supplies
    // the true perspective value at anchor rows (current + look-ahead via a local copy).
    void StepEdgeAttrsDDA(RendererPolygon::EdgeEndpoints& e,
                          const Interpolator<1>& interp, s32 y, s32* out) const;
#endif
    void SetupPolygon(RendererPolygon* rp, Polygon* polygon) const;
#ifdef LITEV_SOFT3D_COMPACTVTX
    // Compact-vertex arena, built ONCE per frame in RenderPolygons (render thread / the
    // synchronous path) before the band workers are woken. Packed tightly: each polygon i
    // writes NumVertices records starting at CompactBase[i]. Sized to the worst case
    // (RenderPolygonRAM caps at 2048 polys, <=10 verts each); allocated lazily (like
    // GeomEventLog) so the base pointer is stable for the frame. Written by one thread,
    // then read-only across all band threads (the BandStartSema post/wait pair provides
    // the release/acquire ordering).
    std::unique_ptr<CompactVtx[]> CompactArena;
    u32 CompactBase[2048] = {};
    void BuildCompactVtx(Polygon** polygons, int npolys);
#endif
    void RenderShadowMaskScanline(RendererPolygon* rp, s32 y);
    void RenderPolygonScanline(RendererPolygon* rp, s32 y);
    void RenderScanline(s32 y, int npolys);
#ifdef LITEV_SOFT3D_FAST
    // Active-Edge-Table (classic scanline rasterization). Replaces RenderScanline's
    // O(npolys) per-scanline Y-range re-scan: bin polygons by YTop once per frame,
    // then keep an ACTIVE list (polys covering the current scanline) that is updated
    // incrementally as y advances (add polys entering at YTop, drop polys whose
    // YBottom<=y). The active list stays sorted by polygon index so draw order (and
    // thus depth/priority) is preserved. Scratch is thread_local so each concurrent
    // RenderBand thread owns its own copy (FAST always compiles with BANDED).
    static thread_local int AET_Bucket[2048];      // poly indices, counting-sorted by YTop
    static thread_local int AET_Active[2048];       // current active list, sorted by index
    static thread_local s32 AET_BucketStart[193];   // start offset of each scanline's bucket
    void AETBuild(int npolys);
    int  AETAdvance(int nActive, s32 y);
    void RenderActiveList(s32 y, int nActive);
#endif
    u32 CalculateFogDensity(u32 pixeladdr) const;
    void ScanlineFinalPass(s32 y);
    void ClearBuffers();
    void RenderPolygons(bool threaded, Polygon** polygons, int npolys);

    void RenderThreadFunc();

    // buffer dimensions are 258x194 to add a offscreen 1px border
    // which simplifies edge marking tests
    // buffer is duplicated to keep track of the two topmost pixels
    // TODO: check if the hardware can accidentally plot pixels
    // offscreen in that border

    static constexpr int ScanlineWidth = 258;
    static constexpr int NumScanlines = 194;
    static constexpr int BufferSize = ScanlineWidth * NumScanlines;
#ifdef LITEV_SOFT3D_UNDERCOLO
#if defined(LITEV_SOFT3D_BANDTILE) || defined(LITEV_SOFT3D_PIPELINE2)
#error "LITEV_SOFT3D_UNDERCOLO remaps the framebuffer layout and is incompatible with BANDTILE/PIPELINE2 (which shadow the buffer bases/BufferSize). Disable those (the shipping config already does)."
#endif
    // Per-ROW interleaved layout: each buffer row occupies RowStride (pow2 1024) words --
    // top sub-row cols [0,258), under sub-row [512,770). A pixel's under-slot is UnderOffset
    // (512 words, 2KB, same 4KB page) from its top-slot, not +BufferSize (+195KB). bit9
    // (UnderOffset) tags top vs under so the slot test is a cheap AND. Top stays stride-1
    // within a row. Cols [258,512) and [770,1024) are inert padding (never addressed).
    static constexpr int RowStride   = 1024;
    static constexpr int UnderOffset = 512;
    static constexpr int PlaneWords  = NumScanlines * RowStride;   // 194 * 1024
#else
    static constexpr int RowStride   = ScanlineWidth;
    static constexpr int UnderOffset = BufferSize;
    static constexpr int PlaneWords  = BufferSize * 2;
#endif
    static constexpr int FirstPixelOffset = RowStride + 1;

#ifdef LITEV_SOFT3D_PIPELINE2
    // Pipeline step 1: DOUBLE-BUFFER the 3D planes by frame parity so a depth-2 pipeline
    // (step 3) can have two frames' 3D output coexist. A "bank" = one frame's planes (the
    // usual [BufferSize*2], i.e. AA top+under slots). The raster writes bank[P2RenderBank];
    // GetLine/ScanlineFinalPass read bank[P2ConsumeBank], via the shadow-pointer trick.
    // UNDER DEPTH-1 (step 1) the barrier still sequences 2D-N before 3D-N+1, so only one
    // bank is ever live and P2RenderBank==P2ConsumeBank per frame -> byte-identical; the
    // second bank is allocated but the parity just alternates.
    static constexpr int P2BankStride = BufferSize * 2;   // words per parity bank
    // Part 3: THREE 3D-plane banks (mod-3), not two. The 3D raster is kicked at VCount 215,
    // ~1 frame AHEAD of its 2D consumer (VBlank); under depth-2 the 2D-N consumer reads
    // bank[p_N] for ~2 frames while 3D-{N+2} (kicked before the VBlank(N+2) drain) would
    // reuse bank[p_N] with only 2 banks -> data race. mod-3 gives 3D-{N+2} a distinct bank;
    // bank[p_N] is only reused by 3D-{N+3}, long after 2D-N drained. Byte-identical at
    // depth-1 (one bank live; mod-3 just alternates 0/1/2). NB the TEXTURE shadow stays
    // 2-bank (read only by the SERIAL 3D raster, <=1 outstanding) -> keyed on P2*Parity & 1.
    u32 ColorBuffer[P2BankStride * 3];
    u32 DepthBuffer[P2BankStride * 3];
    u32 AttrBuffer[P2BankStride * 3];
    // Parity (0/1/2) cycled by the emu at the 3D kick; the banks' word offsets. Shared
    // members set at ordered points (kick / render-wake / 2D-frame-start) with sema
    // release/acquire.
    int P2KickParity = 0;    // emu, cycled 0->1->2->0 in RenderFrame (real frames only)
    int P2RenderParity = 0;  // render-thread latch of P2KickParity (0/1/2); drives P2RenderBank
                             // AND the flat-texture read shadow (& 1 -> mod-2 A/B).
    int P2RenderBank = 0;    // = P2RenderParity * P2BankStride (raster + producer final pass)
    int P2ConsumeBank = 0;   // = consume-parity * P2BankStride (GetLine + consumer final pass).
                             // Depth-2: set per-frame by the 2D thread from the in-flight ring
                             // (via the public Pipeline2SetConsumeParity). Depth-1: latched by
                             // Pipeline2LatchConsume (== P2KickParity).
#else
    u32 ColorBuffer[PlaneWords];
    u32 DepthBuffer[PlaneWords];
    u32 AttrBuffer[PlaneWords];
#endif

#ifdef LITEV_SOFT3D_BANDTILE
    // DraStic-style cache-resident raster tiles (LITEV_SOFT3D_BANDTILE). We rasterize into
    // the full-frame Color/Depth/Attr buffers (~1.2MB incl. the x2 AA under-slot) -> every
    // per-pixel depth read-modify-write + attr access misses the 512KB shared L2 to DRAM.
    // Instead each band rasters into a small CACHE-RESIDENT sub-tile (BtChunkRows lines,
    // both AA slots), then copies the tile out to the real framebuffer -- keeping the raster
    // working set in L2/L1 (DraStic's ctx+0x20000 16-line tiles). ScanlineFinalPass (Phase 2)
    // is UNCHANGED and runs on the full framebuffer AFTER copy-out, so its y-1/y+1 neighbour
    // reads never cross a tile edge. OUTPUT byte-identical: same raster math + writes, just
    // staged through a tile then copied out.
    //
    // Retarget with ZERO per-site edits: the raster fns (RenderPolygonScanline /
    // RenderShadowMaskScanline / PlotTranslucentPixel) SHADOW the member ColorBuffer /
    // DepthBuffer / AttrBuffer / BufferSize / FirstPixelOffset with per-chunk locals sourced
    // from Bt* below. Shadowing the buffer bases retargets every ColorBuffer[pixeladdr];
    // shadowing BufferSize retargets the AA under-slot stride (pixeladdr+BufferSize);
    // shadowing FirstPixelOffset (= (SW+1) - BtTileY0*SW) folds the chunk's y-origin into
    // pixeladdr so it becomes tile-relative with no change to the pixeladdr expressions.
    static constexpr int BtChunkRows = 16;                        // sub-tile height (tunable)
    static constexpr int BtTileRows  = BtChunkRows + 2;           // + a border row of slack
    static constexpr int BtTileSlot  = ScanlineWidth * BtTileRows; // per-AA-slot stride (words)
    static thread_local u32 BtTileColor[BtTileSlot * 2];
    static thread_local u32 BtTileDepth[BtTileSlot * 2];
    static thread_local u32 BtTileAttr [BtTileSlot * 2];
    // Per-chunk retarget state (thread_local: bands run concurrently). Set to the REAL
    // buffers by default (shadow == no-op) and pointed at the tile by RenderBand per chunk.
    static thread_local u32* BtCB;
    static thread_local u32* BtDB;
    static thread_local u32* BtAB;
    static thread_local s32  BtSlot;
    static thread_local s32  BtTileY0;
    void BandTileCopyIn(s32 cy0, s32 cy1);   // real (cleared) top-slot rows -> tile
    void BandTileCopyOut(s32 cy0, s32 cy1);  // tile (both slots) -> real framebuffer
#endif

    // attribute buffer:
    // bit0-3: edge flags (left/right/top/bottom)
    // bit4: backfacing flag
    // bit8-12: antialiasing alpha
    // bit15: fog enable
    // bit16-21: polygon ID for translucent pixels
    // bit22: translucent flag
    // bit24-29: polygon ID for opaque pixels

#ifdef LITEV_SOFT3D_BANDED
    static thread_local u8 StencilBuffer[256*2];
    static thread_local bool PrevIsShadowMask;
#else
    u8 StencilBuffer[256*2];
    bool PrevIsShadowMask;
#endif

    bool Enabled;

    bool FrameIdentical;

    u32 ScrolledLine[256];

    // threading

    bool Threaded = false;
    Platform::Thread* RenderThread;
    std::atomic_bool RenderThreadRunning;
    std::atomic_bool RenderThreadRendering;

    // Used by the main thread to tell the render thread to start rendering a frame
    Platform::Semaphore* Sema_RenderStart;

    // Used by the render thread to tell the main thread that it's done rendering a frame
    Platform::Semaphore* Sema_RenderDone;

    // Used to allow the main thread to read some scanlines
    // before (the 3D portion of) the entire frame is rasterized.
    Platform::Semaphore* Sema_ScanlineCount;
};
}
