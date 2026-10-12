#!/bin/bash

# ==============================================================================
# show_progress.sh
#
# Description:
#   Parses the output of a benchmark log stream to display progress bars for
#   the 'load' and 'run' phases.
#
#   - It reads log data from standard input.
#   - It prints the original log lines to standard output.
#   - It prints the progress bars to standard error.
#
# Usage:
#   ./ycsb_benchmark_command | tee log.txt | ./show_progress.sh [total_load_ops] [total_run_ops]
#
# Example:
#   ./ycsb -load -run -db rocksdb -threads 16 -P workloads/workloada -s -p status.interval=5 | tee ycsb.log | ./show_progress.sh 100000 100000
#
# Arguments:
#   [total_load_ops]: The total number of operations in the load phase.
#   [total_run_ops]:  The total number of operations in the run phase.
# ==============================================================================

# --- Argument Validation ---
if [[ -z "$1" || -z "$2" ]]; then
    echo "Usage: $0 [total_load_ops] [total_run_ops]" >&2
    exit 1
fi

TOTAL_LOAD_OPS=$1
TOTAL_RUN_OPS=$2

# --- Progress Bar Configuration ---
BAR_WIDTH=50
PHASE="load" # Start in the 'load' phase

# --- Progress Bar Drawing Function ---
# Arguments: 1:current_ops, 2:total_ops, 3:phase_name
function draw_progress_bar() {
    local current_ops=$1
    local total_ops=$2
    local phase_name=$3

    # Prevent division by zero if total_ops is 0
    if (( total_ops == 0 )); then
        return
    fi

    # 1. Calculate percentage (scale by 100 first for integer arithmetic)
    local percent=$(( (current_ops * 100) / total_ops ))

    # 2. Calculate the number of '#' and '-' characters
    local filled_width=$(( (percent * BAR_WIDTH) / 100 ))
    local empty_width=$(( BAR_WIDTH - filled_width ))

    # 3. Build the bar strings
    local filled_bar=""
    if (( filled_width > 0 )); then
        filled_bar=$(printf "%${filled_width}s" | tr ' ' '#')
    fi

    local empty_bar=""
    if (( empty_width > 0 )); then
        empty_bar=$(printf "%${empty_width}s" | tr ' ' '-')
    fi

    # 4. Print the progress bar to stderr and use \r to overwrite the line
    printf "\r\033[K%s Progress: [%s%s] %3d%% (%d/%d)" "$phase_name" "$filled_bar" "$empty_bar" "$percent" "$current_ops" "$total_ops" >&2
}

# --- Main Processing Loop ---
# Reads from stdin line by line
while IFS= read -r line; do
    #echo "$line"

    # --- State Transition Logic ---
    # Check if the load phase is finished, then switch to 'run'
    if [[ "$line" == *"Load operations(ops):"* ]]; then
        # Ensure the load bar finishes at 100%
        draw_progress_bar "$TOTAL_LOAD_OPS" "$TOTAL_LOAD_OPS" "Load"
        echo >&2 # Print a newline on stderr to move to the next line for the run bar
        PHASE="run"
        continue # Move to the next line
    fi

    # --- Progress Parsing Logic ---
    # Check if the line contains a progress update (e.g., "... X operations;")
    if [[ "$line" =~ ([0-9]+)\ operations\; ]]; then
        # Extract the number of operations (it's in the BASH_REMATCH array)
        current_ops=${BASH_REMATCH[1]}

        # Call the appropriate progress bar function based on the current phase
        if [[ "$PHASE" == "load" ]]; then
            draw_progress_bar "$current_ops" "$TOTAL_LOAD_OPS" "Load"
        elif [[ "$PHASE" == "run" ]]; then
            draw_progress_bar "$current_ops" "$TOTAL_RUN_OPS" "Run"
        fi
    fi
done

# After the loop, ensure the final progress bar is at 100%
if [[ "$PHASE" == "run" ]]; then
    draw_progress_bar "$TOTAL_RUN_OPS" "$TOTAL_RUN_OPS" "Run"
fi

echo >&2

exit 0

