/*
 * memwatch.c
 *
 * Allocation wrappers for the game and FMOD.
 *  - When the heap runs out, log the size that failed and the heap state
 *    (the game then dies with std::bad_alloc).
 *  - Track every live allocation with the address of the code that made it,
 *    and once a minute log the callers holding the most memory, so a leak
 *    shows up in a tester's log with the exact place it comes from.
 */
#include <stdlib.h>
#include <malloc.h>
#include <stdint.h>
#include <string.h>
#include "utils/logger.h"

extern void *_Znwj(size_t);
extern void *_Znaj(size_t);

static size_t peak_used;

size_t memwatch_used(void) {
    struct mallinfo mi = mallinfo();
    if ((size_t)mi.uordblks > peak_used) peak_used = mi.uordblks;
    return mi.uordblks;
}
size_t memwatch_peak(void) { return peak_used; }
size_t memwatch_arena(void) { return mallinfo().arena; }

// ---- live allocation table (open addressing, key = pointer) ----
#define SLOTS (1 << 19)
typedef struct { uintptr_t p; uint32_t size; uint32_t caller; } slot_t;
static slot_t *tab;
static int tab_live, tab_full, tab_dead;
static volatile int lock_;
static void lock(void)   { while (__sync_lock_test_and_set(&lock_, 1)) ; }
static void unlock(void) { __sync_lock_release(&lock_); }
static inline uint32_t h(uintptr_t p) { return (uint32_t)((p >> 3) * 2654435761u) & (SLOTS - 1); }

// Drop the deleted markers by re-inserting the live entries (in place, via a
// scratch copy) so probe chains stay short and there are always empty slots.
static slot_t *scratch;
static void rehash(void) {
    if (!scratch) scratch = calloc(SLOTS, sizeof(slot_t));
    if (!scratch) { tab_full = 1; return; }
    memcpy(scratch, tab, SLOTS * sizeof(slot_t));
    memset(tab, 0, SLOTS * sizeof(slot_t));
    for (int k = 0; k < SLOTS; k++) {
        if (scratch[k].p <= 1) continue;
        uint32_t i = h(scratch[k].p);
        while (tab[i].p) i = (i + 1) & (SLOTS - 1);
        tab[i] = scratch[k];
    }
    tab_dead = 0;
}

// Per-caller tracking found the zip handle leak (apk_hook.c). It costs time
// on every allocation (and a table rebuild now and then), so it is off by
// default; set MEMWATCH_TRACK to 1 to use it again.
#define MEMWATCH_TRACK 0
static void track(void *p, size_t size, uintptr_t caller) {
    if (!MEMWATCH_TRACK || !p) return;
    lock();
    if (!tab) { tab = calloc(SLOTS, sizeof(slot_t)); }
    if (tab && tab_live + tab_dead > SLOTS / 2) rehash();
    if (!tab || tab_live > SLOTS / 2) { tab_full = 1; unlock(); return; }
    uint32_t i = h((uintptr_t)p);
    while (tab[i].p && tab[i].p != 1 && tab[i].p != (uintptr_t)p) i = (i + 1) & (SLOTS - 1);
    if (tab[i].p == 1) tab_dead--;
    if (tab[i].p != (uintptr_t)p) tab_live++;
    tab[i].p = (uintptr_t)p; tab[i].size = size; tab[i].caller = caller;
    unlock();
}
static void untrack(void *p) {
    if (!MEMWATCH_TRACK || !p || !tab) return;
    lock();
    uint32_t i = h((uintptr_t)p);
    while (tab[i].p) {
        if (tab[i].p == (uintptr_t)p) { tab[i].p = 1; tab_live--; tab_dead++; break; }   // 1 = deleted
        i = (i + 1) & (SLOTS - 1);
    }
    unlock();
}

void memwatch_report(void) {
    if (!tab) return;
    #define TOP 12
    static struct { uint32_t caller; uint32_t bytes, n; } agg[512];
    int na = 0;
    lock();
    for (int i = 0; i < SLOTS; i++) {
        if (tab[i].p <= 1) continue;
        int k;
        for (k = 0; k < na; k++) if (agg[k].caller == tab[i].caller) break;
        if (k == na) { if (na == 512) continue; agg[na].caller = tab[i].caller; agg[na].bytes = agg[na].n = 0; na++; }
        agg[k].bytes += tab[i].size; agg[k].n++;
    }
    int live = tab_live, full = tab_full;
    unlock();
    for (int t = 0; t < TOP && t < na; t++) {
        int best = t;
        for (int k = t + 1; k < na; k++) if (agg[k].bytes > agg[best].bytes) best = k;
        uint32_t c = agg[t].caller, b = agg[t].bytes, n = agg[t].n;
        agg[t] = agg[best]; agg[best].caller = c; agg[best].bytes = b; agg[best].n = n;
        logv_info("memwatch: top %d caller 0x%08x holds %u KB in %u blocks", t + 1,
                  (unsigned)agg[t].caller, (unsigned)(agg[t].bytes / 1024), (unsigned)agg[t].n);
    }
    logv_info("memwatch: %d live blocks tracked%s", live, full ? " (table full)" : "");
}

static void report(const char *what, size_t n, uintptr_t caller) {
    static int count = 0;
    if (count++ > 20) return;
    struct mallinfo mi = mallinfo();
    logv_info("memwatch: %s(%u) from 0x%08x FAILED - heap in use %u KB, free in heap %u KB, arena %u KB, peak %u KB",
              what, (unsigned)n, (unsigned)caller, (unsigned)(mi.uordblks / 1024), (unsigned)(mi.fordblks / 1024),
              (unsigned)(mi.arena / 1024), (unsigned)(peak_used / 1024));
    if (count == 1) memwatch_report();
}

#define CALLER ((uintptr_t)__builtin_return_address(0))

void *mw_malloc(size_t n) {
    void *p = malloc(n);
    if (!p && n) report("malloc", n, CALLER);
    track(p, n, CALLER);
    return p;
}
void *mw_calloc(size_t a, size_t b) {
    void *p = calloc(a, b);
    if (!p && a && b) report("calloc", a * b, CALLER);
    track(p, a * b, CALLER);
    return p;
}
void *mw_realloc(void *o, size_t n) {
    void *p = realloc(o, n);
    if (!p && n) { report("realloc", n, CALLER); return p; }
    if (o) untrack(o);
    track(p, n, CALLER);
    return p;
}
void *mw_memalign(size_t al, size_t n) {
    void *p = memalign(al, n);
    if (!p && n) report("memalign", n, CALLER);
    track(p, n, CALLER);
    return p;
}
void mw_free(void *p) { untrack(p); free(p); }
void *mw_new(size_t n) {
    void *p = malloc(n ? n : 1);
    if (p) { track(p, n, CALLER); return p; }
    report("operator new", n, CALLER);
    return _Znwj(n);   // throws std::bad_alloc like before
}
void *mw_new_array(size_t n) {
    void *p = malloc(n ? n : 1);
    if (p) { track(p, n, CALLER); return p; }
    report("operator new[]", n, CALLER);
    return _Znaj(n);
}
void mw_delete(void *p) { untrack(p); free(p); }
