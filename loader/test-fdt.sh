#!/bin/bash
set -eu
cd /mnt/e/default-workspace/z80u-collect/efi-loader
gcc -O1 -Wall -o fdt_test p2_fdt_test.c 2>&1 | head -20
./fdt_test /mnt/e/default-workspace/z80u-collect/efi-loader/p2_test.dtb
echo "=== file ==="
ls -la p2_test.dtb
echo "=== dtc decompile ==="
if command -v dtc >/dev/null 2>&1; then
    dtc -I dtb -O dts -o p2_test.dts p2_test.dtb 2>&1 | head -5
    cat p2_test.dts
else
    echo "dtc not installed; hexdump header:"
    xxd -l 64 p2_test.dtb
fi
