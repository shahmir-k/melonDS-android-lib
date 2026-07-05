/*
    LiteGLStateCache - CPU-side shadow of bound GL state for the melonDS OpenGL
    renderer (liteDS-v2, Appendix D.7 §R3: "GL redundant-state diet").

    R0 diagnosis: the Mali userspace GL driver spends ~5ms/frame CONSTRUCTING the
    command buffer from the renderer's GL calls, and that work sits on the serial
    critical path. R3 measurement aimed the diet: per-frame in-race medians of
    binds=529 (glBindTexture), texparam=251 (glTexParameteri), with ~411 binds
    present even at a STATIC menu -> a large FIXED redundant-rebind baseline.
    Cutting call count shortens the frame directly.

    This header keeps a small CPU-side shadow of the currently-bound GL state
    (active texture unit + bound texture per unit/target, bound program, bound
    read/draw framebuffers, bound GL_ARRAY_BUFFER, and per-texture-object sampler
    params). It then redefines the renderer's state-changing GL entry points as
    tiny wrappers: a bind/param whose shadow already holds the requested value is
    a driver-level no-op, so we SKIP the call entirely. Everything else forwards
    to the real GL call unchanged.

    Correctness invariant (the critical point): skipping a redundant call must
    leave GL in the identical state to issuing it. That is a driver identity (a
    bind to the already-bound object changes nothing). The ONLY way this can go
    wrong is a stale shadow -- GL state changed by some path we do not track.
    Guards against staleness:
      * ResetStateCache() is called at frame start (end of the previous frame's
        VBlank, before the app glue's own blit/present binds run) and at GL
        context (re)creation, so NO shadow assumption ever survives across a
        frame boundary or a context reset. A reset marks every slot "unknown",
        so the FIRST bind of each object each frame always issues.
      * glDeleteTextures unbinds the deleted name from every binding point in the
        context (GL sets them to 0). We wrap it and invalidate the texture-bind
        shadow so we can never skip a re-bind to a name GL silently reset.
      * glGenTextures / glDeleteTextures drop the per-object param cache entry so
        a reused texture name (fresh GL-default params) is never assumed cached.
    Anything else the cache does not own (glBindBufferBase's generic binding,
    VAO element-array binding, the compute renderer, the app-glue blit) is either
    left uncached or neutralised by the per-frame reset -- see the scope notes on
    each wrapper below.

    Gating: active only when LITEV_GL_STATE_CACHE && __ANDROID__ (the Mali target).
    Flag OFF (default) or non-Android: this header is completely inert -- the GL
    names are left untouched and the reset macro is a no-op, so default builds and
    the host golden are byte-for-byte unaffected. Include AFTER the GL headers AND
    AFTER LiteProfileGL.h (same ordering rule as LiteProfileGL.h).
*/

#pragma once

#include "LiteProfile.h"

#if defined(LITEV_GL_STATE_CACHE) && defined(__ANDROID__)

#include <cstdint>
#include <unordered_map>

// LiteProfileGL.h (when LITEV_PROFILE is also on) has already #defined these
// entry points as per-frame counting macros. We take over exactly the six
// state-changing calls below (plus gen/delete for invalidation), so drop the
// profiling macro for those names first. Our wrappers do the profiling
// increment THEMSELVES, but only when a call is actually ISSUED -- that is what
// makes the before/after LITEV_GL counter drop equal the number of redundant
// calls removed. The un-taken entry points (draws, uniforms, uploads) keep
// their LiteProfileGL.h counting macros unchanged.
#ifdef glBindTexture
#undef glBindTexture
#endif
#ifdef glActiveTexture
#undef glActiveTexture
#endif
#ifdef glUseProgram
#undef glUseProgram
#endif
#ifdef glBindFramebuffer
#undef glBindFramebuffer
#endif
#ifdef glBindBuffer
#undef glBindBuffer
#endif
#ifdef glTexParameteri
#undef glTexParameteri
#endif
#ifdef glGenTextures
#undef glGenTextures
#endif
#ifdef glDeleteTextures
#undef glDeleteTextures
#endif

// Count an ISSUED call into the LiteProfile per-frame counter, but only when the
// profiler is compiled in (LITEV_PROFILE). When the profiler is off there is no
// g_Frame, so this compiles to nothing.
#if LITEV_PROFILE
#define LITEV_GLSC_COUNT(field) \
    (::melonDS::LiteProfile::g_Frame.field.fetch_add(1, std::memory_order_relaxed))
#else
#define LITEV_GLSC_COUNT(field) ((void)0)
#endif

namespace melonDS::LiteGLCache
{

// Shadow of the GL state the renderer actually touches. Single shared instance
// (all four renderer .cpp files bind on the one GL context). Every field carries
// a "known" flag; a Reset() marks everything unknown so the first bind of each
// object after a reset always issues. Sentinel-free: we use explicit known-bits
// rather than a magic object id so that binding to 0 (unbind) is cached too.
struct State
{
    static constexpr int MaxUnits = 8; // renderer uses units 0..2; 8 is headroom

