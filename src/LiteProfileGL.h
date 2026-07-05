/*
    LiteProfileGL - thin per-frame GL call counters for the melonDS OpenGL
    renderer (liteDS-v2, Appendix D.7 §R3).

    This header redefines the handful of GL entry points the renderer uses as
    tiny counting wrappers so we can measure the per-frame GL call distribution
    (draws / texture binds / program switches / uniform updates / sampler-state
    changes / buffer+texture upload volume) without building a full GL
    interposer. Each wrapper increments an atomic in LiteProfile::g_Frame and
    then forwards to the real GL call.

    IMPORTANT ordering: include this header in a renderer .cpp AFTER the GL
    headers (i.e. after GPU_OpenGL.h / OpenGLSupport.h), because on Android the
    GLES3 compat shim (GLES_Compat.h) already remaps the glTexImage / glTexSubImage
    entry points to melonGL* wrappers; we #undef those and forward to the melonGL*
    directly so the 1555->5551 conversion is preserved.

    Gating: the wrappers only exist when LITEV_PROFILE && __ANDROID__. That is
    where the Mali userspace GL driver is the ~7ms/frame in-race suspect and
    where the instrumented APK runs. Desktop GL builds (and every LITEV_PROFILE=0
    build) see NOTHING here: the GL names are left completely untouched, so
    default builds and the host golden gate are byte-for-byte unaffected. This
    also sidesteps glad's object-like glFoo->glad_glFoo macros on desktop.
*/

#pragma once

#include "LiteProfile.h"

#if LITEV_PROFILE && defined(__ANDROID__)

#include <atomic>
#include <cstdint>

namespace melonDS::LiteProfile
{

// Estimate the byte volume of a texture upload. Exact byte accounting needs the
// format/type -> bytes-per-texel mapping; we cover the cases the DS renderer
// actually uses (RGBA8 4bpp, packed 5551/565/4444 shorts 2bpp, R8 1bpp) and
// fall back to 4 bytes/texel for anything else. Cheap: a couple of switches.
inline uint64_t GLPixelBytes(GLenum format, GLenum type,
                             GLsizei w, GLsizei h, GLsizei d = 1)
{
    const uint64_t texels = (uint64_t)(w > 0 ? w : 0)
                          * (uint64_t)(h > 0 ? h : 0)
                          * (uint64_t)(d > 0 ? d : 0);

    // Packed pixel types describe the whole texel in one unit.
    switch (type)
    {
        case GL_UNSIGNED_SHORT_5_6_5:
        case GL_UNSIGNED_SHORT_4_4_4_4:
        case GL_UNSIGNED_SHORT_5_5_5_1:
        case GL_HALF_FLOAT:
            return texels * 2ull;
        case GL_UNSIGNED_INT_2_10_10_10_REV:
        case GL_UNSIGNED_INT_24_8:
        case GL_UNSIGNED_INT_10F_11F_11F_REV:
        case GL_UNSIGNED_INT_5_9_9_9_REV:
            return texels * 4ull;
        default:
            break;
    }

    uint64_t comps;
    switch (format)
    {
        case GL_RED: case GL_RED_INTEGER: case GL_DEPTH_COMPONENT:
            comps = 1; break;
        case GL_RG: case GL_RG_INTEGER: case GL_DEPTH_STENCIL:
            comps = 2; break;
        case GL_RGB: case GL_RGB_INTEGER:
            comps = 3; break;
        default: // GL_RGBA / GL_RGBA_INTEGER / anything else
            comps = 4; break;
    }

    uint64_t bpc;
    switch (type)
    {
        case GL_UNSIGNED_BYTE: case GL_BYTE:
            bpc = 1; break;
        case GL_UNSIGNED_SHORT: case GL_SHORT:
            bpc = 2; break;
        default: // GL_UNSIGNED_INT / GL_INT / GL_FLOAT
            bpc = 4; break;
    }

    return texels * comps * bpc;
}

} // namespace melonDS::LiteProfile

// Fetch-add helpers (relaxed): one for a plain +1 count, one for a byte volume.
#define LITEV_GL_INC(field) \
    (::melonDS::LiteProfile::g_Frame.field.fetch_add(1, std::memory_order_relaxed))
#define LITEV_GL_ADD(field, n) \
    (::melonDS::LiteProfile::g_Frame.field.fetch_add((uint64_t)(n), std::memory_order_relaxed))

