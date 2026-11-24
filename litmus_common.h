/*
 * Shared C definitions for the MicroBlaze V litmus tests.
 * See config_flags.h for build options and CONFIGURATION.md for setup notes.
 */
#ifndef LITMUS_COMMON_H
#define LITMUS_COMMON_H

#include <stdint.h>

#if !defined(TEST_SB) && !defined(TEST_MP) && !defined(TEST_MP_DEP) && \
    !defined(TEST_LB) && !defined(TEST_CORR) && !defined(TEST_2P2W) && \
    !defined(TEST_S) && !defined(TEST_R) && !defined(TEST_COUNTER)
#define TEST_SB 1
#endif

#define NITER 256u
#define MAGIC 0xD0DE0000u

/* ---------------- memory regions ---------------- */
#define HARNESS_BASE 0xC0000000u
#define TEST_BRAM_BASE 0xC2000000u
#define TWO_BRAM_Y_BASE 0xC1000000u

/* Keep the old name because later code and older notes still use it. */
#define SHARED_BASE HARNESS_BASE

#define REG32(address) (*(volatile uint32_t *)(address))
#define LITMUS_ALWAYS_INLINE static inline __attribute__((always_inline))

/* X_OFF and Y_OFF are offsets inside whichever BRAM this build chooses. */
#ifndef X_OFF
#define X_OFF 0x000u
#endif

#ifndef Y_OFF
#if defined(CACHED_TEST)
#define Y_OFF 0x010u
#else
#define Y_OFF 0x004u
#endif
#endif

/* These are the possible physical addresses for litmus variable X. */
#define HARNESS_X_ADDR ((HARNESS_BASE) + (X_OFF))
#define TEST_X_ADDR    ((TEST_BRAM_BASE) + (X_OFF))

/* These are the possible physical addresses for litmus variable Y. */
#define HARNESS_Y_ADDR       ((HARNESS_BASE) + (Y_OFF))
#define HARNESS_FIXED_Y_ADDR ((HARNESS_BASE) + 0x004u)
#define TEST_Y_ADDR          ((TEST_BRAM_BASE) + (Y_OFF))
#define TWO_BRAM_Y_ADDR      ((TWO_BRAM_Y_BASE) + (Y_OFF))

/*
 * Choose where X and Y live for this compiled ELF.
 *
 * Default/T1: both are in the always-uncached harness BRAM.
 * TWO_BRAM/T2: X stays in the harness BRAM, Y moves to a second BRAM.
 * CACHED_TEST/T3+T4: both move to the test BRAM, unless a split-path
 * experiment deliberately keeps Y in the uncached harness BRAM.
 */
#if defined(CACHED_TEST)
#define TEST_BASE TEST_BRAM_BASE
#if defined(MP_DATA_UNCACHED)
#define X_ADDR HARNESS_X_ADDR
#else
#define X_ADDR TEST_X_ADDR
#endif
#if defined(MP_FLAG_UNCACHED)
#define Y_ADDR HARNESS_FIXED_Y_ADDR
#else
#define Y_ADDR TEST_Y_ADDR
#endif
#elif defined(TWO_BRAM)
#define X_ADDR HARNESS_X_ADDR
#define Y_ADDR TWO_BRAM_Y_ADDR
#else
#define X_ADDR HARNESS_X_ADDR
#define Y_ADDR HARNESS_Y_ADDR
#endif

/* -------- MP explicit observer-prime protocol --------
 * This is a directed cache-state protocol, not an outcome-forcing control.
 * The uncached MP producer owns X/Y initialization. After an init-done
 * barrier, the cached consumer may perform the PRIME-only X load. A second
 * barrier completes the prime window before either MP body begins.
 */
#if defined(MP_EXPLICIT_PRIME_PROTOCOL)
#if !defined(TEST_MP) || !defined(MP_ROLE_SWAP) || !defined(MP_FLAG_UNCACHED)
#error "MP_EXPLICIT_PRIME_PROTOCOL requires TEST_MP + MP_ROLE_SWAP + MP_FLAG_UNCACHED"
#endif
#if defined(MP_DATA_UNCACHED) || defined(MP_INVALIDATE_X_BEFORE_DATA) || \
    defined(NO_CACHE_SETUP) || defined(TEST_FLUSH_AFTER_BODY) || \
    defined(STOP_ON_WEAK) || defined(TRIGGER_ON_WEAK) || \
    defined(MATRIX_FORCE_00) || defined(MATRIX_FORCE_01) || \
    defined(MATRIX_FORCE_11) || defined(MATRIX_FORCE_10_PIPELINE) || \
    defined(SB_DELAY_CORE0_STORE) || defined(SB_DELAY_BEFORE_LOAD)
#error "MP_EXPLICIT_PRIME_PROTOCOL forbids cache-off, maintenance, forcing, delay, and outcome-conditioned controls"
#endif
#endif

#if defined(MP_PRIME_X_READ) && !defined(MP_EXPLICIT_PRIME_PROTOCOL)
#error "MP_PRIME_X_READ requires MP_EXPLICIT_PRIME_PROTOCOL"
#endif

#if defined(MP_PRIME_WAIT_FOR_FLAG) && \
    (!defined(MP_EXPLICIT_PRIME_PROTOCOL) || !defined(MP_PRIME_X_READ))
#error "MP_PRIME_WAIT_FOR_FLAG requires MP_EXPLICIT_PRIME_PROTOCOL + MP_PRIME_X_READ"
#endif

/* ---------------- cache hooks (no-ops unless -DUSE_XIL_CACHE) ---------
 * A core built with USE_XIL_CACHE enables its D-cache at startup, flushes
 * the test region after init, and invalidates before final-state reads.
 * These maintenance ops are themselves visible on the DC-port ILA.
 * FLUSH_ADDR flushes only the cache line(s) covering [addr, addr+len). */
#if defined(USE_XIL_CACHE)
#include "xil_cache.h"
#define TEST_REGION_SIZE   0x2000u
#define CACHE_SETUP()      do { TRAP_REPORT_SETUP(); Xil_DCacheEnable(); } while (0)

