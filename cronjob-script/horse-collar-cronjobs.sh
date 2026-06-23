#!/bin/bash

BASE_URL="https://horse-collar.sandro-raess.ch/api/cronjobs"
LOG_FILE="/var/log/horse-monitor/cronjobs.log"

mkdir -p "$(dirname "$LOG_FILE")"

timestamp() {
  date -u +"%Y-%m-%dT%H:%M:%SZ"
}

call_job() {
  local name="$1"
  local url="$2"

  # Capture HTTP status + curl errors
  response=$(curl -sS \
    -o /dev/null \
    -w "%{http_code}" \
    --fail-with-body \
    --max-time 30 \
    -X POST \
    -H "Content-Type: application/json" \
    -d '{}' \
    "$url")

  echo "$name -> $response"

  if [ "$response" -ge 200 ] && [ "$response" -lt 300 ]; then
    echo "$(timestamp) [OK]   $name -> $response" >> "$LOG_FILE"
    echo "ok"
  else
    echo "$(timestamp) [FAIL] $name -> $response" >> "$LOG_FILE"
    echo "error"
  fi
}

call_job "check-heartbeat" "$BASE_URL/check-heartbeat"
call_job "check-posture"   "$BASE_URL/check-posture"
