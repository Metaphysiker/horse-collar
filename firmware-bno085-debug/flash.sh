#!/bin/bash
set -e
cd "$(dirname "$0")/.."
arduino-cli compile --fqbn esp32:esp32:featheresp32 firmware-bno085-debug
arduino-cli upload  --fqbn esp32:esp32:featheresp32 --port "${PORT:-/dev/ttyUSB0}" firmware-bno085-debug