#if defined(DISABLE_CACHE_MAINT)
#define TEST_FLUSH()       do { } while (0)
#define TEST_INVALIDATE()  do { } while (0)
#define FLUSH_ADDR(addr, len)  do { } while (0)
#else
#define TEST_FLUSH()       Xil_DCacheFlushRange(TEST_BASE, TEST_REGION_SIZE)
#define TEST_INVALIDATE()  Xil_DCacheInvalidateRange(TEST_BASE, TEST_REGION_SIZE)
#define FLUSH_ADDR(addr, len)  Xil_DCacheFlushRange((INTPTR)(addr), (len))
#endif

#else
#define CACHE_SETUP()      do { } while (0)
#define TEST_FLUSH()       do { } while (0)
#define TEST_INVALIDATE()  do { } while (0)
#define FLUSH_ADDR(addr, len)  do { } while (0)
#endif

#define VAR_X   REG32(X_ADDR)
#define VAR_Y   REG32(Y_ADDR)

/* ---------------- SB fresh-Y variant (SB_Y_STRIDE) ----------------
 * Per-iteration y address, one line per iteration, so a cached core's SB
 * load of y is a COLD MISS (a genuine memory observation) instead of a
 * stale D-cache hit on its own init store. Core1 (uncached) zeroes the
 * fresh line during init; core0 must NOT touch y_i before its load.
 * Stride assumes 16-byte lines, the same assumption as the default
 * diffline Y_OFF=0x010 (verify against the configured line length;
 * override SB_Y_STRIDE_SHIFT if longer). Span must stay below
 * WEAK_TEST_TRIG at +0x1F00: default 0x010..0x100F. */
#if defined(SB_Y_STRIDE)
#ifndef SB_Y_STRIDE_SHIFT
#define SB_Y_STRIDE_SHIFT 4u
#endif
#if defined(CACHED_TEST) && ((0x010u + (256u << SB_Y_STRIDE_SHIFT)) > 0x1F00u)
#error "SB_Y_STRIDE span exceeds WEAK_TEST_TRIG at +0x1F00; lower SB_Y_STRIDE_SHIFT"
#endif
static inline uint32_t sb_y_address(uint32_t iter)
{
    uint32_t slot;
    uint32_t byte_offset;

    slot = iter & 0xFFu;
    byte_offset = slot << SB_Y_STRIDE_SHIFT;

    return Y_ADDR + byte_offset;
}

#define VAR_Y_SB(i) REG32(sb_y_address((uint32_t)(i)))
#else
#define VAR_Y_SB(i) VAR_Y
#endif

/* -------- T4 dual-cached per-iteration placement (XY_STRIDE) --------
 * BOTH litmus variables move to a fresh cache line every iteration, so on
 * a dual-cached tier EACH core's read of the other core's variable is a
 * cold miss (a genuine memory observation) instead of a frozen stale hit.
 *
 * Default geometry is derived from the cache-line length. Pin the real
 * D-cache line first: -DDCACHE_LINE_BYTES=<16|32|64> (xparameters.h
 * *_DCACHE_LINE_LEN words * 4). Defaults derived from it:
 *
 *   line  shift  XY_NITER   x_i range        y_i range
 *   16 B    4      240      0x000..0xEFF     0x1000..0x1EFF
 *   32 B    5      120      0x000..0xEFF     0x1000..0x1EFF
 *   64 B    6       60      0x000..0xEFF     0x1000..0x1EFF
 *
 * Only the first XY_NITER of the 256 result rows are live; rows >= XY_NITER
 * are stale BRAM and MUST be filtered by the parser (Iter < XY_NITER).
 * The reserved test-BRAM region 0x1F00..0x1FFF (WEAK_TEST_TRIG + slack) is
 * protected by span checks for the configured banks and iteration count.
 * Custom offsets and alignment still need review; see CONFIGURATION.md.
 * Harness/control words live in the separate 0xC0000000 BRAM.
 * XY_SAMELINE control: y_i = x_i + 4 (same line, single bank).
 * Init ownership rule: each core zeroes ONLY the variable(s) it will STORE.
 * SB: core0 zeroes x_i, core1 zeroes y_i. MP: producer core0 zeroes both.
 * CLEAN-RUN experiment: forcing/delay macros are rejected outright. */
#if defined(XY_STRIDE)
#if !defined(CACHED_TEST)
#error "XY_STRIDE is a cached-tier experiment (T3/T4); build with CACHED_TEST"
#endif
#if !(defined(TEST_SB) || defined(TEST_MP))
#error "XY_STRIDE supports TEST_SB and TEST_MP only"
#endif
#if defined(SB_Y_STRIDE) || defined(MP_FLAG_UNCACHED) || defined(MP_ROLE_SWAP) || defined(TEST_MP_DEP)
#error "XY_STRIDE cannot combine with SB_Y_STRIDE / MP_FLAG_UNCACHED / MP_ROLE_SWAP / TEST_MP_DEP"
#endif
#if defined(MATRIX_FORCE_00) || defined(MATRIX_FORCE_01) || defined(MATRIX_FORCE_11) ||     defined(MATRIX_FORCE_10_PIPELINE) || defined(SB_DELAY_CORE0_STORE) || defined(SB_DELAY_BEFORE_LOAD)
#error "XY_STRIDE is a clean-run experiment: no forcing or one-sided delay macros"
#endif
#if !defined(DCACHE_LINE_BYTES)
#error "XY_STRIDE requires -DDCACHE_LINE_BYTES=<16|32|64>: pin the real line length (xparameters *_DCACHE_LINE_LEN * 4) before running strided layouts"
#endif

