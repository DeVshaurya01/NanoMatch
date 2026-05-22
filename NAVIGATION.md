# NanoMatch — File Navigation

A one-stop map of every file in the project: what it is, which brick from the
blueprint it implements, and what role it plays at runtime.

The blueprint groups work into 20 numbered **bricks**. Each header below cites
the brick(s) the file maps to so you can read the file alongside the
corresponding section of `compass_artifact_wf-...text_markdown.md`.

---

## Top-level

| File | Purpose |
|---|---|
| [CMakeLists.txt](CMakeLists.txt) | Build script. C++20, `-O3 -march=native -DNDEBUG`, no external deps. Produces three binaries: `nanomatch` (demo), `nanomatch_bench` (Brick 14), `nanomatch_tests` (Brick 5). |
| [.gitignore](.gitignore) | Build artifacts, perf data, generated CSVs. |
| [README.md](README.md) | Project overview, build/run instructions, expected numbers. |
| [NAVIGATION.md](NAVIGATION.md) | **This file.** |
| [compass_artifact_wf-...text_markdown.md](compass_artifact_wf-6eb4518e-e1eb-4b0b-863f-c00aa39b9544_text_markdown.md) | The original blueprint. |

---

## `include/nanomatch/` — Public headers

| File | Brick | What it does |
|---|---|---|
| [types.hpp](include/nanomatch/types.hpp) | **2** | The integer-tick foundation. `Price = int64_t`, `Order` (≤64B, intrusive list links via pool indices), `Trade`, `OrderEvent`. `static_assert(sizeof(Order) <= 64)` enforces cache-line discipline. |
| [object_pool.hpp](include/nanomatch/object_pool.hpp) | **6** | Slab pool with free-list threaded through unused slots. O(1) alloc/free, zero heap traffic after construction, contiguous memory for prefetcher friendliness. |
| [spsc_ring.hpp](include/nanomatch/spsc_ring.hpp) | **11** | Lamport SPSC ring with Rigtorp opposite-cursor caching. Every shared atomic and every per-side local cache sits on its own cache line (`alignas(kCacheLine)`) to eliminate false sharing. |
| [timing.hpp](include/nanomatch/timing.hpp) | **13a** | `now_ns()` (steady_clock), `rdtsc_start/end()` (x86 only), `calibrate_tsc_ghz()`. |
| [histogram.hpp](include/nanomatch/histogram.hpp) | **13b** | Tiny log-bucket latency histogram — the HdrHistogram stand-in. p50/p90/p99/p99.9/max in O(1) per record, ~64 buckets, fixed memory. |
| [order_book_naive.hpp](include/nanomatch/order_book_naive.hpp) | **3** | The "before" baseline: `std::map<Price, std::list<Order>>` + `std::unordered_map` for cancel. Exists to lose. |
| [order_book.hpp](include/nanomatch/order_book.hpp) | **7, 8, 9** | The "after": fixed-size **bucket book** (array of `Level` indexed by tick) + **intrusive doubly-linked list** linked through `uint32_t` pool indices + **O(1) cancel hash**. Cached `best_bid_tick_` / `best_ask_tick_` for O(1) BBO. |
| [file_map.hpp](include/nanomatch/file_map.hpp) | **10a** | Cross-platform mmap. POSIX `mmap` + `MADV_SEQUENTIAL`; Windows `CreateFileMappingA` + `MapViewOfFile`. |
| [itch_parser.hpp](include/nanomatch/itch_parser.hpp) | **10b** | NASDAQ TotalView-ITCH 5.0 message dispatcher (`A`, `F`, `E`, `X`, `D`, `U`). Emits `OrderEvent`s; cold/admin types are routed to a skip path. |
| [logger.hpp](include/nanomatch/logger.hpp) | **12** | `AsyncTradeLogger` — matcher pushes `Trade`s into an SPSC ring; a dedicated thread batches them out to disk. Backpressure increments `dropped_`; the matcher never blocks. |

---

## `src/` — Implementation

