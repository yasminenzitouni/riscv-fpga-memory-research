/* Distinct counter patterns identify each core in AXI captures.
 * TEST_COUNTER is implemented in the per-core main files. */
#ifndef COUNTER_TEST_H
#define COUNTER_TEST_H
#define COUNTER_PATTERN_CORE0(i) (0xC0000000u | (i))
#define COUNTER_PATTERN_CORE1(i) (0xD0000000u | (i))
#endif