#ifndef XY_STRIDE_SHIFT
#if (DCACHE_LINE_BYTES) == 16u || (DCACHE_LINE_BYTES) == 16
#define XY_STRIDE_SHIFT 4u
#elif (DCACHE_LINE_BYTES) == 32u || (DCACHE_LINE_BYTES) == 32
#define XY_STRIDE_SHIFT 5u
#elif (DCACHE_LINE_BYTES) == 64u || (DCACHE_LINE_BYTES) == 64
#define XY_STRIDE_SHIFT 6u
#else
#error "DCACHE_LINE_BYTES must be 16, 32, or 64"
#endif
#endif

#ifndef XY_X_BANK
#define XY_X_BANK 0x0000u
#endif
#if defined(XY_SAMELINE)
#define XY_Y_BANK XY_X_BANK
#define XY_Y_OFF  0x4u
#else
#ifndef XY_Y_BANK
#define XY_Y_BANK 0x1000u
#endif
#define XY_Y_OFF  0x0u
#endif
#ifndef XY_NITER
#define XY_NITER (0xF00u >> XY_STRIDE_SHIFT)   /* 240 / 120 / 60 */
#endif
#define XY_SPAN (XY_NITER << XY_STRIDE_SHIFT)

/* ---- hard geometry guards: distinct lines, no overlaps, no reserved hits */
#if ((1u << XY_STRIDE_SHIFT) < (DCACHE_LINE_BYTES))
#error "XY_STRIDE_SHIFT smaller than the D-cache line: strided x_i/y_i are NOT on distinct lines"
#endif
#if (XY_NITER) > 256u
#error "XY_NITER exceeds the 256-slot result arrays"
#endif
#if (XY_NITER) < 1u
#error "XY_NITER must be at least 1"
#endif
#if defined(XY_SAMELINE)
#if ((XY_X_BANK) + (XY_SPAN)) > 0x1F00u
#error "XY_STRIDE same-line span overlaps the reserved test-BRAM region at 0x1F00 (WEAK_TEST_TRIG); reduce XY_NITER or XY_STRIDE_SHIFT"
#endif
#else
#if ((XY_X_BANK) + (XY_SPAN)) > (XY_Y_BANK)
#error "XY_STRIDE x bank span overlaps the y bank; reduce XY_NITER/XY_STRIDE_SHIFT or move XY_Y_BANK"
#endif
#if ((XY_Y_BANK) + (XY_SPAN)) > 0x1F00u
#error "XY_STRIDE y bank span overlaps the reserved test-BRAM region at 0x1F00 (WEAK_TEST_TRIG); reduce XY_NITER or XY_STRIDE_SHIFT"
#endif
#endif

static inline uint32_t xy_slot_offset(uint32_t iter)
{
    uint32_t slot;

    slot = iter & 0xFFu;
    return slot << XY_STRIDE_SHIFT;
}

static inline uint32_t xy_x_address(uint32_t iter)
{
    return TEST_BASE + XY_X_BANK + xy_slot_offset(iter);
}

static inline uint32_t xy_y_address(uint32_t iter)
{
    return TEST_BASE + XY_Y_BANK + XY_Y_OFF + xy_slot_offset(iter);
}

#define VAR_X_T(i) REG32(xy_x_address((uint32_t)(i)))
#define VAR_Y_T(i) REG32(xy_y_address((uint32_t)(i)))
#define NITER_RUN (XY_NITER)
#else
#define VAR_X_T(i) VAR_X
#define VAR_Y_T(i) VAR_Y_SB(i)
#define NITER_RUN NITER
#endif

/* run length / loop isolation */
/* Framework/loop isolation. The normal campaign runs all NITER iterations.
 * For framework-effect checks, build with e.g. -DRUN_NITER=1u or choose a
 * single later iteration with -DRUN_START_ITER=<k> -DRUN_NITER=1u. This keeps
 * the same program/harness, but avoids interpreting accumulated loop history
 * as if it were one isolated litmus instance. */
#ifndef RUN_START_ITER
#define RUN_START_ITER 0u
#endif
#ifndef RUN_NITER
#define RUN_NITER NITER_RUN
#endif
#define RUN_END_ITER ((RUN_START_ITER) + (RUN_NITER))
#if (RUN_START_ITER) >= (NITER_RUN)
#error "RUN_START_ITER must be below NITER_RUN"
#endif
#if (RUN_END_ITER) > (NITER_RUN)
#error "RUN_START_ITER + RUN_NITER exceeds NITER_RUN"
#endif

/* Optional line-length assertions for NON-strided cached layouts:
 * -DDCACHE_LINE_BYTES also checks the static placements. */
#if defined(DCACHE_LINE_BYTES)
#if defined(SB_Y_STRIDE) && ((1u << SB_Y_STRIDE_SHIFT) < (DCACHE_LINE_BYTES))
#error "SB_Y_STRIDE_SHIFT smaller than the D-cache line: strided y_i are NOT on distinct lines"
#endif
#if defined(CACHED_TEST) && !defined(XY_STRIDE) && !defined(MP_FLAG_UNCACHED) &&     (Y_OFF >= 8u) && (Y_OFF < (DCACHE_LINE_BYTES))
#error "Y_OFF is inside x's cache line: intended diff-line placement is actually same-line (Y_OFF=0x004 is the declared same-line convention)"
#endif
#endif

/* Clean outcome observers need the markers compiled in. */
#if (defined(STOP_ON_SB_OUTCOME) || defined(STOP_ON_MP_OUTCOME)) && !defined(TRIGGER_ON_WEAK)
#error "STOP_ON_*_OUTCOME requires TRIGGER_ON_WEAK so run_litmus exits on the FACE marker"
#endif
#define BAR0    REG32(SHARED_BASE + 0x010u)
#define BAR1    REG32(SHARED_BASE + 0x014u)
#define DONE0   REG32(SHARED_BASE + 0x020u)
#define DONE1   REG32(SHARED_BASE + 0x024u)
#define RUN_SEED0 REG32(SHARED_BASE + 0x060u)
#define RUN_SEED1 REG32(SHARED_BASE + 0x064u)

#define FENCE_RW_RW() __asm__ volatile("fence rw, rw" ::: "memory")

