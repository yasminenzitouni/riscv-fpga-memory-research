/* Build-option reference; definitions are in litmus_common.h and the core mains.
 * See CONFIGURATION.md for unsupported combinations and placement limits.
 * Tests (exactly one; default TEST_SB if none):
 *   TEST_COUNTER TEST_SB TEST_MP TEST_MP_DEP TEST_LB TEST_CORR
 *   TEST_2P2W TEST_S TEST_R
 *   CORR_READER_DELAY=<n>   CoRR sanity helper: delay the reader before its
 *                            first read so the run is non-vacuous (reader
 *                            should observe X=1 sometimes/often, while
 *                            forbidden read pair (1,0) must remain absent).
 *
 * MP matrix forcing modifiers (ALL DIRECTED - label evidence accordingly):
 *   MATRIX_FORCE_11          delay consumer so it reads after producer writes X,Y
 *   MATRIX_FORCE_01          delay producer between X=1 and Y=1
 *   MATRIX_FORCE_00          delay producer before writing X,Y
 *   MATRIX_FORCE_10_PIPELINE consumer reads X early, delays, then reads Y;
 *                            core0 ALSO delays MATRIX_DELAY_LONG/2 before X=1
 *                            Sampling-order evidence only: the consumer's
 *                            program order is X-then-Y.
 *   MATRIX_DELAY_SHORT=<n>   optional delay-loop count override
 *   MATRIX_DELAY_LONG=<n>    optional delay-loop count override
 *
 * SB outcome modifiers:
 *   SB_Y_STRIDE              fresh-Y NATURAL-layout variant: per-iteration y
 *                            line (core1 zeroes it; core0 never pre-touches),
 *                            so core0's SB load of y is a cold miss instead of
 *                            a stale D-cache hit. Fixes init poisoning; does
 *                            not bias the schedule. Build BOTH cores with it.
 *   SB_Y_STRIDE_SHIFT=<s>    stride log2 (default 4 = 16-byte lines; keep
 *                            0x010 + 256<<s below 0x1F00)
 *   SB_DELAY_CORE0_STORE=<n> DIRECTED: core0 delays before its x=1 store.
 *                            Without SB_Y_STRIDE -> 00; with it -> 10.
 *   SB_DELAY_BEFORE_LOAD=<n> DIRECTED: delay between store and load on the
 *                            core(s) built with it. Both cores + SB_Y_STRIDE
 *                            -> 11.
 *   SB_NO_FINY_READ          natural-stride hygiene (build BOTH cores):
 *                            core0 skips the FINY load/write entirely (its
 *                            y load would cache the line); uncached core1
 *                            records FINY from memory after the finish
 *                            barrier instead. SB natural runs only.
 *   JITTER_MASK=<m>          symmetric jitter amplitude for BOTH cores
 *                            (default 0x3Fu). Widening overlaps the racing
 *                            windows when barrier start-skew exceeds the
 *                            default range; record the mask with the run.
 *   SEED_FROM_HARNESS        XOR per-run host-provided seed words from
 *                            SHARED_BASE+0x060/+0x064 into each core's
 *                            jitter LFSR seed. Symmetric entropy injection:
 *                            changes the pseudo-random jitter sequence, not
 *                            the litmus body.
 *
 * MP mixed-placement / stale-consumer modifiers (directed cache-state tests,
 * separate from FLUSH_Y_ONLY_AFTER_FLAG; T3 TEST_MP only, same set on BOTH cores):
 *   MP_FLAG_UNCACHED         y/flag lives in uncached harness BRAM at
 *                            SHARED_BASE+0x004; x stays in cached test BRAM
 *   MP_ROLE_SWAP             core0 (cached) = consumer, RES0=(rf<<16)|rd;
 *                            core1 (uncached) = producer, RES1=0xBBBB0000.
 *                            Parse the outcome from RES0, not RES1.
 *   MP_EXPLICIT_PRIME_PROTOCOL
 *                            clean two-stage init protocol: uncached core1
 *                            writes X=0/Y=0; init-done barrier; cached core0
 *                            optionally primes X; prime-done barrier; body.
 *                            Requires TEST_MP + MP_ROLE_SWAP +
 *                            MP_FLAG_UNCACHED on both cores.
 *   MP_PRIME_X_READ          with MP_EXPLICIT_PRIME_PROTOCOL, cached core0
 *                            performs the genuine volatile X load. NOPRIME
 *                            omits only this define.
 *   MP_PRIME_WAIT_FOR_FLAG   PRIME-v2 consumer performs bounded genuine
 *                            uncached Y loads (WAIT_CAP, default 200000)
 *                            until Y=1 or timeout, then reads primed X. It
 *                            requires MP_EXPLICIT_PRIME_PROTOCOL and
 *                            MP_PRIME_X_READ; timeout never fabricates Y=1.
 *   WAIT_CAP=<n>             bounded publication-wait iteration cap.
 *
 * T4 dual-cached per-iteration placement:
 *   XY_STRIDE                BOTH x and y move to a fresh cache line each
 *                            iteration, so on a dual-cached tier every
 *                            cross-core read is a genuine cold-miss memory
 *                            observation. SB/MP only; REJECTS all forcing/
 *                            delay macros at compile time; REQUIRES
 *                            DCACHE_LINE_BYTES. Build BOTH cores with it.
 *                            Default geometry (reserved region begins at 0x1F00):
 *                              line 16B: shift 4, XY_NITER 240
 *                              line 32B: shift 5, XY_NITER 120
 *                              line 64B: shift 6, XY_NITER  60
 *                            x bank 0x000..0xEFF, y bank 0x1000..0x1EFF.
 *                            Result rows >= XY_NITER are STALE: parser must
 *                            filter Iter < XY_NITER.
 *   XY_SAMELINE              control: y_i = x_i + 4 (same cache line)
 *   XY_STRIDE_SHIFT=<s>      stride log2 override (guard: 1<<s >= line)
 *   XY_X_BANK/XY_Y_BANK=<o>  bank offset overrides (all spans guarded)
 *   XY_NITER=<n>             live-iteration override (guarded vs banks/256)
 *   DCACHE_LINE_BYTES=<n>    REQUIRED for XY_STRIDE; also asserts static
 *                            diff-line Y_OFF and SB_Y_STRIDE geometry
 *   STOP_ON_SB_OUTCOME=<n>   CAPTURE HELPER ONLY, never a frequency-run
 *                            setting (it stops at first match). Clean
 *                            observer (core0, post-barrier): stop + WEAK
 *                            markers when ((RES0!=0)<<1)|(RES1!=0)==n
 *                            (00->0 01->1 10->2 11->3). STRICTLY read-only
 *                            on results: inspects already-computed
 *                            RES0/RES1, writes only WEAK_* markers, cannot
 *                            modify RES or influence the body. Requires
 *                            TRIGGER_ON_WEAK; TEST_SB.
 *   STOP_ON_MP_OUTCOME=<n>   CAPTURE HELPER ONLY (same rules): clean
 *                            observer on the consumer's computed (rf,rd)
 *                            after RES1 is written. TRIGGER_ON_WEAK; TEST_MP
 *   ILA_TRACE_MARK_ITER=<n>   TRACE ONLY. After the MP_ROLE_SWAP consumer
 *                            (core0) has computed and stored RES0(n), write a
 *                            C0DE|n marker to uncached harness word +0x54,
 *                            then continue normally.
 *                            The selected row's body is complete before the
 *                            marker, so it cannot force that row's outcome.
 *                            Intended for pre-registered matched OFF/ON ILA
 *                            captures; never enable in frequency campaigns.
 *   MEASURE_SB_CYCLES        MECHANISM PROBE ONLY, never litmus evidence.
 *                            On core0/P_X TEST_SB, record cycle deltas:
 *                            FINX(i)=cycles consumed by X store instruction;
 *                            FINY(i)=cycles from before X store to after Y load.
 *                            RES0/RES1 still carry the SB outcome. Use to test
 *                            whether write-through stores post or block.
 *   MEASURE_JITTER_CYCLES    MECHANISM PROBE ONLY, never litmus evidence.
 *                            On core0/P_X, record:
 *                            FINX(i)=jitter loop iteration count;
 *                            FINY(i)=cycles consumed by the jitter loop.
 *                            This calibrates JITTER_MASK in cycles so the
 *                            narrow-jitter 11-band can be converted into an
 *                            actual cycle-width estimate.
 *
 * Background/noise traffic modifiers (STRESS variants; real unrelated memory
 * traffic, but asymmetric placement can intentionally bias timing):
 *   NOISE_ITERS=<n>          add n unrelated memory accesses in each enabled
 *                            core. Requires CACHED_TEST; traffic goes to a
 *                            guarded TEST BRAM window away from X/Y/results.
 *   NOISE_READS              perform unrelated reads (default if no RW mode set)
 *   NOISE_WRITES             perform unrelated writes
 *   NOISE_BEFORE_BODY        noise before first litmus op
 *   NOISE_BETWEEN_OPS        noise between store/load or flag/data ops; this
 *                            changes the two-thread litmus program, so label
 *                            as in-thread stress evidence
 *   NOISE_AFTER_BODY         noise after litmus body, before finish barrier
 *   NOISE_WORDS=<n>          noise window size in 32-bit words (default 64)
 *   NOISE_BASE_OFF=<off>     noise window offset (default 0x1400; guarded)
 *   ZW_BEFORE_BODY           explicit independent Z/W in-thread traffic before
 *                            first litmus op. Core0 owns Z_i; core1 owns W_i.
 *                            This modifies the two litmus threads; it is not
 *                            the same as separate background AXI traffic.
 *   ZW_BETWEEN_OPS           explicit Z/W traffic between store/load or
 *                            flag/data ops; in-thread stress evidence. For SB,
 *                            this gives stores more time to become visible;
 *                            use timing controls when interpreting changes.
 *   ZW_AFTER_BODY            explicit Z/W traffic after litmus body
 *   ZW_INIT_ONLY             initialize this core's owned Z/W word during the
 *                            init phase, but add no body traffic. Use this on
 *                            the passive core when the active core reads the
 *                            other-owned word.
 *   ZW_WRITE_ONLY            write this core's own independent word
 *   ZW_READ_ONLY             read the other core's independent word
 *   ZW_READ_WRITE            do both. Prefer these three explicit modes for
 *                            new runs. Legacy aliases still work:
 *                            ZW_WRITE_OWN and ZW_READ_OTHER.
 *   ZW_WORD_STRIDE           packed per-iteration words, 4 bytes apart
 *                            (default if no stride mode is selected)
 *   ZW_LINE_STRIDE           one fresh cache line per active iteration;
 *                            requires DCACHE_LINE_BYTES and usually a smaller
 *                            RUN_NITER so Z/W still fit below 0x1F00
 *   ZW_STRIDE_BYTES=<n>      manual stride override; do not combine with
 *                            ZW_WORD_STRIDE or ZW_LINE_STRIDE
 *   ZW_Z_OFF/ZW_W_OFF=<off>  per-iteration Z/W word arrays, default
 *                            0x1400 and 0x1800, guarded below 0x1F00
 *   CONTROL_LOOP_ITERS=<n>   timing-only control loop, no extra memory access
 *   CONTROL_LOOP_BEFORE_BODY / _BETWEEN_OPS / _AFTER_BODY
 *                            same placement as ZW_*; use to separate
 *                            memory-traffic effects from loop-time effects.
 *                            Implemented as an inline asm register loop;
 *                            verify disassembly shows no lw/sw inside it.
 *   RUN_START_ITER=<k>       framework/loop isolation: start at iteration k
 *   RUN_NITER=<n>            run only n iterations; e.g. RUN_NITER=1u for
 *                            single-litmus-instance checks inside same harness
 *   PRIME_X_LINE_NEIGHBOR    read TEST_BASE+X_OFF+4 before the body to load
 *                            X's cache line without reading X itself. Legacy
 *                            alias PRIME_X_LINE_VIA_Z still works, but the
 *                            neighbor is not the Z_i stress array at 0x1400.
 *   PRIME_Y_LINE_NEIGHBOR    read TEST_BASE+Y_OFF+4 (or strided y_i+4) before
 *                            the body to load Y's cache line without reading
 *                            Y itself. Legacy alias PRIME_Y_LINE_VIA_W still
 *                            works, but the neighbor is not W_i at 0x1800.
 *   PRIME_ON_CORE0 / _CORE1  restrict line-neighbor priming to one core.
 *                            If neither is set, both cores prime.
 *   DCACHE_LINE_BYTES=<n>    required for PRIME_* so the same-line claim is
 *                            explicit and auditable
 *
 * Not implemented in this source:
 *   TEST_MP_TRIGGER_DIFFLINE
 *   TEST_FENCE_IS_DRAIN
 *
 * Modifiers:
 *   USE_FENCE            fence rw,rw in the test body (both cores)
 *   TWO_BRAM             T2: y at 0xC1000000
 *   CACHED_TEST          T3/T4: x,y at TEST_BASE 0xC2000000
 *   USE_XIL_CACHE        this core enables D-cache + flush/invalidate hooks
 *   DISABLE_CACHE_MAINT  with USE_XIL_CACHE: enable D-cache, but make
 *                        TEST_FLUSH/TEST_INVALIDATE/FLUSH_ADDR no-ops
 *   TRAP_REPORT          with USE_XIL_CACHE: install a trap reporter before
 *                        cache maintenance; useful for CBO smoke tests
 *   X_OFF=0x... Y_OFF=0x...  variable placement (direct-mapped experiments)
 * Address definitions are in litmus_common.h; setup notes are in CONFIGURATION.md. */
#ifndef CONFIG_FLAGS_H
#define CONFIG_FLAGS_H
#endif
