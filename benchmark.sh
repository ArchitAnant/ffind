#!/bin/bash

# --- Colors ---
PURPLE="\033[0;35m"
GRAY="\033[0;37m"
GREEN="\033[1;32m"
RED="\033[1;31m"
YELLOW="\033[1;33m"
NC="\033[0m"  # No Color

# --- Time format: user + sys only ---
TIMEFORMAT='%U %S'

# --- List of dirs to benchmark ---
dirs=("testdir" "/usr/bin" "/etc")

# --- Single regex ---
regex="*.txt"

# --- Benchmark loop ---
bench_id=1
for dir in "${dirs[@]}"; do
  echo -e "${PURPLE}BENCH $bench_id${NC} ${YELLOW}(dir=$dir, regex=$regex)${NC}"

  # --- Run find with 2 sec timeout ---
  timestr1=$( { timeout 2s time find "$dir" -name "$regex" >/dev/null; } 2>&1 )
  if [[ $? -eq 124 ]]; then  # timeout exit code
    total_time1="STALL"
    user_time1="STALL"
    sys_time1="STALL"
  else
    user_time1=$(echo "$timestr1" | awk '{print $1}')
    sys_time1=$(echo "$timestr1" | awk '{print $2}')
    total_time1=$(awk -v u="$user_time1" -v s="$sys_time1" 'BEGIN {print u+s}')
  fi

  # --- Run ffind with 2 sec timeout ---
  timestr2=$( { timeout 2s time ffind "$dir" "$regex" >/dev/null; } 2>&1 )
  if [[ $? -eq 124 ]]; then
    total_time2="STALL"
    user_time2="STALL"
    sys_time2="STALL"
  else
    user_time2=$(echo "$timestr2" | awk '{print $1}')
    sys_time2=$(echo "$timestr2" | awk '{print $2}')
    total_time2=$(awk -v u="$user_time2" -v s="$sys_time2" 'BEGIN {print u+s}')
  fi

  # --- Diff calculation ---
  if [[ "$total_time1" == "STALL" || "$total_time2" == "STALL" ]]; then
    diff="STALL"
    diff_color=$RED
  else
    diff=$(awk -v f="$total_time1" -v g="$total_time2" 'BEGIN {print f-g}')
    # --- Color logic for diff ---
    if (( $(echo "$diff > 0" | bc -l) )); then
      diff_color=$RED
    elif (( $(echo "$diff < 0" | bc -l) )); then
      diff_color=$GREEN
    else
      diff_color=$GRAY
    fi
  fi

  # --- Report ---
  echo -e "find:\t${GRAY}[$total_time1]${NC}"
  echo -e "ffind:\t${GREEN}[$total_time2]${NC}"
  echo -e "diff:\t${diff_color}[$diff]${NC}"
  echo ""

  ((bench_id++))
done
