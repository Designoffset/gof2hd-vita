#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "utils/utils.h"
#include "utils/logger.h"

#define ARRLEN(a) (sizeof(a)/sizeof(a[0]))

typedef void * zip_file_t;
typedef void * zip_t;
typedef void * zip_flags_t;

zip_t * (*zip_open)(const char *path, int flags, int *errorp);

zip_t * zip_patch_hd = NULL;
zip_t * zip_patch_rebalance_no_prices = NULL;
zip_t * zip_patch_rebalance = NULL;
zip_t * zip_patch_user = NULL;

// These arrays must be sorted alphabetically
char * zip_patch_hd_files [] = {
    "assets/data/textures/additional/planet_big_0.aei",
    "assets/data/textures/additional/planet_big_1.aei",
    "assets/data/textures/additional/planet_big_10.aei",
    "assets/data/textures/additional/planet_big_11.aei",
    "assets/data/textures/additional/planet_big_12.aei",
    "assets/data/textures/additional/planet_big_13.aei",
    "assets/data/textures/additional/planet_big_14.aei",
    "assets/data/textures/additional/planet_big_15.aei",
    "assets/data/textures/additional/planet_big_16.aei",
    "assets/data/textures/additional/planet_big_17.aei",
    "assets/data/textures/additional/planet_big_18.aei",
    "assets/data/textures/additional/planet_big_19.aei",
    "assets/data/textures/additional/planet_big_2.aei",
    "assets/data/textures/additional/planet_big_3.aei",
    "assets/data/textures/additional/planet_big_4.aei",
    "assets/data/textures/additional/planet_big_5.aei",
    "assets/data/textures/additional/planet_big_6.aei",
    "assets/data/textures/additional/planet_big_7.aei",
    "assets/data/textures/additional/planet_big_8.aei",
    "assets/data/textures/additional/planet_big_9.aei",
    "assets/data/textures/additional/planet_small_0.aei",
    "assets/data/textures/additional/planet_small_1.aei",
    "assets/data/textures/additional/planet_small_10.aei",
    "assets/data/textures/additional/planet_small_11.aei",
    "assets/data/textures/additional/planet_small_12.aei",
    "assets/data/textures/additional/planet_small_13.aei",
    "assets/data/textures/additional/planet_small_14.aei",
    "assets/data/textures/additional/planet_small_15.aei",
    "assets/data/textures/additional/planet_small_16.aei",
    "assets/data/textures/additional/planet_small_17.aei",
    "assets/data/textures/additional/planet_small_18.aei",
    "assets/data/textures/additional/planet_small_19.aei",
    "assets/data/textures/additional/planet_small_2.aei",
    "assets/data/textures/additional/planet_small_3.aei",
    "assets/data/textures/additional/planet_small_4.aei",
    "assets/data/textures/additional/planet_small_5.aei",
    "assets/data/textures/additional/planet_small_6.aei",
    "assets/data/textures/additional/planet_small_7.aei",
    "assets/data/textures/additional/planet_small_8.aei",
    "assets/data/textures/additional/planet_small_9.aei",
    "assets/data/textures/gof2_interface.aei",
    "assets/data/textures/gof2_logos.aei",
    "assets/data/textures/planet_big_0.aei",
    "assets/data/textures/planet_big_1.aei",
    "assets/data/textures/planet_big_10.aei",
    "assets/data/textures/planet_big_11.aei",
    "assets/data/textures/planet_big_12.aei",
    "assets/data/textures/planet_big_13.aei",
    "assets/data/textures/planet_big_14.aei",
    "assets/data/textures/planet_big_15.aei",
    "assets/data/textures/planet_big_16.aei",
    "assets/data/textures/planet_big_17.aei",
    "assets/data/textures/planet_big_18.aei",
    "assets/data/textures/planet_big_19.aei",
    "assets/data/textures/planet_big_2.aei",
    "assets/data/textures/planet_big_3.aei",
    "assets/data/textures/planet_big_4.aei",
    "assets/data/textures/planet_big_5.aei",
    "assets/data/textures/planet_big_6.aei",
    "assets/data/textures/planet_big_7.aei",
    "assets/data/textures/planet_big_8.aei",
    "assets/data/textures/planet_big_9.aei",
    "assets/data/textures/planet_small_0.aei",
    "assets/data/textures/planet_small_1.aei",
    "assets/data/textures/planet_small_10.aei",
    "assets/data/textures/planet_small_11.aei",
    "assets/data/textures/planet_small_12.aei",
    "assets/data/textures/planet_small_13.aei",
    "assets/data/textures/planet_small_14.aei",
    "assets/data/textures/planet_small_15.aei",
    "assets/data/textures/planet_small_16.aei",
    "assets/data/textures/planet_small_17.aei",
    "assets/data/textures/planet_small_18.aei",
    "assets/data/textures/planet_small_19.aei",
    "assets/data/textures/planet_small_2.aei",
    "assets/data/textures/planet_small_3.aei",
    "assets/data/textures/planet_small_4.aei",
    "assets/data/textures/planet_small_5.aei",
    "assets/data/textures/planet_small_6.aei",
    "assets/data/textures/planet_small_7.aei",
    "assets/data/textures/planet_small_8.aei",
    "assets/data/textures/planet_small_9.aei",
    "assets/de.lang",
    "assets/es.lang",
    "assets/fr.lang",
    "assets/gb.lang",
    "assets/it.lang",
    "assets/pl.lang",
    "assets/ru.lang"
};