/* host-provided run seeds */
#if defined(SEED_FROM_HARNESS)
static inline uint32_t mix_run_seed_value(uint32_t default_seed, uint32_t my_id)
{
    uint32_t host_seed;
    uint32_t mixed_seed;

    if (my_id == 0u) {
        host_seed = RUN_SEED0;
    } else {
        host_seed = RUN_SEED1;
    }

    mixed_seed = default_seed ^ host_seed;

    if (mixed_seed == 0u) {
        mixed_seed = 0x1u | my_id;
    }

    return mixed_seed;
}

#define MIX_RUN_SEED(seed, my_id) \
    do {                          \
        (seed) = mix_run_seed_value((seed), (my_id)); \
    } while (0)
#else
#define MIX_RUN_SEED(seed, my_id) do { } while (0)
#endif

/* ---------------- weak-outcome trigger/status ----------------
 * Software trigger for linking ISA-level weak outcomes to bus-level
 * traces. Harness copies live in the uncached harness region so the
 * writes are visible on the harness AXI path.
 *
 *   WEAK_ITER = iteration where weak outcome was detected
 *   WEAK_CODE = which test/outcome fired
 *   WEAK_TRIG = trigger marker (FACE|iter) for ILA trigger/correlation */
#define WEAK_TRIG REG32(SHARED_BASE + 0x030u)
#define WEAK_ITER REG32(SHARED_BASE + 0x034u)
#define WEAK_CODE REG32(SHARED_BASE + 0x038u)
/* Trace-only word between trap state (+0x44..+0x50) and seeds (+0x60/+0x64). */
#define ILA_TRACE_WORD REG32(SHARED_BASE + 0x054u)

#define WEAK_MAGIC 0xFACE0000u

#define WEAK_CODE_SB      0x00000001u
#define WEAK_CODE_MP      0x00000002u
#define WEAK_CODE_MP_DEP  0x00000003u
#define WEAK_CODE_LB      0x00000004u
#define WEAK_CODE_CORR    0x00000005u
#define WEAK_CODE_2P2W    0x00000006u
#define WEAK_CODE_S       0x00000007u
#define WEAK_CODE_R       0x00000008u

/* Test-side trigger marker (cached tiers only).
 * Lives in the TEST BRAM so the write traverses the test-side
 * interconnect, where the DC/test ILA can trigger on
 * AWADDR == 0xC2001F00. Keep X_OFF/Y_OFF well below 0x1F00. */
#if defined(CACHED_TEST)
#define WEAK_TEST_TRIG_ADDR (TEST_BASE + 0x1F00u)
#define WEAK_TEST_TRIG      REG32(WEAK_TEST_TRIG_ADDR)
#endif

/* ---------------- background/noise traffic ----------------
 * Stress variant for unrelated memory traffic. The litmus variables remain
 * X/Y; this helper touches a separate guarded TEST BRAM window to add cache
 * fills, writes, and AXI arbitration pressure. Check any separately
 * configured traffic source for address overlap before combining it.
 *
 * Guardrail: NOISE_BETWEEN_OPS changes the two-thread program by inserting
 * independent accesses between the litmus operations. Label those runs as
 * in-thread stress evidence, not canonical natural litmus frequency. */
#if defined(NOISE_ITERS) && ((NOISE_ITERS) > 0u)
#if !defined(NOISE_BEFORE_BODY) && !defined(NOISE_BETWEEN_OPS) && !defined(NOISE_AFTER_BODY)
#error "NOISE_ITERS is set, but no NOISE_* placement flag is enabled"
#endif
#if !defined(CACHED_TEST)
#error "NOISE_* stress traffic currently targets TEST BRAM; build with CACHED_TEST"
#endif
#if defined(XY_STRIDE)
#error "NOISE_* is not combined with XY_STRIDE in this source: both use test-BRAM placement windows"
#endif
#ifndef NOISE_BASE_OFF
#define NOISE_BASE_OFF 0x1400u
#endif
#ifndef NOISE_WORDS
#define NOISE_WORDS 64u
#endif
#if (NOISE_WORDS) < 1u
#error "NOISE_WORDS must be at least 1"
#endif
#if (NOISE_BASE_OFF) < 0x020u
#error "NOISE_BASE_OFF is too low and may overlap X/Y"
#endif
#if ((NOISE_BASE_OFF) + ((NOISE_WORDS) * 4u)) > 0x1F00u
#error "NOISE window overlaps reserved test-BRAM marker region at +0x1F00"
#endif

LITMUS_ALWAYS_INLINE void noise_traffic(uint32_t iter, uint32_t core)
{
    uint32_t accumulator;
    uint32_t noise_iter;

    accumulator = iter ^ (core << 16);

    for (noise_iter = 0u; noise_iter < (uint32_t)(NOISE_ITERS); noise_iter++) {
        uint32_t noise_index;
        uint32_t noise_address;
        volatile uint32_t *noise_word;

        noise_index = noise_iter;
        noise_index += iter * 17u;
        noise_index += core * 23u;
        noise_index %= (uint32_t)(NOISE_WORDS);

        noise_address = TEST_BASE + (uint32_t)(NOISE_BASE_OFF);
        noise_address += noise_index * 4u;
        noise_word = (volatile uint32_t *)noise_address;

#if defined(NOISE_READS) || !defined(NOISE_WRITES)
        accumulator ^= *noise_word;
#endif

#if defined(NOISE_WRITES)
        {
            uint32_t write_value;

            write_value = 0xA5000000u;
            write_value |= (core & 0xFu) << 20;
            write_value |= (iter & 0xFFu) << 8;
            write_value |= noise_iter & 0xFFu;
            *noise_word = write_value;
        }
#endif
    }

    __asm__ volatile("" :: "r"(accumulator) : "memory");
}
#else
#define noise_traffic(iter, core) do { } while (0)
#endif

