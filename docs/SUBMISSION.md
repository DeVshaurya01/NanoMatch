# NanoMatch — Submission Cover Letter

**Project:** NanoMatch — Ultra-Low Latency Order Matching Engine
**Author:** Shaurya Atram
**Submission date:** 2026-05-22
**Addressed to:** Shubham Rane, Tanishq Kothari (FEC IIT Guwahati DIY '26 mentors)

---

## Overview

NanoMatch is a from-scratch C++20 limit-order-book matching engine built for the FEC IIT Guwahati DIY '26 project. It implements strict price-time priority, integer-tick pricing, and a fully cache-resident hot path with zero heap allocation, zero locking, and zero I/O on the matcher thread. The engine ingests NASDAQ TotalView-ITCH 5.0 data via memory-mapped I/O and outputs matched trades through a lock-free SPSC ring to an asynchronous logger.

## Headline Performance Numbers

Microbenchmark results from `nanomatch_bench 10000000` on Intel i7-14650HX, GCC 13.3.0, `-O3 -march=native -DNDEBUG` (full table in docs/REPORT.md, Section 4):

| Metric | Naive baseline | Optimized engine | Improvement |
|---|---|---|---|
| Throughput | 4.64 M ops/s | 17.93 M ops/s | 3.9x |
| Median latency (p50) | 96 ns | 24 ns | 4x |
| Max latency | 137 ms | 148 us | 925x |

## Real-Data Validation

The optimized engine processed the complete NASDAQ TotalView-ITCH 5.0 feed for 2019-07-30 (8.66 GB, 282 million messages). It matched 105,722,822 trades in 35.5 minutes wall-clock time with zero logger drops. The output covers the full 16-hour session from pre-market open (04:00 EST) through after-hours close (20:00 EST).

## Where to Look

| Deliverable | File |
|---|---|
| Project overview, build/run instructions | [README.md](../README.md) |
| Performance Benchmark Report | [docs/REPORT.md](REPORT.md) |
| CPU flame graph (naive vs optimized) | [docs/flame_combined.svg](flame_combined.svg) |
| Hardware and toolchain specs | [docs/hardware.md](hardware.md) |

## Caveats

The engine is single-instrument and in-process; there is no PCAP network layer, and hardware cache-miss PMU counters are unavailable under WSL2 (flame graph attribution uses DWARF software stack sampling instead).

## Acknowledgement

Thank you to Shubham Rane and Tanishq Kothari for the mentorship and the FEC project framework that made this work possible.

---

*PDF versions of this letter and the benchmark report are included as `docs/SUBMISSION.pdf` and `docs/REPORT.pdf`. To regenerate: `pandoc docs/REPORT.md -o docs/REPORT.pdf --pdf-engine=typst`.*
