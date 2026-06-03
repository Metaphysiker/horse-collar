#!/bin/bash
set -e

FQBN="esp32:esp32:featheresp32"
PORT="${PORT:-/dev/ttyUSB0}"
SKETCH="firmware-huzzah32-guardian.ino"

cd "$(dirname "$0")"

echo "Compiling..."
arduino-cli compile --fqbn "$FQBN" "$SKETCH"

echo "Uploading..."
arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$SKETCH"

CONFIG=$(grep '#include "config' "$SKETCH" | grep -o '"config[^"]*"' | tr -d '"')
cat > last-flash.txt <<EOF
date:   $(date '+%Y-%m-%d %H:%M:%S')
config: $CONFIG
commit: $(git rev-parse --short HEAD 2>/dev/null || echo "no git")
EOF
echo "Done. Logged to last-flash.txt. Run ./monitor.sh to see serial output."
