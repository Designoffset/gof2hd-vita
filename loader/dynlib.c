/*
 * dynlib.c
 *
 * Resolving dynamic imports of the .so.
 *
 * Copyright (C) 2021 Andy Nguyen
 * Copyright (C) 2021 Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

// Disable IDE complaints about _identifiers and global interfaces
#pragma ide diagnostic ignored "bugprone-reserved-identifier"
#pragma ide diagnostic ignored "cppcoreguidelines-interfaces-global-init"
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#include "dynlib.h"

#include <OpenSLES.h>
#include <math.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/unistd.h>

#include <so_util/so_util.h>
#include <AFakeNative/AFakeNative.h>

#include "utils/utils.h"
#include "utils/glutil.h"
#include "utils/settings.h"
#include "reimpl/io.h"
#include "reimpl/log.h"
#include "reimpl/mem.h"
#include "reimpl/pthr.h"

extern void *__aeabi_atexit;
extern void *__cxa_pure_virtual;
extern void *__stack_chk_fail;
extern void *__stack_chk_guard;

extern void * __aeabi_d2f;
extern void * __aeabi_d2iz;
extern void * __aeabi_dadd;
extern void * __aeabi_dcmpeq;
extern void * __aeabi_dcmpgt;
extern void * __aeabi_dcmplt;
extern void * __aeabi_ddiv;
extern void * __aeabi_dmul;
extern void * __aeabi_dsub;
extern void * __aeabi_f2d;
extern void * __aeabi_f2iz;
extern void * __aeabi_f2lz;
extern void * __aeabi_f2ulz;
extern void * __aeabi_fadd;
extern void * __aeabi_fcmpeq;
extern void * __aeabi_fcmpge;
extern void * __aeabi_fcmpgt;
extern void * __aeabi_fcmple;
extern void * __aeabi_fcmplt;
extern void * __aeabi_fdiv;
extern void * __aeabi_fmul;
extern void * __aeabi_fsub;
extern void * __aeabi_i2d;
extern void * __aeabi_i2f;
extern void * __aeabi_idiv;
extern void * __aeabi_idivmod;
extern void * __aeabi_l2f;
extern void * __aeabi_ldivmod;
extern void * __aeabi_lmul;
extern void * __aeabi_ui2d;
extern void * __aeabi_ui2f;
extern void * __aeabi_uidiv;
extern void * __aeabi_uidivmod;
extern void * __aeabi_ul2f;
extern void * __aeabi_uldivmod;
extern void * __dso_handle;
extern void * __swbuf;
extern void * _ZdaPv;
extern void * _ZdlPv;
extern void * _Znaj;
extern void * _Znwj;
void *mw_malloc(size_t); void *mw_calloc(size_t, size_t); void *mw_realloc(void *, size_t);
void *mw_memalign(size_t, size_t); void *mw_new(size_t); void *mw_new_array(size_t);

extern const char *BIONIC_ctype_;
extern const short *BIONIC_tolower_tab_;
extern const short *BIONIC_toupper_tab_;

static FILE __sF_fake[3];

bool first_swap = true;

uint32_t last_render_time;
uint32_t delta;

EGLBoolean eglSwapBuffers_wrapper(EGLDisplay display, EGLSurface surface) {
    if (setting_fpsLock == 0) return eglSwapBuffers(display, surface);

    if (first_swap) {
        last_render_time = sceKernelGetProcessTimeLow();
        delta = (1000000 / (setting_fpsLock+1));
        first_swap = false;
    }

    while (sceKernelGetProcessTimeLow() - last_render_time < delta) {
        sched_yield();
    }

    last_render_time = sceKernelGetProcessTimeLow();
    return eglSwapBuffers(display, surface);
}


// ---- Additions for the GOF2 HD 2012 build (three .so modules + FMOD) ----
#include <pthread.h>
#include <ctype.h>
#include "utils/logger.h"
#include "utils/dialog.h"
#include <SLES/OpenSLES_Android.h>

static int islower_soloader(int c) { return c >= 'a' && c <= 'z'; }
static int isxdigit_soloader(int c) { return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'f'); }

extern so_module so_mod, so_fmodex, so_fmodevent;
volatile unsigned long long io_bytes_read = 0;
volatile unsigned int io_reads = 0, io_seeks = 0;
static int fseek_counted(FILE *f, long o, int w) { io_seeks++; return fseek(f, o, w); }
static size_t fread_counted(void *ptr, size_t size, size_t n, FILE *f) {
    io_reads++;
    size_t r = fread(ptr, size, n, f);
    io_bytes_read += (unsigned long long)r * size;
    return r;
}

extern so_default_dynlib default_dynlib[];
extern const int default_dynlib_count;

void __assert2_soloader(const char *file, int line, const char *func, const char *expr) {
    fatal_error("Assertion failed: %s (%s:%d %s)", expr, file, line, func);
}

static int ret_minus1() { return -1; }
static void *ret_null() { return NULL; }

#include <psp2/audioout.h>

extern JNIEnv jni;
static int (*fmodGetInfo)(JNIEnv *, void *, int) = NULL;
static int (*fmodProcess)(JNIEnv *, void *, void *) = NULL;
static struct JNINativeInterface fmod_env_table;
static JNIEnv fmod_env;

// fmodProcess asks the "Java" side for the ByteBuffer's address; the buffer
// we pass already is the address.
static void *fmod_GetDirectBufferAddress(JNIEnv *env, jobject buf) { return (void *)buf; }
static jlong fmod_GetDirectBufferCapacity(JNIEnv *env, jobject buf) { return 1 << 20; }

static int fmod_audio_thread(SceSize args, void *argp) {
    int rate = -1, len = -1, num = -1;
    for (int i = 0; i < 500 && (rate <= 0 || len <= 0); i++) {
        rate = fmodGetInfo(&fmod_env, NULL, 0);
        len = fmodGetInfo(&fmod_env, NULL, 1);
        num = fmodGetInfo(&fmod_env, NULL, 2);
        if (rate <= 0 || len <= 0) sceKernelDelayThread(10000);
    }
    logv_info("FMOD audio: rate %d, block %d samples, %d blocks", rate, len, num);
    if (rate <= 0 || len <= 0) return 0;

    // sceAudioOut wants a grain that is a multiple of 64 samples.
    int grain = (len + 63) & ~63;
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, grain, rate, SCE_AUDIO_OUT_MODE_STEREO);
    logv_info("FMOD audio: sceAudioOutOpenPort(grain %d, %d Hz) -> 0x%x", grain, rate, port);
    if (port < 0) return 0;
    int vol[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
    sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);

    int16_t *block = calloc(len * 2, sizeof(int16_t));
    int16_t *out = calloc(grain * 2, sizeof(int16_t));
    int fill = 0, failures = 0;
    for (;;) {
        int r = fmodProcess(&fmod_env, NULL, (void *)block);
        if (r != 0) {
            if (failures++ < 5) logv_info("FMOD audio: fmodProcess -> %d", r);
            memset(block, 0, len * 4);
        }
        // feed the FMOD block into grain-sized output buffers
        int pos = 0;
        while (pos < len) {
            int n = len - pos < grain - fill ? len - pos : grain - fill;
            memcpy(out + fill * 2, block + pos * 2, n * 4);
            fill += n;
            pos += n;
            if (fill == grain) {
                sceAudioOutOutput(port, out);
                fill = 0;
            }
        }
    }
    return 0;
}

static void fmod_start_audio(void) {
    static int started = 0;
    if (started) return;
    started = 1;
    fmodGetInfo = (void *)so_symbol(&so_fmodex, "Java_org_fmod_FMODAudioDevice_fmodGetInfo");
    fmodProcess = (void *)so_symbol(&so_fmodex, "Java_org_fmod_FMODAudioDevice_fmodProcess");
    if (!fmodGetInfo || !fmodProcess) return;
    memcpy(&fmod_env_table, (void *)jni, sizeof(fmod_env_table));
    fmod_env_table.GetDirectBufferAddress = fmod_GetDirectBufferAddress;
    fmod_env_table.GetDirectBufferCapacity = fmod_GetDirectBufferCapacity;
    fmod_env = &fmod_env_table;
    SceUID t = sceKernelCreateThread("FMOD audio", fmod_audio_thread, 0x10000100 - 10, 0x10000, 0, 0, NULL);
    if (t >= 0) sceKernelStartThread(t, 0, NULL);
}

// FMOD event system: log what happens and load the banks from the unpacked
// audio folder (see extract_audio in init.c).
int FMOD_EventSystem_init_hook(void *es, int maxchannels, unsigned int flags, void *extra, unsigned int eflags) {
    int (*real)(void *, int, unsigned int, void *, unsigned int) =
        (void *)so_symbol(&so_fmodevent, "_ZN4FMOD11EventSystem4initEijPvj");
    int r = real(es, maxchannels, flags, extra, eflags);
    logv_info("FMOD EventSystem::init(%d, 0x%x, 0x%x) -> %d", maxchannels, flags, eflags, r);
    if (r == 0) fmod_start_audio();
    return r;
}

int FMOD_EventSystem_load_hook(void *es, const char *name, void *info, void **project) {
    int (*real)(void *, const char *, void *, void **) =
        (void *)so_symbol(&so_fmodevent, "_ZN4FMOD11EventSystem4loadEPKcP19FMOD_EVENT_LOADINFOPPNS_12EventProjectE");
    int (*setMediaPath)(void *, const char *) =
        (void *)so_symbol(&so_fmodevent, "_ZN4FMOD11EventSystem12setMediaPathEPKc");
    char path[512];
    const char *base = name ? strrchr(name, '/') : NULL;
    base = base ? base + 1 : name;
    if (base && strstr(base, ".fev")) {
        if (setMediaPath) setMediaPath(es, DATA_PATH "audio/");
        // with the media path set, FMOD prefixes it to the name itself
        snprintf(path, sizeof(path), "%s", base);
    } else {
        snprintf(path, sizeof(path), "%s", name ? name : "");
    }
    int r = real(es, path, info, project);
    logv_info("FMOD EventSystem::load(%s -> %s) -> %d", name ? name : "(null)", path, r);
    return r;
}


// Music and voice are MP3 streams; log how FMOD gets on opening them.
int FMOD_System_createStream_hook(void *sys, const char *name, unsigned int mode, void *exinfo, void **sound) {
    int (*real)(void *, const char *, unsigned int, void *, void **) =
        (void *)so_symbol(&so_fmodex, "_ZN4FMOD6System12createStreamEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE");
    int r = real(sys, name, mode, exinfo, sound);
    static int n = 0;
    if (r != 0 || n < 20) { n++; logv_info("FMOD createStream(%s, 0x%x) -> %d", name ? name : "(null)", mode, r); }
    return r;
}

int FMOD_SystemI_createSoundInternal_hook(void *sys, const char *name, unsigned int mode, unsigned int bufsize,
                                          unsigned int buftype, void *exinfo, void **file, int async, void **sound) {
    int (*real)(void *, const char *, unsigned int, unsigned int, unsigned int, void *, void **, int, void **) =
        (void *)so_symbol(&so_fmodex, "_ZN4FMOD7SystemI19createSoundInternalEPKcjjjP22FMOD_CREATESOUNDEXINFOPPNS_4FileEbPPNS_6SoundIE");
    extern volatile uint32_t fmod_internal_site;
    fmod_internal_site = 0;
    int r = real(sys, name, mode, bufsize, buftype, exinfo, file, async, sound);
    static int n = 0;
    if (r != 0 || n < 40) { n++; logv_info("FMOD createSoundInternal(%s, 0x%x, async %d) -> %d (site %u)", (name && (mode & 0x800) == 0) ? name : "(mem)", mode, async, r, (unsigned)fmod_internal_site); }
    return r;
}

int FMOD_Sound_getOpenState_hook(void *snd, int *state, unsigned int *pct, int *starving, int *busy) {
    int (*real)(void *, int *, unsigned int *, int *, int *) =
        (void *)so_symbol(&so_fmodex, "_ZN4FMOD5Sound12getOpenStateEP14FMOD_OPENSTATEPjPbS4_");
    int r = real(snd, state, pct, starving, busy);
    static int n = 0;
    if ((r != 0 || (state && *state == 1)) && n < 40) { n++; logv_info("FMOD getOpenState(%p) -> %d state %d", snd, r, state ? *state : -1); }
    return r;
}

// FMOD's OpenSL ES output fails against the Vita's OpenSL library (and then
// deadlocks tearing it down). Use FMOD's "Audio Track" output instead: on
// Android a Java thread pulls mixed blocks out of it with fmodProcess() and
// writes them to an AudioTrack. fmod_audio_thread below plays that role and
// sends the blocks to sceAudioOut.
typedef int (*fmod_fn)();
int FMOD_EventSystem_Create_hook(void **eventsystem) {
    int (*create)(void **) = (void *)so_symbol(&so_fmodevent, "FMOD_EventSystem_Create");
    int (*getSystem)(void *, void **) = (void *)so_symbol(&so_fmodevent, "FMOD_EventSystem_GetSystemObject");
    int (*getNum)(void *, int, int *) = (void *)so_symbol(&so_fmodex, "FMOD_System_GetNumPlugins");
    int (*getHandle)(void *, int, int, unsigned int *) = (void *)so_symbol(&so_fmodex, "FMOD_System_GetPluginHandle");
    int (*getInfo)(void *, unsigned int, int *, char *, int, unsigned int *) = (void *)so_symbol(&so_fmodex, "FMOD_System_GetPluginInfo");
    int (*setByPlugin)(void *, unsigned int) = (void *)so_symbol(&so_fmodex, "FMOD_System_SetOutputByPlugin");
    int (*getOutput)(void *, int *) = (void *)so_symbol(&so_fmodex, "FMOD_System_GetOutput");

    int r = create(eventsystem);
    logv_info("FMOD_EventSystem_Create -> %d", r);
    if (r != 0 || !*eventsystem) return r;

    void *sys = NULL;
    getSystem(*eventsystem, &sys);
    int (*getVersion)(void *, unsigned int *) = (void *)so_symbol(&so_fmodex, "FMOD_System_GetVersion");
    unsigned int ver = 0;
    if (getVersion) getVersion(sys, &ver);
    logv_info("FMOD version %x", ver);
    int cur = -1;
    if (getOutput) getOutput(sys, &cur);
    int n = 0;
    getNum(sys, 0 /* FMOD_PLUGINTYPE_OUTPUT */, &n);
    logv_info("FMOD: default output type %d, %d output plugins", cur, n);
    unsigned int chosen = 0;
    for (int i = 0; i < n; i++) {
        unsigned int h = 0, ver = 0;
        int type = 0;
        char name[128] = "";
        getHandle(sys, 0, i, &h);
        getInfo(sys, h, &type, name, sizeof(name), &ver);
        logv_info("FMOD: output plugin %d: %s (handle %u)", i, name, h);
        if (strstr(name, "Audio Track")) chosen = h;
    }
    if (chosen) {
        int rr = setByPlugin(sys, chosen);
        if (getOutput) getOutput(sys, &cur);
        logv_info("FMOD: switched to Audio Track output -> %d (output type now %d)", rr, cur);
    }
    return r;
}

