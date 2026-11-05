# Ghidra Analysis — Nokia 2660 Loaders

This directory contains the Ghidra reverse-engineering project, headless automation scripts, and decompiled exports for the **Nokia 2660 Flip (TA-1480 / Unisoc UMS9117)** bootloader stages and flash loaders.

---

## 🎯 Role in this Project: Ghidra vs Capstone

* **Ghidra (Primary Static Analysis & Decompilation):**
  Used for deep reverse engineering of firmware images, resolving function call graphs, analyzing anti-rollback and certificate verification routines, recovering hardware peripheral structures, and exporting decompiled C pseudo-code.
* **Capstone (Dynamic Verification):**
  Used in automated Python helper scripts for fast bytecode validation, test runner checks, and instruction inspection without launching the JVM.

---

## 📁 Project Overview

* **Project File:** [`ghidra/projects/Nokia2660_Loaders.gpr`](projects/Nokia2660_Loaders.gpr)
* **Project Directory:** `ghidra/projects/Nokia2660_Loaders.rep/`
* **Processor Architecture:** `ARM:LE:32:v7` (Thumb-heavy Cortex-A7)
* **Compiler Specification:** `default`

### Verified Hardware Base Addresses

The base addresses for raw binary imports have been verified through reset vector targets (`0x200` offset), CP15 initialization stubs, and `UMS9117.xml` flash tool schemes:

| Program | Hardware Role | Execution Domain | Verified Base Address | Reset Entry Point |
|---------|---------------|------------------|-----------------------|-------------------|
| `boot0.bin` | First NAND Stage Loader | IRAM / SRAM | **`0x00006200`** | `0x00006420` (ARM $\to$ `0x6F3C` Thumb) |
| `boot1.bin` | Second NAND Stage Loader | DDR RAM | **`0x80100000`** | `0x80100220` |
| `fdl1.bin` | First Download-Mode Loader | IRAM / SRAM | **`0x00006200`** | `0x00006420` |
| `fdl2.bin` | Second Download-Mode Loader | DDR RAM | **`0x80100000`** | `0x80100220` |

---

## 🚀 Quick Start: Automated Headless Analysis

You can build or refresh the entire Ghidra project, import all 4 loaders, run analysis, and export decompiled C code with a single command:

```bash
./ghidra/analyze_headless.sh
```

This script:
1. Automatically detects `analyzeHeadless` on your system (macOS Homebrew, Linux, `$GHIDRA_HOME`, or `$PATH`).
2. Imports all available dump and PAC loader binaries at their exact hardware load addresses.
3. Executes auto-analysis (instruction flow, data references, decompiler switch analysis).
4. Runs [`ExportLoaderNotes.java`](scripts/ExportLoaderNotes.java) to record memory blocks and identified functions.
5. Runs [`ExportDecompiledCode.java`](scripts/ExportDecompiledCode.java) to export complete decompiled C source files into [`exports/`](exports/).

---

## 🖥️ Interactive GUI Workflow

To explore the loaders interactively in the Ghidra GUI:

1. Open **Ghidra**.
2. Select **File $\to$ Open Project...**
3. Navigate to this repository and select:
   ```text
   ghidra/projects/Nokia2660_Loaders.gpr
   ```
4. Double-click on any of the imported programs (`boot0.bin`, `boot1.bin`, `fdl1.bin`, `fdl2.bin`) in the Project Manager.
5. In the CodeBrowser, jump to the reset vector entry point (e.g. `0x00006420` for `boot0.bin`) or explore decompiled functions in the Decompiler window.

### Manual Import Settings (if importing manually)

* **Format:** `Raw Binary`
* **Language:** `ARM` $\to$ `ARMv7` $\to$ `little` $\to$ `32` (`ARM:LE:32:v7`)
* **Options:**
  * For `boot0.bin` / `fdl1.bin`: Set **Base Address** to `0x00006200`.
  * For `boot1.bin` / `fdl2.bin`: Set **Base Address** to `0x80100000`.

---

## 📜 Ghidra Scripts

All custom automation scripts reside in [`ghidra/scripts/`](scripts/):

| Script | Purpose |
|--------|---------|
| [`ExportLoaderNotes.java`](scripts/ExportLoaderNotes.java) | Iterates memory blocks and logs identified function entry points and sizes to `exports/<program>_notes.txt`. |
| [`ExportDecompiledCode.java`](scripts/ExportDecompiledCode.java) | Invokes Ghidra's decompiler across all identified functions and writes formatted C pseudo-code to `exports/<program>_decompiled.c`. |

---

## 📦 Exported Artifacts

Even if you do not have Ghidra installed on your machine, you can read the analysis results and decompiled functions committed under [`ghidra/exports/`](exports/):

* [`boot0.bin_notes.txt`](exports/boot0.bin_notes.txt) — Memory map and function index for `boot0.bin`.
* [`boot0.bin_decompiled.c`](exports/boot0.bin_decompiled.c) — **195 decompiled C functions** from the TA-1480 first-stage bootloader.
* [`boot1.bin_notes.txt`](exports/boot1.bin_notes.txt) — Memory map and function index for `boot1.bin`.
* [`fdl1.bin_notes.txt`](exports/fdl1.bin_notes.txt) — Memory map and function index for `fdl1.bin`.
* [`fdl2.bin_notes.txt`](exports/fdl2.bin_notes.txt) — Memory map and function index for `fdl2.bin`.