char * zip_patch_rebalance_no_prices_files [] = {
    "assets/data/bin/items.bin",
    "assets/data/bin/ships.bin"
};

char * zip_patch_rebalance_files [] = {
    "assets/data/bin/items.bin",
    "assets/data/bin/ships.bin"
};

static int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

bool has_patched_file(char *fname, char ** file_list, int file_list_len) {
    char **preload_file = bsearch(&fname, file_list, file_list_len, sizeof(file_list[0]), compare_strings);
    if (preload_file) {
        return true;
    }
    return false;
}

so_hook zip_fopen_hook;
static zip_t **APKArchive_p, **ZIPArchive_p;
static zip_t *zip_fopen_real(zip_t *archive, const char *fname, zip_flags_t flags);

/*
 * Memory leak fix. FileInterfaceAndroid::OpenRead and ::FileExist open every
 * file twice: "assets/<name>" in APKArchive and "<content dir>/assets/<name>"
 * in ZIPArchive (the expansion file on Android), keep the APK one when both
 * exist and never close the other. Our single APK holds both trees, so every
 * file read left an open zip handle behind (~17 KB each: struct, read buffer
 * and inflate state) - about 10 MB per jump until the heap ran out. The APK
 * lookup always comes first, so when it just succeeded, report the
 * expansion copy as missing.
 */
zip_t * zip_fopen(zip_t *archive, const char *fname, zip_flags_t flags) {
    static int apk_hit = 0;
    zip_t *apk = APKArchive_p ? *APKArchive_p : NULL, *exp = ZIPArchive_p ? *ZIPArchive_p : NULL;
    int is_exp = exp && archive == exp && (apk != exp || (fname && strncmp(fname, "assets/", 7) != 0));
    if (!is_exp && apk && archive == apk) {
        zip_t *r = zip_fopen_real(archive, fname, flags);
        apk_hit = (r != NULL);
        return r;
    }
    if (is_exp && apk_hit) {
        apk_hit = 0;
        return NULL;
    }
    apk_hit = 0;
    return zip_fopen_real(archive, fname, flags);
}

static zip_t *zip_fopen_real(zip_t *archive, const char *fname, zip_flags_t flags) {
    if (zip_patch_user != NULL) {
        // User patch takes precedence over everything
        zip_t * ret = SO_CONTINUE(zip_t *, zip_fopen_hook, zip_patch_user, fname, flags);
        if (ret) return ret;
    }

    if (setting_useHdMod && has_patched_file(fname, zip_patch_hd_files, ARRLEN(zip_patch_hd_files)) && zip_patch_hd != NULL) {
        zip_t * ret = SO_CONTINUE(zip_t *, zip_fopen_hook, zip_patch_hd, fname, flags);
        return ret;
    }

    if (setting_useRebalanceModOldPrices && has_patched_file(fname, zip_patch_rebalance_no_prices_files, ARRLEN(zip_patch_rebalance_no_prices_files)) && zip_patch_rebalance_no_prices != NULL) {
        return SO_CONTINUE(zip_t *, zip_fopen_hook, zip_patch_rebalance_no_prices, fname, flags);
    } else if (setting_useRebalanceMod && has_patched_file(fname, zip_patch_rebalance_files, ARRLEN(zip_patch_rebalance_files)) && zip_patch_rebalance != NULL) {
        return SO_CONTINUE(zip_t *, zip_fopen_hook, zip_patch_rebalance, fname, flags);
    }

    return SO_CONTINUE(zip_t *, zip_fopen_hook, archive, fname, flags);
}

