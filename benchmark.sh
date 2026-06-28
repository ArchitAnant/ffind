#!/bin/bash
# benchmark.sh
#
# Compares GNU find vs ffind on the same directories.
# Run setup_testenv.sh first to create testdir/.
#
# Usage:  bash benchmark.sh

# --- Colors ---
PURPLE="\033[0;35m"
GRAY="\033[0;37m"
GREEN="\033[1;32m"
RED="\033[1;31m"
YELLOW="\033[1;33m"
NC="\033[0m"  # No Color

# --- Time format: user + sys only ---
TIMEFORMAT='%U %S'

# --- Check testdir exists ---
if [ ! -d "testdir" ]; then
    echo -e "${RED}Error:${NC} testdir/ not found."
    echo "Run  bash setup_testenv.sh  first to create it."
    exit 1
fi

# --- Check ffind binary exists ---
if [ ! -x "./build/ffind" ]; then
    echo -e "${RED}Error:${NC} ./build/ffind not found. Run  make  first."
    exit 1
fi

# --- List of dirs to benchmark ---
dirs=("testdir" "/usr/bin" "/etc")

# --- Predicate: what we search for ---
# GNU find:  find <dir> -name "*.txt"
# ffind:     ./build/ffind <dir> -name "*.txt"
PATTERN="*.txt"

# --- Benchmark loop ---
bench_id=1
for dir in "${dirs[@]}"; do

    # Skip dirs that don't exist (e.g. /usr/bin missing in minimal containers)
    if [ ! -d "$dir" ]; then
        echo -e "${YELLOW}Skipping $dir (not found)${NC}"
        echo ""
        ((bench_id++))
        continue
    fi

    echo -e "${PURPLE}BENCH $bench_id${NC} ${YELLOW}(dir=$dir, pattern=$PATTERN)${NC}"

    # --- Run find with 2 sec timeout ---
    timestr1=$( { timeout 2s time find "$dir" -name "$PATTERN" >/dev/null; } 2>&1 )
    rc1=$?
    if [[ $rc1 -eq 124 ]]; then
        total_time1="STALL"
        user_time1="STALL"
        sys_time1="STALL"
    else
        user_time1=$(echo "$timestr1" | awk '{print $1}')
        sys_time1=$(echo  "$timestr1" | awk '{print $2}')
        total_time1=$(awk -v u="$user_time1" -v s="$sys_time1" 'BEGIN {print u+s}')
    fi

    # --- Run ffind with 2 sec timeout ---
    # NOTE: ffind now uses GNU find-style expression syntax.
    #   ./build/ffind <dir> -name "*.txt"
    timestr2=$( { timeout 2s time ./build/ffind "$dir" -name "$PATTERN" >/dev/null; } 2>&1 )
    rc2=$?
    if [[ $rc2 -eq 124 ]]; then
        total_time2="STALL"
        user_time2="STALL"
        sys_time2="STALL"
    else
        user_time2=$(echo "$timestr2" | awk '{print $1}')
        sys_time2=$(echo  "$timestr2" | awk '{print $2}')
        total_time2=$(awk -v u="$user_time2" -v s="$sys_time2" 'BEGIN {print u+s}')
    fi

    # --- Diff calculation ---
    if [[ "$total_time1" == "STALL" || "$total_time2" == "STALL" ]]; then
        diff="STALL"
        diff_color=$RED
    else
        diff=$(awk -v f="$total_time1" -v g="$total_time2" 'BEGIN {print f-g}')
        # Positive diff = find was slower than ffind (ffind wins, show green)
        if (( $(echo "$diff > 0" | bc -l) )); then
            diff_color=$GREEN
        elif (( $(echo "$diff < 0" | bc -l) )); then
            diff_color=$RED
        else
            diff_color=$GRAY
        fi
    fi

    # --- Report ---
    echo -e "find: \t${GRAY}[$total_time1]${NC}"
    echo -e "ffind:\t${GREEN}[$total_time2]${NC}"
    echo -e "diff: \t${diff_color}[$diff]${NC}  (positive = ffind faster)"
    echo ""

    ((bench_id++))
done
