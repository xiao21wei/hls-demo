#!/usr/bin/env bash

set -u -o pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
MAX_CONCURRENT="${MAX_CONCURRENT:-5}"

MODES=(GS GA TS GSGA GSTS GATS)

if ! [[ "$MAX_CONCURRENT" =~ ^[1-9][0-9]*$ ]]; then
    echo "Error: MAX_CONCURRENT must be a positive integer: $MAX_CONCURRENT" >&2
    exit 1
fi

if [[ ! -x "$ROOT_DIR/run_brandimarte.sh" ]]; then
    echo "Error: executable script not found: $ROOT_DIR/run_brandimarte.sh" >&2
    exit 1
fi

failed_modes=()
for mode in "${MODES[@]}"; do
    mode_log_dir="$ROOT_DIR/logs/$mode"
    echo "========================================"
    echo "Starting ablation mode: $mode"
    echo "Output directory: $mode_log_dir"
    echo "Experiments: 10 instances x 10 runs"
    echo "========================================"

    if ! ALGORITHM_MODE="$mode" \
        LOG_DIR="$mode_log_dir" \
        MAX_CONCURRENT="$MAX_CONCURRENT" \
        "$ROOT_DIR/run_brandimarte.sh"; then
        failed_modes+=("$mode")
        echo "Mode $mode failed; continuing with the next mode." >&2
    fi
done

echo "========================================"
echo "Ablation experiments finished."
echo "Total planned runs: 600"
echo "Results directory: $ROOT_DIR/logs"
echo "========================================"

if (( ${#failed_modes[@]} > 0 )); then
    echo "Failed modes: ${failed_modes[*]}" >&2
    exit 1
fi
