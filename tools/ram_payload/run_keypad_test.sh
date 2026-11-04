#!/usr/bin/env bash
# run_keypad_test.sh — Launch bare-metal Hardware Keypad Matrix Diagnostic Tool on Nokia 2660 Flip (TA-1480)
# Interactive diagnostic tool: maps all 26 keys, tests PMIC SC2720 EIC Power/End, displays matrix coordinates in real-time.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SPD_DUMP="${REPO_ROOT}/tools/spd_dump"
FDL1="${REPO_ROOT}/firmware/extracted/v23_update_Nokia_2660/fdl1.bin"
PAYLOAD="${SCRIPT_DIR}/bin/keypad_test.bin"
LIBC_SERVER="${SCRIPT_DIR}/bin/libc_server"
DATA_DIR="${SCRIPT_DIR}/data"
PINMAP="${DATA_DIR}/pinmap.bin"
KEYMAP="${DATA_DIR}/keymap.bin"

echo "================================================================="
echo "  Nokia 2660 Flip (TA-1480) Hardware Keypad Diagnostic Tool"
echo "================================================================="
echo "SAFETY GUARANTEE: This script loads code STRICTLY into volatile DRAM."
echo "                  NAND flash is NEVER written or modified."
echo "                  Zero risk of bricking."
echo

SCREEN_TARGET="${1:-main}"
if [[ "${SCREEN_TARGET}" == "cover" ]]; then
    echo "[*] Display Target: Outer Cover Screen (1.77\" 128x160 GC9104)"
    DISP_ARGS=(--gpio_data 0x28,-0x5f --bl_extra w=0x2d --bright 80)
else
    echo "[*] Display Target: Inner Main Screen (2.8\" 240x320 GC9307)"
    echo "    (Make sure to OPEN the flip to view the screen!)"
    echo "    Tip: To test the outer cover screen instead, pass 'cover': ./run_keypad_test.sh cover"
    DISP_ARGS=(--gpio_data -0x28,0x5f --bl_extra rgb=0x19 --bright 80)
fi
echo

# Pre-flight checks
for f in "${SPD_DUMP}" "${FDL1}" "${PAYLOAD}" "${LIBC_SERVER}" "${PINMAP}" "${KEYMAP}"; do
    if [[ ! -e "$f" ]]; then
        echo "[-] Error: Required file missing: $f" >&2
        exit 1
    fi
done

chmod +x "${SPD_DUMP}" "${LIBC_SERVER}"

echo "[*] Preparing bare-metal RAM injection..."
echo "    INSTRUCTIONS:"
echo "    1. Disconnect phone from USB and ensure battery is OUT."
echo "    2. Press the red Power / End key 2-3 times while unplugged (drains residual capacitor charge)."
echo "    3. Press and firmly HOLD the '*' (Asterisk) key on the keypad."
echo "    4. While holding '*', plug the USB cable into the phone and computer."
echo
echo "[*] Waiting for phone connection in BootROM mode (120s timeout)..."

# Step 1: Inject FDL1 and our bare-metal keypad_test payload into DRAM
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
echo "[+] Stage 1 complete: Payload successfully injected and executed at 0x80100000 in DRAM!"
echo "[*] Stage 2: Handing off to libc_server for hardware display & keypad event logging..."
echo

# Step 2: Switch to libc_server to drive SPI display, backlight, and keypad events
cd "${DATA_DIR}"
exec "${LIBC_SERVER}" --wait 15 --verbose 0 -- "${DISP_ARGS[@]}" fptest display
