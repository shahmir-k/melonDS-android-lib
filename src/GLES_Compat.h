/*
    liteDS-v2-android GLES3 compatibility shim.

    The upstream melonDS OpenGL renderer targets desktop OpenGL (GLSL 140 /
    glad). On Android the frontend builds the core against GLES3 headers
    (MELONDS_GL_HEADER=<GLES3/gl32.h>). A handful of desktop-only entry points
    and enums are missing from GLES3; this header provides equivalent inline
    shims so the renderer source compiles unchanged.

    Included by PlatformOGL.h AFTER the real GL header, only when building for
    Android. Desktop builds never see this file, so their behaviour is intact.

    NOTE (colour order): GLES3 has no GL_UNSIGNED_SHORT_1_5_5_5_REV. We alias it
    to GL_UNSIGNED_SHORT_5_5_5_1. For textures allocated with a null data pointer
    this is purely a format-validation token and has no visual effect. For the
    few sites that upload real DS palette/VRAM data (BGR555) the channel order
    differs and is compensated in the sampling shaders (see OpenGL_shaders).
*/
#ifndef MELONDS_GLES_COMPAT_H
#define MELONDS_GLES_COMPAT_H

#if defined(__ANDROID__)

#include <cstdint>

// --- Enum aliases -----------------------------------------------------------
#ifndef GL_UNSIGNED_SHORT_1_5_5_5_REV
#define GL_UNSIGNED_SHORT_1_5_5_5_REV GL_UNSIGNED_SHORT_5_5_5_1
#endif

// GLES has the *f suffixed depth entry points only.
static inline void melonGLClearDepth(double d) { glClearDepthf((float)d); }
static inline void melonGLDepthRange(double n, double f) { glDepthRangef((float)n, (float)f); }
#define glClearDepth  melonGLClearDepth
#define glDepthRange  melonGLDepthRange

// glDrawBuffer(single) -> glDrawBuffers(1, &buf)
static inline void melonGLDrawBuffer(GLenum buf)
{
    GLenum bufs[1] = { buf };
    glDrawBuffers(1, bufs);
}
#define glDrawBuffer melonGLDrawBuffer

// Frag data locations are declared in-shader via layout(location=) on GLES
// (see the GL_ES guarded FRAGLOC macro in the MRT shaders). This is a no-op.
static inline void melonGLBindFragDataLocation(GLuint /*program*/, GLuint /*colorNumber*/, const char* /*name*/) {}
#define glBindFragDataLocation melonGLBindFragDataLocation

// glMapBuffer(target, access) -> glMapBufferRange over the whole buffer.
#ifndef GL_READ_ONLY
#define GL_READ_ONLY  0x88B8
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9
#endif
#ifndef GL_READ_WRITE
#define GL_READ_WRITE 0x88BA
#endif
static inline void* melonGLMapBuffer(GLenum target, GLenum access)
{
    GLint size = 0;
    glGetBufferParameteriv(target, GL_BUFFER_SIZE, &size);
    GLbitfield bits = 0;
    if (access == GL_READ_ONLY)  bits = GL_MAP_READ_BIT;
    else if (access == GL_WRITE_ONLY) bits = GL_MAP_WRITE_BIT;
    else bits = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
    return glMapBufferRange(target, 0, size, bits);
}
#define glMapBuffer melonGLMapBuffer

#endif // __ANDROID__
#endif // MELONDS_GLES_COMPAT_H
