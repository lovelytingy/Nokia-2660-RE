#!/usr/bin/env bash
# Dump firmware partitions from Nokia 2660 Flip (TA-1480 / UMS9117) via spd_dump
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

SPD_DUMP="${REPO_ROOT}/tools/spd_dump"
FDL1="${REPO_ROOT}/firmware/extracted/v23_update_Nokia_2660/fdl1.bin"
FDL2="${REPO_ROOT}/firmware/extracted/v23_update_Nokia_2660/fdl2.bin"

# Output folder for dumps (matching confirmed software version)
OUTDIR="${REPO_ROOT}/firmware/dumps/ta1480_10.02.17.00"

echo "================================================================="
echo " Nokia 2660 Flip (TA-1480) Firmware Dumper — Part 2 (Assets & NV)"
echo " Target: Unisoc T107 / UMS9117 (MOCOR 10.02.17.00)"
echo " Output: ${OUTDIR}"
echo "================================================================="

# Pre-flight checks
if [[ ! -x "${SPD_DUMP}" ]]; then
  echo "[-] spd_dump executable not found at: ${SPD_DUMP}"
  exit 1
fi

if [[ ! -f "${FDL1}" ]] || [[ ! -f "${FDL2}" ]]; then
  echo "[-] FDL loaders not found. Expected:"
  echo "    FDL1: ${FDL1}"
  echo "    FDL2: ${FDL2}"
  exit 1
fi

mkdir -p "${OUTDIR}"

echo ""
echo "[*] INSTRUCTIONS:"
echo "    1. Ensure phone USB cable is DISCONNECTED."
echo "    2. Remove the battery from the phone (keep battery out)."
echo "    3. Press and HOLD the '*' (Asterisk) key on the keypad."
echo "    4. While holding '*', plug the USB cable into the phone and Mac."
echo ""
echo "[*] Starting spd_dump in listening mode (will wait up to 120s)..."
echo ""

"${SPD_DUMP}" \
  --wait 120 \
  --verbose 1 \
  keep_charge 1 \
  timeout 5000 \
  fdl "${FDL1}" 0x6200 \
  blk_size 0x1000 \
  fdl "${FDL2}" 0x80100000 \
  read_flash 0x90000001 0 0x100000 "${OUTDIR}/nvitem.bin" \
  read_flash 0x90000004 0 0x880000 "${OUTDIR}/mmi_res.bin" \
  read_flash 0x90000005 0 0x380000 "${OUTDIR}/lte_dsp.bin" \
  read_flash 0x90000006 0 0x300000 "${OUTDIR}/gsm_dsp.bin" \
  read_flash 0x90000013 0 0x20000  "${OUTDIR}/pm_cm4.bin"

echo ""
echo "[+] Dump finished successfully!"
echo "[*] Generating SHA-256 manifest..."

MANIFEST="${OUTDIR}/manifest.csv"
echo "filename,size,sha256" > "${MANIFEST}"

for f in "${OUTDIR}"/*.bin; do
  if [[ -f "$f" ]]; then
    BN="$(basename "$f")"
    SZ="$(stat -f%z "$f" 2>/dev/null || stat -c%s "$f")"
    HSH="$(shasum -a 256 "$f" | awk '{print $1}')"
    echo "${BN},${SZ},${HSH}" >> "${MANIFEST}"
    printf "  %-14s %10d bytes  %s\n" "${BN}" "${SZ}" "${HSH}"
  fi
done

echo ""
echo "[+] Manifest saved to: ${MANIFEST}"
echo "[+] All dumped partitions stored in: ${OUTDIR}"
