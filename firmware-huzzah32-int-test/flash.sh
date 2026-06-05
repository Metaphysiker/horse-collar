#!/bin/bash
set -e

FQBN="esp32:esp32:featheresp32"
PORT="${PORT:-/dev/ttyUSB1}"
SKETCH="firmware-huzzah32-int-test.ino"

cd "$(dirname "$0")"

echo "Compiling INT test..."
arduino-cli compile --fqbn "$FQBN" "$SKETCH"

echo "Uploading INT test..."
arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$SKETCH"

echo "Done flashing INT test."
