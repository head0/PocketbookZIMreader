#!/bin/sh
# Cross-compiles ZIMReader.app for PocketBook e-readers.
#   PB_SDK=/path/to/SDK-B288 ./build.sh
# SDK: git clone --depth 1 -b 6.5 https://github.com/pocketbook/SDK_6.3.0
set -e
cd "$(dirname "$0")"
: "${PB_SDK:?set PB_SDK to the SDK-B288 directory of the PocketBook SDK}"
P="$PB_SDK/usr/bin/arm-obreey-linux-gnueabi"
# the SDK compiler needs its own bundled libmpfr/libmpc
run() { LD_LIBRARY_PATH="$PB_SDK/usr/lib" "$@"; }
ARCH="-march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=softfp"
W=third_party/libwebp
INC="-Ithird_party -I$W -I$W/src"
OUT=build/pb
mkdir -p "$OUT"
for f in third_party/zstddeclib.c $W/src/dec/*.c $W/src/dsp/*.c $W/src/utils/*.c; do
  o=$OUT/$(echo "$f" | tr '/' '_').o
  [ "$o" -nt "$f" ] || run "$P-gcc" -O2 -DNDEBUG $ARCH $INC -w -c "$f" -o "$o"
done
for f in zim html layout image app pb_main; do
  run "$P-g++" -std=c++14 -O2 -DNDEBUG $ARCH -fsigned-char -Wall $INC -c src/$f.cpp -o $OUT/$f.o
done
run "$P-g++" $OUT/*.o -o $OUT/ZIMReader.app -s -static-libstdc++ -static-libgcc \
  -Wl,-Bstatic -llzma -Wl,-Bdynamic -linkview -lpthread -lm
ls -l $OUT/ZIMReader.app
