# Bare-Metal RAM Payload Toolkit

This directory contains bare-metal code execution payloads, hardware drivers, and runners for the **Nokia 2660 Flip (Model TA-1480, Unisoc UMS9117 / T107)**.

All payloads in this directory run **strictly in volatile DRAM** at `0x80100000`. NAND flash is never modified or erased, ensuring **zero risk of bricking**.

<div align="center">
  <br>
  <img src="../../docs/images/nokia_2660_hello_world.jpg" width="380" alt="Nokia 2660 Flip physical hardware running custom bare-metal Hello World payload">
  <p><em>Physical hardware verification: Custom "Hello World" payload executing in volatile DRAM on the Nokia 2660 Flip (TA-1480).</em></p>
  <br>
</div>

---

## Directory Structure

```text
tools/ram_payload/
├── run_hello_world.sh          # Primary runner: launches visual Hello World on physical LCD
├── run_ram_payload.sh          # Minimal runner: executes 612-byte raw ARM assembly payload
├── README.md                   # This documentation
│
├── bin/                        # Precompiled binary payloads & host utilities
│   ├── hello_world.bin         # Complete graphical Hello World payload (23 KB)
│   ├── baremetal_asm.bin       # Minimal 616-byte raw ARM assembly payload
│   └── libc_server             # Native macOS host USB serial / display server
│
├── data/                       # Device calibration & hardware maps (extracted from kernel.bin)
│   ├── pinmap.bin              # Unisoc T107 pin multiplexing table for TA-1480
│   └── keymap.bin              # Keypad matrix mapping table for TA-1480
│
└── src/                        # Full source code for both payloads
    ├── hello_world/            # C graphical display payload
    │   ├── hello_world_main.c  # RGB565 framebuffer & GC9307 driver source
    │   ├── font8x16.h          # Scaled 8x16 ASCII bitmap font table
    │   └── build.sh            # Automated compilation script (arm-none-eabi-gcc)
    │
    └── baremetal_asm/          # Minimal ARM assembly payload
        ├── payload.S           # Raw ARMv7-A assembly source
        └── build_payload.py    # Python assembler & DHTB packager (native clang)
```

---

## Quick Start: Running Payloads

### 1. Graphical "Hello World" Display Test (Recommended)
Powers on the SC2720 PMIC backlight, initializes the GC9307 SPI display, and draws a glowing "Hello World" screen at 60 FPS:

```bash
./tools/ram_payload/run_hello_world.sh
```

* To test the outer **Cover Screen (1.77")** instead of the main inner screen:
  ```bash
  ./tools/ram_payload/run_hello_world.sh cover
  ```

### 2. Minimal Bare-Metal Assembly Test
Executes a 1,000,000-cycle loop in DRAM, writes hardware marker `0x14802660` to `0x80700100`, transmits `BSL_REP_ACK` (`0x80`) over USB via FDL1's `send_ack`, and returns cleanly:

```bash
./tools/ram_payload/run_ram_payload.sh
```

---

## Connection Instructions

Both scripts follow the standard Unisoc BootROM handshake procedure:

1. Disconnect phone from USB and remove the battery.
2. Press the red **Power / End** key 2–3 times while unplugged (discharges residual capacitor charge so the PMIC detects USB cleanly).
3. Press and firmly **HOLD** the **`*`** (Asterisk) key on the keypad.
4. While holding **`*`**, plug the USB cable into the phone and computer.
5. Once detection begins, release the key and watch the console / phone screen!

---

## Rebuilding Payloads from Source

### Rebuilding Hello World (`hello_world.bin`)
Requires `arm-none-eabi-gcc` (install via `brew install arm-none-eabi-gcc` on macOS):
```bash
./tools/ram_payload/src/hello_world/build.sh
```

### Rebuilding Minimal Assembly (`baremetal_asm.bin`)
Uses native system `clang` (no cross-compiler needed):
```bash
python3 tools/ram_payload/src/baremetal_asm/build_payload.py
```