/* ---------------- explicit Z/W independent accesses ----------------
 * Cleaner stress/control layer than the bulk NOISE loop. Z and W are
 * independent per-iteration words in TEST BRAM, away from X/Y and the
 * weak marker. Core0 is the designated writer for Z_i; core1 is the
 * designated writer for W_i. Optional reads of the other core's word create
 * separate-address traffic without touching the litmus variables. This
 * directly models "extra independent locations" while keeping the X/Y
 * outcome encoding unchanged.
 *
 * These accesses are inserted into the two test threads. A separate
 * background AXI master leaves those instruction streams unchanged.
 *
 * Init discipline: both cores call zw_init() during the init phase before the
 * existing phase+2 start barrier. Body Z/W traffic therefore starts from a
 * synchronized known zero. */
#if defined(ZW_BEFORE_BODY) || defined(ZW_BETWEEN_OPS) || defined(ZW_AFTER_BODY) || defined(ZW_INIT_ONLY)
#if !defined(CACHED_TEST)
#error "ZW_* stress traffic targets TEST BRAM; build with CACHED_TEST"
#endif
#if defined(NOISE_ITERS) && ((NOISE_ITERS) > 0u)
#error "Do not combine ZW_* and NOISE_* in one build; use Z/W first, bulk NOISE as a separate escalation"
#endif
#if defined(XY_STRIDE)
#error "ZW_* is not combined with XY_STRIDE in this source: both use test-BRAM placement windows"
#endif
#ifndef ZW_Z_OFF
#define ZW_Z_OFF 0x1400u
#endif
#ifndef ZW_W_OFF
#define ZW_W_OFF 0x1800u
#endif

#if defined(ZW_WORD_STRIDE) && defined(ZW_LINE_STRIDE)
#error "Choose only one Z/W placement: ZW_WORD_STRIDE or ZW_LINE_STRIDE"
#endif
#if defined(ZW_LINE_STRIDE) && !defined(DCACHE_LINE_BYTES)
#error "ZW_LINE_STRIDE requires -DDCACHE_LINE_BYTES=<16|32|64>"
#endif
#if defined(ZW_STRIDE_BYTES) && (defined(ZW_WORD_STRIDE) || defined(ZW_LINE_STRIDE))
#error "ZW_STRIDE_BYTES is a manual override; do not combine with ZW_WORD_STRIDE/ZW_LINE_STRIDE"
#endif
#ifndef ZW_STRIDE_BYTES
#if defined(ZW_LINE_STRIDE)
#define ZW_STRIDE_BYTES (DCACHE_LINE_BYTES)
#else
#define ZW_STRIDE_BYTES 4u
#endif
#endif
#if (ZW_STRIDE_BYTES) < 4u
#error "ZW_STRIDE_BYTES must be at least 4"
#endif
#if ((ZW_STRIDE_BYTES) % 4u) != 0u
#error "ZW_STRIDE_BYTES must be word-aligned"
#endif
#if (RUN_END_ITER) > 256u
#error "Z/W indexing uses result-slot iteration numbers; RUN_END_ITER must not exceed 256"
#endif
#define ZW_FIRST_DELTA ((RUN_START_ITER) * (ZW_STRIDE_BYTES))
#define ZW_LAST_DELTA  (((RUN_END_ITER) - 1u) * (ZW_STRIDE_BYTES))
#define ZW_WINDOW_END  ((ZW_LAST_DELTA) + 4u)
#define ZW_Z_FIRST     ((ZW_Z_OFF) + (ZW_FIRST_DELTA))
#define ZW_Z_END       ((ZW_Z_OFF) + (ZW_WINDOW_END))
#define ZW_W_FIRST     ((ZW_W_OFF) + (ZW_FIRST_DELTA))
#define ZW_W_END       ((ZW_W_OFF) + (ZW_WINDOW_END))
#if ((ZW_Z_OFF) < 0x020u) || ((ZW_W_OFF) < 0x020u)
#error "ZW offsets are too low and may overlap X/Y"
#endif
#if (ZW_Z_END) > 0x1F00u
#error "Z window overlaps reserved test-BRAM marker region at +0x1F00"
#endif
#if (ZW_W_END) > 0x1F00u
#error "W window overlaps reserved test-BRAM marker region at +0x1F00"
#endif
#if ((ZW_Z_FIRST) < (ZW_W_END)) && ((ZW_W_FIRST) < (ZW_Z_END))
#error "Z and W windows overlap"
#endif

#if (defined(ZW_READ_ONLY) + defined(ZW_WRITE_ONLY) + defined(ZW_READ_WRITE)) > 1
#error "Choose one Z/W traffic mix: ZW_READ_ONLY, ZW_WRITE_ONLY, or ZW_READ_WRITE"
#endif
#if (defined(ZW_READ_ONLY) || defined(ZW_WRITE_ONLY) || defined(ZW_READ_WRITE)) && \
    (defined(ZW_WRITE_OWN) || defined(ZW_READ_OTHER))
#error "Use either high-level ZW_READ_ONLY/WRITE_ONLY/READ_WRITE or legacy ZW_WRITE_OWN/READ_OTHER, not both"
#endif
#if defined(ZW_READ_ONLY)
#define ZW_DO_READ 1
#elif defined(ZW_WRITE_ONLY)
#define ZW_DO_WRITE 1
#elif defined(ZW_READ_WRITE)
#define ZW_DO_READ 1
#define ZW_DO_WRITE 1
#else
#if defined(ZW_WRITE_OWN) || (!defined(ZW_READ_OTHER) && !defined(ZW_WRITE_OWN))
#define ZW_DO_WRITE 1
#endif
#if defined(ZW_READ_OTHER)
#define ZW_DO_READ 1
#endif
#endif

static inline uint32_t zw_z_address(uint32_t iter)
{
    return TEST_BASE + (ZW_Z_OFF) + ((ZW_STRIDE_BYTES) * iter);
}

static inline uint32_t zw_w_address(uint32_t iter)
{
    return TEST_BASE + (ZW_W_OFF) + ((ZW_STRIDE_BYTES) * iter);
}