// --- Plain GLES3 entry points (not remapped by GLES_Compat) -----------------
// The parenthesised (glFoo) suppresses re-expansion of the function-like macro
// during rescan, so it resolves to the real GLES symbol.
#define glDrawArrays(...)      ( LITEV_GL_INC(GLDrawCalls),        (glDrawArrays)(__VA_ARGS__) )
#define glDrawElements(...)    ( LITEV_GL_INC(GLDrawCalls),        (glDrawElements)(__VA_ARGS__) )
#define glBindTexture(...)     ( LITEV_GL_INC(GLTexBinds),         (glBindTexture)(__VA_ARGS__) )
#define glUseProgram(...)      ( LITEV_GL_INC(GLProgramSwitches),  (glUseProgram)(__VA_ARGS__) )
#define glUniform1i(...)       ( LITEV_GL_INC(GLUniformCalls),     (glUniform1i)(__VA_ARGS__) )
#define glUniform1ui(...)      ( LITEV_GL_INC(GLUniformCalls),     (glUniform1ui)(__VA_ARGS__) )
#define glUniform2f(...)       ( LITEV_GL_INC(GLUniformCalls),     (glUniform2f)(__VA_ARGS__) )
#define glUniform4ui(...)      ( LITEV_GL_INC(GLUniformCalls),     (glUniform4ui)(__VA_ARGS__) )
#define glTexParameteri(...)   ( LITEV_GL_INC(GLTexParamCalls),    (glTexParameteri)(__VA_ARGS__) )
#define glBindBuffer(...)      ( LITEV_GL_INC(GLBufferBinds),      (glBindBuffer)(__VA_ARGS__) )
#define glBindFramebuffer(...) ( LITEV_GL_INC(GLFramebufferBinds), (glBindFramebuffer)(__VA_ARGS__) )

// Buffer uploads carry their byte count in the size argument directly.
#define glBufferData(t, s, d, u) \
    ( LITEV_GL_INC(GLBufferUploads), \
      LITEV_GL_ADD(GLUploadBytes, (int64_t)(s) > 0 ? (int64_t)(s) : 0), \
      (glBufferData)((t), (s), (d), (u)) )
#define glBufferSubData(t, o, s, d) \
    ( LITEV_GL_INC(GLBufferUploads), \
      LITEV_GL_ADD(GLUploadBytes, (int64_t)(s) > 0 ? (int64_t)(s) : 0), \
      (glBufferSubData)((t), (o), (s), (d)) )

// --- Texture uploads: remapped to melonGL* by GLES_Compat.h -----------------
// #undef the compat macro, then forward to the melonGL* wrapper (Android-only,
// so the symbol is guaranteed present) after estimating the byte volume.
#undef glTexImage2D
#undef glTexSubImage2D
#undef glTexImage3D
#undef glTexSubImage3D

#define glTexImage2D(t, l, ifmt, w, h, b, f, ty, data) \
    ( LITEV_GL_INC(GLTexUploads), \
      LITEV_GL_ADD(GLUploadBytes, (data) ? ::melonDS::LiteProfile::GLPixelBytes((f), (ty), (w), (h)) : 0), \
      melonGLTexImage2D((t), (l), (ifmt), (w), (h), (b), (f), (ty), (data)) )
#define glTexSubImage2D(t, l, x, y, w, h, f, ty, data) \
    ( LITEV_GL_INC(GLTexUploads), \
      LITEV_GL_ADD(GLUploadBytes, (data) ? ::melonDS::LiteProfile::GLPixelBytes((f), (ty), (w), (h)) : 0), \
      melonGLTexSubImage2D((t), (l), (x), (y), (w), (h), (f), (ty), (data)) )
#define glTexImage3D(t, l, ifmt, w, h, dep, b, f, ty, data) \
    ( LITEV_GL_INC(GLTexUploads), \
      LITEV_GL_ADD(GLUploadBytes, (data) ? ::melonDS::LiteProfile::GLPixelBytes((f), (ty), (w), (h), (dep)) : 0), \
      melonGLTexImage3D((t), (l), (ifmt), (w), (h), (dep), (b), (f), (ty), (data)) )
#define glTexSubImage3D(t, l, x, y, z, w, h, dep, f, ty, data) \
    ( LITEV_GL_INC(GLTexUploads), \
      LITEV_GL_ADD(GLUploadBytes, (data) ? ::melonDS::LiteProfile::GLPixelBytes((f), (ty), (w), (h), (dep)) : 0), \
      melonGLTexSubImage3D((t), (l), (x), (y), (z), (w), (h), (dep), (f), (ty), (data)) )

#endif // LITEV_PROFILE && __ANDROID__
