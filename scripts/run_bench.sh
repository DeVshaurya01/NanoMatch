#!/usr/bin/env bash
# Brick 15 -- reproducible benchmark environment. Linux-only; on Windows just
# build with Release and run the binary directly.
set -euo pipefail

# 1. Performance governor (requires sudo)
if command -v cpupower >/dev/null; then
    sudo cpupower frequency-set -g performance >/dev/null
fi

# 2. Disable Turbo so TSC and frequency are stable
if [[ -w /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
    echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo >/dev/null
fi

# 3. Lock pages, avoid swap noise
ulimit -l unlimited || true

# 4. Build release
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 5. Pin to an isolated core if available. Add `isolcpus=2,3 nohz_full=2,3` to
#    your kernel cmdline first; here we just bind.
if command -v taskset >/dev/null; then
    taskset -c 2 build/nanomatch_bench "${@:-2000000}"
else
    build/nanomatch_bench "${@:-2000000}"
fi

# 6. Flame graph (Brick 16) -- requires `perf` + Brendan Gregg's FlameGraph
if command -v perf >/dev/null && [[ -d FlameGraph ]]; then
    perf record -F 997 -g --call-graph dwarf -- \
        taskset -c 2 build/nanomatch_bench 2000000
    perf script | FlameGraph/stackcollapse-perf.pl \
                | FlameGraph/flamegraph.pl > flame.svg
    echo "flame graph -> flame.svg"
fi
