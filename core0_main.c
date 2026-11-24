/*
 * MicroBlaze V hart 0 experiment loops.
 * Select with -DTEST_SB / _MP / _MP_DEP / _LB / _CORR / _2P2W / _S / _R
 * plus optional -DUSE_FENCE and -DTWO_BRAM. Hart 0 also resets x,y each
 * iteration and records final x/y values after each round.
 *
 * Cached-tier extras:
 *   -DFLUSH_Y_ONLY_AFTER_FLAG  (MP/MP_DEP producer) flush ONLY y's cache
 *     line immediately after writing the flag. Publishes flag before
 *     data: the directed maintenance-ordering experiment.
 */
#include "litmus_common.h"
#define MY_ID 0u

#if defined(MP_EXPLICIT_PRIME_PROTOCOL)
#if !defined(USE_XIL_CACHE) || !defined(DISABLE_CACHE_MAINT)
#error "MP explicit-prime consumer must keep the write-back cache enabled with maintenance disabled"
#endif

/* Stable disassembly boundaries for the complete consumer pre-body window. */
__attribute__((noinline, used))
void mp_prime_protocol_consumer_window_begin(void)
{
    __asm__ volatile("" ::: "memory");
}

__attribute__((noinline, used))
void mp_prime_protocol_consumer_window_end(void)
{
    __asm__ volatile("" ::: "memory");
}
#endif

#if defined(MP_PRIME_X_READ)
/*
 * Keep the explicit prime visible as its own disassembly symbol. The volatile
 * VAR_X access is the treatment; no value, delay, or outcome is injected.
 */
__attribute__((noinline, used))
uint32_t mp_prime_x_read_once(void)
{
    return VAR_X;
}
#endif

#if defined(MP_PRIME_WAIT_FOR_FLAG)
#ifndef WAIT_CAP
#define WAIT_CAP 200000u
#endif

/*
 * Bounded uncached publication wait, matching the WRC terminal-observer
 * construction. Every observation is a genuine volatile load of Y. The
 * returned value is the final load result; timeout does not manufacture 1.
 */
__attribute__((noinline, used))
uint32_t mp_prime_wait_for_flag_bounded(void)
{
    uint32_t flag_value;
    uint32_t wait_count;

    flag_value = 0u;
    wait_count = 0u;
    while ((flag_value != 1u) && (wait_count < (uint32_t)WAIT_CAP)) {
        flag_value = VAR_Y;
        wait_count++;
    }
    return flag_value;
}
#endif

