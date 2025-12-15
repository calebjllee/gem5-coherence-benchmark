# Cache Coherence Benchmark (gem5)

Minimal framework for evaluating **MI vs MESI vs MOESI** coherence behavior in gem5 Ruby using controlled shared-memory workloads.

## Files

- **`share_se.py`**  
  Custom SE config enabling Ruby coherence and multi-core shared-memory execution.

- **`coh_bench.c`**  
  Multithreaded coherence microbenchmark.  
./coh_bench <thread_id> <iters> <mode>

Modes: `0` false sharing, `1` no sharing (padded), `2` shared hot line.

- **`run_coherence.sh`**  
Runs parameter sweeps across protocols, core counts, and modes.  
Outputs results under `results/`.

- **`generate_stats_csv.py`**  
Parses gem5 `stats.txt` files and produces CSVs for analysis.

## Usage

place coh_bench. in the tests folder and compile with ```bash gcc coh_bench.c -O2 -pthread -o coh_bench ```

in the gem5 directory
```bash
./run_coherence.sh
python3 generate_stats_csv.py results/
```
