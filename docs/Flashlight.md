# Nokia 2660 Flip (TA-1480) Rear Flashlight Subsystem

This document details the reverse-engineered hardware architecture, register maps, and bare-metal drivers for the **Rear Flashlight / Torch LED** on the **Nokia 2660 Flip (Model TA-1480, Unisoc UMS9117 / T107 SoC, SC2720 PMIC)**.

> [!NOTE]
> For the **Haptic Vibration Motor** (`0x40608e00`), see the dedicated reference [`docs/Haptics.md`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/docs/Haptics.md).  
> For the **External Speaker PA & Audio Codec**, see [`docs/Audio Subsystem.md`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/docs/Audio%20Subsystem.md).

---

## 1. Hardware Overview

```
                      +---------------------------------------+
                      |       Unisoc UMS9117 / T107 SoC       |
                      |                                       |
                      |  +---------------------------------+  |
                      |  | ADI Master Serial Bus           |  |
                      |  | Base: 0x40600000                |  |
                      |  +-----------------+---------------+  |
                      +--------------------|------------------+
                                           | ADI Serial Bus
                                           v
                      +---------------------------------------+
                      |         Unisoc SC2720 PMIC            |
                      |                                       |
                      |  +---------------------------------+  |
                      |  | Flashlight / Torch LED Driver   |  |
                      |  | ADI Register: 0x40608df4        |  |
                      |  | Power: VBUS (5V USB) / Boost    |  |
                      |  +-----------------+---------------+  |
                      +--------------------|------------------+
                                           |
                                           v
                              +--------------------------+
                              |   Rear Flashlight LED    |
                              |  (Works without battery) |
                              +--------------------------+
```

---

## 2. Rear Flashlight / Torch LED Subsystem

### Hardware Discovery
Physical hardware testing confirmed that register **`0x40608df4`** inside the **Spreadtrum / Unisoc SC2720 PMIC** controls the high-brightness white LED mounted on the outer rear shell near the camera module.

* **Controller:** SC2720 PMIC White LED Current Sink Driver
* **Interface:** High-speed Analog-Digital Interface (ADI) serial bus mapped at `0x40608000`
* **Firmware Reference:** Function `FLASHLIGHT_Ctrl` in `kernel.bin` at offset `0xbdf4d8`:
  - `0xbdf4f0: orr r1, r0, #0x8000` -> Sets bit 15, prints `"FLASHLIGHT open!!!!!!!!!!!!!!!!!!!!!!!!!!!!"`
  - `0xbdf508: bic r1, r0, #0x8000` -> Clears bit 15, prints `"FLASHLIGHT close!!!!!!!!!!!!!!!!!!!!!!!!!!!!"`
* **Power Source:** Powered via the PMIC's internal step-up LED driver from **`VBUS` (5V USB)**. **Works without a battery installed!**

### Register Bitfield: `PMIC_TORCH_REG` (`0x40608df4`)

| Bits | Field | Description |
|:---:|:---|:---|
| **[15]** | `TORCH_EN` | **LED Master Power Gate:** `1` = Energize LED, `0` = Power off |
| **[14:4]** | `RESERVED` | Preserved during read-modify-write operations |
| **[3:0]** | `TORCH_CURRENT` | **LED Current / Brightness Level:** `0x0` (Minimum / Dim) to `0xF` (Maximum / Full Torch) |

### Bare-Metal C Driver
```c
#define PMIC_TORCH_REG 0x40608df4

uint32_t adi_read(uint32_t addr);
void adi_write(uint32_t addr, uint32_t val);

// Turn Flashlight ON or OFF with intensity level (0..15)
void flashlight_set(int on, int level) {
    uint32_t cur = adi_read(PMIC_TORCH_REG);
    if (on) {
        level &= 0x0f;
        // Keep reserved bits, set bit 15 (enable), apply intensity in [3:0]
        adi_write(PMIC_TORCH_REG, (cur & ~0x800f) | 0x8000 | level);
    } else {
        // Clear bit 15 to disable LED driver
        adi_write(PMIC_TORCH_REG, cur & ~0x8000);
    }
}

// Generate timed optical pulse
void flashlight_pulse(uint32_t duration_ms, int level) {
    flashlight_set(1, level);
    sys_wait_ms(duration_ms);
    flashlight_set(0, level);
}

// Emergency SOS Morse Code Optical Beacon: ... --- ...
void flashlight_morse_sos(int level) {
    // S: 3 short flashes (100ms on, 100ms off)
    for (int i = 0; i < 3; i++) {
        flashlight_pulse(100, level);
        sys_wait_ms(100);
    }
    sys_wait_ms(250);
    // O: 3 long flashes (300ms on, 100ms off)
    for (int i = 0; i < 3; i++) {
        flashlight_pulse(300, level);
        sys_wait_ms(100);
    }
    sys_wait_ms(250);
    // S: 3 short flashes (100ms on, 100ms off)
    for (int i = 0; i < 3; i++) {
        flashlight_pulse(100, level);
        sys_wait_ms(100);
    }
}
```

---

## 3. Interactive Diagnostic Tool (`flashlight_test.bin`)

A dedicated diagnostic tool is provided in `tools/ram_payload/`:

* Source: [`tools/ram_payload/src/flashlight_test/`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/tools/ram_payload/src/flashlight_test/)
* Binary: [`tools/ram_payload/bin/flashlight_test.bin`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/tools/ram_payload/bin/flashlight_test.bin)
* Runner Script: [`tools/ram_payload/run_flashlight_test.sh`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/tools/ram_payload/run_flashlight_test.sh)

### Keypad Controls

| Key | Mode / Action | Description |
|:---:|:---|:---|
| **`[1]`** | Toggle Torch | Flips LED state ON / OFF at current brightness level |
| **`[2]`** | Camera Flash | Short high-intensity flash pulse (50 ms) |
| **`[3]`** | Timed Pulse | 200 ms flash |
| **`[4]`** | Timed Pulse | 500 ms flash |
| **`[5]`** | Long Pulse | 1.0 second torch burst |
| **`[6]`** | Fast Strobe | 5 rapid strobes (50 ms ON / 50 ms OFF) |
| **`[7]`** | Slow Strobe | 3 beacon pulses (150 ms ON / 150 ms OFF) |
| **`[8]`** | Single Tick | Quick 25 ms optical tick |
| **`[9]`** | **SOS Morse Code** | International distress beacon (`... --- ...`) |
| **`[0]`** | Full Torch | Sets max level 15 and enables LED |
| **`[*]`** | Instant Turn Off | Immediately powers down LED driver |
| **`[Vol+]`** | Increase Brightness | Steps current up (`0` to `15`) with optical tick feedback |
| **`[Vol-]`** | Decrease Brightness | Steps current down (`0` to `15`) with optical tick feedback |
| **Hold `[End]`** | Clean Exit | Restores idle state and returns to bootloader/FDL |

### Launch Commands

```bash
# Launch on Main Screen (2.8" 240x320)
./tools/ram_payload/run_flashlight_test.sh

# Launch on Cover Screen (1.77" 128x160)
./tools/ram_payload/run_flashlight_test.sh cover
```
