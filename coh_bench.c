/***
 * 
 * Generic cohorence protocol testing
 * Run this on multiple processes with a different tid
 * Program simulates shared reads and writes
 * 
 * Caleb Lee
 * 
 ***/


#define _GNU_SOURCE
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CACHELINE 64
#define MAXP 64

// MUST match the VA you map in the Python config.
#define SHM_VA 0x700000000000ULL

static inline void cpu_relax(void) { asm volatile("" ::: "memory"); }

typedef struct {
    volatile uint32_t count;
    volatile uint32_t sense;
    volatile uint32_t nprocs;
    volatile uint32_t _pad;
} shm_barrier_t;

static inline void shm_barrier(shm_barrier_t* b, uint32_t* local_sense) {
    *local_sense ^= 1;
    uint32_t prior = __sync_fetch_and_add(&b->count, 1);
    if (prior + 1 == b->nprocs) {
        b->count = 0;
        b->sense = *local_sense;
    } else {
        while (b->sense != *local_sense) cpu_relax();
    }
}

typedef struct __attribute__((aligned(64))) { volatile uint64_t w[8]; } line64_t;

typedef struct __attribute__((aligned(64))) {
    // true sharing target
    volatile uint64_t hot;
    volatile uint64_t _pad0[7];

    // false sharing target: all threads write different indices within same line
    line64_t false_line;

    // padded target: each thread gets its own line
    line64_t padded_lines[MAXP];

    // migratory: token indicates which tid is allowed to write the shared line
    volatile uint64_t token;
    volatile uint64_t epoch;
    volatile uint64_t _pad1[6];

    // sanity / init / barrier
    volatile uint64_t magic;
    volatile uint32_t inited;
    volatile uint32_t nprocs;
    shm_barrier_t bar;

    // optional: per-thread counters for debugging
    volatile uint64_t started[MAXP];
    volatile uint64_t done[MAXP];
} shm_t;

// ---- patterns ----
typedef enum {
    PAT_PADDED   = 0,
    PAT_FALSE    = 1,
    PAT_HOT      = 2,
    PAT_MIGRATORY= 3
} pattern_t;

static pattern_t parse_pattern(const char* s) {
    if (!s) return PAT_PADDED;
    if (s[0]=='p') return PAT_PADDED;      // "padded"
    if (s[0]=='f') return PAT_FALSE;       // "false"
    if (s[0]=='h') return PAT_HOT;         // "hot"
    if (s[0]=='m') return PAT_MIGRATORY;   // "migratory"
    fprintf(stderr, "Not recognized: '%s' (use padded|false|hot|migratory)\n", s);
    exit(1);
}

// Prevent compiler from optimizing away loop bodies.
static inline void sink_u64(volatile uint64_t x) { asm volatile("" :: "r"(x) : "memory"); }

int main(int argc, char** argv) {
    // ./coh_bench <tid> <nprocs> <iters> <pattern> <reads_per_write>
    if (argc < 6) {
        fprintf(stderr,
            "usage: %s <tid> <nprocs> <iters> <pattern> <reads_per_write>\n"
            "  pattern: padded | false | hot | migratory\n"
            "  reads_per_write: 0 (write-heavy) .. 100+ (read-mostly)\n",
            argv[0]);
        return 1;
    }

    int tid = atoi(argv[1]);
    int nprocs = atoi(argv[2]);
    uint64_t iters = (uint64_t)strtoull(argv[3], NULL, 0);
    pattern_t pat = parse_pattern(argv[4]);
    uint32_t rpw = (uint32_t)strtoul(argv[5], NULL, 0);

    if (tid < 0 || tid >= MAXP || nprocs < 1 || nprocs > MAXP) {
        fprintf(stderr, "bad tid/nprocs (tid 0..%d, nprocs 1..%d)\n", MAXP-1, MAXP);
        return 1;
    }

    volatile shm_t* shm = (volatile shm_t*)(uintptr_t)SHM_VA;

    // pinning is handled by gem5 config mapping this is just a sanity check
    (void)sched_getcpu();

    // ---- init once ----
    if (tid == 0) {
        shm->magic = 0xC0DECAFEDEADBEEFULL;
        shm->nprocs = (uint32_t)nprocs;
        shm->bar.nprocs = (uint32_t)nprocs;
        shm->bar.count = 0;
        shm->bar.sense = 0;

        shm->hot = 1;
        for (int i = 0; i < 8; i++) shm->false_line.w[i] = 1;
        for (int t = 0; t < MAXP; t++) {
            for (int i = 0; i < 8; i++) shm->padded_lines[t].w[i] = 1;
            shm->started[t] = 0;
            shm->done[t] = 0;
        }

        shm->token = 0;
        shm->epoch = 0;

        shm->inited = 1;
    } else {
        while (shm->inited != 1) cpu_relax();
        while ((int)shm->nprocs != nprocs) cpu_relax();
    }

    shm->started[tid] = 1;

    uint32_t ls = 0;
    shm_barrier((shm_barrier_t*)&shm->bar, &ls); // align start

    // ---- benchmark loop ----
    // reads_per_write controls read intensity without changing sharing topology
    // We do rpw reads of the target, then 1 write
    for (uint64_t i = 0; i < iters; i++) {
        if (pat == PAT_PADDED) {
            // Each thread writes its own cache line -> coherence mostly idle
            volatile uint64_t* x = &shm->padded_lines[tid].w[0];
            uint64_t v = *x;
            for (uint32_t r = 0; r < rpw; r++) v += *x;
            *x = v + 1;
            sink_u64(v);
        } else if (pat == PAT_FALSE) {
            // All threads write different words within the SAME cache line -> false sharing
            volatile uint64_t* x = &shm->false_line.w[tid & 7];
            uint64_t v = *x;
            for (uint32_t r = 0; r < rpw; r++) v += *x;
            *x = v + 1;
            sink_u64(v);
        } else if (pat == PAT_HOT) {
            // All threads write the SAME word -> true sharing hotspot
            volatile uint64_t* x = &shm->hot;
            uint64_t v = *x;
            for (uint32_t r = 0; r < rpw; r++) v += *x;
            *x = v + 1;
            sink_u64(v);
        } else { // PAT_MIGRATORY
            // Token passing: only token-holder writes the shared line, then hands off.
            // Creates clean "ownership migration" behavior.
            while ((uint32_t)shm->token != (uint32_t)tid) cpu_relax();

            volatile uint64_t* x = &shm->hot; // use the same shared line, but serialized
            uint64_t v = *x;
            for (uint32_t r = 0; r < rpw; r++) v += *x;
            *x = v + 1;
            sink_u64(v);

            shm->epoch++; // extra shared write to show ownership behavior (optional)
            shm->token = (uint64_t)((tid + 1) % nprocs);
        }
    }

    shm->done[tid] = 1;
    shm_barrier((shm_barrier_t*)&shm->bar, &ls); // align end

    return 0;
}