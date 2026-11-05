# Unisoc UMS9117 (Nokia 2660 Flip) Emulator

A lightweight, high-performance CPU and peripheral emulator for the **Unisoc UMS9117 (T117)** chipset built on the **Unicorn Engine** (QEMU TCG) and **Capstone** disassembler.

This emulator allows developers and reverse engineers to **test bare-metal RAM payloads and trace bootloader binaries locally** on macOS and Linux without physical hardware or bricking risks.

---

## 🎯 Features

* **Cycle-Accurate ARMv7-A & Thumb-2 Emulation**: Executes 32-bit ARM instructions with dynamic mode switching (`bx` / `blx`).
* **Verified Memory Map**:
  * **IRAM / SRAM (`0x00000000` – `0x00020000`)**: For `boot0.bin` and `fdl1.bin` at base `0x00006200`.
  * **DDR RAM (`0x80000000` – `0x82000000`)**: For `boot1.bin`, `fdl2.bin`, and RAM payloads at `0x80100000`.
  * **Mailbox Test Region (`0x80700000`)**: Captures test status markers and counters written by payloads.
  * **FDL1 Dispatcher Stub (`0x00006C22`)**: Stubs `send_ack(0x80)` and return hooks so payloads exit cleanly.
* **Peripheral MMIO Mocking**:
  * **UART1 Serial Console (`0x70100000`)**: TX FIFO writes stream directly to the host terminal.
  * **SC2720 PMIC & ADI Bus (`0xA1600000` / `0x40608000`)**: Emulates synchronous serial reads and writes with address tags.
  * **Flashlight Torch (`0x40608df4`)**: Intercepts LED power gating and PWM brightness levels.
  * **Haptic Vibrator (`0x40608e00`)**: Intercepts `LDO_VIBR` voltage steps and pulse durations.
  * **Hardware Boot Key Pinmux (`0x402A0018`)**: Simulates holding down Key `7` (UART mode) or Key `3` (JTAG mode).
  * **Graceful Unmapped MMIO Handling**: Unknown peripheral registers are auto-mapped on-the-fly and return `0x00` without crashing.

---

## 📦 Prerequisites & Installation

```bash
python3 -m pip install -r requirements.txt
```
*(Requires `unicorn>=2.0.0` and `capstone>=5.0.0`)*

---

## 🚀 Usage Guide

### 1. Run a Bare-Metal Payload
Test a compiled payload in DRAM (`0x80100000`, entry `0x80100200`):

```bash
./tools/emulator/ums9117_emu.py tools/ram_payload/bin/baremetal_asm.bin --profile payload
```

Expected Output:
```text
=================================================================
  Unisoc UMS9117 (Nokia 2660 Flip) CPU & Peripheral Emulator
  Target Binary: tools/ram_payload/bin/baremetal_asm.bin
  Load Address:  0x80100000
  Entry Point:   0x80100200
=================================================================

[*] Loaded 616 bytes into virtual memory @ 0x80100000
[*] Starting execution @ 0x80100200...

[+] Emulation finished in 0.403s

=== Mailbox Test Diagnostics (0x80700100) ===
  Marker 1: 0x14802660 (Nokia 2660 TA-1480 OK)
  Marker 2: 0xcafebabe (Exec Confirmed OK)
  Marker 3: 0xdeadbeef (Payload Alive OK)
  Counter:  1048575
  Status:   0x00000001 (SUCCESS)
```

---

### 2. Instruction-by-Instruction Tracing (`--trace`)
Disassemble and print every instruction as the virtual CPU executes it:

```bash
./tools/emulator/ums9117_emu.py tools/ram_payload/bin/baremetal_asm.bin --profile payload --trace --max-insn 15
```

Output:
```text
[ARM  ] 0x80100200:  push     {r4, r5, r6, r7, r8, sb, sl, fp, lr}
[ARM  ] 0x80100204:  ldr      r0, [pc, #0x48]
[ARM  ] 0x80100208:  ldr      r1, [pc, #0x48]
[ARM  ] 0x8010020c:  str      r1, [r0]
[ARM  ] 0x80100210:  ldr      r1, [pc, #0x44]
[ARM  ] 0x80100214:  str      r1, [r0, #4]
...
```

---

### 3. Emulate Bootloader Stages (`boot0.bin` / `fdl1.bin`)
Emulate the first on-device boot stage from IRAM (`0x00006200`, entry `0x00006420`):

```bash
./tools/emulator/ums9117_emu.py firmware/dumps/ta1480_10.02.17.00/boot0.bin --profile boot0 --trace --max-insn 25
```

Simulate holding Key `7` at power-on to enable UART debug mode:

```bash
./tools/emulator/ums9117_emu.py firmware/dumps/ta1480_10.02.17.00/boot0.bin --profile boot0 --boot-key uart
```

---

### 4. Run the Automated Test Suite

```bash
./tools/emulator/test_payloads.py
```

Runs comprehensive unit tests verifying payload execution, mailbox status flags, CP15 register initialization, and boot key pinmux detection.
