<p align="center">
  <h1 align="center">⚡ NanoMatch</h1>
  <p align="center">
    <strong>Ultra-Low Latency Order Matching Engine</strong>
    <br/>
    <em>From-scratch C++20 &middot; 24 ns median latency &middot; 17.9 M ops/s &middot; Zero malloc on hot path</em>
  </p>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue?style=for-the-badge&logo=cplusplus" alt="C++20"/>
  <img src="https://img.shields.io/badge/Latency-24ns%20p50-brightgreen?style=for-the-badge" alt="p50 Latency"/>
  <img src="https://img.shields.io/badge/Throughput-17.9M%20ops%2Fs-orange?style=for-the-badge" alt="Throughput"/>
  <img src="https://img.shields.io/badge/Max%20Latency%20Improvement-925×-red?style=for-the-badge" alt="925x"/>
  <img src="https://img.shields.io/badge/NASDAQ%20Trades%20Matched-105.7M-purple?style=for-the-badge" alt="Trades"/>
</p>

<p align="center">
  <b>FEC IIT Guwahati DIY '26 Submission</b><br/>
  <a href="docs/SUBMISSION.pdf">Mentor Cover Letter</a> · <a href="docs/REPORT.pdf">Benchmark Report (PDF)</a> · <a href="docs/flame_combined.svg">Flame Graph</a> · <a href="docs/hardware.md">Hardware Specs</a>
</p>

---

## Headline Results

<table>
<tr>
<td width="50%">

### 🏎️ Throughput

| | Naive | Optimized |
|---|---|---|
| **ops/s** | 4.64 M | **17.93 M** |
| **Speedup** | | **3.9×** |

</td>
<td width="50%">

### 📉 Tail Latency

| Percentile | Naive | Optimized | Δ |
|---|---|---|---|
| p50 | 96 ns | **24 ns** | 4× |
| p99 | 768 ns | **96 ns** | 8× |
| p99.9 | 3,072 ns | **192 ns** | 16× |
| **Max** | **137 ms** | **148 μs** | **925×** |

</td>
</tr>
</table>

> **The 925× max-latency collapse is the headline number.** Naive's worst case (137 ms) comes from `_int_malloc` arena consolidation and `std::map` rebalancing at depth — both eliminated entirely in the optimized path. 137 ms is structurally incompatible with HFT; 148 μs is viable for production backends.

### Real-Data Validation — Full NASDAQ Trading Day

The optimized engine processed the **complete NASDAQ TotalView-ITCH 5.0 feed for July 30, 2019** (8.66 GB uncompressed, 282M messages):

| Metric | Value |
|---|---|
| Trades matched | **105,722,822** |
| Wall-clock time | **35.5 minutes** |
| Logger drops | **0** |
| Session coverage | 04:00 — 20:00 EST (full 16h) |
| Output CSV | 4.94 GB |

No message types skipped. No crashes. No data loss. See [docs/REPORT.pdf](docs/REPORT.pdf) §5 for full validation details.

---

## Architecture

```
  ITCH file  ──►  mmap (zero-copy)  ──►  parser  ──►  OrderEvent
                                                          │
                                                          ▼
                                              ┌─────────────────────┐
                                              │  Matching Engine     │
                                              │  ┌───────────────┐  │
                                              │  │ Bucket Book    │  │  ◄── O(1) BBO via array index
                                              │  │ (price levels) │  │
                                              │  └───────┬───────┘  │
                                              │  ┌───────▼───────┐  │
                                              │  │ Intrusive List │  │  ◄── Pool-index links, no pointers
                                              │  │ (time priority)│  │
                                              │  └───────┬───────┘  │
                                              │  ┌───────▼───────┐  │
                                              │  │ Slab Pool      │  │  ◄── O(1) alloc, zero malloc
                                              │  │ (order memory) │  │
                                              │  └───────┬───────┘  │
                                              │  ┌───────▼───────┐  │
                                              │  │ Cancel Hash    │  │  ◄── O(1) cancel by order ID
                                              │  └───────────────┘  │
                                              └──────────┬──────────┘
                                                         │ Trade
                                                         ▼
                                              SPSC Ring Buffer (lock-free)
                                                         │
                                                         ▼
                                              Async Logger Thread
                                                         │
                                                         ▼
                                                    trades.csv
```

### Design Principles

