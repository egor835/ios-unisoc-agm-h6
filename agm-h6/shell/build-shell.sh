#!/bin/bash
# Build the shell userland for the AGM H6 iOS root on the Mac build host:
# toybox (sh, vi, coreutils-like applets), agm-shelld (root shell over TCP port 2323) and hfsgrow (grows
# the root volume over its partition), ad-hoc signed.
#   shell/build-shell.sh            -> shell/out/{toybox,agm-shelld}
# Env: MAC=user@host of the Mac (Xcode with the iPhoneOS SDK, Homebrew), TOYBOX_REV (default 6a0c0ef).
set -euo pipefail
cd "$(dirname "$0")"
MAC=${MAC:?set MAC=user@host of the Mac build host}
REV=${TOYBOX_REV:-6a0c0ef}
mkdir -p out
scp -q agm-shelld.c hfsgrow.c ents.plist "$MAC:/tmp/"
ssh "$MAC" REV="$REV" bash -s <<'MACEOF'
set -euo pipefail
eval "$(/opt/homebrew/bin/brew shellenv)"
brew list gnu-sed >/dev/null 2>&1 || brew install -q gnu-sed
export PATH="$(brew --prefix)/opt/gnu-sed/libexec/gnubin:$PATH"
cd ~
[ -d toybox ] || git clone -q https://github.com/landley/toybox.git
cd toybox
git fetch -q --depth 50 origin 2>/dev/null || true
git checkout -q "$REV"
make clean >/dev/null 2>&1 || true
make macos_defconfig >/dev/null
# the shell and a few extras on top of the macOS set (toysh is in pending/)
for c in SH SHIFT VI SYNC TR AWK; do sed -i "/CONFIG_$c is not set/d; /^CONFIG_$c=/d" .config; echo "CONFIG_$c=y" >> .config; done
yes "" 2>/dev/null | make oldconfig >/dev/null 2>&1 || true   # yes ends by SIGPIPE
# the iOS SDK has no sys/disk.h (lib/portability.c, block device size): borrow the macOS one
mkdir -p ios-inc/sys
cp "$(xcrun -sdk macosx --show-sdk-path)/usr/include/sys/disk.h" ios-inc/sys/
SDK=$(xcrun -sdk iphoneos --show-sdk-path)
F="-arch arm64 -isysroot $SDK -miphoneos-version-min=16.0"
CC="$(xcrun -sdk iphoneos -f clang)" CFLAGS="$F -I$PWD/ios-inc" LDFLAGS="$F" HOSTCC=cc NOSTRIP=1 make toybox >/tmp/toybox-ios.log 2>&1 ||
	{ grep error: /tmp/toybox-ios.log | sort -u | tail; exit 1; }
cp toybox /tmp/toybox && chmod u+w /tmp/toybox
cd /tmp
xcrun -sdk iphoneos clang $F -O2 -Wall -o agm-shelld agm-shelld.c
xcrun -sdk iphoneos clang $F -O2 -Wall -o hfsgrow hfsgrow.c
for f in toybox agm-shelld hfsgrow; do codesign -f -s - --entitlements ents.plist $f; done
MACEOF
scp -q "$MAC:/tmp/toybox" "$MAC:/tmp/agm-shelld" "$MAC:/tmp/hfsgrow" out/
ls -la out/