SLresult slCreateEngine_logged(SLObjectItf *pEngine, SLuint32 numOptions, const SLEngineOption *pEngineOptions,
                               SLuint32 numInterfaces, const SLInterfaceID *pInterfaceIds, const SLboolean *pInterfaceRequired) {
    SLresult r = slCreateEngine(pEngine, numOptions, pEngineOptions, numInterfaces, pInterfaceIds, pInterfaceRequired);
    logv_info("slCreateEngine(%u options, %u interfaces) -> %u", (unsigned)numOptions, (unsigned)numInterfaces, (unsigned)r);
    return r;
}

// FMOD dlopen()s libOpenSLES.so and dlsym()s slCreateEngine / SL_IID_*.
void *dlopen_soloader(const char *filename, int flag) {
    logv_info("dlopen(%s)", filename ? filename : "(null)");
    return (void *)0x1;
}
int dlclose_soloader(void *handle) { return 0; }
const char *dlerror_soloader(void) { return NULL; }
void *dlsym_soloader(void *handle, const char *symbol) {
    for (int i = 0; i < default_dynlib_count; i++) {
        if (strcmp(symbol, default_dynlib[i].symbol) == 0) {
            logv_info("dlsym(%s) -> loader", symbol);
            return (void *)default_dynlib[i].func;
        }
    }
    uintptr_t s;
    if ((s = so_symbol(&so_fmodex, symbol))) return (void *)s;
    if ((s = so_symbol(&so_fmodevent, symbol))) return (void *)s;
    if ((s = so_symbol(&so_mod, symbol))) return (void *)s;
    logv_error("dlsym(%s): not found", symbol);
    return NULL;
}

