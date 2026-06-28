#!/bin/bash
# benchmark_avg.sh
#
# Runs each benchmark N times and reports average CPU time + % speed difference.
# Tests a variety of expression complexities to stress both the parser and evaluator.
#
# Run setup_testenv.sh first, then: bash benchmark_avg.sh [runs]
#
# Usage:
#   bash benchmark_avg.sh          # default: 10 runs per benchmark
#   bash benchmark_avg.sh 20       # 20 runs per benchmark

RUNS="${1:-10}"

# --- Colors ---
PURPLE="\033[0;35m"
GRAY="\033[0;37m"
GREEN="\033[1;32m"
RED="\033[1;31m"
YELLOW="\033[1;33m"
BOLD="\033[1m"
NC="\033[0m"

TIMEFORMAT='%U %S'

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
# Test cases: each entry is "LABEL|DIR|EXPR..."
# The EXPR tokens (everything after the second |) are passed
# as separate shell arguments to both find and ffind.
# ============================================================
TEST_CASES=(
    # ---------- simple ----------
    "simple name match              |testdir|-name *.txt"
    "simple type filter             |testdir|-type f"
    "simple NOT                     |testdir|-not -name *.txt"

    # ---------- compound / implicit AND ----------
    "name AND type (implicit AND)   |testdir|-name *.txt -type f"
    "name AND type (explicit -a)    |testdir|-name *.c -a -type f"
    "name AND NOT                   |testdir|-name *.txt -not -type d"

    # ---------- OR expressions ----------
    "two extensions OR              |testdir|-name *.txt -o -name *.md"
    "three extensions OR            |testdir|-name *.c -o -name *.h -o -name *.txt"

    # ---------- parenthesised groups ----------
    "paren OR then AND type         |testdir|( -name *.txt -o -name *.md ) -type f"
    "paren NOT group                |testdir|-not ( -name *.o -o -name *.out )"
    "nested parens                  |testdir|( -name *.c -o ( -name *.h -type f ) )"

    # ---------- stat-requiring predicates ----------
    "empty files                    |testdir|-empty"
    "size > 1 byte                  |testdir|-size +1c"
    "size > 1 byte AND name         |testdir|-size +1c -name *.txt"
    "modified < 2 days              |testdir|-mtime -2"
    "modified < 2 days AND name     |testdir|-mtime -2 -name *.txt"

    # ---------- longer compound chains ----------
    "long chain AND OR mix          |testdir|-name *.txt -type f -size +1c -o -name *.md -type f"
    "long NOT chain                 |testdir|-not -name *.o -not -name *.out -not -name *.log"
    "complex paren chain            |testdir|( -name *.txt -o -name *.c ) -type f -not -name *stress*"

    # ---------- system dirs (real-world feel) ----------
    "name match on /etc             |/etc|-name *.conf"
    "type f on /etc                 |/etc|-type f"
    "name AND type on /etc          |/etc|-name *.conf -type f"
    "NOT name on /etc               |/etc|-not -name *.conf -type f"
    "OR names on /usr/bin           |/usr/bin|-name find -o -name grep -o -name awk"
)

echo -e "${BOLD}Runs per benchmark: $RUNS${NC}"
echo ""

# ---- Helper: run one timed command, return total CPU time or "STALL" ----
run_once() {
    local -n _out=$1
    shift
    local timestr rc
    timestr=$( { time timeout 2s "$@" >/dev/null 2>&1; } 2>&1 )
    rc=$?
    if [[ $rc -eq 124 ]]; then
        _out="STALL"
    else
        local u s
        u=$(echo "$timestr" | awk '{print $1}')
        s=$(echo "$timestr" | awk '{print $2}')
        _out=$(awk -v u="$u" -v s="$s" 'BEGIN { printf "%.6f", u+s }')
    fi
}

# ---- Helper: average space-separated numbers, skipping STALLs ----
average() {
    echo "$@" | tr ' ' '\n' | grep -v STALL | \
        awk '{sum+=$1; n++} END { if(n>0) printf "%.6f", sum/n; else print "STALL" }'
}

# ---- Helper: % faster (positive = ffind faster) ----
pct_diff() {
    awk -v f="$1" -v g="$2" 'BEGIN {
        if (f==0) { print "N/A"; exit }
        printf "%.1f", (f - g) / f * 100
    }'
}

bench_id=1
for tc in "${TEST_CASES[@]}"; do
    # Parse the test case: split on '|'
    IFS='|' read -r label dir expr_str <<< "$tc"
    label=$(echo "$label" | sed 's/[[:space:]]*$//')   # trim trailing spaces

    # Split expr_str into an array of tokens
    read -ra expr_tokens <<< "$expr_str"

    if [ ! -d "$dir" ]; then
        echo -e "${YELLOW}[$bench_id] Skipping \"$label\" — $dir not found${NC}"
        echo ""
        ((bench_id++))
        continue
    fi

    echo -e "${PURPLE}[$bench_id]${NC} ${BOLD}${label}${NC}"
    echo -e "     ${GRAY}dir: $dir  |  expr: ${expr_str}${NC}"

    find_times=()
    ffind_times=()
    stall_find=0
    stall_ffind=0

    for run in $(seq 1 "$RUNS"); do
        printf "\r     Run %d/%d ..." "$run" "$RUNS"

        run_once t1 find   "$dir" "${expr_tokens[@]}"
        run_once t2 ./build/ffind "$dir" "${expr_tokens[@]}"

        find_times+=("$t1")
        ffind_times+=("$t2")
        [[ "$t1" == "STALL" ]] && ((stall_find++))
        [[ "$t2" == "STALL" ]] && ((stall_ffind++))
    done
    printf "\r     Done.              \n"

    avg_find=$(average  "${find_times[@]}")
    avg_ffind=$(average "${ffind_times[@]}")

    echo -e "     find  avg: ${GRAY}${avg_find}s${NC}  (${stall_find}/${RUNS} stalls)"
    echo -e "     ffind avg: ${GREEN}${avg_ffind}s${NC}  (${stall_ffind}/${RUNS} stalls)"

    if [[ "$avg_find" == "STALL" || "$avg_ffind" == "STALL" ]]; then
        echo -e "     speedup:  ${RED}[STALL — cannot compare]${NC}"
    else
        pct=$(pct_diff "$avg_find" "$avg_ffind")
        if (( $(echo "$pct > 0" | bc -l) )); then
            echo -e "     speedup:  ${GREEN}${BOLD}ffind is ${pct}% faster${NC}"
        elif (( $(echo "$pct < 0" | bc -l) )); then
            abs=$(echo "$pct" | tr -d -)
            echo -e "     speedup:  ${RED}${BOLD}ffind is ${abs}% SLOWER${NC}"
        else
            echo -e "     speedup:  ${GRAY}no measurable difference${NC}"
        fi
    fi

    echo ""
    ((bench_id++))
done
