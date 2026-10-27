#!/bin/bash
# AGM H6 build of the topaz shim: compile in the hackios-tc container, then
# overlay the Image onto the stock boot_b image (layout + AVB footer kept),
# ready for `../tools/run-b-once.sh out/shim-boot_b.img`.
#   build-agm.sh [PAYLOAD]   PAYLOAD (tools/mkpayload.py output) rides behind the shim:
#                            loader.c looks for it at the first 4K boundary after _end.
set -euo pipefail
cd "$(dirname "$0")"
docker run --rm -u "$(id -u):$(id -g)" -e SHIM_CFLAGS="${SHIM_CFLAGS:-}" -v "$PWD":/work -w /work hackios-tc bash -c '
set -euo pipefail
CC=aarch64-linux-gnu-gcc
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra ${SHIM_CFLAGS:-}"
mkdir -p out
for f in start.S shim.c console.c mmu.c fdt.c loader.c ufs.c pmic.c cts.c; do
	$CC $CFLAGS -c $f -o out/${f%.*}.o
done
abs=$(aarch64-linux-gnu-readelf -r out/shim.o out/console.o out/mmu.o out/fdt.o out/loader.o out/ufs.o out/pmic.o out/cts.o | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
$CC $CFLAGS -static -T shim.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-N \
	out/start.o out/shim.o out/console.o out/mmu.o out/fdt.o out/loader.o out/ufs.o out/pmic.o out/cts.o -o out/shim.elf
aarch64-linux-gnu-objcopy -O binary out/shim.elf out/Image
size=$((16#$(aarch64-linux-gnu-nm out/shim.elf | awk "\$3==\"_end\"{print \$1}")))
truncate -s "$size" out/Image
'
python3 -I - ../backup/boot_b.img out/Image out/shim-boot_b.img "${1:-}" <<'PY'
import struct, sys
stock = bytearray(open(sys.argv[1], "rb").read())
image = bytearray(open(sys.argv[2], "rb").read())
if sys.argv[4]:
    payload = open(sys.argv[4], "rb").read()
    assert payload[:8] == b"SWEETXNU", "not a mkpayload.py payload"
    image += b"\0" * (-len(image) % 0x1000)
    image += payload
    struct.pack_into("<Q", image, 16, len(image))   # image_size covers the payload
    print("payload %d bytes appended" % len(payload))
assert stock[:8] == b"ANDROID!" and struct.unpack_from("<I", stock, 40)[0] == 4
assert stock[-64:-60] == b"AVBf"
ksize = struct.unpack_from("<I", stock, 8)[0]
assert len(image) <= ksize, "Image larger than the stock kernel slot"
stock[4096:4096 + ksize] = image.ljust(ksize, b"\0")
open(sys.argv[3], "wb").write(stock)
print("Image %d bytes -> %s" % (len(image), sys.argv[3]))
PY
