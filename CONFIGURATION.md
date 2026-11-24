# Configuration notes

## AXI captures

The decoder records individual AXI channel events: write address, write data, write response, read address and read data. Interpreting a complete memory access requires correlating the channels with the program result and cache configuration.

- Export address/data probes as HEX and sample indices as UNSIGNED decimal. Handshake probes can use HEX, BINARY or UNSIGNED scalar values. Other payload encodings are rejected. Files without a radix row use the same HEX/decimal convention.
- Multiple `net_slot_N_axi_*` interfaces require `--all-slots`. This keeps each interface's handshake signals together. The global trigger annotation is associated with slot 0.
- If BREADY was not captured, response acceptance is labelled as assumed. Check the instrumented design before treating it as a completed handshake.
- Equal sample indices share a timebase; display order within one sample does not establish causality. A capture covers a finite window, and private cache hits may be absent from the bus trace.

The decoder extracts channel observations. Burst reconstruction, outstanding-request matching and AXI protocol validation require additional analysis.

## C experiment helpers

The source includes several experiment variants. Each reported result depends on its recorded build and settings; this source snapshot is not the exact revision for every campaign. The WRC-specific experiment implementation and full hardware build are outside this repository.

The default cached controls and arbitrary flag combinations should be treated separately. Known helper limitations are:

- `SB_Y_STRIDE` with uncached placement can overlap harness controls. A custom cached Y offset can also make the stride enter the reserved marker region.
- Custom X/Y offsets, stride banks and cache-line sizes need geometry checks. Their addresses can cross a line boundary or overlap the background-traffic windows.
- The standalone third-core generator uses `0xC2001D00`-`0xC2001EFC`, which overlaps Y locations under the default separate-bank `XY_STRIDE` layout. That combination is unsupported.
- The S/R diagnostic markers use partial candidate predicates. Evaluating a forbidden outcome also requires the final shared state.
- `STOP_ON_MP_OUTCOME` applies to the normal MP consumer; the optional stop is not implemented for `MP_ROLE_SWAP`.

Use the report's documented experimental settings and corresponding build artifacts when reproducing results. The supplied Python tests exercise synthetic captures; they do not run the FPGA system.