#define VAR_Z_T(i) REG32(zw_z_address((uint32_t)(i)))
#define VAR_W_T(i) REG32(zw_w_address((uint32_t)(i)))

LITMUS_ALWAYS_INLINE void zw_init(uint32_t iter, uint32_t core)
{
    if (core == 0u) {
        VAR_Z_T(iter) = 0u;
    } else {
        VAR_W_T(iter) = 0u;
    }
    __asm__ volatile("" ::: "memory");
}

LITMUS_ALWAYS_INLINE void zw_traffic(uint32_t iter, uint32_t core)
{
    uint32_t sink;

    sink = iter ^ (core << 12);

#if defined(ZW_DO_WRITE)
    {
        uint32_t write_value;

        write_value = iter & 0xFFFFu;

        if (core == 0u) {
            VAR_Z_T(iter) = 0x5A000000u | write_value;
        } else {
            VAR_W_T(iter) = 0xA5000000u | write_value;
        }
    }
#endif

#if defined(ZW_DO_READ)
    if (core == 0u) {
        sink ^= VAR_W_T(iter);
    } else {
        sink ^= VAR_Z_T(iter);
    }
#endif
    __asm__ volatile("" :: "r"(sink) : "memory");
}
#else
#define zw_init(iter, core) do { } while (0)
#define zw_traffic(iter, core) do { } while (0)
#endif

/* ---------------- cache-line priming without touching X/Y ----------------
 * These flags read a neighbor word in the same cache line as X or Y,
 * without reading X/Y themselves. This changes cache
 * residency/line-fill state while leaving the litmus variables' values
 * untouched.
 *
 * Requires DCACHE_LINE_BYTES so the same-line claim is explicit. Defaults:
 *   PRIME_X_LINE_NEIGHBOR reads TEST_BASE + X_OFF + 4
 *   PRIME_Y_LINE_NEIGHBOR reads TEST_BASE + Y_OFF + 4
 * Legacy spellings PRIME_X_LINE_VIA_Z / PRIME_Y_LINE_VIA_W are aliases kept
 * for existing runbooks. Do not use X-neighbor priming when Y_OFF == X_OFF+4;
 * that would read Y. */
#if defined(PRIME_X_LINE_VIA_Z)
#define PRIME_X_LINE_NEIGHBOR 1
#endif
#if defined(PRIME_Y_LINE_VIA_W)
#define PRIME_Y_LINE_NEIGHBOR 1
#endif
#if defined(PRIME_X_LINE_NEIGHBOR) || defined(PRIME_Y_LINE_NEIGHBOR)
#if !defined(CACHED_TEST)
#error "PRIME_*_LINE_NEIGHBOR is a cached-tier experiment; build with CACHED_TEST"
#endif
#if defined(XY_STRIDE)
#error "PRIME_*_LINE_NEIGHBOR is not combined with XY_STRIDE; use fixed placement"
#endif
#if !defined(DCACHE_LINE_BYTES)
#error "PRIME_*_LINE_NEIGHBOR requires -DDCACHE_LINE_BYTES=<16|32|64>"
#endif
#if (DCACHE_LINE_BYTES) < 8u
#error "DCACHE_LINE_BYTES too small for neighbor-word priming"
#endif
#if defined(PRIME_X_LINE_NEIGHBOR) && !defined(MP_FLAG_UNCACHED) && (((X_OFF) + 4u) == (Y_OFF))
#error "PRIME_X_LINE_NEIGHBOR would read Y because Y_OFF == X_OFF+4"
#endif
#if defined(PRIME_X_LINE_NEIGHBOR) && (((X_OFF) / (DCACHE_LINE_BYTES)) != (((X_OFF) + 4u) / (DCACHE_LINE_BYTES)))
#error "X neighbor is not in the same D-cache line as X; adjust X_OFF or DCACHE_LINE_BYTES"
#endif
#if defined(PRIME_Y_LINE_NEIGHBOR) && (((Y_OFF) + 4u) == (X_OFF))
#error "PRIME_Y_LINE_NEIGHBOR would read X because X_OFF == Y_OFF+4"
#endif
#if defined(PRIME_Y_LINE_NEIGHBOR) && defined(MP_FLAG_UNCACHED)
#error "PRIME_Y_LINE_NEIGHBOR cannot prime uncached MP_FLAG_UNCACHED Y"
#endif
#if defined(PRIME_Y_LINE_NEIGHBOR) && (((Y_OFF) + 4u) >= 0x1F00u)
#error "PRIME_Y_LINE_NEIGHBOR neighbor overlaps reserved test-BRAM marker region"
#endif
#if defined(PRIME_Y_LINE_NEIGHBOR) && (((Y_OFF) / (DCACHE_LINE_BYTES)) != (((Y_OFF) + 4u) / (DCACHE_LINE_BYTES)))
#error "Y neighbor is not in the same D-cache line as Y; adjust Y_OFF or DCACHE_LINE_BYTES"
#endif
#if defined(PRIME_Y_LINE_NEIGHBOR) && defined(SB_Y_STRIDE) && (((1u << SB_Y_STRIDE_SHIFT) % (DCACHE_LINE_BYTES)) != 0u)
#error "SB_Y_STRIDE must advance by a whole number of D-cache lines for Y-neighbor priming"
#endif

#if defined(PRIME_ON_CORE0)
#define PRIME_ENABLE_CORE0 1u
#else
#define PRIME_ENABLE_CORE0 0u
#endif
#if defined(PRIME_ON_CORE1)
#define PRIME_ENABLE_CORE1 1u
#else
#define PRIME_ENABLE_CORE1 0u
#endif
#if !defined(PRIME_ON_CORE0) && !defined(PRIME_ON_CORE1)
#undef PRIME_ENABLE_CORE0
#undef PRIME_ENABLE_CORE1
#define PRIME_ENABLE_CORE0 1u
#define PRIME_ENABLE_CORE1 1u
#endif

