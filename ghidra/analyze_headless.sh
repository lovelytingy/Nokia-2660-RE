#!/usr/bin/env bash
#
# analyze_headless.sh — Headless Ghidra Analyzer for Nokia 2660 Loaders
#
# Automatically locates Ghidra, creates/updates the Ghidra project at
# ghidra/projects/Nokia2660_Loaders, imports raw loader binaries at their
# verified hardware base addresses, runs auto-analysis, and exports notes/C code.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

PROJECT_DIR="${REPO_ROOT}/ghidra/projects"
PROJECT_NAME="Nokia2660_Loaders"
EXPORTS_DIR="${REPO_ROOT}/ghidra/exports"
SCRIPTS_DIR="${REPO_ROOT}/ghidra/scripts"

# 1. Locate analyzeHeadless
find_ghidra() {
    if [[ -n "${GHIDRA_HOME:-}" && -x "${GHIDRA_HOME}/support/analyzeHeadless" ]]; then
        echo "${GHIDRA_HOME}/support/analyzeHeadless"
        return
    fi

    if command -v analyzeHeadless >/dev/null 2>&1; then
        command -v analyzeHeadless
        return
    fi

    # Check common Homebrew locations (macOS Apple Silicon & Intel)
    local brew_ghidra
    brew_ghidra="$(find /opt/homebrew/Cellar/ghidra /usr/local/Cellar/ghidra -name analyzeHeadless -type f 2>/dev/null | sort -V | tail -n 1 || true)"
    if [[ -n "${brew_ghidra}" && -x "${brew_ghidra}" ]]; then
        echo "${brew_ghidra}"
        return
    fi

    # Check Applications / opt
    local app_ghidra
    app_ghidra="$(find /Applications /opt -maxdepth 4 -name analyzeHeadless -type f 2>/dev/null | sort -V | tail -n 1 || true)"
    if [[ -n "${app_ghidra}" && -x "${app_ghidra}" ]]; then
        echo "${app_ghidra}"
        return
    fi

    echo ""
}

ANALYZER="$(find_ghidra)"

if [[ -z "${ANALYZER}" ]]; then
    echo "[-] Error: analyzeHeadless not found." >&2
    echo "    Please install Ghidra ('brew install --cask ghidra' or package manager)" >&2
    echo "    or set GHIDRA_HOME=/path/to/ghidra" >&2
    exit 1
fi

echo "[*] Using Ghidra Analyzer: ${ANALYZER}"
mkdir -p "${PROJECT_DIR}" "${EXPORTS_DIR}"

import_binary() {
    local bin_path="$1"
    local bin_name="$2"
    local base_addr="$3"

    if [[ ! -f "${bin_path}" ]]; then
        echo "[!] Skipping ${bin_name}: file not found at ${bin_path}"
        return
    fi

    echo ""
    echo "================================================================="
    echo "[*] Importing ${bin_name} @ ${base_addr} into ${PROJECT_NAME}..."
    echo "================================================================="

    "${ANALYZER}" \
        "${PROJECT_DIR}" "${PROJECT_NAME}" \
        -import "${bin_path}" \
        -processor ARM:LE:32:v7 \
        -cspec default \
        -loader BinaryLoader \
        -loader-baseAddr "${base_addr}" \
        -overwrite
}

# Verified hardware load base addresses:
# boot0.bin: 0x00006200 (IRAM / SRAM, reset entry at 0x6420)
# boot1.bin: 0x80100000 (DDR, reset entry at 0x80100220)
# fdl1.bin:  0x00006200 (IRAM / SRAM, download mode)
# fdl2.bin:  0x80100000 (DDR, download mode)

import_binary "${REPO_ROOT}/firmware/dumps/ta1480_10.02.17.00/boot0.bin" "boot0.bin" "0x00006200"
import_binary "${REPO_ROOT}/firmware/dumps/ta1480_10.02.17.00/boot1.bin" "boot1.bin" "0x80100000"
import_binary "${REPO_ROOT}/firmware/extracted/v23_update_Nokia_2660/fdl1.bin" "fdl1.bin" "0x00006200"
import_binary "${REPO_ROOT}/firmware/extracted/v23_update_Nokia_2660/fdl2.bin" "fdl2.bin" "0x80100000"

echo ""
echo "================================================================="
echo "[*] Running analysis scripts & exporting notes..."
echo "================================================================="

"${ANALYZER}" \
    "${PROJECT_DIR}" "${PROJECT_NAME}" \
    -process '*' \
    -postScript ExportLoaderNotes.java \
    -scriptPath "${SCRIPTS_DIR}"

echo ""
echo "================================================================="
echo "[*] Exporting decompiled C code for boot0.bin..."
echo "================================================================="

"${ANALYZER}" \
    "${PROJECT_DIR}" "${PROJECT_NAME}" \
    -process boot0.bin \
    -postScript ExportDecompiledCode.java \
    -scriptPath "${SCRIPTS_DIR}"

echo ""
echo "[+] Done! Ghidra project ready at: ${PROJECT_DIR}/${PROJECT_NAME}.gpr"
echo "    Exported notes and C code saved in: ${EXPORTS_DIR}/"
ls -la "${EXPORTS_DIR}"