void patch__apk_hook() {
    zip_open = (void *) so_symbol(&so_mod, "zip_open");

    if (file_exists("app0:patch/patch_hd.zip") && setting_useHdMod) {
        zip_patch_hd = zip_open("app0:patch/patch_hd.zip", 0, NULL);
        if (!zip_patch_hd) {
            setting_useHdMod = false;
            logv_error("Failed to open patch zip %s", "app0:patch/patch_hd.zip");
        }
    } else {
        logv_info("HD Patch not enabled. File exists: %i; Setting enabled: %i", file_exists("app0:patch/patch_hd.zip"), setting_useHdMod);
    }

    if (file_exists("app0:patch/patch_rebalance_no_prices.zip") && setting_useRebalanceModOldPrices) {
        zip_patch_rebalance_no_prices = zip_open("app0:patch/patch_rebalance_no_prices.zip", 0, NULL);
        if (!zip_patch_rebalance_no_prices) {
            setting_useRebalanceModOldPrices = false;
            logv_error("Failed to open patch zip %s", "app0:patch/patch_rebalance_no_prices.zip");
        }
    } else if (file_exists("app0:patch/patch_rebalance.zip") && setting_useRebalanceMod) {
        zip_patch_rebalance = zip_open("app0:patch/patch_rebalance.zip", 0, NULL);
        if (!zip_patch_rebalance) {
            setting_useRebalanceMod = false;
            logv_error("Failed to open patch zip %s", "app0:patch/patch_rebalance.zip");
        }
    }

    if (file_exists("app0:patch/patch_user.zip")) {
        zip_patch_user = zip_open("app0:patch/patch_user.zip", 0, NULL);
        if (!zip_patch_user) {
            logv_error("Failed to open patch zip %s", "app0:patch/patch_user.zip");
        }
    }

    zip_fopen_hook = hook_addr(so_symbol(&so_mod, "zip_fopen"), (uintptr_t)zip_fopen);
    APKArchive_p = (zip_t **)so_symbol(&so_mod, "APKArchive");
    ZIPArchive_p = (zip_t **)so_symbol(&so_mod, "ZIPArchive");
}

// ---------------------------------------------------------------------------
// GOF2 HD 2012: the game asks for "data/..." paths (e.g. data/bin/systems.bin),
// which it looks up in the archive as "assets/data/..." (or under the content
// zip prefix "47947_GOF2CONTENT_ETC/assets/data/..."). The repacked APK stores
// these files without the "data/" level ("assets/bin/systems.bin"), so when an
// exact lookup fails, retry it with that level removed.
// ---------------------------------------------------------------------------
so_hook zip_name_locate_hook;

static int zip_name_locate_retry(void *za, const char *fname, int flags, void *error) {
    int idx = SO_CONTINUE(int, zip_name_locate_hook, za, fname, flags, error);
    if (idx >= 0 || !fname) return idx;

    static const char *prefixes[] = { "assets/data/", "47947_GOF2CONTENT_ETC/assets/data/" };
    for (int i = 0; i < 2; i++) {
        size_t n = strlen(prefixes[i]);
        if (strncmp(fname, prefixes[i], n) == 0) {
            char alt[512];
            snprintf(alt, sizeof(alt), "assets/%s", fname + n);
            int r = SO_CONTINUE(int, zip_name_locate_hook, za, alt, flags, error);
            if (r >= 0) return r;

            // The repack also sorted cubemaps into subfolders:
            // .../cubemaps/cubemap_skybox_N.aei -> .../cubemaps/skyboxes/...
            // .../cubemaps/cubemap_hangar_X.aei -> .../cubemaps/hangars/...
            char *cm = strstr(alt, "/cubemaps/");
            if (cm) {
                const char *file = cm + strlen("/cubemaps/");
                const char *sub = strstr(file, "hangar") ? "hangars/" : "skyboxes/";
                char alt2[512];
                snprintf(alt2, sizeof(alt2), "%.*s/cubemaps/%s%s", (int)(cm - alt), alt, sub, file);
                r = SO_CONTINUE(int, zip_name_locate_hook, za, alt2, flags, error);
                if (r >= 0) return r;
            }

            // Character portraits: the game asks for X_ipad.aei, the repack
            // only has the high-resolution X_ipad_large.aei, which comes out
            // twice too big. Half-size copies made from those live as loose
            // files in DATA_PATH data/textures/; let the game fall through to
            // them, and only use the large one when there is no loose copy.
            size_t al = strlen(alt);
            char loose[512];
            snprintf(loose, sizeof(loose), DATA_PATH "data/%s", fname + n);
            if (al > 9 && strcmp(alt + al - 9, "_ipad.aei") == 0 && !file_exists(loose)) {
                char alt3[512];
                snprintf(alt3, sizeof(alt3), "%.*s_ipad_large.aei", (int)(al - 9), alt);
                r = SO_CONTINUE(int, zip_name_locate_hook, za, alt3, flags, error);
                if (r >= 0) return r;
            }
        }
    }
    static int misses = 0;
    if (misses++ < 200) logv_info("archive: not found: %s", fname);
    return idx;
}

void patch__apk_paths() {
    uintptr_t f = so_symbol(&so_mod, "_zip_name_locate");
    if (f) zip_name_locate_hook = hook_addr(f, (uintptr_t)zip_name_locate_retry);
}
