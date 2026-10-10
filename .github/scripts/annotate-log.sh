#!/usr/bin/env bash
# Usage: annotate-log.sh <log file> <title>
# Re-posts the interesting part of a failed step's log as GitHub annotations, so the
# failure is readable from the run summary without downloading raw logs.
set -u
log="$1"; title="$2"
[[ -f "$log" ]] || exit 0

emit() { # $1 = text block
  local text="$1"
  text="${text//'%'/'%25'}"; text="${text//$'\r'/}"; text="${text//$'\n'/'%0A'}"
  echo "::error title=${title}::${text}"
}

# 1) compiler / CMake error lines with a little context (max 6 annotations)
errors="$( (grep -n -E 'Undefined symbols|ld: |duplicate symbol' "$log"; grep -n -A3 -E '^ *"_.*", referenced from' "$log"; grep -n -E 'error:|CMake Error|FATAL_ERROR|fatal error' "$log") | head -60 | cut -c1-400)"
if [[ -n "$errors" ]]; then
  i=0
  while IFS= read -r -d '' chunk && (( i < 6 )); do emit "$chunk"; i=$((i+1)); done \
    < <(printf '%s\n' "$errors" | awk 'NR%10==1 && NR>1 {printf "\0"} {print}' ; printf '\0')
fi

# 2) the tail of the log (max 3 annotations)
tailtext="$(tail -n 90 "$log" | cut -c1-300)"
i=0
while IFS= read -r -d '' chunk && (( i < 3 )); do emit "$chunk"; i=$((i+1)); done \
  < <(printf '%s\n' "$tailtext" | awk 'NR%30==1 && NR>1 {printf "\0"} {print}' ; printf '\0')
