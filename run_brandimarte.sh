#!/usr/bin/env bash



set -u -o pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
INPUT_DIR="${INPUT_DIR:-$ROOT_DIR/datasets/SmartLab_Challenge_2020}"
ALGORITHM_MODE="${ALGORITHM_MODE:-HLS}"
if [[ -n "${LOG_DIR:-}" ]]; then
    LOG_DIR="$LOG_DIR"
elif [[ "$ALGORITHM_MODE" == "HLS" ]]; then
    LOG_DIR="$ROOT_DIR/logs"
else
    LOG_DIR="$ROOT_DIR/logs/$ALGORITHM_MODE"
fi
BIN="${PRODUCTION_SCHEDULING_BIN:-$ROOT_DIR/build/production_scheduling}"

RUNS=10
MAX_CONCURRENT="${MAX_CONCURRENT:-5}"
SEARCH_MODE_PARAM="${SEARCH_MODE_PARAM:-0.5}"
RANDOM_SEARCH_STRATEGY_PARAM="${RANDOM_SEARCH_STRATEGY_PARAM:-0.5}"
MAX_REPEAT_COUNT="${MAX_REPEAT_COUNT:-100}"
MAX_ITER_COUNT="${MAX_ITER_COUNT:-30}"
POPULATION_SIZE="${POPULATION_SIZE:-20}"
TABU_LIST_LENGTH="${TABU_LIST_LENGTH:-25}"

# main.cpp seeds rand() with time(nullptr); stagger launches by default so
# concurrent processes are less likely to receive the same seed.
LAUNCH_DELAY="${LAUNCH_DELAY:-1}"

if [[ ! -x "$BIN" ]]; then
    echo "Error: executable not found or not executable: $BIN" >&2
    echo "Build it first with: cmake -S . -B build && cmake --build build -j" >&2
    exit 1
fi

if [[ ! -d "$INPUT_DIR" ]]; then
    echo "Error: input directory not found: $INPUT_DIR" >&2
    exit 1
fi

case "$ALGORITHM_MODE" in
    HLS|GS|GA|TS|GSGA|GSTS|GATS) ;;
    *)
        echo "Error: unsupported ALGORITHM_MODE: $ALGORITHM_MODE" >&2
        echo "Use HLS, GS, GA, TS, GSGA, GSTS, or GATS." >&2
        exit 1
        ;;
esac

mapfile -t INSTANCES < <(
    find "$INPUT_DIR" -maxdepth 1 -type f \
        -name 'fjsp.brandimarte.Mk*.txt' -print | sort -V
)

if [[ "${#INSTANCES[@]}" -ne 10 ]]; then
    echo "Error: expected 10 Brandimarte instances, found ${#INSTANCES[@]} in $INPUT_DIR" >&2
    exit 1
fi

mkdir -p "$LOG_DIR"
RESULTS_FILE="$LOG_DIR/results.csv"

if ! [[ "$MAX_CONCURRENT" =~ ^[1-9][0-9]*$ ]]; then
    echo "Error: MAX_CONCURRENT must be a positive integer: $MAX_CONCURRENT" >&2
    exit 1
fi

{
    printf 'instance'
    for ((run = 1; run <= RUNS; run++)); do
        printf ',run_%02d' "$run"
    done
    printf '\n'
} > "$RESULTS_FILE"

extract_makespan() {
    # The current executable prints the final makespan as the second-to-last
    # non-empty line, followed by the elapsed time as a bare integer.
    awk '
        NF { previous = current; current = $0 }
        END {
            if (previous ~ /^[[:space:]]*[0-9]+[[:space:]]*$/) {
                gsub(/[[:space:]]/, "", previous)
                print previous
                exit 0
            }
            exit 1
        }
    ' "$1"
}

RUN_RESULT_DIR="$(mktemp -d "$LOG_DIR/.run-results.XXXXXX")"
trap 'rm -rf -- "$RUN_RESULT_DIR"' EXIT

run_one() {
    local instance_path="$1"
    local log_file="$2"
    local result_file="$3"

    "$BIN" "$instance_path" \
        "$SEARCH_MODE_PARAM" \
        "$RANDOM_SEARCH_STRATEGY_PARAM" \
        "$MAX_REPEAT_COUNT" \
        "$MAX_ITER_COUNT" \
        "$POPULATION_SIZE" \
        "$TABU_LIST_LENGTH" \
        "$ALGORITHM_MODE" > "$log_file" 2>&1
    local return_code=$?

    local result=""
    if [[ "$return_code" -eq 0 ]]; then
        result="$(extract_makespan "$log_file" 2>/dev/null || true)"
    fi

    if [[ "$result" =~ ^[0-9]+$ ]]; then
        printf '%s\n' "$result" > "$result_file"
    else
        printf 'NA\n' > "$result_file"
        return 1
    fi
}

pids=()
for instance_path in "${INSTANCES[@]}"; do
    instance_name="$(basename "$instance_path")"
    instance_stem="${instance_name%.txt}"

    for ((run = 1; run <= RUNS; run++)); do
        run_label="$(printf '%02d' "$run")"
        log_file="$LOG_DIR/${instance_stem}.run${run_label}.log"
        result_file="$RUN_RESULT_DIR/${instance_stem}.run${run_label}.result"

        echo "Running $instance_name (run $run/$RUNS)"
        run_one "$instance_path" "$log_file" "$result_file" &
        pids+=("$!")

        if [[ "$LAUNCH_DELAY" != "0" ]]; then
            sleep "$LAUNCH_DELAY"
        fi

        if (( ${#pids[@]} >= MAX_CONCURRENT )); then
            for pid in "${pids[@]}"; do
                wait "$pid" || true
            done
            pids=()
        fi
    done
done

for pid in "${pids[@]}"; do
    wait "$pid" || true
done

failures=0
for instance_path in "${INSTANCES[@]}"; do
    instance_name="$(basename "$instance_path")"
    instance_stem="${instance_name%.txt}"
    printf '%s' "$instance_name" >> "$RESULTS_FILE"

    for ((run = 1; run <= RUNS; run++)); do
        run_label="$(printf '%02d' "$run")"
        result_file="$RUN_RESULT_DIR/${instance_stem}.run${run_label}.result"
        if [[ -f "$result_file" ]]; then
            result="$(< "$result_file")"
        else
            result="NA"
        fi
        if [[ ! "$result" =~ ^[0-9]+$ ]]; then
            result="NA"
            failures=$((failures + 1))
            log_file="$LOG_DIR/${instance_stem}.run${run_label}.log"
            echo "Warning: failed to extract Makespan for $instance_name run $run; see $log_file" >&2
        fi
        printf ',%s' "$result" >> "$RESULTS_FILE"
    done
    printf '\n' >> "$RESULTS_FILE"
done

echo "Results written to: $RESULTS_FILE"
echo "Run logs written to: $LOG_DIR"

if [[ "$failures" -ne 0 ]]; then
    echo "Completed with $failures failed run(s)." >&2
    exit 1
fi