    bool   ActiveUnitKnown = false;
    GLenum ActiveUnit = GL_TEXTURE0;   // assumed default for keying pre-first-glActiveTexture

    GLuint Tex2D[MaxUnits] = {};       // GL_TEXTURE_2D bound per unit
    bool   Tex2DKnown[MaxUnits] = {};
    GLuint Tex2DArray[MaxUnits] = {};  // GL_TEXTURE_2D_ARRAY bound per unit
    bool   Tex2DArrayKnown[MaxUnits] = {};

    bool   ProgramKnown = false;  GLuint Program = 0;
    bool   ReadFBKnown  = false;  GLuint ReadFB  = 0;
    bool   DrawFBKnown  = false;  GLuint DrawFB  = 0;
    bool   ArrayBufKnown = false; GLuint ArrayBuf = 0;

    // Per-texture-object sampler params (only the four pnames the renderer sets).
    struct TexParams
    {
        bool  WrapSKnown = false; GLint WrapS = 0;
        bool  WrapTKnown = false; GLint WrapT = 0;
        bool  MinKnown   = false; GLint Min   = 0;
        bool  MagKnown   = false; GLint Mag   = 0;
    };
    std::unordered_map<GLuint, TexParams> Params;

    void Reset()
    {
        ActiveUnitKnown = false;
        ActiveUnit = GL_TEXTURE0;
        for (int i = 0; i < MaxUnits; i++)
        {
            Tex2DKnown[i] = false;      Tex2D[i] = 0;
            Tex2DArrayKnown[i] = false; Tex2DArray[i] = 0;
        }
        ProgramKnown = false;  Program = 0;
        ReadFBKnown  = false;  ReadFB  = 0;
        DrawFBKnown  = false;  DrawFB  = 0;
        ArrayBufKnown = false; ArrayBuf = 0;
        Params.clear();
    }
};

inline State g_GLState;

// GL_TEXTUREi -> unit index, or -1 if outside the tracked range (pass-through).
inline int UnitIndex(GLenum unit)
{
    if (unit < GL_TEXTURE0) return -1;
    int idx = (int)(unit - GL_TEXTURE0);
    return (idx >= 0 && idx < State::MaxUnits) ? idx : -1;
}

// Currently-bound texture object on the active unit for a target, or 0/unknown.
inline bool CurrentTexObj(GLenum target, GLuint& out)
{
    State& s = g_GLState;
    if (!s.ActiveUnitKnown) return false;
    int u = UnitIndex(s.ActiveUnit);
    if (u < 0) return false;
    if (target == GL_TEXTURE_2D && s.Tex2DKnown[u]) { out = s.Tex2D[u]; return true; }
    if (target == GL_TEXTURE_2D_ARRAY && s.Tex2DArrayKnown[u]) { out = s.Tex2DArray[u]; return true; }
    return false;
}

inline void ActiveTexture(GLenum unit)
{
    State& s = g_GLState;
    if (s.ActiveUnitKnown && s.ActiveUnit == unit) return; // redundant no-op
    s.ActiveUnit = unit;
    s.ActiveUnitKnown = true;
    glActiveTexture(unit); // real GLES entry (macro #undef'd above)
}

inline void BindTexture(GLenum target, GLuint tex)
{
    State& s = g_GLState;
    // Only skip when we CONFIRMED the active unit this frame; otherwise the unit
    // key is unproven, so always issue (correctness over savings).
    int u = s.ActiveUnitKnown ? UnitIndex(s.ActiveUnit) : -1;
    if (u >= 0 && target == GL_TEXTURE_2D)
    {
        if (s.Tex2DKnown[u] && s.Tex2D[u] == tex) return;
        s.Tex2D[u] = tex; s.Tex2DKnown[u] = true;
    }
    else if (u >= 0 && target == GL_TEXTURE_2D_ARRAY)
    {
        if (s.Tex2DArrayKnown[u] && s.Tex2DArray[u] == tex) return;
        s.Tex2DArray[u] = tex; s.Tex2DArrayKnown[u] = true;
    }
    // else: unknown unit or unhandled target -> fall through and issue.
    LITEV_GLSC_COUNT(GLTexBinds);
    glBindTexture(target, tex);
}

inline void UseProgram(GLuint prog)
{
    State& s = g_GLState;
    if (s.ProgramKnown && s.Program == prog) return;
    s.Program = prog; s.ProgramKnown = true;
    LITEV_GLSC_COUNT(GLProgramSwitches);
    glUseProgram(prog);
}

inline void BindFramebuffer(GLenum target, GLuint fb)
{
    State& s = g_GLState;
    if (target == GL_FRAMEBUFFER)
    {
        // GL_FRAMEBUFFER sets BOTH the read and draw binding points.
        if (s.ReadFBKnown && s.DrawFBKnown && s.ReadFB == fb && s.DrawFB == fb) return;
        s.ReadFB = fb; s.DrawFB = fb;
        s.ReadFBKnown = true; s.DrawFBKnown = true;
    }
    else if (target == GL_READ_FRAMEBUFFER)
    {
        if (s.ReadFBKnown && s.ReadFB == fb) return;
        s.ReadFB = fb; s.ReadFBKnown = true;
    }
    else if (target == GL_DRAW_FRAMEBUFFER)
    {
        if (s.DrawFBKnown && s.DrawFB == fb) return;
        s.DrawFB = fb; s.DrawFBKnown = true;
    }
    // else: unknown target -> issue.
    LITEV_GLSC_COUNT(GLFramebufferBinds);
    glBindFramebuffer(target, fb);
}

inline void BindBuffer(GLenum target, GLuint buf)
{
    State& s = g_GLState;
    // Only GL_ARRAY_BUFFER is cached. Its generic binding point is NOT affected
    // by glBindVertexArray (VAO state) nor by glBindBufferBase (the renderer only
    // uses glBindBufferBase on GL_UNIFORM_BUFFER), so the shadow cannot go stale.
    // Other targets (UNIFORM_BUFFER, ELEMENT_ARRAY_BUFFER) are deliberately left
    // uncached because ownership is ambiguous -- see report.
    if (target == GL_ARRAY_BUFFER)
    {
        if (s.ArrayBufKnown && s.ArrayBuf == buf) return;
        s.ArrayBuf = buf; s.ArrayBufKnown = true;
    }
    LITEV_GLSC_COUNT(GLBufferBinds);
    glBindBuffer(target, buf);
}

inline void TexParameteri(GLenum target, GLenum pname, GLint param)
{
    State& s = g_GLState;
    GLuint obj = 0;
    if (CurrentTexObj(target, obj) && obj != 0)
    {
        State::TexParams& p = s.Params[obj];
        bool* known = nullptr; GLint* val = nullptr;
        switch (pname)
        {
            case GL_TEXTURE_WRAP_S:     known = &p.WrapSKnown; val = &p.WrapS; break;
            case GL_TEXTURE_WRAP_T:     known = &p.WrapTKnown; val = &p.WrapT; break;
            case GL_TEXTURE_MIN_FILTER: known = &p.MinKnown;   val = &p.Min;   break;
            case GL_TEXTURE_MAG_FILTER: known = &p.MagKnown;   val = &p.Mag;   break;
            default: break; // uncached pname -> always issue
        }
        if (known)
        {
            if (*known && *val == param) return; // param already set on this object
            *known = true; *val = param;
        }
    }
    LITEV_GLSC_COUNT(GLTexParamCalls);
    glTexParameteri(target, pname, param);
}

inline void GenTextures(GLsizei n, GLuint* textures)
{
    glGenTextures(n, textures); // real call fills the names
    if (!textures) return;
    State& s = g_GLState;
    // A freshly generated name carries GL-default params; drop any stale entry so
    // the next glTexParameteri on it is not wrongly skipped.
    for (GLsizei i = 0; i < n; i++)
        s.Params.erase(textures[i]);
}

inline void DeleteTextures(GLsizei n, const GLuint* textures)
{
    glDeleteTextures(n, textures); // real call unbinds deleted names -> binding 0
    if (!textures) return;
    State& s = g_GLState;
    for (GLsizei i = 0; i < n; i++)
        s.Params.erase(textures[i]);
    // GL just reset every binding point that held a deleted name to 0; our
    // per-unit texture-bind shadow could now be stale. Invalidate it wholesale
    // (deletes are rare -- texcache eviction only) so a re-bind is never skipped.
    // Program / FBO / buffer shadows are unaffected by texture deletion.
    for (int u = 0; u < State::MaxUnits; u++)
    {
        s.Tex2DKnown[u] = false;
        s.Tex2DArrayKnown[u] = false;
    }
}

inline void ResetStateCache() { g_GLState.Reset(); }

} // namespace melonDS::LiteGLCache

