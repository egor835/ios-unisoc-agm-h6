#!/bin/bash
# AGM H6: the GL pieces for backboardd, built on the Mac (after HackiOS build.sh GL=1 and topaz):
#   libagm-gl.dylib      topaz's eagl.c + gl_osmesa.c over ../agm-mesa/out/libagm-OSMesa.dylib
#   libagm-inject.dylib  agm_inject.c: loads libagm-gl into backboardd when AgmMaliGPU is there
set -e
cd "$(dirname "$0")"
M=../agm-mesa/out
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
CC="$(xcrun --sdk iphoneos -f clang) -target arm64-apple-ios16.0 -isysroot $SDK -O2 -Wall -Wno-unused-function"
mkdir -p obj out
python3 mkeagl.py eagl_slots.txt $M/gl.xml $M/exported-gl.txt eagl.c > obj/eagl_gen.h
$CC -dynamiclib -install_name /usr/lib/libagm-gl.dylib -Iobj -framework CoreFoundation -framework IOKit \
	-framework IOSurface -lobjc $M/libagm-OSMesa.dylib -o out/libagm-gl.dylib eagl.c gl_osmesa.c
$CC -dynamiclib -install_name /usr/lib/libagm-inject.dylib -framework CoreFoundation -framework IOKit -lobjc \
	-o out/libagm-inject.dylib agm_inject.c
cp $M/libagm-OSMesa.dylib out/
for f in out/*.dylib; do codesign -s - -f "$f"; done
ls -l out
