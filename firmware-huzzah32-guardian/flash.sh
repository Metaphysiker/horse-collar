#!/bin/bash
set -e

FQBN="esp32:esp32:featheresp32"
PORT="/dev/ttyUSB0"
SKETCH="firmware-huzzah32-guardian.ino"

cd "$(dirname "$0")"

echo "Compiling..."
arduino-cli compile --fqbn "$FQBN" "$SKETCH"

echo "Uploading..."
arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$SKETCH"

echo "Done. Run ../firmware-huzzah32/monitor.sh to see serial output."
