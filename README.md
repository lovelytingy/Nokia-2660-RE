# Nokia 2660 Flip (TA-1480) — Reverse Engineering & Custom Code

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: Unisoc UMS9117](https://img.shields.io/badge/SoC-Unisoc%20T107%20%2F%20UMS9117-orange.svg)](docs/hardware.md)
[![Arch: ARM Cortex-A7](https://img.shields.io/badge/Arch-ARMv7--A%20Cortex--A7-red.svg)](docs/boot-process.md)
[![Execution: Volatile DRAM](https://img.shields.io/badge/Payloads-Safe%20Volatile%20DRAM-brightgreen.svg)](docs/custom-code-execution.md)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-green.svg)](CONTRIBUTING.md)

Hardware documentation, BootROM protocol analysis, MOCOR firmware reverse engineering, and safe bare-metal custom code execution on the **Nokia 2660 Flip (TA-1480)** 4G feature phone.

<div align="center">
  <br>
  <img src="docs/images/nokia_2660_hello_world.jpg" width="400" alt="Nokia 2660 Flip physical hardware running custom bare-metal Hello World payload">
  <p><em>Physical hardware verification: Bare-metal "Hello World" payload executing in volatile DRAM on the Nokia 2660 Flip (TA-1480) 2.8" QVGA screen.</em></p>
  <br>
</div>

---

## 🎯 Project Mission

The **Nokia 2660 Flip** is a modern 4G VoLTE flip phone powered by the **Unisoc T107 (UMS9117)** chipset running Spreadtrum's **MOCOR** RTOS.

Our goal is to thoroughly document the platform internals, map out peripheral register controls, understand the boot and firmware verification chains, and pioneer **custom firmware and homebrew execution** on modern feature phones.

> [!TIP]
> **Zero Brick Risk:** All custom code demonstrations and diagnostic tools in this repository execute strictly in **volatile DRAM** (`0x80100000`) via USB download mode (FDL1). **Zero bytes are written to onboard NAND flash.** Unplugging the USB cable and reseating the battery immediately returns the device to 100% stock condition.

---

## ⚡ Current Milestones & Status

- ✅ **Chipset & USB Identification:** Reverse-engineered BootROM BSL handshake (`1782:4D00`) and normal Mass Storage mode (`1782:4D01`).
- ✅ **Firmware Package Unpacking:** Fully mapped Unisoc `BP_R1.0.0` PAC archive structure and built automated extractor tool (`tools/extract_pac.py`).
- ✅ **Boot & Download Protocol Mapping:** Documented complete FDL1 $\to$ FDL2 download sequence and `boot0` $\to$ `boot1` on-device boot chain ([docs/boot-process.md](docs/boot-process.md)).
- ✅ **Clean Reference Dump:** Complete partition dump from authentic physical hardware (TA-1480, SW `10.02.17.00`, HW3.0) — [firmware/dumps/ta1480_10.02.17.00/](firmware/dumps/ta1480_10.02.17.00/).
- ✅ **Peripheral Hardware Reverse Engineering:**
  - **Displays:** Dual SPI LCD controllers (Main 2.8" QVGA GC9307 & Outer 1.77" QQVGA GC9104).
  - **Input:** 4×5 APB Keypad Matrix scanner (`0x40250000`) and PMIC EIC Power button decoder (`0x40608280`).
  - **Power & Aux:** SC2720 PMIC rear torch LED sink (`0x40608df4`) and ERM haptic vibrator LDO (`0x40608e00`).
  - **Audio:** External loudspeaker PA gate (GPIO 31) and PMIC audio subsystem clocks (`0x40608c08`).
- ✅ **Application Layer Discovery:** Analyzed MOCOR monolithic C architecture in `user.bin` ([docs/application-layer.md](docs/application-layer.md)).
- ✅ **Tethered Custom Code Execution:** Bare-metal ARMv7-A execution in volatile DRAM with full hardware control ([docs/custom-code-execution.md](docs/custom-code-execution.md)).
- ✅ **Hardware Secure Boot & eFuse Audit:** Physical audit proved Gate 1 & Gate 2 are active (16-bit eFuse lock mask `0xFFFF` burned in Row 0); on-device boot strictly enforces RSA-2048 verification ([docs/boot-security-verification.md](docs/boot-security-verification.md)).
- ✅ **Firmware Triage & Static Analysis Suite:** Built automated analysis tools ([`tools/extract_strings.py`](tools/extract_strings.py) & [`tools/mocor-info.py`](tools/mocor-info.py)) to parse DHTB headers, RSA certificate chains, RTOS task tables, 560+ AT commands, and secret dialer codes ([docs/hidden-codes.md](docs/hidden-codes.md)).
- ✅ **Coprocessor & Auxiliary Firmware Decompilation:** Decompiled Cortex-M4 power management coprocessor (`pm_cm4.bin`) with 245 exported C functions in Ghidra, reverse-engineered `mmi_res.bin` RGB565 boot splashes, CEVA baseband DSP memory maps, and 28 acoustic calibration profiles in `nvitem.bin`.
- ✅ **Unicorn CPU & Hardware Emulator:** Built host-side ARMv7-A test environment ([`tools/emulator/`](tools/emulator/)) to emulate and validate bare-metal payloads, bootloader stages (`boot0`/`fdl1`), UART consoles, and PMIC registers before hardware flashing.
- 🔄 **Untethered Execution:** Exploring signature bypass, bootloader chain modification, or micro-payload injection from NAND flash.

---

## 📱 Hardware Reference (TA-1480)

Captured via dialer code **`*#0000#`** on physical reference hardware:

| Parameter | Specification | Details |
|-----------|---------------|---------|
| **Device Model** | Nokia 2660 Flip | Form factor: Clamshell / Dual-screen |
| **Type Designator** | `TA-1480` | Related regional SKUs: TA-1469, TA-1474, TA-1711 |
| **Hardware ID** | **HW3.0** | Board revision |
| **Software Version** | `10.02.17.00` | MOCOR OS build `MOCOR_20A_MP_W22.04.6_P4_Release` |
| **NDT Code** | `MH_13ARA80IN00_0` | Indian market SKU |
| **SoC / Chipset** | Unisoc T107 (`UMS9117`) | ARM Cortex-A7 single core @ up to 1.0 GHz |
| **Auxiliary Cores** | Cortex-M4 (PM) + CEVA DSP | Power Management (`pm_cm4.bin`) & LTE/GSM Baseband |
| **Memory** | 48 MB LPDDR | Integrated System-in-Package (SiP) |
| **Storage** | 128 MB NAND Flash | Partitioned for Boot, Kernel, MOCOR OS, NV, and U-Disk |
| **Main Display** | 2.8" QVGA (240×320) | SPI interface, GC9307 controller |
| **Cover Display** | 1.77" QQVGA (128×160) | SPI interface, GC9104 controller |
| **Power Management** | Unisoc SC2720 PMIC | Controls LDO rails, battery charging, flashlight, haptics |

*Full details: [docs/device.md](docs/device.md) and [docs/hardware.md](docs/hardware.md).*

---

## 🎮 Interactive Payloads & Demos

You can test custom code on your own Nokia 2660 Flip right now without modifying flash memory:

```text
Host PC (spd_dump) ──USB──> [BootROM 1782:4D00] ──loads──> [FDL1 in SRAM] ──executes──> [Custom Payload in DDR]
```

### Prebuilt Test Payloads

All precompiled payloads reside in [`tools/ram_payload/`](tools/ram_payload/):

| Runner Script | Description |
|---------------|-------------|
| [`run_hello_world.sh`](tools/ram_payload/run_hello_world.sh) | Initializes GC9307 SPI controller, clears frame, renders text on 2.8" LCD |
| [`run_keypad_test.sh`](tools/ram_payload/run_keypad_test.sh) | Real-time interactive diagnostic dashboard showing all keypad buttons & PMIC power key |
| [`run_flashlight_test.sh`](tools/ram_payload/run_flashlight_test.sh) | Cycles the SC2720 PMIC rear white LED through variable brightness levels |
| [`run_haptics_test.sh`](tools/ram_payload/run_haptics_test.sh) | Tests ERM vibration motor pulse sequences and strength profiles via LDO_VIBR |
| [`run_audio_test.sh`](tools/ram_payload/run_audio_test.sh) | Ungates audio clock (`0x40608c08`) and probes GPIO 31 external PA amplifier |
| [`run_secure_boot_diag.sh`](tools/ram_payload/run_secure_boot_diag.sh) | Audits hardware Secure Boot gates (AON `0x402e0568` & eFuse Block 1), anti-rollback & root key |

### Running a Payload

1. Disconnect phone from USB and remove the battery.
2. Run the payload runner script (e.g. for Hello World):
   ```bash
   ./tools/ram_payload/run_hello_world.sh
   ```
3. While the script displays `Waiting for connection...`, hold down the **`*`** (Asterisk) key on the keypad and plug in the USB cable.
4. Keep holding **`*`** for 1–2 seconds until the BootROM handshakes, then release. The screen will illuminate with your payload!

---

## 📚 Documentation Index

Our research findings are structured into focused, technical references:

### 🔌 Hardware & Peripherals
* [**Hardware Overview**](docs/hardware.md) — Chipset specs, auxiliary cores, screen controllers, pin assignments.
* [**Keypad Matrix Controller**](docs/keypad-matrix.md) — 4×5 matrix scanner registers, PMIC EIC power key decoder, keymap tables.
* [**UART Serial & Debug Console**](docs/uart-serial-interface.md) — 1.8V UART1 (`0x70100000`), bootloader log streams, boot key pinmux multiplexing (holding key `7`/`3`).
* [**Flashlight (Rear Torch)**](docs/flashlight.md) — SC2720 PMIC current sink (`0x40608df4`), duty cycle calculation, disassembly.
* [**Haptic Vibration Subsystem**](docs/haptics.md) — SC2720 `LDO_VIBR` voltage regulation (`0x40608e00`) and pulse timing.
* [**Audio Subsystem & PA**](docs/audio-subsystem.md) — Speaker PA enable (GPIO 31), PMIC Line-Out, VBC codec, and clock routing.
* [**USB Enumeration & Modes**](docs/usb-enumeration.md) — Raw macOS descriptor dumps for Normal U-Disk (`1782:4D01`) and BootROM (`1782:4D00`).

### 🚀 Boot & Firmware Internals
* [**Boot Process & Chain**](docs/boot-process.md) — FDL1/FDL2 download flow, `DHTB` header verification, `boot0`/`boot1` NAND stages.
* [**Boot Security & Verification**](docs/boot-security-verification.md) — RSA-2048, SHA-256, eFuse gates (`0x402e0568`), and anti-rollback analysis.
* [**BootROM Behavior**](docs/bootrom.md) — Download-mode triggers, BSL framing, watchdog timeouts, security checks.
* [**Flash Layout & Partitions**](docs/flash-layout.md) — NAND base addresses (`0x8000000x` / `0x9000000x`), partition boundaries, PAC mapping.
* [**Memory Map**](docs/memory-map.md) — SRAM (`0x00006200`), DDR execution layout (`0x80100000`), MMU tables, MOCOR tasks.
* [**Firmware Triage Reference**](docs/firmware-triage-reference.md) — In-depth reference on RTOS tasks, LCD drivers (`GC9307`/`GC9107`), 560+ AT commands, eFuse security strings, and panic diagnostics.
* [**Firmware Provenance & PAC**](firmware/readme.md) — PAC image hashes, extraction instructions, stock partition inventory.
* [**Cortex-M4 Power Management**](docs/cortex-m4-power-management.md) — Deep dive into `pm_cm4.bin` ThreadX Lite RTOS, ADI bus to SC2720 PMIC, IPC mailboxes (`0xBEEE`), and LPDDR self-refresh memory retention.
* [**MMI Resource Pack Format**](docs/resource-pack-format.md) — Reverse-engineered `mmi_res.bin` `sres0001` container, 792 theme assets, 240×320 and 128×160 RGB565 BMP boot splashes, and language tables.
* [**Non-Volatile Parameter Architecture**](docs/nvram-items.md) — `nvitem.bin` structure, RF calibration tables, IMEI storage, and 28 discrete acoustic path DSP profiles.
* [**Baseband Modem DSP Subsystems**](docs/dsp-modem-subsystem.md) — CEVA DSP architectures for `gsm_dsp.bin` and `lte_dsp.bin`, memory-mapped mailbox registers (`0xE0410000`/`0xE0490000`), and VoLTE/EVS engines.

### 💻 Software & Execution Architecture
* [**Application Layer Architecture**](docs/application-layer.md) — MOCOR window system, event loop, dynamic loader analysis.
* [**Kernel Architecture & Subsystems**](docs/kernel-architecture.md) — Monolithic `kernel.bin` architecture (`0x80a05e00`), CP15 reset init (`0x80a061f0`), ThreadX task tables, and peripheral driver maps.
* [**MMI Application Framework**](docs/user-application-framework.md) — Userspace `user.bin` architecture (`0x88000000`), MMK GUI framework, worker tasks, and Opera/Java/Bluetooth runtimes.
* [**Custom Code Execution Guide**](docs/custom-code-execution.md) — Bare-metal linker scripts, toolchain setup, payload entry points.
* [**Community Findings & Tips**](docs/community-findings.md) — Boot key shortcuts, dialer codes, safe firmware dump walkthrough.
* [**Hidden Dialer Codes**](docs/hidden-codes.md) — Complete audit of secret codes: Unisoc Engineer Mode (`*#*#372733#*#`), factory logging toggle (`*#8378*733425*1#`), MMI hardware test, and 30+ language shortcuts.

---

## 🗂️ Repository Structure

```text
Nokia-2660-RE/
├── README.md               # Main project overview & quickstart
├── CONTRIBUTING.md         # Guidelines for contributing research/code
├── DISCLAIMER.md           # Safety & legal disclaimers
├── LICENSE                 # MIT License
├── requirements.txt        # Optional Python dependencies (Capstone)
│
├── docs/                   # Technical documentation & register maps
│   ├── hardware.md         # Hardware specifications & components
│   ├── boot-process.md     # BootROM & stage loader execution flow
│   ├── flash-layout.md     # Partition table & flash geometry
│   ├── keypad-matrix.md    # Keypad matrix controller registers
│   ├── custom-code-execution.md # Bare-metal homebrew execution guide
│   └── images/             # Photos & hardware captures
│
├── firmware/               # Firmware provenance, hashes & manifests
│   ├── readme.md           # PAC samples, hashes, and verification info
│   ├── dumps/              # Verified partition manifests from real devices
│   └── extracted/          # Extracted partition XML & headers (binaries gitignored)
│
├── tools/                  # Python & shell utilities
│   ├── readme.md           # Tools guide & dependencies
│   ├── emulator/           # Unicorn CPU & peripheral hardware emulator
│   ├── extract_pac.py      # Unisoc BP_R1.0.0 PAC archive unpacker
│   ├── extract_strings.py  # String triage & domain categorization tool
│   ├── mocor-info.py       # DHTB header, RSA chain & MOCOR OS inspector
│   ├── dump_device.sh      # Firmware partition dumping helper
│   ├── spd_dump            # Unisoc BootROM/FDL protocol tool (local binary)
│   └── ram_payload/        # Bare-metal payload source code, headers & runners
│
├── ghidra/                 # Reverse-engineering project setup
│   ├── readme.md           # Ghidra loader base addresses & script guide
│   └── scripts/            # Headless analysis scripts
│
└── captures/               # Hardware trace guides (USB, UART, logic analyser)
    └── readme.md           # Capture naming rules & git policy
```

---

## 🛠️ Quick Start

### 1. Prerequisites

* **Host Machine:** macOS (Apple Silicon/Intel) or Linux.
* **Python 3.8+** (for PAC extraction and analysis scripts).
* **ARM Toolchain** (optional, only needed if compiling payloads from scratch):
  ```bash
  # macOS
  brew install arm-none-eabi-gcc
  # Ubuntu / Debian
  sudo apt install gcc-arm-none-eabi
  ```

### 2. Setup

```bash
git clone https://github.com/lovelytingy/Nokia-2660-RE.git
cd Nokia-2660-RE
python3 -m pip install -r requirements.txt   # Optional (Capstone disassembler)
```

### 3. Unpacking Firmware

If you have acquired an official PAC firmware update file (e.g. `v23_update_Nokia_2660.pac`):

```bash
python3 tools/extract_pac.py firmware/v23_update_Nokia_2660.pac
```

This extracts all component partitions into `firmware/extracted/v23_update_Nokia_2660/` along with `manifest.csv` containing cryptographic SHA-256 hashes.

---

## 🌐 Host OS & Compatibility

This repository is actively developed and tested primarily on **macOS** (Darwin / Apple Silicon).

* **macOS:** Native support via `libusb` (`brew install libusb pkg-config`). No kernel extensions required for BootROM communication.
* **Linux:** Fully supported. Requires `udev` rules granting non-root access to Spreadtrum USB vendor ID `1782`.
* **Windows:** Requires Spreadtrum SCI USB drivers (`SciU2S`) and Windows builds of `spd_dump.exe`.

*Contributions improving automated runner scripts across Linux and Windows are warmly welcomed!*

---

## 🤝 Acknowledgments & Credits

This research builds upon invaluable foundational work by the open-source Unisoc/Spreadtrum community:

* **[Ilya Kurdyukov (ilyakurdyukov)](https://github.com/ilyakurdyukov)**:
  - **[`spreadtrum_flash`](https://github.com/ilyakurdyukov/spreadtrum_flash)** (`spd_dump`): The foundational reverse-engineered download-mode protocol implementation for Spreadtrum/Unisoc SoCs, enabling BootROM handshakes, FDL loading, memory dumps, and DRAM injection.
  - **[`fpdoom`](https://github.com/ilyakurdyukov/fpdoom)**: Embedded port of Doom for feature phones, providing reference bare-metal hardware drivers for the GC9307/GC9104 SPI LCDs, PMIC backlight registers, and USB `libc_server`.
* **[id Software](https://www.idsoftware.com/)**: For the legendary [DOOM](https://github.com/id-Software/DOOM) engine and shareware `doom1.wad`.
* **[Capstone Disassembly Framework](https://www.capstone-engine.org/)**: Automated validation and disassembly of compiled ARMv7-A bare-metal payloads.
* **[NSA Ghidra](https://ghidra-sre.org/)**: Static analysis and decompilation of MOCOR RTOS binaries, bootloader stages (`boot0`/`boot1`), and kernel verification routines.
* **The Feature Phone Reverse-Engineering Community**: For documenting chip errata, boot keys, and flash structures across the SC6531 and UMS9117 series.

---

## ⚠️ Disclaimer

**No warranty. No guarantee. We are not responsible for bricked or damaged devices.**

This repository is published solely for educational and research purposes. Flashing or modifying phones carries inherent risk. You assume **full responsibility** for any actions taken using the information or tools provided here.

See [DISCLAIMER.md](DISCLAIMER.md) for full terms. Never redistribute proprietary firmware binaries or publish private NV calibration data (IMEI, MAC addresses).

---

## 📄 License

Code and documentation created in this repository are released under the [MIT License](LICENSE).  
Upstream firmware binaries remain the intellectual property of their respective owners.