int main(void)
{
    uint32_t i;
    uint32_t seed;

    /* core0/P0 program starts here */
    seed = 0xC0FFEE01u;

    CACHE_SETUP();
    DONE0 = 0;
    MIX_RUN_SEED(seed, MY_ID);

    #if defined(TRIGGER_ON_WEAK)
    /* clear weak/ILA marker before the run */
    WEAK_TRIG = 0u;
    WEAK_ITER = 0xFFFFFFFFu;
    WEAK_CODE = 0u;
    FENCE_RW_RW();
    #endif

    for (i = RUN_START_ITER; i < RUN_END_ITER; i++) {   /* NITER unless narrowed */
        uint32_t phase;

        /* one loop iteration = one result row */
        phase = i * 4u;
#if defined(MEASURE_SB_CYCLES) && defined(TEST_SB)
        uint32_t sb_t_store = 0u;
        uint32_t sb_t_body = 0u;
#endif
#if defined(MEASURE_JITTER_CYCLES)
        uint32_t jitter_delay_limit = 0u;
        uint32_t jitter_cycle_count = 0u;
#endif

        barrier(MY_ID, phase + 1u);
        /* reset X/Y for this iteration */
#if defined(MP_EXPLICIT_PRIME_PROTOCOL)
        /*
         * Deliberately touch neither X nor Y. The uncached producer owns both
         * zeroing stores, so this cached consumer has no resident X line
         * before the optional explicit load.
         */
#elif defined(XY_STRIDE)
#if defined(TEST_MP)
        /* MP producer owns both variables. */
        VAR_X_T(i) = 0;
        VAR_Y_T(i) = 0;
#else
        VAR_X_T(i) = 0;     /* SB: core1 zeroes y_i; never touch it here */
#endif
#elif defined(SB_Y_STRIDE)
        VAR_X = 0;          /* core1 zeroes the fresh y_i line; core0 must
                             * not touch it (would poison its D-cache).   */
#else
        VAR_X = 0;
        VAR_Y = 0;
#endif
        zw_init(i, MY_ID);
#if defined(MP_EXPLICIT_PRIME_PROTOCOL)
        barrier(MY_ID, phase + 2u);           /* uncached init is complete */
        mp_prime_protocol_consumer_window_begin();
#if defined(MP_PRIME_X_READ)
        /* The consumer's first X access: a genuine volatile line-fill load. */
        {
            volatile uint32_t prime;

            prime = mp_prime_x_read_once();
            (void)prime;
        }
#endif
        barrier(MY_ID, phase + 3u);           /* optional prime is complete */
        mp_prime_protocol_consumer_window_end();
#else
        TEST_FLUSH();       /* cached tiers: push the zeros to memory */
        FENCE_RW_RW();

        barrier(MY_ID, phase + 2u);           /* start line */
#endif
        /* start barrier, then randomized jitter */
#if defined(MEASURE_JITTER_CYCLES)
        jitter_measured(&seed, &jitter_delay_limit, &jitter_cycle_count);
#else
        jitter(&seed);
#endif
        prime_lines_via_neighbors(i, MY_ID);
        /* optional priming / ZW / noise hooks */
#if defined(CONTROL_LOOP_BEFORE_BODY)
        control_loop(i, MY_ID);
#endif
#if defined(ZW_BEFORE_BODY)
        zw_traffic(i, MY_ID);
#endif
#if defined(NOISE_BEFORE_BODY)
        noise_traffic(i, MY_ID);
#endif

        /* ======================= test body ======================= */
#if defined(TEST_COUNTER)
        {
            uint32_t value;

            value = 0xC0000000u | i;
            VAR_X = value;
            RES0(i) = value;
        }
#elif defined(TEST_MP) || defined(TEST_MP_DEP)
#if defined(MP_ROLE_SWAP)
#if defined(MP_EXPLICIT_PRIME_PROTOCOL)
        /* Explicit-prime protocol: the uncached producer initialized X/Y.
         * PRIME optionally filled X=0 before the prime-done barrier;
         * NOPRIME did not touch X before this body. */
#else
        /* Legacy directed mechanism (T3): the cached consumer's own init
         * store leaves x=0 resident in its D-cache. */
#endif
        {
            uint32_t flag_value;
            uint32_t data_value;

#if defined(MP_PRIME_WAIT_FOR_FLAG)
            /* Wait for real uncached publication, then sample primed X. */
            flag_value = mp_prime_wait_for_flag_bounded();
#else
            flag_value = VAR_Y;       /* read flag (uncached harness slot) */
#endif
            TEST_FENCE();             /* consumer fence */
#if defined(MP_INVALIDATE_X_BEFORE_DATA) && defined(USE_XIL_CACHE)
            Xil_DCacheInvalidateRange((INTPTR)X_ADDR, 4u);
            FENCE_RW_RW();
#endif
            data_value = VAR_X;       /* read data (may hit stale D-cache) */

            RES0(i) = (flag_value << 16) | (data_value & 0xFFFFu);

#if defined(ILA_TRACE_MARK_ITER)
            /*
             * Trace-only post-outcome marker on an uncached harness word.
             * The selected row's reads and RES0 store are already complete.
             */
            if (i == (uint32_t)(ILA_TRACE_MARK_ITER)) {
                FENCE_RW_RW();
                ILA_TRACE_WORD = 0xC0DE0000u | (i & 0xFFFFu);
                FENCE_RW_RW();
            }
#endif

            if ((flag_value == 1u) && (data_value == 0u)) {
                REPORT_WEAK(i, WEAK_CODE_MP);
                STOP_IF_WEAK();
            }
        }
#else
#if defined(MATRIX_FORCE_00)
        /* Force early consumer window: let core1 sample before producer writes. */
        matrix_delay(MATRIX_DELAY_LONG);
#endif
#if defined(MATRIX_FORCE_10_PIPELINE)
        /* Directed schedule: delay before X=1 so the consumer can sample
         * X=0 before its later Y read. */
        matrix_delay(MATRIX_DELAY_LONG / 2u);
#endif

        VAR_X_T(i) = 1;       /* data  */

#if defined(CONTROL_LOOP_BETWEEN_OPS)
        control_loop(i, MY_ID);
#endif
#if defined(ZW_BETWEEN_OPS)
        zw_traffic(i, MY_ID);
#endif
#if defined(NOISE_BETWEEN_OPS)
        noise_traffic(i, MY_ID);
#endif

#if defined(MATRIX_FORCE_01)
        /* Force mid-producer window: X visible/issued before Y is written. */
        matrix_delay(MATRIX_DELAY_LONG);
#endif

        TEST_FENCE();         /* producer fence (w,w suffices)        */
        VAR_Y_T(i) = 1;       /* flag  */
#if defined(FLUSH_Y_ONLY_AFTER_FLAG)
        /* Directed maintenance ordering: publish ONLY the flag's line.
         * x stays dirty in this core's D-cache. Different-line builds
         * predict core1 sees (rf=1, rd=0); same-line builds predict
         * suppression because y's line carries x too. */
        FLUSH_ADDR(Y_ADDR, 4u);
        FENCE_RW_RW();
#endif
        RES0(i) = 0xAAAA0000u;
#endif /* MP_ROLE_SWAP */
#elif defined(TEST_LB)
        {
            uint32_t loaded_y;

            loaded_y = VAR_Y;
            TEST_FENCE();
            VAR_X = 1;
            RES0(i) = loaded_y;
        }
#elif defined(TEST_CORR)
        VAR_X = 1;            /* lone writer                          */
        RES0(i) = 0xAAAA0000u;
#elif defined(TEST_2P2W)
        VAR_X = 1;
        TEST_FENCE();
        VAR_Y = 2;
        RES0(i) = 0xAAAA0000u;
#elif defined(TEST_S)
        VAR_X = 2;
        TEST_FENCE();
        VAR_Y = 1;
        RES0(i) = 0xAAAA0000u;
#elif defined(TEST_R)
        VAR_X = 1;
        TEST_FENCE();
        VAR_Y = 1;
        RES0(i) = 0xAAAA0000u;
#else /* TEST_SB */
        {
            uint32_t loaded_y;
            /* SB body on core0: X=1; r0=Y; RES0=r0 */
#if defined(MEASURE_SB_CYCLES)
            uint32_t t0;
            uint32_t t1;
            uint32_t t2;
#endif
#if defined(SB_DELAY_CORE0_STORE)
            /* Directed schedule bias: delay this core's store so core1's
             * load of x samples first. Without SB_Y_STRIDE this yields 00
             * (stale-y read); with SB_Y_STRIDE it yields 10 (fresh y=1). */
            matrix_delay(SB_DELAY_CORE0_STORE);
#endif
#if defined(MEASURE_SB_CYCLES)
            t0 = rd_mcycle_lo();
#endif
            VAR_X_T(i) = 1;     /* store x */
#if defined(MEASURE_SB_CYCLES)
            t1 = rd_mcycle_lo();
#endif
            TEST_FENCE();
#if defined(SB_DELAY_BEFORE_LOAD)
            /* Directed: let the other core's store land before loading. */
            matrix_delay(SB_DELAY_BEFORE_LOAD);
#endif
#if defined(CONTROL_LOOP_BETWEEN_OPS)
            control_loop(i, MY_ID);
#endif
#if defined(ZW_BETWEEN_OPS)
            zw_traffic(i, MY_ID);
#endif
#if defined(NOISE_BETWEEN_OPS)
            noise_traffic(i, MY_ID);
#endif
            loaded_y = VAR_Y_T(i);    /* load y, fresh line if strided */
#if defined(MEASURE_SB_CYCLES)
            /* posting probe timing values */
            t2 = rd_mcycle_lo();
            sb_t_store = t1 - t0;
            sb_t_body = t2 - t0;
#endif
            RES0(i) = loaded_y;
        }
#endif
        /* ========================================================= */

#if defined(CONTROL_LOOP_AFTER_BODY)
        control_loop(i, MY_ID);
#endif
#if defined(ZW_AFTER_BODY)
        zw_traffic(i, MY_ID);
#endif
#if defined(NOISE_AFTER_BODY)
        noise_traffic(i, MY_ID);
#endif

#if defined(TEST_FLUSH_AFTER_BODY)
        TEST_FLUSH();       /* cached tiers: push this core's test writes */
        FENCE_RW_RW();
#endif

#if defined(MP_EXPLICIT_PRIME_PROTOCOL)
        barrier(MY_ID, phase + 4u);           /* protocol finish line */
#else
        barrier(MY_ID, phase + 3u);           /* finish line */
#endif

        /* final-state / timing bookkeeping */
        /* record final state (hart 1 is parked at next start barrier) */
#if defined(MEASURE_JITTER_CYCLES)
        FINX(i) = jitter_delay_limit;
        FINY(i) = jitter_cycle_count;
#elif defined(MEASURE_SB_CYCLES) && defined(TEST_SB)
        FINX(i) = sb_t_store;
        FINY(i) = sb_t_body;
#else
        TEST_INVALIDATE();  /* cached tiers: don't record stale lines  */
        FINX(i) = VAR_X_T(i);
#if defined(SB_NO_FINY_READ)
        /* Natural-stride hygiene: this core must never LOAD y, not even
         * for bookkeeping - the miss fill would leave y's line resident
         * in its D-cache (with DISABLE_CACHE_MAINT it can never be
         * evicted deliberately, and could survive into later runs).
         * Core1 (uncached) records FINY instead; core0 does not write
         * FINY at all in this mode. */
#else
        FINY(i) = VAR_Y_T(i);    /* == VAR_Y unless strided; on cached
                                  * no-maint cores this read can be stale */
#endif
#endif
#if defined(STOP_ON_SB_OUTCOME) && defined(TEST_SB)
        /* Clean outcome observer. Inspects the ALREADY-COMPUTED results in
         * the uncached harness: RES0(i) written by this core in the body,
         * RES1(i) by core1 before the finish barrier (fenced). It writes
         * only the WEAK_* markers and stops; it cannot create, modify, or
         * force an outcome. Encoding ((RES0!=0)<<1)|(RES1!=0):
         * 00->0  01->1  10->2  11->3. Core1 parks at the next barrier. */
        /* encode RES0/RES1 as 00, 01, 10, or 11 */
        {
            uint32_t core0_saw_one;
            uint32_t core1_saw_one;
            uint32_t outcome_code;

            if (RES0(i) != 0u) {
                core0_saw_one = 1u;
            } else {
                core0_saw_one = 0u;
            }

            if (RES1(i) != 0u) {
                core1_saw_one = 1u;
            } else {
                core1_saw_one = 0u;
            }

            outcome_code = (core0_saw_one << 1) | core1_saw_one;

            if (outcome_code == (STOP_ON_SB_OUTCOME)) {
                REPORT_WEAK(i, WEAK_CODE_SB);
                while (1) { }
            }
        }
#endif
    }

    FENCE_RW_RW();
    DONE0 = MAGIC | 0u;
    while (1) { }
    return 0;
}
