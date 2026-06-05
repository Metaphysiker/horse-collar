#!/bin/bash
PORT="${PORT:-/dev/ttyUSB1}"

echo "Monitoring serial output on $PORT..."
arduino-cli monitor -p "$PORT" --config baudrate=115200
