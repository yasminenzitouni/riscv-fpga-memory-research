/*
 * MicroBlaze V hart 1 experiment loops.
 * Build with the SAME symbols as core 0 (minus USE_XIL_CACHE and
 * FLUSH_Y_ONLY_AFTER_FLAG on asymmetric T3 builds).
 * Result packing where two registers exist: RES1 = (rA<<16)|rB.
 */
#include "litmus_common.h"
#define MY_ID 1u

#if defined(MP_EXPLICIT_PRIME_PROTOCOL)
#if defined(USE_XIL_CACHE)
#error "MP explicit-prime initializer must be the uncached producer"
#endif

/* Stable disassembly symbol proving two real uncached zeroing bus stores. */
__attribute__((noinline, used))
void mp_prime_protocol_uncached_init(void)
{
    VAR_X = 0u;
    VAR_Y = 0u;
    FENCE_RW_RW();
}
#endif

int main(void)
{
    uint32_t i;
    uint32_t seed;

    /* core1/P1 program starts here */
    seed = 0xDEADBEA7u;

    CACHE_SETUP();
    DONE1 = 0;
    MIX_RUN_SEED(seed, MY_ID);

    /* T3 observer only (uncached core on a cached tier): clear the
     * test-side trigger marker with a real bus write BEFORE the first
     * barrier, so a stale FACE from a previous run can't false-trigger
     * the test ILA. No-op on all other builds. Core0 must NEVER touch
     * this address: on cached cores the clear would sit dirty in the
     * D-cache and could overwrite a real marker on later eviction. */
    CLEAR_WEAK_TESTSIDE();
    FENCE_RW_RW();

    for (i = RUN_START_ITER; i < RUN_END_ITER; i++) {   /* NITER unless narrowed */
        uint32_t phase;

        /* one loop iteration = one result row */
        phase = i * 4u;

        barrier(MY_ID, phase + 1u);           /* init (hart 0 resets) */
        /* core1-side reset/init for this iteration */
#if defined(MP_EXPLICIT_PRIME_PROTOCOL)
        mp_prime_protocol_uncached_init();
#elif defined(XY_STRIDE)
#if !defined(TEST_MP)
        /* SB: this core owns all y stores; zero the fresh line. On T4 this
         * core is cached too, but it only ever STORES y_i (write-through
         * keeps memory current) and never loads it, so no staleness. */
        VAR_Y_T(i) = 0;
        FENCE_RW_RW();
#endif
        /* MP: consumer touches NEITHER variable before its body loads. */
#elif defined(SB_Y_STRIDE)
        /* Fresh-Y variant: the UNCACHED observer zeroes this iteration's
         * y line with a real bus write, so core0 never caches it before
         * its SB load. Runs in the init window; both harts wait at the
         * next barrier, so ordering vs core0's init is safe. */
        VAR_Y_SB(i) = 0;
        FENCE_RW_RW();
#endif
        zw_init(i, MY_ID);
#if defined(MP_EXPLICIT_PRIME_PROTOCOL)
        barrier(MY_ID, phase + 2u);           /* uncached init is complete */
        barrier(MY_ID, phase + 3u);           /* optional prime is complete */
#else
        barrier(MY_ID, phase + 2u);           /* start line */
#endif
        /* start barrier, then randomized jitter */
        jitter(&seed);
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

            value = 0xD0000000u | i;
            VAR_Y = value;
            RES1(i) = value;
        }
#elif defined(TEST_MP)
#if defined(MP_ROLE_SWAP)
        /* Uncached producer: both body stores are real bus writes. Under the
         * explicit-prime protocol this core also performed both zeroing
         * stores before the init-done barrier. */
        VAR_X = 1;            /* data  */
        TEST_FENCE();         /* producer fence                       */
#if defined(CONTROL_LOOP_BETWEEN_OPS)
        control_loop(i, MY_ID);
#endif
#if defined(ZW_BETWEEN_OPS)
        zw_traffic(i, MY_ID);
#endif
#if defined(NOISE_BETWEEN_OPS)
        noise_traffic(i, MY_ID);
#endif
        VAR_Y = 1;            /* flag  */
        RES1(i) = 0xBBBB0000u;
#else
        {
            uint32_t flag_value;
            uint32_t data_value;

#if defined(MATRIX_FORCE_11)
            /* Force late consumer window: producer should have written both X and Y. */
            matrix_delay(MATRIX_DELAY_LONG);
            flag_value = VAR_Y;
            TEST_FENCE();
            data_value = VAR_X;

#elif defined(MATRIX_FORCE_10_PIPELINE)
            /* Pipeline-skew mode: sample X early as historical data, then sample Y later. */
            data_value = VAR_X;
            matrix_delay(MATRIX_DELAY_LONG);
            flag_value = VAR_Y;

#else
            flag_value = VAR_Y_T(i);    /* read flag, fresh line if strided */
            TEST_FENCE();
#if defined(CONTROL_LOOP_BETWEEN_OPS)
            control_loop(i, MY_ID);
#endif
#if defined(ZW_BETWEEN_OPS)
            zw_traffic(i, MY_ID);
#endif
#if defined(NOISE_BETWEEN_OPS)
            noise_traffic(i, MY_ID);
#endif
            data_value = VAR_X_T(i);    /* read data, fresh line if strided */
#endif

            RES1(i) = (flag_value << 16) | (data_value & 0xFFFFu);

            if ((flag_value == 1u) && (data_value == 0u)) {
                REPORT_WEAK(i, WEAK_CODE_MP);
                STOP_IF_WEAK();
            }
#if defined(STOP_ON_MP_OUTCOME)
            /* Clean outcome observer on the locally computed (flag, data)
             * pair. Marker + stop only, after the fact. */
            {
                uint32_t flag_was_one;
                uint32_t data_was_one;
                uint32_t outcome_code;

                if (flag_value != 0u) {
                    flag_was_one = 1u;
                } else {
                    flag_was_one = 0u;
                }

                if (data_value != 0u) {
                    data_was_one = 1u;
                } else {
                    data_was_one = 0u;
                }

                outcome_code = (flag_was_one << 1) | data_was_one;

                if (outcome_code == (STOP_ON_MP_OUTCOME)) {
                    REPORT_WEAK(i, WEAK_CODE_MP);
                    while (1) { }
                }
            }
#endif
        }
#endif /* MP_ROLE_SWAP */
#elif defined(TEST_MP_DEP)
        {
            uint32_t flag_value;
            uint32_t data_value;

            flag_value = VAR_Y;
            data_value = load_x_dep_on(flag_value);
            RES1(i) = (flag_value << 16) | (data_value & 0xFFFFu);

            if ((flag_value == 1u) && (data_value == 0u)) {
                REPORT_WEAK(i, WEAK_CODE_MP_DEP);
                STOP_IF_WEAK();
            }
        }
#elif defined(TEST_LB)
        {
            uint32_t loaded_x;

            loaded_x = VAR_X;
            TEST_FENCE();
            VAR_Y = 1;
            RES1(i) = loaded_x;

            /* LB weak outcome needs core0's r1 too, so this is not
             * fully detected locally here. Keep result logging only. */
        }
#elif defined(TEST_CORR)
        {
            uint32_t first_read;
            uint32_t second_read;

#if defined(CORR_READER_DELAY)
            matrix_delay(CORR_READER_DELAY);
#endif
            first_read = VAR_X;
            second_read = VAR_X;
            RES1(i) = (first_read << 16) | (second_read & 0xFFFFu);

            if ((first_read == 1u) && (second_read == 0u)) {
                REPORT_WEAK(i, WEAK_CODE_CORR);
                STOP_IF_WEAK();
            }
        }
#elif defined(TEST_2P2W)
        VAR_Y = 1;
        TEST_FENCE();
        VAR_X = 2;
        RES1(i) = 0xBBBB0000u;
#elif defined(TEST_S)
        {
            uint32_t loaded_y;

            loaded_y = VAR_Y;
            TEST_FENCE();
            VAR_X = 1;
            RES1(i) = loaded_y;

            if (loaded_y == 1u) {
                REPORT_WEAK(i, WEAK_CODE_S);
                STOP_IF_WEAK();
            }
        }
#elif defined(TEST_R)
        {
            uint32_t loaded_x;

            VAR_Y = 2;
            TEST_FENCE();
            loaded_x = VAR_X;
            RES1(i) = loaded_x;

            if (loaded_x == 0u) {
                REPORT_WEAK(i, WEAK_CODE_R);
                STOP_IF_WEAK();
            }
        }
#else /* TEST_SB */
        {
            uint32_t loaded_x;

            /* SB body on core1: Y=1; r1=X; RES1=r1 */
            VAR_Y_T(i) = 1;     /* store y, fresh line if strided */
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
            loaded_x = VAR_X_T(i);    /* load x, fresh line if strided */
            RES1(i) = loaded_x;

            /* SB weak outcome needs core0's r1 too, so this is not
             * fully detected locally here. Keep result logging only. */
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
        /* finish barrier / optional final-state bookkeeping */

#if defined(SB_NO_FINY_READ)
        /* This uncached core records y's true final MEMORY state (core0
         * skips its FINY load/write entirely in this mode, so there is
         * exactly one writer of FINY(i)). A real bus read: on SB runs the
         * expected value is this core's own y=1, read back from memory. */
        FINY(i) = VAR_Y_T(i);
#endif
    }

    FENCE_RW_RW();
    DONE1 = MAGIC | 1u;
    while (1) { }
    return 0;
}
