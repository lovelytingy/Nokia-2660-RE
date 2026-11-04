#!/usr/bin/env bash
# build.sh — Build custom "Hello World" display payload using fpdoom toolchain
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$(cd "${SRC_DIR}/../../bin" && pwd)"
VENDOR_DIR="$(cd "${SRC_DIR}/../../vendor" && pwd)"
FPDOOM_DIR="${VENDOR_DIR}/fpdoom"

echo "=== Building Nokia 2660 Flip 'Hello World' Payload ==="

if [[ ! -d "${FPDOOM_DIR}" ]]; then
    echo "[*] Initializing vendored fpdoom toolchain in ${FPDOOM_DIR}..."
    mkdir -p "${VENDOR_DIR}"
    git clone --depth 1 https://github.com/ilyakurdyukov/fpdoom.git "${FPDOOM_DIR}"
fi

# Ensure headers and font are present
cp "${SRC_DIR}/hello_world_main.c" "${FPDOOM_DIR}/fptest/main.c"
cp "${SRC_DIR}/font8x16.h" "${FPDOOM_DIR}/fptest/font8x16.h"
cp "${SRC_DIR}/elf.h" "${FPDOOM_DIR}/pack_reloc/elf.h"
cp "${SRC_DIR}/string.h" "${FPDOOM_DIR}/fpdoom/include/string.h"
cp "${SRC_DIR}/malloc.h" "${FPDOOM_DIR}/fpdoom/include/malloc.h"

# Fix EIC bitmask bug in upstream fpdoom syscode.c (prevents Key 8 on EIC Pin 9 from corrupting PB on Pin 1)
python3 -c "
path = '${FPDOOM_DIR}/ums9117/syscode.c'
with open(path, 'r') as f: s = f.read()
s = s.replace('return eic_read() >> EIC_PB ^ 1;', 'return (eic_read() >> EIC_PB & 1) ^ 1;')
with open(path, 'w') as f: f.write(s)
"

echo "[*] Compiling with arm-none-eabi-gcc (Cortex-A7)..."
make -C "${FPDOOM_DIR}/pack_reloc" CFLAGS="-I."
make -C "${FPDOOM_DIR}/fptest" clean
make -C "${FPDOOM_DIR}/fptest" T117=1 TOOLCHAIN=arm-none-eabi

mkdir -p "${BIN_DIR}"
cp "${FPDOOM_DIR}/fptest/fptest.bin" "${BIN_DIR}/hello_world.bin"

echo
echo "[+] Successfully built Hello World payload:"
echo "    Output: ${BIN_DIR}/hello_world.bin ($(wc -c < "${BIN_DIR}/hello_world.bin" | tr -d ' ') bytes)"
