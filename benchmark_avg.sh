#!/bin/bash
# benchmark_avg.sh
#
# Runs ~40 searches with find and ffind, accumulates all timings,
# and prints a single overall average + % speed difference at the end.
#
# Run setup_testenv.sh first, then: bash benchmark_avg.sh

# %R = elapsed real (wall clock) time — correct metric for a multi-threaded tool.
# CPU time (user+sys) would always make ffind look slower because it uses
# a thread pool that burns more total CPU even when finishing faster.
TIMEFORMAT='%R'

# --- Colors ---
GREEN="\033[1;32m"
RED="\033[1;31m"
GRAY="\033[0;37m"
BOLD="\033[1m"
NC="\033[0m"

# --- Guards ---
if [ ! -d "testdir" ]; then
    echo -e "${RED}Error:${NC} testdir/ not found. Run  bash setup_testenv.sh  first."
    exit 1
fi
if [ ! -x "./build/ffind" ]; then
    echo -e "${RED}Error:${NC} ./build/ffind not found. Run  make  first."
    exit 1
fi

# ============================================================
# Full search list — 40 searches run verbatim against both
# find and ffind. Format: "DIR|ARG1 ARG2 ..."
# ============================================================
SEARCHES=(
    "testdir|-name *.txt"
    "testdir|-name *.c"
    "testdir|-name *.md"
    "testdir|-name *.log"
    "testdir|-name *.dat"
    "testdir|-type f"
    "testdir|-type d"
    "testdir|-not -name *.o"
    "testdir|-not -name *.out"
    "testdir|-name *.txt -type f"
    "testdir|-name *.c -type f"
    "testdir|-name *.txt -a -type f"
    "testdir|-name *.c -o -name *.h"
    "testdir|-name *.txt -o -name *.md"
    "testdir|-name *.c -o -name *.h -o -name *.txt"
    "testdir|( -name *.txt -o -name *.md ) -type f"
    "testdir|( -name *.c -o -name *.h ) -type f"
    "testdir|-not ( -name *.o -o -name *.out )"
    "testdir|-not -name *.o -not -name *.out -not -name *.log"
    "testdir|-name *.txt -type f -not -name *stress*"
    "testdir|-size +1c"
    "testdir|-size +1c -type f"
    "testdir|-size +1c -name *.txt"
    "testdir|-empty"
    "testdir|-mtime -2"
    "testdir|-mtime -2 -type f"
    "testdir|-mtime -2 -name *.txt"
    "testdir|-name *.txt -mtime -2 -type f"
    "testdir|-name *.log -o -name *.dat -type f"
    "testdir|( -name *.txt -o -name *.c ) -type f -not -name *stress*"
    "testdir|-name *.txt -type f -size +1c -o -name *.md -type f"
    "testdir|-type f -not -name *.o -not -name *.out"
    "/etc|-name *.conf"
    "/etc|-type f"
    "/etc|-name *.conf -type f"
    "/etc|-not -name *.conf -type f"
    "/etc|-name *.conf -o -name *.cfg"
    "/usr/bin|-type f"
    "/usr/bin|-name find -o -name grep -o -name awk"
    "/usr/bin|-type f -not -name python*"
)

total_find=0
total_ffind=0
count_find=0
count_ffind=0
stalls_find=0
stalls_ffind=0
total=${#SEARCHES[@]}

echo "Running $total searches × 2 (find + ffind) ..."
echo ""

for i in "${!SEARCHES[@]}"; do
    IFS='|' read -r dir expr_str <<< "${SEARCHES[$i]}"
    read -ra expr_tokens <<< "$expr_str"

    n=$((i + 1))
    printf "\r  [%2d/%d] %-55s" "$n" "$total" "$dir ${expr_tokens[*]}"

    # Skip if dir does not exist
    if [ ! -d "$dir" ]; then
        ((stalls_find++))
        ((stalls_ffind++))
        continue
    fi

    # --- find ---
    ts=$( { time timeout 3s find "$dir" "${expr_tokens[@]}" >/dev/null 2>&1; } 2>&1 )
    rc=$?
    if [[ $rc -eq 124 ]]; then
        ((stalls_find++))
    else
        t=$(echo "$ts" | awk '{print $1}')
        total_find=$(awk -v a="$total_find" -v b="$t" 'BEGIN{printf "%.6f", a+b}')
        ((count_find++))
    fi

    # --- ffind ---
    ts=$( { time timeout 3s ./build/ffind "$dir" "${expr_tokens[@]}" >/dev/null 2>&1; } 2>&1 )
    rc=$?
    if [[ $rc -eq 124 ]]; then
        ((stalls_ffind++))
    else
        t=$(echo "$ts" | awk '{print $1}')
        total_ffind=$(awk -v a="$total_ffind" -v b="$t" 'BEGIN{printf "%.6f", a+b}')
        ((count_ffind++))
    fi
done

printf "\r  Done.%-60s\n" ""
echo ""
echo "========================================"
echo " RESULTS  ($total searches)"
echo "========================================"

avg_find=$(awk  -v t="$total_find"  -v n="$count_find"  'BEGIN{ if(n>0) printf "%.6f",t/n; else print "N/A" }')
avg_ffind=$(awk -v t="$total_ffind" -v n="$count_ffind" 'BEGIN{ if(n>0) printf "%.6f",t/n; else print "N/A" }')

echo -e " find  — avg wall time: ${GRAY}${avg_find}s${NC}  (${stalls_find} stalls / $total)"
echo -e " ffind — avg wall time: ${GREEN}${avg_ffind}s${NC}  (${stalls_ffind} stalls / $total)"
echo ""

if [[ "$avg_find" == "N/A" || "$avg_ffind" == "N/A" ]]; then
    echo -e " speedup: ${RED}cannot compute (all stalls)${NC}"
else
    pct=$(awk -v f="$avg_find" -v g="$avg_ffind" 'BEGIN{
        if(f==0){ print "N/A"; exit }
        printf "%.1f", (f-g)/f*100
    }')
    if (( $(echo "$pct > 0" | bc -l) )); then
        echo -e " speedup: ${GREEN}${BOLD}ffind is ${pct}% faster on average${NC}"
    elif (( $(echo "$pct < 0" | bc -l) )); then
        abs=$(echo "$pct" | tr -d -)
        echo -e " speedup: ${RED}${BOLD}ffind is ${abs}% SLOWER on average${NC}"
    else
        echo -e " speedup: ${GRAY}no measurable difference${NC}"
    fi
fi
echo "========================================"
