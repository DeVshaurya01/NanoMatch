#!/usr/bin/env bash
# One-shot flame graph generation in WSL2 (Ubuntu). Required deliverable for
# the FEC NanoMatch spec ("Visual Profiling Evidence — CPU Flame Graphs").
#
# Prereqs (run once inside WSL2):
#   sudo apt update
#   sudo apt install -y build-essential cmake ninja-build \
#                       linux-tools-generic linux-tools-$(uname -r) \
#                       git
#   git clone https://github.com/brendangregg/FlameGraph.git
#
# Then from the project root inside WSL2:
#   bash scripts/wsl_flamegraph.sh
#
# Output: docs/flame_baseline.svg + docs/flame_optimized.svg
set -euo pipefail

if [[ ! -d FlameGraph ]]; then
  echo "FlameGraph repo not found; cloning..."
  git clone https://github.com/brendangregg/FlameGraph.git
fi

mkdir -p docs build

# Build in Release with frame pointers (already set in CMakeLists for GCC).
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Performance governor (best effort).
if command -v cpupower >/dev/null; then
    sudo cpupower frequency-set -g performance >/dev/null 2>&1 || true
fi

# Pin to a single core to keep TSC stable.
PIN=${PIN:-2}
# Bigger run -> more samples -> sharper flame graph. ~10 M events takes
# only a couple of seconds on the optimized path.
EVENTS=${EVENTS:-10000000}

echo "=== recording (naive + optimized, $EVENTS events) ==="
sudo perf record -F 4000 -g --call-graph dwarf -o build/perf.data \
    -- taskset -c $PIN build/nanomatch_bench $EVENTS
sudo perf script -i build/perf.data \
    | FlameGraph/stackcollapse-perf.pl \
    | FlameGraph/flamegraph.pl \
        --title "NanoMatch -- naive vs optimized matcher" \
    > docs/flame_combined.svg

echo "=== perf stat (cache + branch breakdown -- needs hardware PMU) ==="
echo "Note: WSL2 typically returns <not supported> for cache events because"
echo "the Hyper-V kernel does not expose PMU counters. Use Cachegrind for"
echo "L1 miss attribution instead (see README)."
echo
sudo perf stat -d -d -d -- taskset -c $PIN build/nanomatch_bench $EVENTS \
    2> docs/perf_stat.txt || true
cat docs/perf_stat.txt

echo
echo "outputs:"
echo "  docs/flame_combined.svg   (open in a browser)"
echo "  docs/perf_stat.txt        (L1/L2/L3 + branch miss rates)"
