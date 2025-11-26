#! /bin/bash

GEM5_ROOT="/home/kenzoy0426/reproduce-ghostminion-paper/gem5"
GAPBS_ROOT="/home/kenzoy0426/reproduce-ghostminion-paper/micro-bench/gapbs"
OUTPUT_DIR="gapbs_results_$(date +%Y%m%d_%H%M%S)"

GEM5_BIN="$GEM5_ROOT/build/ARM/gem5.opt"
SE_SCRIPT="$GEM5_ROOT/configs/example/se.py"

BENCHMARKS=("bc" "bfs" "cc" "pr" "sssp" "tc")

# how many running in parallel
MAX_JOBS=2

#change here
GAPBS_ARGS="-g 10 -n 1"
GEM5_ARGS="--caches --l2cache --cpu-type=DerivO3CPU --maxinsts=1000000000 --mem-size=4096MB --ghostminion --prefetch_ordered"

mkdir -p "$OUTPUT_DIR"
echo "Starting GAPBS PARALLEL batch run (Limit: $MAX_JOBS jobs)."
echo "Results and logs will be saved to: $OUTPUT_DIR"

for bench in "${BENCHMARKS[@]}"; do
    CMD_PATH="$GAPBS_ROOT/$bench"
    if [ ! -f "$CMD_PATH" ]; then
        echo "Error: Binary not found at $CMD_PATH. Skipping..."
        continue
    fi

    while [ $(jobs -p | wc -l) -ge $MAX_JOBS ]; do
        sleep 5
    done

    BENCH_OUT_DIR="$OUTPUT_DIR/$bench"
    mkdir -p "$BENCH_OUT_DIR"

    CMD="$GEM5_BIN -d $BENCH_OUT_DIR $SE_SCRIPT -c $CMD_PATH -o \"$GAPBS_ARGS\" $GEM5_ARGS"
    echo "Launching $bench (Active Jobs: $(jobs -p | wc -l))..."

    (
        eval "$CMD" > "$BENCH_OUT_DIR/console.log" 2>&1
        if [ $? -eq 0 ]; then
            echo "✅ $bench finished successfully."
        else
            echo "❌ $bench failed. Check $BENCH_OUT_DIR/console.log"
        fi
    ) &

    sleep 2
done

echo "----------------------------------------------------------------"
echo "All tasks launched. Waiting for remaining jobs to complete..."
echo "You can check progress by looking at 'console.log' in each subfolder."
echo "----------------------------------------------------------------"

wait

echo "================================================================"
echo "All parallel runs completed."

