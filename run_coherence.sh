#!/usr/bin/env bash
set -euo pipefail

# ============================================================
#
# A) Core scaling @ write-heavy (rpw=0)
#    patterns: padded, false, hot
#    cores:    1,2,4,8,16
#
# B) Read/write sweep @ 16 cores
#    pattern:  hot
#    rpw:      0,9,99
# ============================================================

PROTOCOLS=("MI_example" "MESI_Two_Level" "MOESI_CMP_directory")

CORES_SCALING=(1 2 4 8 16)
PATTERNS_SCALING=("padded" "false" "hot")
RPW_SCALING=0

CORE_RPW=4
PATTERN_RPW="hot"
RPW_LIST=(0 1 2 4 8)

ITERS=10000
DEBUG=0

# ---- paths ----
GEM5=./build/ALL/gem5.opt
SEPY=configs/shared_se.py
BIN=./tests/coh_bench
RESULTS_DIR=results_coh

# ---- helpers ----
join_by() { local IFS="$1"; shift; echo "$*"; }

make_cmd_list() {
  local n="$1"
  local -a cmds=()
  for ((i=0; i<n; i++)); do cmds+=("$BIN"); done
  join_by ';' "${cmds[@]}"
}

# coh_bench args:
#   <tid> <nprocs> <iters> <pattern> <reads_per_write> [debug]
make_options_list() {
  local n="$1" iters="$2" pattern="$3" rpw="$4" debug="$5"
  local -a opts=()
  for ((i=0; i<n; i++)); do
    opts+=("$i $n $iters $pattern $rpw $debug")
  done
  join_by ';' "${opts[@]}"
}

run_one() {
  local protocol="$1" cores="$2" pattern="$3" rpw="$4" tag="$5"

  local outdir="${RESULTS_DIR}/${tag}/${protocol}_${cores}c_${pattern}_rpw${rpw}_iters${ITERS}"
  mkdir -p "$outdir"

  local cmd_list opt_list
  cmd_list="$(make_cmd_list "$cores")"
  opt_list="$(make_options_list "$cores" "$ITERS" "$pattern" "$rpw" "$DEBUG")"

  echo "=== [$tag] protocol=$protocol cores=$cores pattern=$pattern rpw=$rpw iters=$ITERS ==="

  "$GEM5" \
    --outdir="$outdir" \
    "$SEPY" \
    --ruby \
    --protocol="$protocol" \
    --num-cpus="$cores" \
    --cpu-type=X86TimingSimpleCPU \
    -n "$cores" \
    --mem-size=2GB \
    --l1d_size=32kB \
    --l1d_assoc=8 \
    --l2_size=1MB \
    --l2_assoc=16 \
    --cacheline_size=64 \
    --cmd="$cmd_list" \
    --options="$opt_list"
}

mkdir -p "$RESULTS_DIR"

# ------------------------------------------------------------
# A) Core scaling (rpw=0)
# ------------------------------------------------------------
for protocol in "${PROTOCOLS[@]}"; do
 for cores in "${CORES_SCALING[@]}"; do
   for pattern in "${PATTERNS_SCALING[@]}"; do
     run_one "$protocol" "$cores" "$pattern" "$RPW_SCALING" "A_core_scaling_rpw0"
   done
 done
done

# ------------------------------------------------------------
# B) Read/write sweep (hot only, 4 cores)
# ------------------------------------------------------------
for protocol in "${PROTOCOLS[@]}"; do
  for rpw in "${RPW_LIST[@]}"; do
    run_one "$protocol" "$CORE_RPW" "$PATTERN_RPW" "$rpw" "B_hot_rpw_sweep_4c"
  done
done
