#include <psp2/io/stat.h>
#include <stdint.h>
/*
 * utils/glutil.c
 *
 * OpenGL API initializer, related functions.
 *
 * Copyright (C) 2021 Andy Nguyen
 * Copyright (C) 2021 Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/glutil.h"
#include "utils/screen.h"

#include "utils/utils.h"
#include "utils/dialog.h"
#include "utils/logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <psp2/kernel/sysmem.h>

#define GLSL_PATH DATA_PATH
#define GXP_PATH "app0:shaders"

void gl_preload() {
    if (!file_exists("ur0:/data/libshacccg.suprx")
        && !file_exists("ur0:/data/external/libshacccg.suprx")) {
        fatal_error("Error: libshacccg.suprx is not installed. Google \"ShaRKBR33D\" for quick installation.");
    }
}

void gl_init() {
    GLboolean ok = vglInitExtended(0, DISPLAY_W, DISPLAY_H, 6 * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);
    logv_info("vglInitExtended -> %d; free KB: vram=%u ram=%u phycont=%u budget=%u external=%u",
              ok, vglMemFree(VGL_MEM_VRAM) / 1024, vglMemFree(VGL_MEM_RAM) / 1024,
              vglMemFree(VGL_MEM_PHYCONT) / 1024, vglMemFree(VGL_MEM_BUDGET) / 1024,
              vglMemFree(VGL_MEM_EXTERNAL) / 1024);
    // Translate+compile GLSL in glCompileShader so failures are reported
    // there; POSTPONED mode compiles inside glLinkProgram and crashes on failure.
    vglSetSemanticBindingMode(VGL_MODE_GLOBAL);
#ifdef GAME_OFFSCREEN
    gl_create_game_fb();
#endif
}

// The game believes the screen is GAME_W x GAME_H, so its interface is laid
// out with that much room, but it draws straight to the 960x544 display:
// every viewport and scissor rectangle it sets is scaled down to the display.
// 3D keeps the same 16:9 shape and renders at native resolution with MSAA;
// the 2D interface is scaled by its projection along with the viewport.
#define VX(v) ((v) * DISPLAY_W / GAME_W)
#define VY(v) ((v) * DISPLAY_H / GAME_H)
static int scale_rect(GLuint fb) { return fb == 0; }  // only the screen, not the game's own FBOs

void glScissorHook(GLint x, GLint y, GLsizei w, GLsizei h);

// The game draws at GAME_W x GAME_H into game_fb; every frame that is scaled
// down onto the 960x544 display. The game's binds of framebuffer 0 are sent to
// game_fb (glBindFramebufferHook).
GLuint game_fb = 0;
static GLuint game_tex = 0, game_depth = 0;

void gl_create_game_fb(void) {
    glGenTextures(1, &game_tex);
    glBindTexture(GL_TEXTURE_2D, game_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GAME_W, GAME_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenRenderbuffers(1, &game_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, game_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, GAME_W, GAME_H);

    glGenFramebuffers(1, &game_fb);
    glBindFramebuffer(GL_FRAMEBUFFER, game_fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, game_tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, game_depth);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    logv_info("game framebuffer %ux%u -> fb %u, status 0x%x", GAME_W, GAME_H, game_fb, st);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        // Fall back to drawing straight to the display.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        game_fb = 0;
    }
}

void gl_swap() {
    if (game_fb) {
        GLboolean sc = glIsEnabled(GL_SCISSOR_TEST);
        if (sc) glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, game_fb);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, GAME_W, GAME_H, 0, 0, DISPLAY_W, DISPLAY_H, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        vglSwapBuffers(GL_FALSE);
        glBindFramebuffer(GL_FRAMEBUFFER, game_fb);
        if (sc) glEnable(GL_SCISSOR_TEST);
    } else {
        vglSwapBuffers(GL_FALSE);
    }
}

// ---------------------------------------------------------------------------
// Shader handling for the GOF2 HD 2012 build
//
// vitaGL fixes a shader's language when it is created: GL_VERTEX_SHADER /
// GL_FRAGMENT_SHADER go through its GLSL translator (which translates a
// vertex+fragment pair together at link time), GL_CG_*_SHADER_EXT compile
// as Cg. Mixing one Cg and one GLSL shader in a program breaks the
// translator, so the language is chosen per *program* at glLinkProgram:
// hand-written Cg for both halves when we have it, otherwise the game's own
// GLSL for both. The game only ever sees placeholder shader ids.
// ---------------------------------------------------------------------------
#define MAX_SHADERS 2048
#define MAX_PROGRAMS 2048

typedef struct {
    GLenum type;
    char name[41];
    GLchar *glsl;     // game's source
    GLchar *cg;       // hand-written Cg, or NULL
    GLuint real_cg;   // compiled lazily
    GLuint real_glsl;
    int glsl_checked; // 0 = untested, 1 = compiles, -1 = fails
} proxy_shader;

static proxy_shader proxies[MAX_SHADERS];
static GLuint next_proxy = 1;
static GLuint prog_vs[MAX_PROGRAMS], prog_fs[MAX_PROGRAMS];

static void shader_log(const char *fmt, const char *a, const char *b) {
    FILE *f = fopen(DATA_PATH "shader_log.txt", "a");
    if (!f) return;
    fprintf(f, fmt, a, b);
    fclose(f);
}

static proxy_shader *proxy(GLuint id) {
    return (id > 0 && id < next_proxy && id < MAX_SHADERS) ? &proxies[id] : NULL;
}

static GLchar *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    GLchar *buf = malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

GLuint glCreateShaderHook(GLenum type) {
    if (next_proxy >= MAX_SHADERS) {
        log_error("Out of shader slots");
        return 0;
    }
    GLuint id = next_proxy++;
    memset(&proxies[id], 0, sizeof(proxy_shader));
    proxies[id].type = type;
    return id;
}

static void drop_real(proxy_shader *p) {
    if (p->real_cg) glDeleteShader(p->real_cg);
    if (p->real_glsl) glDeleteShader(p->real_glsl);
    p->real_cg = p->real_glsl = 0;
}

void glShaderSourceHook(GLuint shader, GLsizei count, const GLchar **string,
                        const GLint *_length) {
    proxy_shader *p = proxy(shader);
    if (!p || !string || !*string) return;

    drop_real(p);
    free(p->glsl); p->glsl = NULL;
    free(p->cg);   p->cg = NULL;

    // Keep the full GLSL source (all strings concatenated).
    size_t total = 0;
    for (int i = 0; i < count; i++)
        total += (_length && _length[i] >= 0) ? (size_t)_length[i] : strlen(string[i]);
    p->glsl = malloc(total + 1);
    size_t off = 0;
    for (int i = 0; i < count; i++) {
        size_t n = (_length && _length[i] >= 0) ? (size_t)_length[i] : strlen(string[i]);
        memcpy(p->glsl + off, string[i], n);
        off += n;
    }
    p->glsl[total] = '\0';

    // Cg files are named after the SHA1 of the first source string.
    int length = (_length && _length[0] >= 0) ? _length[0] : (int)strlen(string[0]);
    char *sha_name = get_string_sha1((uint8_t *)string[0], length);
    strncpy(p->name, sha_name, 40);
    p->name[40] = '\0';
    free(sha_name);

    char cg_path[128];
    snprintf(cg_path, sizeof(cg_path), "%s/%s.cg", GXP_PATH, p->name);
    p->cg = read_text(cg_path);

    // Rewritten GLSL for shaders vitaGL's translator can't take as-is
    // (e.g. varying arrays flattened into separate varyings).
    char glsl_path[128];
    snprintf(glsl_path, sizeof(glsl_path), "app0:glsl/%s.glsl", p->name);
    GLchar *fixed = read_text(glsl_path);
    if (fixed) {
        free(p->glsl);
        p->glsl = fixed;
    }
}

void glCompileShaderHook(GLuint shader) {
    // Compilation happens per program in glLinkProgramHook.
}

static GLuint realize(proxy_shader *p, int use_cg) {
    if (use_cg) {
        if (p->real_cg) return p->real_cg;
        GLuint r = glCreateShader(p->type == GL_VERTEX_SHADER ? GL_CG_VERTEX_SHADER_EXT
                                                              : GL_CG_FRAGMENT_SHADER_EXT);
        const GLchar *src = p->cg;
        glShaderSource(r, 1, &src, NULL);
        glCompileShader(r);
        GLint ok = GL_FALSE;
        glGetShaderiv(r, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char info[1024] = "";
            glGetShaderInfoLog(r, sizeof(info), NULL, info);
            shader_log("FAILED %s.cg\n%s\n\n", p->name, info);
            glDeleteShader(r);
            return 0;
        }
        return p->real_cg = r;
    } else {
        if (p->real_glsl) return p->real_glsl;
        GLuint r = glCreateShader(p->type);
        const GLchar *src = p->glsl;
        glShaderSource(r, 1, &src, NULL);
        glCompileShader(r);
        GLint ok = GL_FALSE;
        glGetShaderiv(r, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char info[1024] = "";
            glGetShaderInfoLog(r, sizeof(info), NULL, info);
            shader_log("GLSL FAILED %s\n%s\n", p->name, info);
            // Keep the failing source so a Cg port can be written for it.
            char path[256];
            sceIoMkdir(DATA_PATH "glsl_dump", 0777);
            snprintf(path, sizeof(path), DATA_PATH "glsl_dump/%s.%s", p->name,
                     p->type == GL_VERTEX_SHADER ? "vert" : "frag");
            FILE *d = fopen(path, "wb");
            if (d) { fwrite(p->glsl, 1, strlen(p->glsl), d); fclose(d); }
            glDeleteShader(r);
            return 0;
        }
        return p->real_glsl = r;
    }
}

static GLuint placeholder(GLenum type) {
    static GLuint pv = 0, pf = 0;
    GLuint *slot = (type == GL_VERTEX_SHADER) ? &pv : &pf;
    if (*slot) return *slot;
    const GLchar *src = (type == GL_VERTEX_SHADER)
        ? "uniform float4x4 u_ModelViewProjectionMatrix;\n"
          "void main(float4 a_position, float4 out gl_Position : POSITION) {\n"
          "  gl_Position = mul(a_position, u_ModelViewProjectionMatrix);\n}\n"
        : "float4 main() : COLOR { return float4(1.0, 0.0, 1.0, 1.0); }\n";
    *slot = glCreateShader(type == GL_VERTEX_SHADER ? GL_CG_VERTEX_SHADER_EXT : GL_CG_FRAGMENT_SHADER_EXT);
    glShaderSource(*slot, 1, &src, NULL);
    glCompileShader(*slot);
    return *slot;
}

void glAttachShaderHook(GLuint program, GLuint shader) {
    proxy_shader *p = proxy(shader);
    if (!p || program >= MAX_PROGRAMS) return;
    if (p->type == GL_VERTEX_SHADER) prog_vs[program] = shader;
    else prog_fs[program] = shader;
}

void glLinkProgramHook(GLuint program) {
    if (program >= MAX_PROGRAMS) return;
    proxy_shader *vs = proxy(prog_vs[program]);
    proxy_shader *fs = proxy(prog_fs[program]);
    if (!vs || !fs || !vs->glsl || !fs->glsl) {
        logv_error("glLinkProgram(%d): missing shader", program);
        return;
    }

    GLuint rv = 0, rf = 0;
    if (vs->cg && fs->cg) {
        rv = realize(vs, 1);
        rf = realize(fs, 1);
    }
    if (!rv || !rf) {
        rv = realize(vs, 0);
        rf = realize(fs, 0);
        if (rv && rf) {
            shader_log("GLSL pair %s + %s\n", vs->name, fs->name);
        } else {
            // Linking a pair vitaGL can't translate crashes inside vitaGL,
            // so link a placeholder instead (renders pink).
            shader_log("PLACEHOLDER for %s + %s\n", vs->name, fs->name);
            rv = placeholder(GL_VERTEX_SHADER);
            rf = placeholder(GL_FRAGMENT_SHADER);
        }
    }

    glAttachShader(program, rv);
    glAttachShader(program, rf);
    glLinkProgram(program);
}

void glDeleteShaderHook(GLuint shader) {
    // Real shaders may be shared by several programs; keep them.
}

void glGetShaderivHook(GLuint shader, GLenum pname, GLint *params) {
    proxy_shader *p = proxy(shader);
    if (!params) return;
    switch (pname) {
        case GL_SHADER_TYPE:     *params = p ? p->type : 0; break;
        case GL_COMPILE_STATUS:  *params = GL_TRUE; break;
        case GL_INFO_LOG_LENGTH: *params = 0; break;
        default:                 *params = 0; break;
    }
}

void glGetShaderInfoLogHook(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog) {
    if (length) *length = 0;
    if (infoLog && maxLength > 0) infoLog[0] = '\0';
}

// --------------------------------------------------------------- GL stats
static unsigned int st_draws, st_draws_fb0, st_clears, st_fbbinds;
static GLuint cur_fb = 0;
static GLint last_vp[4] = {-1, -1, -1, -1};
static int vp_logs = 0, fb_logs = 0;

void glViewportHook(GLint x, GLint y, GLsizei w, GLsizei h) {
    if ((x != last_vp[0] || y != last_vp[1] || w != last_vp[2] || h != last_vp[3]) && vp_logs < 40) {
        logv_info("glViewport(%d, %d, %d, %d) fb=%u", x, y, w, h, cur_fb);
        vp_logs++;
    }
    last_vp[0] = x; last_vp[1] = y; last_vp[2] = w; last_vp[3] = h;
    if (!game_fb && scale_rect(cur_fb)) {
        GLint x1 = VX(x + w), y1 = VY(y + h);
        x = VX(x); y = VY(y);
        w = x1 - x; h = y1 - y;
    }
    glViewport(x, y, w, h);
}

void glScissorHook(GLint x, GLint y, GLsizei w, GLsizei h) {
    if (!game_fb && scale_rect(cur_fb)) {
        GLint x1 = VX(x + w), y1 = VY(y + h);
        x = VX(x); y = VY(y);
        w = x1 - x; h = y1 - y;
    }
    glScissor(x, y, w, h);
}

void glDrawArraysHook(GLenum mode, GLint first, GLsizei count) {
    st_draws++; if (cur_fb == 0) st_draws_fb0++;
    glDrawArrays(mode, first, count);
}

void glDrawElementsHook(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    st_draws++; if (cur_fb == 0) st_draws_fb0++;
    glDrawElements(mode, count, type, indices);
}

void glBindFramebufferHook(GLenum target, GLuint fb) {
    st_fbbinds++;
    if (fb != cur_fb && fb_logs < 40) {
        logv_info("glBindFramebuffer(0x%x, %u)", target, fb);
        fb_logs++;
    }
    cur_fb = fb;
    // The game's "screen" is the off-screen game framebuffer.
    glBindFramebuffer(target, fb == 0 && game_fb ? game_fb : fb);
}

// Counts clears for the heartbeat stats.
void glClearHook(GLbitfield mask) {
    st_clears++;
    glClear(mask);
}

void gl_stats_reset(void) { st_draws = st_draws_fb0 = st_clears = st_fbbinds = 0; }

// Called once per heartbeat from main.c: per-frame averages since last call.
void gl_stats_report(void) {
    GLenum err = glGetError();
    GLint box[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_SCISSOR_BOX, box);
    logv_info("  gl: scissor=%d box=%d,%d %dx%d depthTest=%d blend=%d cull=%d",
              glIsEnabled(GL_SCISSOR_TEST), box[0], box[1], box[2], box[3],
              glIsEnabled(GL_DEPTH_TEST), glIsEnabled(GL_BLEND), glIsEnabled(GL_CULL_FACE));
    logv_info("  gl: draws=%u (to screen %u) clears=%u fbbinds=%u fb=%u viewport=%d,%d %dx%d err=0x%x",
              st_draws, st_draws_fb0, st_clears, st_fbbinds, cur_fb,
              last_vp[0], last_vp[1], last_vp[2], last_vp[3], err);
    st_draws = st_draws_fb0 = st_clears = st_fbbinds = 0;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
    logv_debug("eglInitialize(0x%x)", (int)dpy);

    gl_init();

    if (major) *major = 2;
    if (minor) *minor = 2;

    return EGL_TRUE;
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface eglSurface, EGLint attribute, EGLint *value)
{
    EGLBoolean ret = EGL_TRUE;
    switch (attribute) {
        case EGL_CONFIG_ID:
            ret = 1;
            break;
        case EGL_WIDTH:
            *value = GAME_W;
            break;
        case EGL_HEIGHT:
            *value = GAME_H;
            break;
        case EGL_TEXTURE_FORMAT:
            *value = 2; // NoTexture = 0, RGB = 1, RGBA = 2
            break;
        case EGL_TEXTURE_TARGET:
            *value = 1;
            break;
        case EGL_SWAP_BEHAVIOR:
            ret = EGL_TRUE;
            *value = EGL_BUFFER_PRESERVED;
            break;
        case EGL_LARGEST_PBUFFER:
        case EGL_MIPMAP_TEXTURE:
            *value = EGL_FALSE;
            break;
        case EGL_MIPMAP_LEVEL:
            *value = 0;
            break;
        case EGL_MULTISAMPLE_RESOLVE:
            // ignored when creating the surface, return default
            *value = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
            break;
        case EGL_HORIZONTAL_RESOLUTION:
        case EGL_VERTICAL_RESOLUTION:
            *value = 220 * EGL_DISPLAY_SCALING; // VITA DPI is 220
            break;
        case EGL_PIXEL_ASPECT_RATIO:
            // Please don't ask why * EGL_DISPLAY_SCALING, the document says it
            *value = 960 / 544 * EGL_DISPLAY_SCALING;
            break;
        case EGL_RENDER_BUFFER:
            *value = EGL_BACK_BUFFER;
            break;
        case EGL_VG_COLORSPACE:
            // ignored when creating the surface, return default
            *value = EGL_VG_COLORSPACE_sRGB;
            break;
        case EGL_VG_ALPHA_FORMAT:
            // ignored when creating the surface, return default
            *value = EGL_VG_ALPHA_FORMAT_NONPRE;
            break;
        case EGL_TIMESTAMPS_ANDROID:
            *value = EGL_FALSE;
            break;
        default:
            logv_error("eglQuerySurface %x  EGL_BAD_ATTRIBUTE", attribute);
            break;
    }

    return ret;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay display,
                              EGLConfig config,
                              EGLint attribute,
                              EGLint * value) {
    switch (attribute) {
        case EGL_ALPHA_SIZE: {
            *value = 8;
            break;
        }
        case EGL_ALPHA_MASK_SIZE: {
            *value = 8;
            break;
        }
        case EGL_BIND_TO_TEXTURE_RGB: {
            *value = EGL_TRUE;
            break;
        }
        case EGL_BIND_TO_TEXTURE_RGBA: {
            *value = EGL_TRUE;
            break;
        }
        case EGL_BLUE_SIZE: {
            *value = 8;
            break;
        }
        case EGL_BUFFER_SIZE: {
            *value = 32;
            break;
        }
        case EGL_COLOR_BUFFER_TYPE: {
            *value = EGL_RGB_BUFFER;
            break;
        }
        case EGL_CONFIG_CAVEAT: {
            *value = EGL_NONE;
            break;
        }
        case EGL_CONFIG_ID: {
            *value = 1;
            break;
        }
        case EGL_CONFORMANT: {
            *value = 0;
            break;
        }
        case EGL_DEPTH_SIZE: {
            *value = 0;
            break;
        }
        case EGL_GREEN_SIZE: {
            *value = 0;
            break;
        }
        case EGL_LEVEL: {
            *value = 0;
            break;
        }
        case EGL_LUMINANCE_SIZE: {
            *value = 0;
            break;
        }
        case EGL_MAX_PBUFFER_WIDTH: {
            *value = 0;
            break;
        }
        case EGL_MAX_PBUFFER_HEIGHT: {
            *value = 0;
            break;
        }
        case EGL_MAX_PBUFFER_PIXELS: {
            *value = 0;
            break;
        }
        case EGL_MAX_SWAP_INTERVAL: {
            *value = 0;
            break;
        }
        case EGL_MIN_SWAP_INTERVAL: {
            *value = 0;
            break;
        }
        case EGL_NATIVE_RENDERABLE: {
            *value = 0;
            break;
        }
        case EGL_NATIVE_VISUAL_ID: {
            *value = 0;
            break;
        }
        case EGL_NATIVE_VISUAL_TYPE: {
            *value = 0;
            break;
        }
        case EGL_RED_SIZE: {
            *value = 0;
            break;
        }
        case EGL_RENDERABLE_TYPE: {
            *value = 0;
            break;
        }
        case EGL_SAMPLE_BUFFERS: {
            *value = 0;
            break;
        }
        case EGL_SAMPLES: {
            *value = 0;
            break;
        }
        case EGL_STENCIL_SIZE: {
            *value = 0;
            break;
        }
        case EGL_SURFACE_TYPE: {
            *value = 0;
            break;
        }
        case EGL_TRANSPARENT_TYPE: {
            *value = 0;
            break;
        }
        case EGL_TRANSPARENT_RED_VALUE: {
            *value = 0;
            break;
        }
        case EGL_TRANSPARENT_GREEN_VALUE: {
            *value = 0;
            break;
        }
        case EGL_TRANSPARENT_BLUE_VALUE: {
            *value = 0;
            break;
        }
        default:
            return EGL_FALSE;
    }
    return EGL_TRUE;
}

EGLBoolean eglChooseConfig (EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config) {
    return EGL_TRUE;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list) {
    return strdup("ctx");
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, void * win, const EGLint *attrib_list) {
    return strdup("surface");
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    return EGL_TRUE;
}

EGLBoolean eglDestroyContext (EGLDisplay dpy, EGLContext ctx) {
    if (ctx) free(ctx);
    return EGL_TRUE;
}

EGLBoolean eglDestroySurface (EGLDisplay dpy, EGLSurface surface) {
    if (surface) free(surface);
    return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy) {
    return EGL_TRUE;
}

// Controller cursor for the menus: a white square with a dark outline, drawn
// with scissored clears after the game's frame (no shader or texture state).
static void fill_rect(int x, int y, int w, int h, float r, float g, float b) {
    glScissorHook(x, GAME_H - y - h, w, h);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void gl_draw_cursor(int x, int y, int pressed) {
    GLint box[4];
    GLfloat cc[4];
    GLboolean cm[4];
    GLboolean sc = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_SCISSOR_BOX, box);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);
    glGetBooleanv(GL_COLOR_WRITEMASK, cm);
    GLint fb = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    if ((GLuint)fb != game_fb) glBindFramebuffer(GL_FRAMEBUFFER, game_fb);  // 0 = display
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_SCISSOR_TEST);

    // Sized for the game framebuffer (GAME_W / 960 bigger than display pixels).
    int o = 7 * GAME_W / 960, in = 5 * GAME_W / 960;
    fill_rect(x - o, y - o, 2 * o, 2 * o, 0.0f, 0.0f, 0.0f);
    if (pressed) fill_rect(x - in, y - in, 2 * in, 2 * in, 1.0f, 0.75f, 0.1f);
    else         fill_rect(x - in, y - in, 2 * in, 2 * in, 1.0f, 1.0f, 1.0f);

    glScissor(box[0], box[1], box[2], box[3]);  // restore the raw (already scaled) box
    if (!sc) glDisable(GL_SCISSOR_TEST);
    glClearColor(cc[0], cc[1], cc[2], cc[3]);
    glColorMask(cm[0], cm[1], cm[2], cm[3]);
    if ((GLuint)fb != game_fb) glBindFramebuffer(GL_FRAMEBUFFER, fb);
}

// ---------------------------------------------------------------------------
// Uniform arrays. The game looks up array elements by name ("u_AmbientColor[1]",
// "s_texture[0]"); vitaGL can't resolve an element inside a Cg program, so the
// Cg ports declare each element separately as name_N. Lookups that fail are
// retried with that spelling, and a whole-array lookup ("u_AmbientColor") maps
// to element 0, with multi-element uploads split across the elements.

#define MAX_SPLIT_UNIFORMS 256
static struct { GLint base; GLint elem[8]; int n; } split_uniforms[MAX_SPLIT_UNIFORMS];
static int split_count = 0;

static int split_find(GLint loc) {
    for (int i = 0; i < split_count; i++)
        if (split_uniforms[i].base == loc) return i;
    return -1;
}

GLint glGetUniformLocationHook(GLuint prog, const GLchar *name) {
    GLint r = glGetUniformLocation(prog, name);
    if (r != -1) return r;

    char alt[96];
    const char *br = strchr(name, '[');
    if (br) {
        // "name[N]" -> "name_N"
        int len = br - name;
        if (len > 80) return -1;
        memcpy(alt, name, len);
        snprintf(alt + len, sizeof(alt) - len, "_%d", atoi(br + 1));
        return glGetUniformLocation(prog, alt);
    }

    // "name" -> "name_0", remembering the other elements for array uploads
    snprintf(alt, sizeof(alt), "%s_0", name);
    r = glGetUniformLocation(prog, alt);
    if (r == -1) return -1;
    if (split_find(r) < 0 && split_count < MAX_SPLIT_UNIFORMS) {
        int s = split_count++;
        split_uniforms[s].base = r;
        split_uniforms[s].n = 0;
        for (int i = 1; i < 8; i++) {
            snprintf(alt, sizeof(alt), "%s_%d", name, i);
            GLint e = glGetUniformLocation(prog, alt);
            if (e == -1) break;
            split_uniforms[s].elem[split_uniforms[s].n++] = e;
        }
    }
    return r;
}

#define SPLIT_UPLOAD(fn, T, comps) \
void fn##Hook(GLint loc, GLsizei count, const T *v) { \
    int s = (count > 1 && loc != -1) ? split_find(loc) : -1; \
    if (s < 0) { fn(loc, count, v); return; } \
    fn(loc, 1, v); \
    for (int i = 1; i < count && i - 1 < split_uniforms[s].n; i++) \
        fn(split_uniforms[s].elem[i - 1], 1, v + i * comps); \
}
SPLIT_UPLOAD(glUniform3fv, GLfloat, 3)
SPLIT_UPLOAD(glUniform4fv, GLfloat, 4)

// vita-elf-create needs a few KB free at the end of the text segment for SCE
// data; when the code ends too close to the boundary the link fails with
// "segment 1 overlaps". This pad shifts the boundary.
__attribute__((used)) static const unsigned char segment_pad[12288] = { 1 };
