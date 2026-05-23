#!/bin/bash
PORT="/dev/ttyUSB0"
arduino-cli monitor -p "$PORT" --config baudrate=115200