// Bionic's pthread_once_t is a plain int (PTHREAD_ONCE_INIT == 0).
static pthread_mutex_t once_lock = PTHREAD_MUTEX_INITIALIZER;
int pthread_once_soloader(volatile int *once, void (*fn)(void)) {
    if (*once) return 0;
    pthread_mutex_lock(&once_lock);
    if (!*once) {
        *once = 1;
        pthread_mutex_unlock(&once_lock);
        fn();
        return 0;
    }
    pthread_mutex_unlock(&once_lock);
    return 0;
}
// Bionic's pthread_key_t is an int; newlib's is a pointer. Both are 4 bytes.
int pthread_key_create_soloader(int *key, void (*dtor)(void *)) {
    return pthread_key_create((pthread_key_t *)key, dtor);
}
void *pthread_getspecific_soloader(int key) { return pthread_getspecific((pthread_key_t)key); }
int pthread_setspecific_soloader(int key, const void *v) { return pthread_setspecific((pthread_key_t)key, v); }

// Only used for C++ exception unwinding through the Android code.
void *__gnu_Unwind_Find_exidx_soloader(void *pc, int *count) { *count = 0; return NULL; }

int __android_log_write_soloader(int prio, const char *tag, const char *text) {
    logv_debug("[%s] %s", tag ? tag : "", text ? text : "");
    return 0;
}

