#!/bin/bash
# AGM H6: Mesa 25.0.7 (softpipe + panfrost) for the iOS 16 userland, built on the Mac with Xcode's
# clang (after topaz's userland/mesa/mk-mesa-mac.sh). Run on the Mac from ~/agm-mesa, which holds
# mesa-25.0.7.tar.xz, mesa-agm.patch, agm-drm/ and the venv (meson, mako).
set -e
VERSION=25.0.7
SHA256=592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6
B=$(cd "$(dirname "$0")" && pwd)
cd "$B"
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
CC=$(xcrun --sdk iphoneos -f clang)
PATCHES=$(cat mesa-agm.patch | shasum -a 256 | cut -d' ' -f1)
if [ ! -d mesa-$VERSION ] || [ "$(cat mesa-$VERSION/.agm-patches 2>/dev/null)" != "$PATCHES" ]; then
	echo "$SHA256  mesa-$VERSION.tar.xz" | shasum -a 256 -c --quiet
	rm -rf mesa-$VERSION build-*
	tar xJf mesa-$VERSION.tar.xz
	patch -s -p1 -d mesa-$VERSION < mesa-agm.patch
	echo "$PATCHES" > mesa-$VERSION/.agm-patches
fi
rm -rf mesa-$VERSION/src/agm-drm
cp -r agm-drm mesa-$VERSION/src/agm-drm
export PATH=$B/venv/bin:/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/ninja/bin:$PATH
cat > cross.ini <<INI
[binaries]
c = ['$CC', '-target', 'arm64-apple-ios16.0', '-isysroot', '$SDK']
cpp = ['$CC++', '-target', 'arm64-apple-ios16.0', '-isysroot', '$SDK', '-fuse-cxa-atexit']
objc = ['$CC', '-target', 'arm64-apple-ios16.0', '-isysroot', '$SDK']
ar = '$(xcrun -f ar)'
ranlib = '$(xcrun -f ranlib)'
strip = '$(xcrun -f strip)'
pkg-config = 'false'

[host_machine]
system = 'darwin'
subsystem = 'ios'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'

[properties]
needs_exe_wrapper = true
INI
DRIVERS=${DRIVERS:-softpipe,panfrost}
BUILD=build-${DRIVERS//,/-}
[ -f $BUILD/build.ninja ] || meson setup $BUILD mesa-$VERSION --cross-file cross.ini --wrap-mode=nofallback \
	--prefix=/usr --libdir=lib --buildtype=release -Db_ndebug=true \
	-Dplatforms= -Dgallium-drivers="$DRIVERS" -Dvulkan-drivers= -Dglx=disabled -Degl=disabled -Dgbm=disabled \
	-Dosmesa=true -Dllvm=disabled -Dshared-glapi=enabled -Dgles1=disabled -Dgles2=enabled -Dopengl=true \
	-Dzstd=disabled -Dxmlconfig=disabled -Dexpat=disabled -Dshader-cache=disabled -Dvalgrind=disabled \
	-Dlibunwind=disabled -Dglvnd=disabled -Dvideo-codecs= -Dtools= -Dgallium-vdpau=disabled -Dgallium-va=disabled \
	-Dgallium-xa=disabled -Dlmsensors=disabled -Dbuild-tests=false > meson-setup.log 2>&1 || { tail -30 meson-setup.log; exit 1; }
ninja -C $BUILD > ninja.log 2>&1 || { grep -E "error|FAILED" ninja.log | head -40; exit 1; }
mkdir -p out
$(xcrun -f strip) -S -o out/libagm-OSMesa.dylib $BUILD/src/gallium/targets/osmesa/libOSMesa.8.dylib
install_name_tool -id /usr/lib/libagm-OSMesa.dylib out/libagm-OSMesa.dylib
nm -gU out/libagm-OSMesa.dylib | awk '$3 ~ /^_gl/ { print substr($3, 2) }' > out/exported-gl.txt
cp mesa-$VERSION/src/mapi/glapi/registry/gl.xml out/gl.xml
ls -l out
