#!/usr/bin/env bash
# build.sh — Build custom "Hello World" display payload using fpdoom toolchain
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$(cd "${SRC_DIR}/../../bin" && pwd)"
FPDOOM_DIR="/tmp/fpdoom"

echo "=== Building Nokia 2660 Flip 'Hello World' Payload ==="

if [[ ! -d "${FPDOOM_DIR}" ]]; then
    echo "[*] Cloning fpdoom repository to ${FPDOOM_DIR}..."
    git clone --depth 1 https://github.com/ilyakurdyukov/fpdoom.git "${FPDOOM_DIR}"
fi

# Ensure headers and font are present
cp "${SRC_DIR}/hello_world_main.c" "${FPDOOM_DIR}/fptest/main.c"
cp "${SRC_DIR}/font8x16.h" "${FPDOOM_DIR}/fptest/font8x16.h"

echo "[*] Compiling with arm-none-eabi-gcc (Cortex-A7)..."
make -C "${FPDOOM_DIR}/pack_reloc" CFLAGS="-I."
make -C "${FPDOOM_DIR}/fptest" clean
make -C "${FPDOOM_DIR}/fptest" T117=1 TOOLCHAIN=arm-none-eabi

mkdir -p "${BIN_DIR}"
cp "${FPDOOM_DIR}/fptest/fptest.bin" "${BIN_DIR}/hello_world.bin"

echo
echo "[+] Successfully built Hello World payload:"
echo "    Output: ${BIN_DIR}/hello_world.bin ($(wc -c < "${BIN_DIR}/hello_world.bin" | tr -d ' ') bytes)"
