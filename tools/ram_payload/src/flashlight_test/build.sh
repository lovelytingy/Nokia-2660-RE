#!/usr/bin/env bash
# build.sh — Build custom Flashlight / Rear Torch Diagnostic payload using vendored fpdoom toolchain
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMMON_DIR="$(cd "${SRC_DIR}/../common" && pwd)"
BIN_DIR="$(cd "${SRC_DIR}/../../bin" && pwd)"
VENDOR_DIR="$(cd "${SRC_DIR}/../../vendor" && pwd)"
FPDOOM_DIR="${VENDOR_DIR}/fpdoom"

echo "=== Building Nokia 2660 Flip 'Flashlight Test' Payload ==="

if [[ ! -d "${FPDOOM_DIR}" ]]; then
    echo "[*] Initializing vendored fpdoom toolchain in ${FPDOOM_DIR}..."
    mkdir -p "${VENDOR_DIR}"
    git clone --depth 1 https://github.com/ilyakurdyukov/fpdoom.git "${FPDOOM_DIR}"
fi

# Ensure headers and font from common directory are present
cp "${SRC_DIR}/flashlight_test_main.c" "${FPDOOM_DIR}/fptest/main.c"
cp "${COMMON_DIR}/font8x16.h" "${FPDOOM_DIR}/fptest/font8x16.h"
cp "${COMMON_DIR}/elf.h" "${FPDOOM_DIR}/pack_reloc/elf.h"
cp "${COMMON_DIR}/string.h" "${FPDOOM_DIR}/fpdoom/include/string.h"
cp "${COMMON_DIR}/malloc.h" "${FPDOOM_DIR}/fpdoom/include/malloc.h"

# Fix EIC bitmask bug in upstream fpdoom syscode.c (prevents Key 8 on EIC Pin 9 from corrupting PB on Pin 1)
python3 -c "
path = '${FPDOOM_DIR}/ums9117/syscode.c'
with open(path, 'r') as f: s = f.read()
s = s.replace('return eic_read() >> EIC_PB ^ 1;', 'return (eic_read() >> EIC_PB & 1) ^ 1;')
with open(path, 'w') as f: f.write(s)
"

echo "[*] Compiling with arm-none-eabi-gcc (Cortex-A7)..."
make -C "${FPDOOM_DIR}/pack_reloc" CFLAGS="-I."
make -C "${FPDOOM_DIR}/fptest" clean T117=1
rm -rf "${FPDOOM_DIR}/fptest/obj" "${FPDOOM_DIR}/fptest/fptest.bin"
make -C "${FPDOOM_DIR}/fptest" T117=1 TOOLCHAIN=arm-none-eabi

mkdir -p "${BIN_DIR}"
cp "${FPDOOM_DIR}/fptest/fptest.bin" "${BIN_DIR}/flashlight_test.bin"

echo
echo "[+] Successfully built Flashlight Test payload:"
echo "    Output: ${BIN_DIR}/flashlight_test.bin ($(wc -c < "${BIN_DIR}/flashlight_test.bin" | tr -d ' ') bytes)"
