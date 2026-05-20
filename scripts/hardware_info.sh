#!/usr/bin/env bash
# Dump the hardware / toolchain / OS info recruiters expect to see attached
# to any latency benchmark. Output goes to docs/hardware.md (and stdout).
# Works in both MSYS2 (Windows) and Linux/WSL2.
set -u

mkdir -p docs
OUT=docs/hardware.md

{
  echo "# Benchmark Environment"
  echo
  echo "_Generated: $(date)_"
  echo
  echo "## CPU"
  if command -v lscpu >/dev/null 2>&1; then
      lscpu | sed 's/^/    /'
  elif command -v wmic >/dev/null 2>&1; then
      wmic cpu get Name,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed,L2CacheSize,L3CacheSize /format:list \
          | tr -d '\r' | grep -v '^$' | sed 's/^/    /'
  else
      echo "    (no lscpu/wmic available)"
  fi
  echo
  echo "## Memory"
  if command -v free >/dev/null 2>&1; then
      free -h | sed 's/^/    /'
  elif command -v wmic >/dev/null 2>&1; then
      wmic ComputerSystem get TotalPhysicalMemory /format:list \
          | tr -d '\r' | grep -v '^$' | sed 's/^/    /'
  fi
  echo
  echo "## OS"
  if command -v uname >/dev/null 2>&1; then
      uname -a | sed 's/^/    /'
  fi
  if command -v ver >/dev/null 2>&1; then
      cmd.exe //c ver 2>/dev/null | tr -d '\r' | sed 's/^/    /' || true
  fi
  echo
  echo "## Toolchain"
  echo "    g++       : $(g++ --version | head -1)"
  echo "    cmake     : $(cmake --version | head -1)"
  echo "    ninja     : $(ninja --version 2>/dev/null || echo n/a)"
  echo
  echo "## Build flags"
  echo "    -O3 -march=native -DNDEBUG -fno-omit-frame-pointer (Release)"
  echo
} | tee "$OUT"

echo
echo "wrote $OUT"