LITMUS_ALWAYS_INLINE void prime_lines_via_neighbors(uint32_t iter, uint32_t core)
{
    uint32_t sink;

    if (!(((core == 0u) && (PRIME_ENABLE_CORE0 != 0u)) ||
          ((core == 1u) && (PRIME_ENABLE_CORE1 != 0u)))) {
        return;
    }
    sink = iter ^ (core << 4);
#if defined(PRIME_X_LINE_NEIGHBOR)
    sink ^= REG32(TEST_BASE + (X_OFF) + 4u);
#endif
#if defined(PRIME_Y_LINE_NEIGHBOR)
#if defined(SB_Y_STRIDE)
    sink ^= REG32(Y_ADDR + (((uint32_t)(iter) & 0xFFu) << SB_Y_STRIDE_SHIFT) + 4u);
#else
    sink ^= REG32(TEST_BASE + (Y_OFF) + 4u);
#endif
#endif
    __asm__ volatile("" :: "r"(sink) : "memory");
}
#else
#define prime_lines_via_neighbors(iter, core) do { } while (0)
#endif

/* Timing-only companion for Z/W and NOISE. Use it as a control: if a Z/W run
 * changes outcomes but a same-placement CONTROL_LOOP run does not, the effect
 * is more plausibly memory/cache/interconnect traffic than loop time alone. */
#if defined(CONTROL_LOOP_ITERS) && ((CONTROL_LOOP_ITERS) > 0u)
#if !defined(CONTROL_LOOP_BEFORE_BODY) && !defined(CONTROL_LOOP_BETWEEN_OPS) && !defined(CONTROL_LOOP_AFTER_BODY)
#error "CONTROL_LOOP_ITERS is set, but no CONTROL_LOOP_* placement flag is enabled"
#endif
LITMUS_ALWAYS_INLINE void control_loop(uint32_t iter, uint32_t core)
{
    (void)iter;
    (void)core;
    __asm__ volatile(
        "li   t0, %0\n\t"
        "1:\n\t"
        "addi t0, t0, -1\n\t"
        "nop\n\t"
        "bnez t0, 1b\n\t"
        :
        : "i" ((uint32_t)(CONTROL_LOOP_ITERS))
        : "t0");
    __asm__ volatile("" ::: "memory");
}
#else
#define control_loop(iter, core) do { } while (0)
#endif

#if defined(TRIGGER_ON_WEAK)

/* Test-side marker: only from an UNCACHED core on a cached tier
 * (i.e. T3 core1, the observer). A cached core's write may allocate
 * in its own D-cache and never reach the bus at detection time, so
 * cached cores (T4 both cores) skip it; they still write the
 * harness-side markers, which are uncached everywhere. */
#if defined(CACHED_TEST) && !defined(USE_XIL_CACHE)
static inline void report_weak_to_test_bram(uint32_t iter)
{
    uint32_t marker_value;

    marker_value = WEAK_MAGIC | (iter & 0xFFFFu);
    WEAK_TEST_TRIG = marker_value;
}

static inline void clear_weak_test_marker(void)
{
    WEAK_TEST_TRIG = 0u;
}

#define REPORT_WEAK_TESTSIDE(iter) report_weak_to_test_bram((uint32_t)(iter))
#define CLEAR_WEAK_TESTSIDE() clear_weak_test_marker()
#else
#define REPORT_WEAK_TESTSIDE(iter) do { } while (0)
#define CLEAR_WEAK_TESTSIDE()      do { } while (0)
#endif

static inline void report_weak(uint32_t iter, uint32_t code)
{
    uint32_t marker_value;

    marker_value = WEAK_MAGIC | (iter & 0xFFFFu);

    WEAK_ITER = iter;
    WEAK_CODE = code;

    FENCE_RW_RW();

    REPORT_WEAK_TESTSIDE(iter);
    WEAK_TRIG = marker_value;

    FENCE_RW_RW();
}

#define REPORT_WEAK(iter, code) report_weak((uint32_t)(iter), (uint32_t)(code))

#else
#define REPORT_WEAK(iter, code)    do { } while (0)
#define CLEAR_WEAK_TESTSIDE()      do { } while (0)
#endif

#if defined(STOP_ON_WEAK)
static inline void stop_forever(void)
{
    while (1) {
        /* Intentionally stop here so XSCT/ILA can inspect the state. */
    }
}

#define STOP_IF_WEAK() stop_forever()
#else
#define STOP_IF_WEAK() do { } while (0)
#endif

/* result arrays RES0/RES1 and final-state arrays FINX/FINY */
static inline uint32_t result0_address(uint32_t iter)
{
    return SHARED_BASE + 0x400u + (4u * iter);
}

static inline uint32_t result1_address(uint32_t iter)
{
    return SHARED_BASE + 0x800u + (4u * iter);
}

static inline uint32_t final_x_address(uint32_t iter)
{
    return SHARED_BASE + 0xC00u + (4u * iter);
}

static inline uint32_t final_y_address(uint32_t iter)
{
    return SHARED_BASE + 0x1000u + (4u * iter);
}

#define RES0(i) REG32(result0_address((uint32_t)(i)))
#define RES1(i) REG32(result1_address((uint32_t)(i)))
#define FINX(i) REG32(final_x_address((uint32_t)(i)))
#define FINY(i) REG32(final_y_address((uint32_t)(i)))

#ifndef MATRIX_DELAY_SHORT
#define MATRIX_DELAY_SHORT 64u
#endif

#ifndef MATRIX_DELAY_LONG
#define MATRIX_DELAY_LONG 4096u
#endif

static inline void matrix_delay(volatile uint32_t n)
{
    while (n != 0u) {
        __asm__ volatile("nop" ::: "memory");
        n--;
    }
}

/* ---------------- optional trap reporter ----------------
 * CBO smoke-test aid. If a cache-maintenance instruction is illegal on this
 * MBV instance, record the machine-trap CSRs in harness BRAM and mark the
 * run as TRAP instead of falling into the BSP trap loop until timeout.
 *
 *   +0x044 TRAP_MCAUSE
 *   +0x048 TRAP_MEPC
 *   +0x04C TRAP_MTVAL
 *   +0x050 TRAP_CORE
 */
