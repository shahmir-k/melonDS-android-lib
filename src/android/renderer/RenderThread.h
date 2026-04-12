/*
    Copyright 2016-2025 melonDS team
    liteDS modifications: PBO async software-renderer upload (Phase 1.4)

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

#ifdef LITEV_THREADED_RENDER

#include <GLES3/gl3.h>
#include "types.h"

namespace MelonDSAndroid
{

// Async software-renderer texture upload via OpenGL ES 3.0 PBO double-buffering.
//
// Problem this solves:
//   glTexSubImage2D(... cpuPointer ...) can block the calling thread while the
//   driver synchronises with the GPU (e.g. waits for the texture to finish
//   displaying before overwriting it). On older Android drivers (Adreno 306,
//   Mali-400) this stall is 1–3 ms per frame — directly eating emulation budget.
//
// Solution:
//   Pixel Buffer Objects let the CPU write into a driver-managed buffer that the
//   GPU DMA engine reads independently. When glTexSubImage2D is called with a
//   PBO bound, the driver schedules a GPU-side DMA and returns immediately.
//   GL_MAP_INVALIDATE_BUFFER_BIT ensures the driver allocates fresh backing
//   memory for the map even if the GPU is still reading the old PBO.
//
// Usage:
//   init(texHeight)                           — once per GL context / size change
//   uploadFrame(top, bot, tex, bottomScreenY) — each software-rendered frame
//   cleanup()                                 — when GL context is torn down
//
// Thread safety: all methods must be called from the same GL thread that owns
// the EGL context. No internal locking is performed.

class SoftwareRenderUploader
{
public:
    // Initialise (or reinitialise) PBOs for the given total texture height.
    // texHeight = screenHeight * 2.  Safe to call every frame; is a no-op if
    // already initialised with the same height.
    void init(int texHeight);

    // Release all GL resources.  Safe to call even if init() was never called.
    void cleanup();

    // Upload both DS screens into `texture` via an async PBO transfer.
    //   topScreen    : 256×192 pixels in the format written by GPU2D_Soft
    //   bottomScreen : 256×192 pixels
    //   texture      : GL_TEXTURE_2D name to upload into
    //   bottomScreenY: Y-offset (in texels) of the bottom screen in the texture
    //                  (typically 194 = 192 rows + 2-pixel gap)
    void uploadFrame(const melonDS::u32* topScreen,
                     const melonDS::u32* bottomScreen,
                     GLuint texture,
                     int bottomScreenY);

    [[nodiscard]] bool isInitialized() const noexcept { return initialized; }

private:
    static constexpr int PBO_COUNT = 2;
    GLuint  pbo[PBO_COUNT] {0, 0};
    int     pboSize  = 0;
    int     texH     = 0;
    int     writeIdx = 0;
    bool    initialized = false;
};

} // namespace MelonDSAndroid

#endif // LITEV_THREADED_RENDER
