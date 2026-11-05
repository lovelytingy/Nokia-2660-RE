#!/usr/bin/env bash
# build.sh — Build standalone bare-metal LCD graphics payload for Unisoc UMS9117
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$(cd "${SRC_DIR}/../../bin" && pwd)"

echo "=== Building Nokia 2660 Flip 'Bare-Metal LCD' Payload ==="

arm-none-eabi-gcc \
    -mcpu=cortex-a7 \
    -marm \
    -fPIE \
    -fno-builtin \
    -nostdlib \
    -O2 \
    -I"${SRC_DIR}/../common" \
    -Wl,-Ttext,0x80100200 \
    "${SRC_DIR}/entry.S" \
    "${SRC_DIR}/payload.c" \
    -o "${SRC_DIR}/payload.elf"

arm-none-eabi-objcopy -O binary "${SRC_DIR}/payload.elf" "${SRC_DIR}/payload.raw"
RAW_SIZE=$(wc -c < "${SRC_DIR}/payload.raw" | tr -d ' ')

python3 -c "
import struct
dhtb = bytearray(512)
dhtb[:4] = b'DHTB'
struct.pack_into('<I', dhtb, 4, 1)
struct.pack_into('<I', dhtb, 0x30, ${RAW_SIZE})
with open('${SRC_DIR}/payload.raw', 'rb') as f_in, open('${BIN_DIR}/baremetal_lcd.bin', 'wb') as f_out:
    f_out.write(dhtb)
    f_out.write(f_in.read())
"

rm -f "${SRC_DIR}/payload.elf" "${SRC_DIR}/payload.raw"

echo "[+] Successfully built bare-metal LCD payload:"
echo "    Output: ${BIN_DIR}/baremetal_lcd.bin ($(wc -c < "${BIN_DIR}/baremetal_lcd.bin" | tr -d ' ') bytes)"