| Principle | Implementation |
|---|---|
| **Zero heap traffic** | Slab pool with free-list — `malloc` never called after startup |
| **Cache-line discipline** | `Order` ≤ 64 bytes (`static_assert`), contiguous pool memory |
| **No pointer chasing** | Intrusive doubly-linked list via 32-bit pool indices |
| **O(1) everything** | Bucket book (array-indexed price levels), hash-map cancel |
| **No floats** | All prices are `int64_t` ticks (NASDAQ uses 4 implied decimals) |
| **Zero-copy ingestion** | `mmap` + `MADV_SEQUENTIAL` (POSIX) / `MapViewOfFile` (Win) |
| **Non-blocking I/O** | SPSC ring to dedicated logger thread; matcher never blocks |

---

## Quickstart for Reviewers

### Prerequisites

- C++20 compiler (GCC 13+ or MSYS2 UCRT64 g++ 16+)
- CMake 3.20+, Ninja
- ~50 GB free disk if you want to replay a full NASDAQ day
- Linux or WSL2 Ubuntu required ONLY for flame graph regeneration

### 1. Build & Run Unit Tests *(60 seconds)*

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/nanomatch_tests
```

> **Expected:** All 8 tests pass — resting orders, full/partial fills, price-time priority, cancel semantics, market orders, and naive-vs-optimized smoke test.

### 2. Run the Benchmark *(10 seconds)*

```bash
./build/nanomatch_bench 10000000
```

> **Expected:** Naive ~4.6 M ops/s, optimized ~17.9 M ops/s, 925× max-latency improvement. See [docs/REPORT.pdf](docs/REPORT.pdf) §4 for the full latency distribution table.

### 3. Run on Synthetic Data *(no download, 1 second)*

```bash
./build/nanomatch --synthetic 1000000
```

> Writes `trades.csv` in the current working directory.

### 4. Run on Real NASDAQ Data *(optional, ~35 min for full day)*

1. Download an ITCH file from [emi.nasdaq.com/ITCH/Nasdaq ITCH/](https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/) (e.g. `07302019.NASDAQ_ITCH50.gz`, 3.66 GB)
2. Decompress: `gunzip 07302019.NASDAQ_ITCH50.gz` (produces ~9 GB)
3. Quick check with a 200 MB sample:
   ```bash
   head -c 209715200 07302019.NASDAQ_ITCH50 > sample_200mb.itch
   ./build/nanomatch sample_200mb.itch
   ```
   Expected: ~2.2M trades, ~45 seconds.
4. Full-day run:
   ```bash
   ./build/nanomatch 07302019.NASDAQ_ITCH50
   ```
   Expected: ~105.7M trades, ~35 minutes.

### 5. Regenerate the Flame Graph *(optional, WSL2 / Linux only)*

```bash
bash scripts/wsl_flamegraph.sh
```

Pre-generated output: [docs/flame_combined.svg](docs/flame_combined.svg)

---

## What the Flame Graph Shows

The CPU flame graph (`docs/flame_combined.svg`) is generated via `perf record -F 4000 -g --call-graph dwarf` over a 10M-event benchmark run.

**Naive (tall red towers):**
- `_int_malloc`, `__GI___libc_malloc` — per-order heap allocation
- `operator new` — C++ allocation wrappers
- `malloc_consolidate`, `unlink` — heap fragmentation bookkeeping
- `std::_Hashtable::*` — hash chain walks and rehashes
- `std::map` — red-black tree rebalancing

**Optimized (barely visible):**
- Consumes ~21% of recording wall-clock (0.558s of 2.71s combined)
- Contains **none** of the malloc/hashtable frames
- Dominated by `match_order` and `pool_alloc` only

The collapsed towers are the visual proof that the optimization works.

---

## Project Structure

```
NanoMatch/
├── include/nanomatch/          # Public headers
│   ├── types.hpp               # Order, Trade, Price — all ≤ 64B, cache-line aligned
│   ├── order_book.hpp          # Optimized: bucket book + intrusive list + cancel hash
│   ├── order_book_naive.hpp    # Naive baseline: std::map + std::list (exists to lose)
│   ├── object_pool.hpp         # Slab pool with free-list, zero heap traffic
│   ├── spsc_ring.hpp           # Lamport SPSC ring, false-sharing-free
│   ├── itch_parser.hpp         # NASDAQ TotalView-ITCH 5.0 message dispatcher
│   ├── file_map.hpp            # Cross-platform mmap (POSIX + Windows)
│   ├── logger.hpp              # Async trade logger (SPSC → disk)
│   ├── timing.hpp              # rdtsc + steady_clock calibration
│   └── histogram.hpp           # Log-bucket latency histogram (p50–p99.9)
├── src/                        # Implementation files
│   ├── main.cpp                # End-to-end: ITCH → mmap → parser → matcher → CSV
│   ├── order_book.cpp          # Optimized book template instantiation
│   ├── order_book_naive.cpp    # Naive book implementation
│   ├── itch_parser.cpp         # ITCH message parser (big-endian helpers)
│   ├── file_map.cpp            # mmap implementations (POSIX + Windows)
│   └── logger.cpp              # Logger thread loop
├── tests/
│   └── test_order_book.cpp     # 8 correctness invariants, no external deps
├── benchmarks/
│   └── bench_match.cpp         # Naive vs optimized, rdtsc-timed, identical input
├── scripts/
│   ├── wsl_flamegraph.sh       # One-command flame graph generation
│   ├── run_bench.sh            # Reproducible bench (isolcpus, no-turbo, taskset)
│   └── hardware_info.sh        # Dump hardware specs to docs/hardware.md
├── data/
│   └── generate_orders.py      # Synthetic order generators (4 flavors)
├── docs/
│   ├── REPORT.md               # Full benchmark report (Markdown)
│   ├── REPORT.pdf              # Full benchmark report (PDF)
│   ├── SUBMISSION.md           # Cover letter to mentors
│   ├── SUBMISSION.pdf          # Cover letter (PDF)
│   ├── flame_combined.svg      # CPU flame graph
│   └── hardware.md             # Hardware & toolchain specs
├── CMakeLists.txt              # C++20, -O3 -march=native, three binary targets
├── NAVIGATION.md               # Per-file map with brick references
└── README.md                   # This file
```

---

## Hardware & Toolchain

| Item | Value |
|---|---|
| CPU | Intel Core i7-14650HX (Raptor Lake-HX) |
| Cores / Threads | 16 / 24 |
| L2 / L3 cache | 24 MB / 30 MB |
| RAM | 16 GB |
| TSC frequency | 2.476 GHz (calibrated) |
| OS (bench) | Windows 11, MSYS2 UCRT64 |
| OS (profiling) | Ubuntu via WSL2 |
| Compiler | GCC 13.3.0 (Linux) / GCC 16.1.0 (Windows) |
| Build flags | `-O3 -march=native -DNDEBUG -fno-omit-frame-pointer` |

Full specs: [docs/hardware.md](docs/hardware.md)

---

## Unit Tests

| # | Test | Property Verified |
|---|---|---|
| 1 | Rest no cross | Resting orders on opposite sides do not self-match |
| 2 | Full cross | Marketable order fully crosses at best price |
| 3 | Partial fill | Partial match leaves remainder at the limit |
| 4 | Price-time priority | Earlier order at same price fills first |
| 5 | Cancel non-top | Cancelling non-best order leaves BBO unchanged |
| 6 | Cancel best | Cancelling best order correctly advances BBO |
| 7 | Market vs empty | Market order against empty book is a no-op |
| 8 | Naive smoke | Naive baseline produces same trades as optimized |

All 8 pass on both Windows (MSYS2 g++ 16.1.0) and Linux (Ubuntu g++ 13.3.0).

---

## Limitations & Honest Caveats

- **Single instrument, in-process.** Multi-symbol sharding and network ingest are out of scope.
- **`std::unordered_map` is the cancel index.** A flat hash map (`ankerl::unordered_dense`) would tighten p99 further.
- **No PCAP network layer.** Ingestion is file-based via mmap, not wire-speed.
- **Cache-miss PMU counters unavailable on WSL2.** Flame graph uses DWARF software stack sampling, not hardware PMU. Bare-metal Linux would unlock direct L1/L2/L3 measurement.
- **Reproducibility harness is Linux-only.** `scripts/run_bench.sh` (isolcpus, no-turbo, taskset) requires bare Linux or WSL2.

---

## FEC Deliverable Status

| Spec Requirement | Status |
|---|---|
| Core LOB (limit / market / cancel, price-time priority) | ✅ Implemented, 8 unit tests pass |
| Cache-optimized memory management | ✅ Slab pool, bucket book, intrusive list |
| High-throughput zero-copy ingestion (ITCH 5.0) | ✅ mmap parser, 282M messages processed |
| Lock-free SPSC ring for trade logging | ✅ Implemented |
| rdtsc bench harness with p50/p90/p99 | ✅ Calibrated, HdrHistogram-style |
| Performance Benchmark Report | ✅ [docs/REPORT.pdf](docs/REPORT.pdf) |
| CPU Flame Graph | ✅ [docs/flame_combined.svg](docs/flame_combined.svg) |
| HFT-narrative README + hardware specs | ✅ This file + [docs/hardware.md](docs/hardware.md) |

---

## License

This project was built as part of the FEC IIT Guwahati DIY '26 program.

---

<p align="center">
  <sub>Built by <strong>Shaurya Atram</strong> · FEC IIT Guwahati DIY '26</sub>
</p>
