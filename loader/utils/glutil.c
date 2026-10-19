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

#include "utils/utils.h"
#include "utils/dialog.h"
#include "utils/logger.h"

#include <stdio.h>
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
    vglInitExtended(0, 960, 544, 6 * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);
}

void gl_swap() {
    vglSwapBuffers(GL_FALSE);
}

// ---------------------------------------------------------------------------
// Shader handling for the GOF2 HD 2012 build
//
// vitaGL fixes a shader's language when it is created: GL_VERTEX_SHADER /
// GL_FRAGMENT_SHADER go through its GLSL translator, GL_CG_*_SHADER_EXT are
// compiled as Cg. We only know which one we want once we see the source
// (hand-written Cg in app0:shaders/<sha1>.cg if we have one, otherwise the
// game's own GLSL), so the game gets placeholder ids and the real vitaGL
// shader is created in glShaderSource.
// ---------------------------------------------------------------------------
#define MAX_SHADERS 2048

typedef struct {
    GLenum type;
    GLuint real;
    int is_cg;
    char name[41];
} proxy_shader;

static proxy_shader proxies[MAX_SHADERS];
static GLuint next_proxy = 1;

static void shader_log(const char *fmt, const char *a, const char *b) {
    FILE *f = fopen(DATA_PATH "shader_log.txt", "a");
    if (!f) return;
    fprintf(f, fmt, a, b);
    fclose(f);
}

static proxy_shader *proxy(GLuint id) {
    return (id > 0 && id < MAX_SHADERS) ? &proxies[id] : NULL;
}

GLuint glCreateShaderHook(GLenum type) {
    if (next_proxy >= MAX_SHADERS) {
        log_error("Out of shader slots");
        return 0;
    }
    GLuint id = next_proxy++;
    proxies[id].type = type;
    proxies[id].real = 0;
    proxies[id].is_cg = 0;
    proxies[id].name[0] = '\0';
    return id;
}

void glShaderSourceHook(GLuint shader, GLsizei count, const GLchar **string,
                        const GLint *_length) {
    proxy_shader *p = proxy(shader);
    if (!p || !string || !*string) return;

    int length = (_length && _length[0] >= 0) ? _length[0] : (int)strlen(string[0]);
    char *sha_name = get_string_sha1((uint8_t *)string[0], length);
    strncpy(p->name, sha_name, 40);
    p->name[40] = '\0';
    free(sha_name);

    if (p->real) {
        glDeleteShader(p->real);
        p->real = 0;
    }

    char cg_path[128];
    snprintf(cg_path, sizeof(cg_path), "%s/%s.cg", GXP_PATH, p->name);
    FILE *file = fopen(cg_path, "rb");

    if (file) {
        fseek(file, 0, SEEK_END);
        GLint size = (GLint)ftell(file);
        fseek(file, 0, SEEK_SET);
        GLchar *buf = malloc(size + 1);
        fread(buf, 1, size, file);
        buf[size] = '\0';
        fclose(file);

        p->is_cg = 1;
        p->real = glCreateShader(p->type == GL_VERTEX_SHADER ? GL_CG_VERTEX_SHADER_EXT
                                                             : GL_CG_FRAGMENT_SHADER_EXT);
        const GLchar *src = buf;
        glShaderSource(p->real, 1, &src, &size);
        free(buf);
    } else {
        // No hand-written Cg: let vitaGL translate the game's GLSL.
        p->is_cg = 0;
        p->real = glCreateShader(p->type);
        glShaderSource(p->real, count, string, _length);
    }
}

void glCompileShaderHook(GLuint shader) {
    proxy_shader *p = proxy(shader);
    if (!p || !p->real) return;

    glCompileShader(p->real);
    if (!p->is_cg) return; // GLSL is translated and compiled at link time

    GLint ok = GL_FALSE;
    glGetShaderiv(p->real, GL_COMPILE_STATUS, &ok);
    if (ok) return;

    char info[1024] = "";
    glGetShaderInfoLog(p->real, sizeof(info), NULL, info);
    shader_log("FAILED %s.cg\n%s\n\n", p->name, info);

    // Placeholder so linking does not crash; shows up bright pink.
    const GLchar *fallback = (p->type == GL_VERTEX_SHADER)
        ? "void main(float4 a_position, float4 out gl_Position : POSITION) { gl_Position = a_position; }"
        : "float4 main() : COLOR { return float4(1.0, 0.0, 1.0, 1.0); }";
    glShaderSource(p->real, 1, &fallback, NULL);
    glCompileShader(p->real);
}

void glAttachShaderHook(GLuint program, GLuint shader) {
    proxy_shader *p = proxy(shader);
    if (p && p->real) glAttachShader(program, p->real);
}

void glDeleteShaderHook(GLuint shader) {
    proxy_shader *p = proxy(shader);
    if (!p) return;
    if (p->real) glDeleteShader(p->real);
    p->real = 0;
}

void glGetShaderivHook(GLuint shader, GLenum pname, GLint *params) {
    proxy_shader *p = proxy(shader);
    if (!p || !params) return;
    if (pname == GL_SHADER_TYPE) { *params = p->type; return; }
    if (!p->real) {
        *params = (pname == GL_COMPILE_STATUS) ? GL_TRUE : 0;
        return;
    }
    glGetShaderiv(p->real, pname, params);
}

void glGetShaderInfoLogHook(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog) {
    proxy_shader *p = proxy(shader);
    if (p && p->real) {
        glGetShaderInfoLog(p->real, maxLength, length, infoLog);
    } else {
        if (length) *length = 0;
        if (infoLog && maxLength > 0) infoLog[0] = '\0';
    }
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
            *value = 960;
            break;
        case EGL_HEIGHT:
            *value = 544;
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
