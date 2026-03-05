#!/usr/bin/env bash
set -euo pipefail

RUNS="${1:-5}"
MIN_MEDIAN_RTF="${MIN_MEDIAN_RTF:-1.15}"
BENCHMARK_BIN="${BENCHMARK_BIN:-./build/debug/vb_engine_benchmark}"

if ! [[ "$RUNS" =~ ^[0-9]+$ ]] || [ "$RUNS" -lt 1 ]; then
  echo "RUNS must be a positive integer" >&2
  exit 2
fi

factors=()
for i in $(seq 1 "$RUNS"); do
  output=$("$BENCHMARK_BIN")
  echo "$output"
  rtf=$(echo "$output" | awk '/Realtime factor:/{gsub("x","",$3); print $3}')
  if [ -z "$rtf" ]; then
    echo "Failed to parse realtime factor on run $i" >&2
    exit 2
  fi
  factors+=("$rtf")
done

median=$(printf '%s\n' "${factors[@]}" | sort -g | awk 'BEGIN{idx=0} {vals[++idx]=$1} END {mid=int((idx+1)/2); print vals[mid]}')
mean=$(printf '%s\n' "${factors[@]}" | awk '{s+=$1} END {printf("%.6f", s/NR)}')

printf 'Performance summary: benchmark=%s median_rtf=%s mean_rtf=%s threshold=%s\n' \
  "$BENCHMARK_BIN" "$median" "$mean" "$MIN_MEDIAN_RTF"

if awk -v median="$median" -v min="$MIN_MEDIAN_RTF" 'BEGIN {exit (median+0 >= min+0) ? 0 : 1}'; then
  echo "PASS: median realtime factor meets threshold"
else
  echo "FAIL: median realtime factor below threshold" >&2
  exit 1
fi