#define TRAP_MAGIC  0xBAD70000u
#define TRAP_MCAUSE REG32(SHARED_BASE + 0x044u)
#define TRAP_MEPC   REG32(SHARED_BASE + 0x048u)
#define TRAP_MTVAL  REG32(SHARED_BASE + 0x04Cu)
#define TRAP_CORE   REG32(SHARED_BASE + 0x050u)

#if defined(TRAP_REPORT)
static volatile uint32_t trap_report_core_id;

static void trap_report_handler(void) __attribute__((interrupt("machine"), aligned(4)));
static void trap_report_handler(void)
{
    uintptr_t mcause;
    uintptr_t mepc;
    uintptr_t mtval;
    uint32_t core = trap_report_core_id & 0xFFFFu;
    uint32_t mark = TRAP_MAGIC | core;

    __asm__ volatile("csrr %0, mcause" : "=r"(mcause));
    __asm__ volatile("csrr %0, mepc"   : "=r"(mepc));
    __asm__ volatile("csrr %0, mtval"  : "=r"(mtval));

    TRAP_MCAUSE = (uint32_t)mcause;
    TRAP_MEPC = (uint32_t)mepc;
    TRAP_MTVAL = (uint32_t)mtval;
    TRAP_CORE = core;
    WEAK_TRIG = mark;
    if (core == 0u) {
        DONE0 = mark;
    } else {
        DONE1 = mark;
    }
    FENCE_RW_RW();
    while (1) { }
}

static inline void trap_report_setup(uint32_t core)
{
    uintptr_t handler = (uintptr_t)&trap_report_handler;

    trap_report_core_id = core;
    TRAP_MCAUSE = 0u;
    TRAP_MEPC = 0u;
    TRAP_MTVAL = 0u;
    TRAP_CORE = core;
    __asm__ volatile("csrw mtvec, %0" :: "r"(handler) : "memory");
}

#define TRAP_REPORT_SETUP() trap_report_setup(MY_ID)
#else
#define TRAP_REPORT_SETUP() do { } while (0)
#endif

#ifdef USE_FENCE
#define TEST_FENCE() FENCE_RW_RW()
#else
#define TEST_FENCE() __asm__ volatile("" ::: "memory")
#endif

/* MP dependency-load helper */
/* Address-dependent load of x, dependency carried through r1 (MP_DEP).
 * xor r1,r1 == 0 but is a *syntactic* dependency (RVWMO tracks syntax,
 * not values) so the two loads are in PPO. Done in asm so the compiler
 * cannot optimise the dependency away. */
static inline uint32_t load_x_dep_on(uint32_t flag_value)
{
    uint32_t data_value;
    uint32_t dependent_address;

    __asm__ volatile(
        "xor %1, %2, %2\n\t"
        "add %1, %1, %3\n\t"
        "lw  %0, 0(%1)\n\t"
        : "=&r"(data_value), "=&r"(dependent_address)
        : "r"(flag_value), "r"(X_ADDR)
        : "memory");

    return data_value;
}

/* Fenced two-hart barrier on monotonic counters in uncached harness memory.
 * Each hart publishes its phase and waits for the other to reach it. */
static inline void barrier(uint32_t my_id, uint32_t phase)
{
    FENCE_RW_RW();

    if (my_id == 0u) {
        BAR0 = phase;

        while (BAR1 < phase) {
            /* Wait for hart 1 to reach this phase. */
        }
    } else {
        BAR1 = phase;

        while (BAR0 < phase) {
            /* Wait for hart 0 to reach this phase. */
        }
    }

    FENCE_RW_RW();
}

/* Pseudo-random per-iteration skew (Galois LFSR) so the two harts' racing
 * windows sweep across each other over many iterations.
 * JITTER_MASK (default 0x3F, the historical value) bounds the random skew.
 * Widening it (e.g. -DJITTER_MASK=0x1FFu on BOTH cores) is a SYMMETRIC
 * fairness parameter - both harts draw from independent LFSRs - intended
 * to overlap racing windows when the fixed barrier start-skew exceeds the
 * default jitter range. It is not a one-sided
 * forced delay, but always record the mask used with the run's evidence. */
#ifndef JITTER_MASK
#define JITTER_MASK 0x3Fu
#endif
static inline uint32_t lfsr_next(uint32_t current_seed)
{
    uint32_t next_seed;

    next_seed = current_seed >> 1;

    if ((current_seed & 1u) != 0u) {
        next_seed ^= 0x80200003u;
    }

    return next_seed;
}

static inline uint32_t rd_mcycle_lo(void)
{
    uint32_t v;
    __asm__ volatile("csrr %0, mcycle" : "=r"(v));
    return v;
}

static inline void jitter(uint32_t *seed)
{
    volatile uint32_t delay_count;
    uint32_t delay_limit;

    *seed = lfsr_next(*seed);
    delay_limit = *seed & (JITTER_MASK);

    for (delay_count = 0u; delay_count < delay_limit; delay_count++) {
        /* Busy wait to create a small symmetric timing skew. */
    }
}

static inline void jitter_measured(uint32_t *seed,
                                   uint32_t *delay_limit_out,
                                   uint32_t *cycle_count_out)
{
    volatile uint32_t delay_count;
    uint32_t delay_limit;
    uint32_t t0;
    uint32_t t1;

    *seed = lfsr_next(*seed);
    delay_limit = *seed & (JITTER_MASK);

    t0 = rd_mcycle_lo();
    for (delay_count = 0u; delay_count < delay_limit; delay_count++) {
        /* Same busy wait body as jitter(), timed for calibration only. */
    }
    t1 = rd_mcycle_lo();

    *delay_limit_out = delay_limit;
    *cycle_count_out = t1 - t0;
}

#endif /* LITMUS_COMMON_H */
