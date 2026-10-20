/* fbooff.c - run Chowdren without its (broken) screen FBO.
 *
 * Established on the console:
 *   - Chowdren creates one framebuffer (id 1) at 1280x720 and attaches texture
 *     1 to GL_COLOR_ATTACHMENT0. gl4es accepts the call but reports status 0x0
 *     with GL_INVALID_ENUM, and every later status query answers 0x8CD7
 *     (INCOMPLETE_MISSING_ATTACHMENT).
 *   - All game draws therefore go nowhere; the final composite samples an empty
 *     texture and presents a uniform white canvas.
 *   - The working fnaf4 build on this device never uses an FBO at all: all
 *     draws land on framebuffer 0 with the game scaled into the window.
 *
 * This shim reproduces that end state without rebuilding the game:
 *
 *   1. SDL_GL_GetProcAddress is interposed (Chowdren resolves the FBO entry
 *      points through it, so plain LD_PRELOAD of glBindFramebufferEXT would
 *      never be reached). Binding a framebuffer always binds 0; the attach is
 *      recorded but not performed; status always reports COMPLETE.
 *   2. glViewport is remapped: the game's 1280x720 canvas becomes the
 *      aspect-correct 640x360 rectangle centred in the 640x480 window, so the
 *      game's own ortho projection scales the picture down correctly.
 *   3. The present phase (after the app unbinds and clears the window, then
 *      draws the FBO texture) is suppressed: its clear would wipe the frame we
 *      just rendered, and its composite would paint the empty texture again.
 *
 * LD_PRELOAD before gl4es.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;

#define GL_BLEND               0x0BE2
#define GL_TEXTURE_2D          0x0DE1
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

typedef void (*PFN_GEN_FB)(GLsizei, GLuint *);
typedef void (*PFN_BIND_FB)(GLenum, GLuint);
typedef void (*PFN_FBTEX)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (*PFN_DEL_FB)(GLsizei, const GLuint *);
typedef GLenum (*PFN_CHECK_FB)(GLenum);

static void *(*real_gpa)(const char *) = NULL;
static PFN_GEN_FB p_gen_fb = NULL;
static PFN_DEL_FB p_del_fb = NULL;

static int phase_canvas = 0;   /* app believes it is drawing into canvas FBO */
static long suppressed_clear = 0, suppressed_present = 0, remapped_vp = 0;

/* ---- framebuffer plumbing -------------------------------------------- */

static void w_gen_fb(GLsizei n, GLuint *ids)
{
    if (p_gen_fb) p_gen_fb(n, ids);
    else if (ids) ids[0] = 1;
}

static void w_bind_fb(GLenum target, GLuint fb)
{
    static void (*real_bind)(GLenum, GLuint) = NULL;
    if (!real_bind) real_bind = (void (*)(GLenum, GLuint))dlsym(RTLD_DEFAULT, "glBindFramebuffer");
    phase_canvas = (fb != 0);
    if (real_bind) real_bind(target, 0);   /* everything lands on the window */
    else {
        void (*real_bind_ext)(GLenum, GLuint) =
            (void (*)(GLenum, GLuint))dlsym(RTLD_DEFAULT, "glBindFramebufferEXT");
        if (real_bind_ext) real_bind_ext(target, 0);
    }
}

static void w_fb_tex(GLenum target, GLenum attachment, GLenum textarget,
                     GLuint texture, GLint level)
{
    (void)target; (void)attachment; (void)textarget; (void)texture; (void)level;
    /* Deliberately not attached: nothing will ever sample this texture. */
}

static void w_del_fb(GLsizei n, const GLuint *ids)
{
    if (p_del_fb) p_del_fb(n, ids);
}

static GLenum w_check_fb(GLenum target)
{
    (void)target;
    return GL_FRAMEBUFFER_COMPLETE;
}

void *SDL_GL_GetProcAddress(const char *name)
{
    if (!real_gpa)
        real_gpa = (void *(*)(const char *))dlsym(RTLD_NEXT, "SDL_GL_GetProcAddress");

    void *p = real_gpa ? real_gpa(name) : NULL;

    if (strcmp(name, "glGenFramebuffersEXT") == 0) {
        p_gen_fb = (PFN_GEN_FB)p;
        return (void *)w_gen_fb;
    }
    if (strcmp(name, "glBindFramebufferEXT") == 0)
        return (void *)w_bind_fb;
    if (strcmp(name, "glFramebufferTexture2DEXT") == 0)
        return (void *)w_fb_tex;
    if (strcmp(name, "glDeleteFramebuffersEXT") == 0) {
        p_del_fb = (PFN_DEL_FB)p;
        return (void *)w_del_fb;
    }
    if (strcmp(name, "glCheckFramebufferStatusEXT") == 0)
        return (void *)w_check_fb;

    return p;
}

/* ---- drawing --------------------------------------------------------- */

void glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    static void (*real)(GLint, GLint, GLsizei, GLsizei) = NULL;
    if (!real) real = (void (*)(GLint, GLint, GLsizei, GLsizei))dlsym(RTLD_NEXT, "glViewport");
    if (!real) return;

    if (w == 1280 && h == 720) {
        /* 16:9 into 640x480 -> 640x360 with 60px bars top and bottom */
        remapped_vp++;
        real(0, 60, 640, 360);
        return;
    }
    real(x, y, w, h);
}

void glClear(GLbitfield mask)
{
    static void (*real)(GLbitfield) = NULL;
    if (!real) real = (void (*)(GLbitfield))dlsym(RTLD_NEXT, "glClear");

    /* The present-phase clear happens after the frame is already drawn into
     * the window, so honouring it would erase the picture. */
    if (!phase_canvas) {
        suppressed_clear++;
        return;
    }
    if (real) real(mask);
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    static void (*real)(GLenum, GLint, GLsizei) = NULL;
    static void (*p_enable)(GLenum) = NULL;
    if (!real) real = (void (*)(GLenum, GLint, GLsizei))dlsym(RTLD_NEXT, "glDrawArrays");
    if (!p_enable) p_enable = (void (*)(GLenum))dlsym(RTLD_DEFAULT, "glEnable");

    if (!phase_canvas) {
        /* the composite of the (now unused) canvas texture */
        suppressed_present++;
        return;
    }
    /* Chowdren never enables GL_TEXTURE_2D: it expects its shader to sample
     * the texture. gl4es does not run that shader, so without this the quads
     * are solid glColor white. */
    if (p_enable) p_enable(GL_TEXTURE_2D);
    if (real) real(mode, first, count);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *idx)
{
    static void (*real)(GLenum, GLsizei, GLenum, const void *) = NULL;
    static void (*p_enable)(GLenum) = NULL;
    if (!real) real = (void (*)(GLenum, GLsizei, GLenum, const void *))dlsym(RTLD_NEXT, "glDrawElements");
    if (!p_enable) p_enable = (void (*)(GLenum))dlsym(RTLD_DEFAULT, "glEnable");
    if (!phase_canvas) {
        suppressed_present++;
        return;
    }
    if (p_enable) p_enable(GL_TEXTURE_2D);
    if (real) real(mode, count, type, idx);
}

__attribute__((destructor))
static void fbooff_report(void)
{
    fprintf(stderr, "screenfix: remapped_vp=%ld suppressed_clear=%ld suppressed_present=%ld\n",
            remapped_vp, suppressed_clear, suppressed_present);
}

