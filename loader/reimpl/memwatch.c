/*
 * memwatch.c
 *
 * Allocation wrappers for the game. When the heap runs out, log the size
 * that failed and the heap state (the game then dies with std::bad_alloc),
 * and track the peak use for the heartbeat log.
 */
#include <stdlib.h>
#include <malloc.h>
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

static void report(const char *what, size_t n) {
    static int count = 0;
    if (count++ > 20) return;
    struct mallinfo mi = mallinfo();
    logv_info("memwatch: %s(%u) FAILED - heap in use %u KB, free in heap %u KB, arena %u KB, peak %u KB",
              what, (unsigned)n, (unsigned)(mi.uordblks / 1024), (unsigned)(mi.fordblks / 1024),
              (unsigned)(mi.arena / 1024), (unsigned)(peak_used / 1024));
}

void *mw_malloc(size_t n) {
    void *p = malloc(n);
    if (!p && n) report("malloc", n);
    return p;
}
void *mw_calloc(size_t a, size_t b) {
    void *p = calloc(a, b);
    if (!p && a && b) report("calloc", a * b);
    return p;
}
void *mw_realloc(void *o, size_t n) {
    void *p = realloc(o, n);
    if (!p && n) report("realloc", n);
    return p;
}
void *mw_memalign(size_t al, size_t n) {
    void *p = memalign(al, n);
    if (!p && n) report("memalign", n);
    return p;
}
void *mw_new(size_t n) {
    void *p = malloc(n ? n : 1);
    if (p) return p;
    report("operator new", n);
    return _Znwj(n);   // throws std::bad_alloc like before
}
void *mw_new_array(size_t n) {
    void *p = malloc(n ? n : 1);
    if (p) return p;
    report("operator new[]", n);
    return _Znaj(n);
}
