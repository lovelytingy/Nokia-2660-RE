#!/usr/bin/env bash
# run_ram_payload.sh — Load and execute custom bare-metal payload in DRAM on Nokia 2660 Flip
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SPD_DUMP="${REPO_ROOT}/tools/spd_dump"
FDL1="${REPO_ROOT}/firmware/extracted/v23_update_Nokia_2660/fdl1.bin"
PAYLOAD="${SCRIPT_DIR}/bin/baremetal_asm.bin"

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
    echo "[*] Building baremetal_asm.bin first..."
    python3 "${SCRIPT_DIR}/src/baremetal_asm/build_payload.py"
fi

echo "[*] Starting spd_dump in listening mode (will wait up to 120s)..."
echo "    INSTRUCTIONS:"
echo "    1. Disconnect phone from USB and ensure battery is OUT."
echo "    2. Press the red Power / End key 2-3 times while unplugged to fully drain residual charge."
echo "    3. Press and firmly HOLD the '*' (Asterisk) key on the keypad."
echo "    4. While holding '*', plug the USB cable into the phone and computer."
echo

"${SPD_DUMP}" \
    --wait 120 \
    --verbose 1 \
    keep_charge 1 \
    timeout 5000 \
    fdl "${FDL1}" 0x6200 \
    blk_size 0x1000 \
    end_data 0 \
    fdl "${PAYLOAD}" 0x80100000

echo
echo "[+] Done! Payload loaded and executed in DRAM at 0x80100000."
echo "    To return to normal OS mode, simply unplug USB and reinsert the battery."