| File | Brick | Notes |
|---|---|---|
| [src/main.cpp](src/main.cpp) | **end-to-end** | Wires everything: ITCH file → mmap → parser → matcher → AsyncTradeLogger → `trades.csv`. Has a `--synthetic <N>` mode that exercises the engine without an ITCH file. |
| [src/order_book.cpp](src/order_book.cpp) | 7-9 | Explicit template instantiation of `OrderBook<>` so CMake has a TU. |
| [src/order_book_naive.cpp](src/order_book_naive.cpp) | 3 | Naive book impl: matching, cancel, market. |
| [src/itch_parser.cpp](src/itch_parser.cpp) | 10b | Per-message-type parser with big-endian helpers (`be16/32/48/64`). Replace/Execute are mapped onto Add/Cancel events for the matcher. |
| [src/logger.cpp](src/logger.cpp) | 12 | Logger thread loop: drain up to 1024 trades per pass, fwrite, sleep 50 µs when idle. |
| [src/file_map.cpp](src/file_map.cpp) | 10a | POSIX and Windows mmap implementations behind a single `MappedFile` class. |

---

## `tests/` — Brick 5

| File | What it tests |
|---|---|
| [tests/test_order_book.cpp](tests/test_order_book.cpp) | Self-contained mini test framework (no GoogleTest dep). Covers: rest-no-cross, full cross, partial fill, **price-time priority**, cancel non-top, cancel best (BBO walks), market against empty book, naive baseline smoke. |

---

## `benchmarks/` — Brick 14

| File | What it measures |
|---|---|
| [benchmarks/bench_match.cpp](benchmarks/bench_match.cpp) | Runs the **identical** event stream against the naive and optimized books and prints throughput + p50/p90/p99/p99.9 from `LatencyHistogram`. The diff IS the deliverable. |

---

## `data/` — Brick 4

| File | What it does |
|---|---|
| [data/generate_orders.py](data/generate_orders.py) | Generates four synthetic flavors — `uniform`, `clustered`, `sweep`, `adversarial` — each stressing a different code path (book depth, hot crossing, sweep walking, fat-finger). |

---

## `scripts/` — Bricks 15, 16

| File | What it does |
|---|---|
| [scripts/run_bench.sh](scripts/run_bench.sh) | Linux-only reproducibility harness: sets the `performance` governor, disables Turbo, locks pages, pins the bench to core 2 via `taskset`, optionally generates a flame graph via `perf` + Brendan Gregg's `FlameGraph` scripts. |

---

## `docs/` — Reports and Deliverables

| File | Purpose |
|---|---|
| [docs/REPORT.md](docs/REPORT.md) | Performance Benchmark Report (FEC deliverable #2) |
| [docs/SUBMISSION.md](docs/SUBMISSION.md) | Cover letter to mentors |
| [docs/flame_combined.svg](docs/flame_combined.svg) | CPU flame graph (FEC deliverable #3) |
| [docs/hardware.md](docs/hardware.md) | Hardware and toolchain specs |

---

## Brick → file index

| Brick | Title | Files |
|---|---|---|
| 1  | Project skeleton                          | `CMakeLists.txt`, `.gitignore` |
| 2  | Core types                                | `include/nanomatch/types.hpp` |
| 3  | Naive baseline                            | `include/nanomatch/order_book_naive.hpp`, `src/order_book_naive.cpp` |
| 4  | Synthetic generator                       | `data/generate_orders.py` |
| 5  | Unit tests                                | `tests/test_order_book.cpp` |
| 6  | Object pool                               | `include/nanomatch/object_pool.hpp` |
| 7  | Bucket book                               | `include/nanomatch/order_book.hpp` |
| 8  | Intrusive list w/ pool indices            | same file (`list_push_back` / `list_unlink`) |
| 9  | Fast cancel hash                          | same file (`id_index_`) |
| 10 | ITCH parser (mmap + dispatch)             | `include/nanomatch/file_map.hpp`, `src/file_map.cpp`, `include/nanomatch/itch_parser.hpp`, `src/itch_parser.cpp` |
| 11 | SPSC ring buffer                          | `include/nanomatch/spsc_ring.hpp` |
| 12 | Async logger thread                       | `include/nanomatch/logger.hpp`, `src/logger.cpp` |
| 13 | rdtsc + histogram                         | `include/nanomatch/timing.hpp`, `include/nanomatch/histogram.hpp` |
| 14 | Benchmark harness                         | `benchmarks/bench_match.cpp` |
| 15 | Reproducible env                          | `scripts/run_bench.sh` |
| 16 | perf + flame graphs                       | `scripts/run_bench.sh` (perf section) |
| 17 | `perf c2c`                                | command in `scripts/run_bench.sh` (manual) |
| 18 | Hot-path polish (`[[likely]]`, PGO, LTO)  | applied inline across hot files; CMake flags TBD |
| 19 | Optional advanced (SIMD, huge pages, NUMA)| not implemented — see blueprint §Brick 19 |
| 20 | Narrative README                          | `README.md` |
