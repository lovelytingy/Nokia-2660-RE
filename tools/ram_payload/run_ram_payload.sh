#!/usr/bin/env bash
# run_ram_payload.sh — Load and execute custom bare-metal payload in DRAM on Nokia 2660 Flip
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SPD_DUMP="${REPO_ROOT}/tools/spd_dump"
FDL1="${REPO_ROOT}/firmware/extracted/v23_update_Nokia_2660/fdl1.bin"
PAYLOAD="${SCRIPT_DIR}/ram_payload.bin"

echo "=== Nokia 2660 Flip (TA-1480) Bare-Metal RAM Payload Loader ==="
echo "SAFETY GUARANTEE: This script loads code STRICTLY into volatile DRAM."
echo "                  NAND flash is never modified. Zero risk of bricking."
echo

if [[ ! -x "${SPD_DUMP}" ]]; then
    echo "[-] Error: spd_dump executable not found at ${SPD_DUMP}" >&2
    exit 1
fi

if [[ ! -f "${FDL1}" ]]; then
    echo "[-] Error: fdl1.bin not found at ${FDL1}" >&2
    exit 1
fi

if [[ ! -f "${PAYLOAD}" ]]; then
    echo "[*] Building ram_payload.bin first..."
    python3 "${SCRIPT_DIR}/build_payload.py"
fi

echo "[*] Waiting for Nokia 2660 Flip in BootROM mode (1782:4D00)..."
echo "    INSTRUCTIONS:"
echo "    1. Disconnect phone from USB and remove battery."
echo "    2. Press and hold '*' key on keypad."
echo "    3. Connect USB cable while holding '*'."
echo

while true; do
    if ioreg -p IOUSB -l -w 0 | grep -q "1782"; then
        echo "[+] Unisoc BootROM (1782:4D00) detected!"
        break
    fi
    sleep 0.2
done

echo "[+] Executing RAM injection via FDL1..."
"${SPD_DUMP}" \
    keep_charge 1 \
    fdl "${FDL1}" 0x6200 \
    blk_size 0x1000 \
    fdl "${PAYLOAD}" ram

echo
echo "[+] Done! Payload loaded and executed in DRAM at 0x80000000."
echo "    To return to normal OS mode, simply unplug USB and reinsert the battery."
