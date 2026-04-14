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

#ifdef LITEV_PBO_UPLOAD

#include "RenderThread.h"

#include <cstdint>
#include <cstring>
#include <GLES3/gl3.h>

namespace MelonDSAndroid
{

void SoftwareRenderUploader::init(int texHeight)
{
    // Re-init only if the texture height changed (e.g. screen layout switch)
    if (initialized && texH == texHeight)
        return;

    cleanup();

    texH    = texHeight;
    pboSize = 256 * texH * 4; // RGBA: 4 bytes/pixel

    glGenBuffers(PBO_COUNT, pbo);
    for (int i = 0; i < PBO_COUNT; ++i)
    {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo[i]);
        // GL_STREAM_DRAW: updated once per frame, read once by GPU DMA.
        glBufferData(GL_PIXEL_UNPACK_BUFFER, pboSize, nullptr, GL_STREAM_DRAW);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    writeIdx    = 0;
    initialized = true;
}

void SoftwareRenderUploader::cleanup()
{
    if (!initialized)
        return;

    glDeleteBuffers(PBO_COUNT, pbo);
    pbo[0] = pbo[1] = 0;
    pboSize  = 0;
    texH     = 0;
    writeIdx = 0;
    initialized = false;
}

void SoftwareRenderUploader::uploadFrame(const melonDS::u32* topScreen,
                                          const melonDS::u32* bottomScreen,
                                          GLuint texture,
                                          int bottomScreenY)
{
    if (!initialized)
        return;

    GLuint curPbo = pbo[writeIdx];
    writeIdx = 1 - writeIdx;

    // ── Write pixel data into the PBO ────────────────────────────────────────
    //
    // GL_MAP_INVALIDATE_BUFFER_BIT: if the GPU is still reading the *previous*
    // contents of this PBO (from two frames ago), the driver must give us fresh
    // memory rather than stalling. This is the key to non-blocking uploads.
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, curPbo);
    auto* mapped = static_cast<melonDS::u8*>(glMapBufferRange(
        GL_PIXEL_UNPACK_BUFFER, 0, pboSize,
        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT));

    if (mapped)
    {
        // Top screen occupies rows [0, 192).
        std::memcpy(mapped,
                    topScreen,
                    256 * 192 * 4);

        // Bottom screen occupies rows [bottomScreenY, bottomScreenY + 192).
        // The rows in between (the 2-pixel gap) stay uninitialised — they're
        // covered by the black letterbox in the display shader.
        std::memcpy(mapped + static_cast<std::size_t>(bottomScreenY) * 256 * 4,
                    bottomScreen,
                    256 * 192 * 4);

        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }

    // ── Async texture upload from PBO ─────────────────────────────────────────
    //
    // With GL_PIXEL_UNPACK_BUFFER bound, the last argument to glTexSubImage2D
    // is interpreted as a byte offset into the PBO, NOT a CPU pointer.
    // The driver initiates a GPU-side DMA and returns immediately.
    glBindTexture(GL_TEXTURE_2D, texture);

    // Top screen: PBO offset 0
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, 0,
                    256, 192,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    reinterpret_cast<void*>(static_cast<std::uintptr_t>(0)));

    // Bottom screen: PBO offset = bottomScreenY * stride
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, bottomScreenY,
                    256, 192,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    reinterpret_cast<void*>(
                        static_cast<std::uintptr_t>(bottomScreenY) * 256 * 4));

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

} // namespace MelonDSAndroid

#endif // LITEV_PBO_UPLOAD