// Redefine the renderer's state-changing entry points as the caching wrappers.
// Fixed arity (matches every call site) so no __VA_ARGS__ is needed.
#define glActiveTexture(u)       ::melonDS::LiteGLCache::ActiveTexture((u))
#define glBindTexture(t, o)      ::melonDS::LiteGLCache::BindTexture((t), (o))
#define glUseProgram(p)          ::melonDS::LiteGLCache::UseProgram((p))
#define glBindFramebuffer(t, f)  ::melonDS::LiteGLCache::BindFramebuffer((t), (f))
#define glBindBuffer(t, b)       ::melonDS::LiteGLCache::BindBuffer((t), (b))
#define glTexParameteri(t, p, v) ::melonDS::LiteGLCache::TexParameteri((t), (p), (v))
#define glGenTextures(n, a)      ::melonDS::LiteGLCache::GenTextures((n), (a))
#define glDeleteTextures(n, a)   ::melonDS::LiteGLCache::DeleteTextures((n), (a))

#define LITEV_GL_RESET_STATE_CACHE() ::melonDS::LiteGLCache::ResetStateCache()

#else // !(LITEV_GL_STATE_CACHE && __ANDROID__)

// Inert: leave every GL name untouched; the reset hook compiles to nothing.
#define LITEV_GL_RESET_STATE_CACHE() ((void)0)

#endif // LITEV_GL_STATE_CACHE && __ANDROID__
