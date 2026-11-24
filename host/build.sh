#!/bin/sh
# Builds the PC test harness (no PocketBook SDK needed).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
W=third_party/libwebp
CFLAGS="-O2 -g -Ithird_party -I$W -I$W/src"
for f in third_party/zstddeclib.c $W/src/dec/*.c $W/src/dsp/*.c $W/src/utils/*.c; do
  o=build/host/$(echo "$f" | tr '/' '_').o
  [ "$o" -nt "$f" ] || cc $CFLAGS -w -c "$f" -o "$o"
done
c++ -std=c++14 -Wall -Wextra $CFLAGS -fsanitize=address,undefined \
  src/zim.cpp src/html.cpp src/layout.cpp src/image.cpp src/app.cpp host/host_main.cpp \
  build/host/*.o -llzma -lpthread -o build/host/host_test
echo built build/host/host_test
