# RISC-V FPGA Memory Experiments

This project investigates how one processor core can see a "data ready" flag but still read an older value of that data.

I used small concurrent C programs, called litmus tests, on two- and three-core MicroBlaze V RISC-V FPGA systems. I varied cache state, memory-ordering fences and background traffic, then compared the software results with AXI bus captures to investigate what happened. The tested configurations have non-coherent private caches: a write from another core does not automatically update a cached copy.

My work covered the Vivado/Vitis testbeds, C experiments, automated board runs and Python trace-analysis tools. During the hardware-traffic study, I automated 300 runs covering 76,800 recorded rounds across the initial campaign and a later replication.

I developed this during my summer 2026 research placement at Imperial College London with Dr. John Wickerson, supported by a Mitacs Globalink Research Award.

[Technical report](https://drive.google.com/file/d/11Thfhhf-1WhdBo8wCphw8BENfqXZm3At/view?usp=sharing) · [Technical appendix: hardware diagrams, configurations and supporting results](https://drive.google.com/file/d/15kY-r00da7GmpbdFbTlfltCBDL-rmlUB/view?usp=sharing)

## Code

Start with **[decode_axi_ila.py](decode_axi_ila.py)**. It turns raw Vivado ILA captures into a timeline of AXI channel events and runs without the FPGA. The [example output](expected_events.csv) shows the six events extracted from the supplied synthetic capture.

- **Concurrent C experiments:** [core0_main.c](core0_main.c) and [core1_main.c](core1_main.c) contain the per-core test loops. [litmus_common.h](litmus_common.h) defines shared-memory placement, barriers, cache controls and result logging.
- **Experiment options:** [config_flags.h](config_flags.h) documents the build flags; [noise_agent_core2.c](noise_agent_core2.c) supplies optional third-core traffic.
- **Decoder tests:** [test_decoder.py](test_decoder.py) and [test_decoder_corrections.py](test_decoder_corrections.py) cover signal selection, backpressure, channel events, input validation and the CLI example.

## Run the decoder

Requires Python 3.10 or newer. No additional packages are needed.

```sh
python -B decode_axi_ila.py synthetic_capture.csv --all-slots --slot-labels 0=writer,1=reader
python -B decode_axi_ila.py synthetic_capture.csv --all-slots --slot-labels 0=writer,1=reader --out-dir generated
python -B -m unittest discover -p "test_decoder*.py" -v
```

Export address and data probes as **HEX**. Use `--all-slots` for captures with multiple monitored interfaces. The included capture is synthetic.

## Hardware setup

The C harness needs a matching MicroBlaze V design, hardware address map, per-core build flags and BSP. This repository contains selected source files; the full FPGA project and experiment datasets are outside it.

See [configuration notes](CONFIGURATION.md) for trace interpretation and limitations of the experiment helpers. The report describes the experimental methods and results.

## Development

My workflow included AI-assisted coding and debugging, alongside software testing, disassembly review and AXI trace analysis.

Copyright 2026 Yasmine Zitouni.
