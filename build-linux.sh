#!/bin/sh
# Builds the Linux version (2.4BatteryMonitor) with g++, GTK4 and Cairo. Windows uses build.bat instead.
# Everything Linux-only lives in linux/ and src/linux/; the Windows build never compiles those files.
#   ./build-linux.sh          build ./2.4BatteryMonitor
#   ./build-linux.sh test     run the Linux HID parser test
#   ./build-linux.sh clean    remove the Linux build output
set -e
cd "$(dirname "$0")"
exec make -f linux/Makefile.linux "$@"