extern void __aeabi_memclr(void *, size_t);
extern void __aeabi_memclr4(void *, size_t);
extern void __aeabi_memclr8(void *, size_t);
extern void __aeabi_memcpy(void *, const void *, size_t);
extern void __aeabi_memcpy4(void *, const void *, size_t);
extern void __aeabi_memcpy8(void *, const void *, size_t);
extern void __aeabi_memmove(void *, const void *, size_t);
extern void __aeabi_memmove4(void *, const void *, size_t);
extern void __aeabi_memset4(void *, size_t, int);
extern int __cxa_atexit(void (*)(void *), void *, void *);
extern void __cxa_finalize(void *);
extern void *__cxa_begin_cleanup;
extern void *__cxa_call_unexpected;
extern void *__cxa_type_match;
extern void *memmem(const void *, size_t, const void *, size_t);
extern void *memalign(size_t, size_t);
extern int utime(const char *, const void *);

so_default_dynlib default_dynlib[] = {
        { "AConfiguration_delete", (uintptr_t)&AConfiguration_delete },
        { "AConfiguration_fromAssetManager", (uintptr_t)&AConfiguration_fromAssetManager },
        { "AConfiguration_getOrientation", (uintptr_t)&AConfiguration_getOrientation },
        { "AConfiguration_new", (uintptr_t)&AConfiguration_new },
        { "AInputEvent_getSource", (uintptr_t)&AInputEvent_getSource },
        { "AInputQueue_attachLooper", (uintptr_t)&AInputQueue_attachLooper },
        { "AInputQueue_detachLooper", (uintptr_t)&AInputQueue_detachLooper },
        { "AInputQueue_finishEvent", (uintptr_t)&AInputQueue_finishEvent },
        { "AInputQueue_getEvent", (uintptr_t)&AInputQueue_getEvent },
        { "AInputQueue_preDispatchEvent", (uintptr_t)&AInputQueue_preDispatchEvent },
        { "AKeyEvent_getAction", (uintptr_t)&AKeyEvent_getAction },
        { "AKeyEvent_getKeyCode", (uintptr_t)&AKeyEvent_getKeyCode },
        { "ALooper_pollAll", (uintptr_t)&ALooper_pollAll },
        { "ALooper_prepare", (uintptr_t)&ALooper_prepare },
        { "AMotionEvent_getAction", (uintptr_t)&AMotionEvent_getAction },
        { "AMotionEvent_getPointerCount", (uintptr_t)&AMotionEvent_getPointerCount },
        { "AMotionEvent_getPointerId", (uintptr_t)&AMotionEvent_getPointerId },
        { "AMotionEvent_getX", (uintptr_t)&AMotionEvent_getX },
        { "AMotionEvent_getY", (uintptr_t)&AMotionEvent_getY },
        { "ANativeWindow_getFormat", (uintptr_t)&ANativeWindow_getFormat },
        { "ANativeWindow_getHeight", (uintptr_t)&ANativeWindow_getHeight },
        { "ANativeWindow_getWidth", (uintptr_t)&ANativeWindow_getWidth },
        { "ANativeWindow_setBuffersGeometry", (uintptr_t)&ANativeWindow_setBuffersGeometry },
        { "ASensorEventQueue_disableSensor", (uintptr_t)&ASensorEventQueue_disableSensor },
        { "ASensorEventQueue_enableSensor", (uintptr_t)&ASensorEventQueue_enableSensor },
        { "ASensorEventQueue_getEvents", (uintptr_t)&ASensorEventQueue_getEvents },
        { "ASensorEventQueue_setEventRate", (uintptr_t)&ASensorEventQueue_setEventRate },
        { "ASensorManager_createEventQueue", (uintptr_t)&ASensorManager_createEventQueue },
        { "ASensorManager_getDefaultSensor", (uintptr_t)&ASensorManager_getDefaultSensor },
        { "ASensorManager_getInstance", (uintptr_t)&ASensorManager_getInstance },
        { "SL_IID_BUFFERQUEUE", (uintptr_t)&SL_IID_BUFFERQUEUE },
        { "SL_IID_EFFECTSEND", (uintptr_t)&SL_IID_EFFECTSEND },
        { "SL_IID_ENGINE", (uintptr_t)&SL_IID_ENGINE },
        { "SL_IID_ENVIRONMENTALREVERB", (uintptr_t)&SL_IID_ENVIRONMENTALREVERB },
        { "SL_IID_PLAY", (uintptr_t)&SL_IID_PLAY },
        { "SL_IID_VOLUME", (uintptr_t)&SL_IID_VOLUME },
        { "_ZdaPv", (uintptr_t)&_ZdaPv },
        { "_ZdlPv", (uintptr_t)&_ZdlPv },
        { "_Znaj", (uintptr_t)&mw_new_array },
        { "_Znwj", (uintptr_t)&mw_new },
        { "__aeabi_atexit", (uintptr_t)&__aeabi_atexit },
        { "__aeabi_d2f", (uintptr_t)&__aeabi_d2f },
        { "__aeabi_d2iz", (uintptr_t)&__aeabi_d2iz },
        { "__aeabi_dadd", (uintptr_t)&__aeabi_dadd },
        { "__aeabi_dcmpeq", (uintptr_t)&__aeabi_dcmpeq },
        { "__aeabi_dcmpgt", (uintptr_t)&__aeabi_dcmpgt },
        { "__aeabi_dcmplt", (uintptr_t)&__aeabi_dcmplt },
        { "__aeabi_ddiv", (uintptr_t)&__aeabi_ddiv },
        { "__aeabi_dmul", (uintptr_t)&__aeabi_dmul },
        { "__aeabi_dsub", (uintptr_t)&__aeabi_dsub },
        { "__aeabi_f2d", (uintptr_t)&__aeabi_f2d },
        { "__aeabi_f2iz", (uintptr_t)&__aeabi_f2iz },
        { "__aeabi_f2lz", (uintptr_t)&__aeabi_f2lz },
        { "__aeabi_f2ulz", (uintptr_t)&__aeabi_f2ulz },
        { "__aeabi_fadd", (uintptr_t)&__aeabi_fadd },
        { "__aeabi_fcmpeq", (uintptr_t)&__aeabi_fcmpeq },
        { "__aeabi_fcmpge", (uintptr_t)&__aeabi_fcmpge },
        { "__aeabi_fcmpgt", (uintptr_t)&__aeabi_fcmpgt },
        { "__aeabi_fcmple", (uintptr_t)&__aeabi_fcmple },
        { "__aeabi_fcmplt", (uintptr_t)&__aeabi_fcmplt },
        { "__aeabi_fdiv", (uintptr_t)&__aeabi_fdiv },
        { "__aeabi_fmul", (uintptr_t)&__aeabi_fmul },
        { "__aeabi_fsub", (uintptr_t)&__aeabi_fsub },
        { "__aeabi_i2d", (uintptr_t)&__aeabi_i2d },
        { "__aeabi_i2f", (uintptr_t)&__aeabi_i2f },
        { "__aeabi_idiv", (uintptr_t)&__aeabi_idiv },
        { "__aeabi_idivmod", (uintptr_t)&__aeabi_idivmod },
        { "__aeabi_l2f", (uintptr_t)&__aeabi_l2f },
        { "__aeabi_ldivmod", (uintptr_t)&__aeabi_ldivmod },
        { "__aeabi_lmul", (uintptr_t)&__aeabi_lmul },
        { "__aeabi_ui2d", (uintptr_t)&__aeabi_ui2d },
        { "__aeabi_ui2f", (uintptr_t)&__aeabi_ui2f },
        { "__aeabi_uidiv", (uintptr_t)&__aeabi_uidiv },
        { "__aeabi_uidivmod", (uintptr_t)&__aeabi_uidivmod },
        { "__aeabi_ul2f", (uintptr_t)&__aeabi_ul2f },
        { "__aeabi_uldivmod", (uintptr_t)&__aeabi_uldivmod },
        { "__android_log_print", (uintptr_t)&android_log_print },
        { "__cxa_pure_virtual", (uintptr_t)&__cxa_pure_virtual },
        { "__dso_handle", (uintptr_t)&__dso_handle },
        { "__errno", (uintptr_t)&__errno },
        { "__sF", (uintptr_t)&__sF_fake },
        { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail },
        { "__stack_chk_guard", (uintptr_t)&__stack_chk_guard },
        { "__swbuf", (uintptr_t)&__swbuf },
        { "_toupper_tab_", (uintptr_t)&BIONIC_toupper_tab_ },
        { "abort", (uintptr_t)&abort },
        { "acos", (uintptr_t)&acos },
        { "atan", (uintptr_t)&atan },
        { "atoi", (uintptr_t)&atoi },
        { "calloc", (uintptr_t)&mw_calloc },
        { "chmod", (uintptr_t)&chmod },
        { "clearerr", (uintptr_t)&clearerr },
        { "close", (uintptr_t)&close_soloader },
        { "cos", (uintptr_t)&cos },
        { "cosf", (uintptr_t)&cosf },
        { "eglChooseConfig", (uintptr_t)&eglChooseConfig },
        { "eglCreateContext", (uintptr_t)&eglCreateContext },
        { "eglCreateWindowSurface", (uintptr_t)&eglCreateWindowSurface },
        { "eglDestroyContext", (uintptr_t)&eglDestroyContext },
        { "eglDestroySurface", (uintptr_t)&eglDestroySurface },
        { "eglGetConfigAttrib", (uintptr_t)&eglGetConfigAttrib },
        { "eglGetDisplay", (uintptr_t)&eglGetDisplay },
        { "eglInitialize", (uintptr_t)&eglInitialize },
        { "eglMakeCurrent", (uintptr_t)&eglMakeCurrent },
        { "eglQuerySurface", (uintptr_t)&eglQuerySurface },
        { "eglSwapBuffers", (uintptr_t)&eglSwapBuffers_wrapper },
        { "eglTerminate", (uintptr_t)&eglTerminate },
        { "fclose", (uintptr_t)&fclose_soloader },
        { "fdopen", (uintptr_t)&fdopen },
        { "fopen", (uintptr_t)&fopen_soloader },
        { "fprintf", (uintptr_t)&fprintf },
        { "fread", (uintptr_t)&fread_counted },
        { "free", (uintptr_t)&free },
        { "fseek", (uintptr_t)&fseek_counted },
        { "fseeko", (uintptr_t)&fseeko },
        { "fstat", (uintptr_t)&fstat_soloader },
        { "ftell", (uintptr_t)&ftell },
        { "ftello", (uintptr_t)&ftello_soloader },
        { "fwrite", (uintptr_t)&fwrite },
        { "getpid", (uintptr_t)&getpid },
        { "gettimeofday", (uintptr_t)&gettimeofday },
        { "glActiveTexture", (uintptr_t)&glActiveTexture },
        { "glAttachShader", (uintptr_t)&glAttachShaderHook },
        { "glBindBuffer", (uintptr_t)&glBindBuffer },
        { "glBindTexture", (uintptr_t)&glBindTexture },
        { "glBlendFunc", (uintptr_t)&glBlendFunc },
        { "glBufferData", (uintptr_t)&glBufferData },
        { "glClear", (uintptr_t)&glClearHook },
        { "glClearColor", (uintptr_t)&glClearColor },
        { "glCompileShader", (uintptr_t)&glCompileShaderHook },
        { "glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2D },
        { "glCreateProgram", (uintptr_t)&glCreateProgram },
        { "glCreateShader", (uintptr_t)&glCreateShaderHook },
        { "glCullFace", (uintptr_t)&glCullFace },
        { "glDeleteBuffers", (uintptr_t)&glDeleteBuffers },
        { "glDeleteProgram", (uintptr_t)&glDeleteProgram },
        { "glDeleteShader", (uintptr_t)&glDeleteShaderHook },
        { "glDeleteTextures", (uintptr_t)&glDeleteTextures },
        { "glDepthMask", (uintptr_t)&glDepthMask },
        { "glDisable", (uintptr_t)&glDisable },
        { "glDrawArrays", (uintptr_t)&glDrawArraysHook },
        { "glDrawElements", (uintptr_t)&glDrawElementsHook },
        { "glEnable", (uintptr_t)&glEnable },
        { "glEnableVertexAttribArray", (uintptr_t)&glEnableVertexAttribArray },
        { "glGenBuffers", (uintptr_t)&glGenBuffers },
        { "glGenTextures", (uintptr_t)&glGenTextures },
        { "glGetAttribLocation", (uintptr_t)&glGetAttribLocation },
        { "glGetError", (uintptr_t)&glGetError },
        { "glGetIntegerv", (uintptr_t)&glGetIntegerv },
        { "glGetProgramInfoLog", (uintptr_t)&glGetProgramInfoLog },
        { "glGetProgramiv", (uintptr_t)&glGetProgramiv },
        { "glGetShaderInfoLog", (uintptr_t)&glGetShaderInfoLogHook },
        { "glGetShaderiv", (uintptr_t)&glGetShaderivHook },
        { "glGetString", (uintptr_t)&glGetString },
        { "glGetUniformLocation", (uintptr_t)&glGetUniformLocationHook },
        { "glHint", (uintptr_t)&glHint },
        { "glLineWidth", (uintptr_t)&glLineWidth },
        { "glLinkProgram", (uintptr_t)&glLinkProgramHook },
        { "glPixelStorei", (uintptr_t)&ret0 },
        { "glScissor", (uintptr_t)&glScissorHook },
        { "glShaderSource", (uintptr_t)&glShaderSourceHook },
        { "glTexImage2D", (uintptr_t)&glTexImage2D },
        { "glTexParameteri", (uintptr_t)&glTexParameteri },
        { "glUniform1f", (uintptr_t)&glUniform1f },
        { "glUniform1i", (uintptr_t)&glUniform1i },
        { "glUniform3f", (uintptr_t)&glUniform3f },
        { "glUniform4fv", (uintptr_t)&glUniform4fvHook },
        { "glUniformMatrix3fv", (uintptr_t)&glUniformMatrix3fv },
        { "glUniformMatrix4fv", (uintptr_t)&glUniformMatrix4fv },
        { "glUseProgram", (uintptr_t)&glUseProgram },
        { "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointer },
        { "glViewport", (uintptr_t)&glViewportHook },
        { "localtime", (uintptr_t)&localtime },
        { "malloc", (uintptr_t)&mw_malloc },
        { "memchr", (uintptr_t)&memchr },
        { "memcmp", (uintptr_t)&memcmp },
        { "memcpy", (uintptr_t)&memcpy },
        { "memmove", (uintptr_t)&memmove },
        { "memset", (uintptr_t)&memset },
        { "mkstemp", (uintptr_t)&mkstemp },
        { "mktime", (uintptr_t)&mktime },
        { "open", (uintptr_t)&open_soloader },
        { "pow", (uintptr_t)&pow },
        { "pthread_attr_init", (uintptr_t)&pthread_attr_init_soloader },
        { "pthread_attr_setdetachstate", (uintptr_t)&pthread_attr_setdetachstate_soloader },
        { "pthread_cond_broadcast", (uintptr_t)&pthread_cond_broadcast_soloader },
        { "pthread_cond_init", (uintptr_t)&pthread_cond_init_soloader },
        { "pthread_cond_wait", (uintptr_t)&pthread_cond_wait_soloader },
        { "pthread_create", (uintptr_t)&pthread_create_soloader },
        { "pthread_mutex_destroy", (uintptr_t)&pthread_mutex_destroy_soloader },
        { "pthread_mutex_init", (uintptr_t)&pthread_mutex_init_soloader },
        { "pthread_mutex_lock", (uintptr_t)&pthread_mutex_lock_soloader },
        { "pthread_mutex_unlock", (uintptr_t)&pthread_mutex_unlock_soloader },
        { "qsort", (uintptr_t)&qsort },
        { "realloc", (uintptr_t)&mw_realloc },
        { "remove", (uintptr_t)&remove },
        { "rename", (uintptr_t)&rename },
        { "sin", (uintptr_t)&sin },
        { "sinf", (uintptr_t)&sinf },
        { "slCreateEngine", (uintptr_t)&slCreateEngine_logged },
        { "snprintf", (uintptr_t)&snprintf },
        { "sprintf", (uintptr_t)&sprintf },
        { "sqrt", (uintptr_t)&sqrt },
        { "sqrtf", (uintptr_t)&sqrtf },
        { "sscanf", (uintptr_t)&sscanf },
        { "stat", (uintptr_t)&stat_soloader },
        { "strcasecmp", (uintptr_t)&strcasecmp },
        { "strcmp", (uintptr_t)&strcmp },
        { "strcpy", (uintptr_t)&strcpy },
        { "strdup", (uintptr_t)&strdup },
        { "strerror", (uintptr_t)&strerror },
        { "strlen", (uintptr_t)&strlen },
        { "strncmp", (uintptr_t)&strncmp },
        { "strrchr", (uintptr_t)&strrchr },
        { "strstr", (uintptr_t)&strstr },
        { "strtoul", (uintptr_t)&strtoul },
        { "time", (uintptr_t)&time },
        { "umask", (uintptr_t)&ret0 },
        { "usleep", (uintptr_t)&usleep },
        // ---- GOF2 HD 2012 additions ----
        { "__aeabi_memclr", (uintptr_t)&__aeabi_memclr },
        { "__aeabi_memclr4", (uintptr_t)&__aeabi_memclr4 },
        { "__aeabi_memclr8", (uintptr_t)&__aeabi_memclr8 },
        { "__aeabi_memcpy", (uintptr_t)&__aeabi_memcpy },
        { "__aeabi_memcpy4", (uintptr_t)&__aeabi_memcpy4 },
        { "__aeabi_memcpy8", (uintptr_t)&__aeabi_memcpy8 },
        { "__aeabi_memmove", (uintptr_t)&__aeabi_memmove },
        { "__aeabi_memmove4", (uintptr_t)&__aeabi_memmove4 },
        { "__aeabi_memset4", (uintptr_t)&__aeabi_memset4 },
        { "__android_log_write", (uintptr_t)&__android_log_write_soloader },
        { "__cxa_atexit", (uintptr_t)&__cxa_atexit },
        { "__cxa_finalize", (uintptr_t)&__cxa_finalize },
        { "__cxa_begin_cleanup", (uintptr_t)&__cxa_begin_cleanup },
        { "__cxa_call_unexpected", (uintptr_t)&__cxa_call_unexpected },
        { "__cxa_type_match", (uintptr_t)&__cxa_type_match },
        { "__gnu_Unwind_Find_exidx", (uintptr_t)&__gnu_Unwind_Find_exidx_soloader },
        { "__assert2", (uintptr_t)&__assert2_soloader },
        { "dlopen", (uintptr_t)&dlopen_soloader },
        { "FMOD_EventSystem_Create", (uintptr_t)&FMOD_EventSystem_Create_hook },
        { "_ZN4FMOD11EventSystem4initEijPvj", (uintptr_t)&FMOD_EventSystem_init_hook },
        { "_ZN4FMOD11EventSystem4loadEPKcP19FMOD_EVENT_LOADINFOPPNS_12EventProjectE", (uintptr_t)&FMOD_EventSystem_load_hook },
        { "_ZN4FMOD6System12createStreamEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE", (uintptr_t)&FMOD_System_createStream_hook },
        { "_ZN4FMOD7SystemI19createSoundInternalEPKcjjjP22FMOD_CREATESOUNDEXINFOPPNS_4FileEbPPNS_6SoundIE", (uintptr_t)&FMOD_SystemI_createSoundInternal_hook },
        { "_ZN4FMOD5Sound12getOpenStateEP14FMOD_OPENSTATEPjPbS4_", (uintptr_t)&FMOD_Sound_getOpenState_hook },
        { "dlsym", (uintptr_t)&dlsym_soloader },
        { "dlclose", (uintptr_t)&dlclose_soloader },
        { "dlerror", (uintptr_t)&dlerror_soloader },
        { "acosf", (uintptr_t)&acosf },
        { "atan2", (uintptr_t)&atan2 },
        { "atanf", (uintptr_t)&atanf },
        { "exp", (uintptr_t)&exp },
        { "floor", (uintptr_t)&floor },
        { "frexp", (uintptr_t)&frexp },
        { "ldexp", (uintptr_t)&ldexp },
        { "log", (uintptr_t)&log },
        { "log10", (uintptr_t)&log10 },
        { "log10f", (uintptr_t)&log10f },
        { "lrintf", (uintptr_t)&lrintf },
        { "powf", (uintptr_t)&powf },
        { "rint", (uintptr_t)&rint },
        { "tan", (uintptr_t)&tan },
        { "lrand48", (uintptr_t)&lrand48 },
        { "srand48", (uintptr_t)&srand48 },
        { "islower", (uintptr_t)&islower_soloader },
        { "isxdigit", (uintptr_t)&isxdigit_soloader },
        { "fcntl", (uintptr_t)&fcntl_soloader },
        { "read", (uintptr_t)&read_soloader },
        { "ferror", (uintptr_t)&ferror },
        { "fileno", (uintptr_t)&fileno },
        { "fputc", (uintptr_t)&fputc },
        { "putc", (uintptr_t)&putc },
        { "puts", (uintptr_t)&puts },
        { "printf", (uintptr_t)&printf },
        { "vfprintf", (uintptr_t)&vfprintf },
        { "vsnprintf", (uintptr_t)&vsnprintf },
        { "vasprintf", (uintptr_t)&vasprintf },
        { "strcat", (uintptr_t)&strcat },
        { "strncasecmp", (uintptr_t)&strncasecmp },
        { "strncpy", (uintptr_t)&strncpy },
        { "strtol", (uintptr_t)&strtol },
        { "memmem", (uintptr_t)&memmem },
        { "memalign", (uintptr_t)&mw_memalign },
        { "unlink", (uintptr_t)&unlink },
        { "utime", (uintptr_t)&ret0 },
        { "chown", (uintptr_t)&ret0 },
        { "raise", (uintptr_t)&ret0 },
        { "pthread_attr_destroy", (uintptr_t)&pthread_attr_destroy_soloader },
        { "pthread_attr_setstacksize", (uintptr_t)&pthread_attr_setstacksize_soloader },
        { "pthread_mutexattr_init", (uintptr_t)&pthread_mutexattr_init_soloader },
        { "pthread_mutexattr_settype", (uintptr_t)&pthread_mutexattr_settype_soloader },
        { "pthread_self", (uintptr_t)&pthread_self_soloader },
        { "pthread_once", (uintptr_t)&pthread_once_soloader },
        { "pthread_key_create", (uintptr_t)&pthread_key_create_soloader },
        { "pthread_getspecific", (uintptr_t)&pthread_getspecific_soloader },
        { "pthread_setspecific", (uintptr_t)&pthread_setspecific_soloader },
        { "sem_destroy", (uintptr_t)&sem_destroy_soloader },
        { "sem_init", (uintptr_t)&sem_init_soloader },
        { "sem_post", (uintptr_t)&sem_post_soloader },
        { "sem_wait", (uintptr_t)&sem_wait_soloader },
        // FMOD's network streaming is never used by the game; keep sockets inert.
        { "socket", (uintptr_t)&ret_minus1 },
        { "connect", (uintptr_t)&ret_minus1 },
        { "bind", (uintptr_t)&ret_minus1 },
        { "listen", (uintptr_t)&ret_minus1 },
        { "accept", (uintptr_t)&ret_minus1 },
        { "send", (uintptr_t)&ret_minus1 },
        { "recv", (uintptr_t)&ret_minus1 },
        { "select", (uintptr_t)&ret_minus1 },
        { "setsockopt", (uintptr_t)&ret_minus1 },
        { "gethostbyname", (uintptr_t)&ret_null },
        { "inet_addr", (uintptr_t)&ret_minus1 },
        // GL entry points this build uses that the Xperia build did not
        { "glBindFramebuffer", (uintptr_t)&glBindFramebufferHook },
        { "glBindRenderbuffer", (uintptr_t)&glBindRenderbuffer },
        { "glCheckFramebufferStatus", (uintptr_t)&glCheckFramebufferStatus },
        { "glColorMask", (uintptr_t)&glColorMask },
        { "glDeleteFramebuffers", (uintptr_t)&glDeleteFramebuffers },
        { "glDeleteRenderbuffers", (uintptr_t)&glDeleteRenderbuffers },
        { "glDepthFunc", (uintptr_t)&glDepthFunc },
        { "glDisableVertexAttribArray", (uintptr_t)&glDisableVertexAttribArray },
        { "glFramebufferRenderbuffer", (uintptr_t)&glFramebufferRenderbuffer },
        { "glFramebufferTexture2D", (uintptr_t)&glFramebufferTexture2D },
        { "glGenFramebuffers", (uintptr_t)&glGenFramebuffers },
        { "glGenRenderbuffers", (uintptr_t)&glGenRenderbuffers },
        { "glGenerateMipmap", (uintptr_t)&glGenerateMipmap },
        { "glRenderbufferStorage", (uintptr_t)&glRenderbufferStorage },
        { "glTexParameterf", (uintptr_t)&glTexParameterf },
        { "glUniform2f", (uintptr_t)&glUniform2f },
        { "glUniform3fv", (uintptr_t)&glUniform3fvHook },
        { "glUniform4f", (uintptr_t)&glUniform4f },
        { "SL_IID_ANDROIDSIMPLEBUFFERQUEUE", (uintptr_t)&SL_IID_ANDROIDSIMPLEBUFFERQUEUE },
        { "SL_IID_ANDROIDCONFIGURATION", (uintptr_t)&SL_IID_ANDROIDCONFIGURATION },
        { "SL_IID_RECORD", (uintptr_t)&SL_IID_RECORD },
};

const int default_dynlib_count = sizeof(default_dynlib) / sizeof(default_dynlib[0]);

void resolve_imports(so_module* mod) {
    __sF_fake[0] = *stdin;
    __sF_fake[1] = *stdout;
    __sF_fake[2] = *stderr;
    so_resolve(mod, default_dynlib, sizeof(default_dynlib), 0);
}
