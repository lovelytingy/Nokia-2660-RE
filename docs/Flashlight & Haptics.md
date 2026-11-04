# Nokia 2660 Flip (TA-1480) Flashlight & Haptics Subsystems

This document details the reverse-engineered hardware architecture, register maps, and bare-metal drivers for the **Rear Flashlight / Torch LED** and the **Haptic Vibration Motor** on the **Nokia 2660 Flip (Model TA-1480, Unisoc UMS9117 / T107 SoC, SC2720 PMIC)**.

---

## 1. Hardware Overview

```
                      +---------------------------------------+
                      |       Unisoc UMS9117 / T107 SoC       |
                      |                                       |
                      |  +---------------------------------+  |
                      |  | APB GPIO Controller             |  |
                      |  | Base: 0x40280000                |  |
                      |  | GPIO Pin 45 (Bank 2, Bit 13)    |  |
                      |  | -> Motor Enable Gate            |  |
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
                      |                    |                  |
                      |  +-----------------v---------------+  |
                      |  | Motor Regulators & LDO          |  |
                      |  | ADI Registers: 0x406081b8 / 180 |  |
                      |  | Power: VBAT (~3.8V Battery)     |  |
                      |  +-----------------+---------------+  |
                      +--------------------|------------------+
                                           |
                +--------------------------+--------------------------+
                |                                                     |
                v                                                     v
   +--------------------------+                         +--------------------------+
   |   Rear Flashlight LED    |                         |  ERM Vibration Motor     |
   |   (Works without battery)|                         |  (REQUIRES BATTERY/VBAT) |
   +--------------------------+                         +--------------------------+
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

## 3. Haptic Vibration Motor Subsystem

### Hardware Architecture
The Nokia 2660 Flip uses an Eccentric Rotating Mass (ERM) vibration motor driven jointly by the Application Processor GPIO bus and the SC2720 PMIC:

1. **AP GPIO 45 (`0x40280100`, Bank 2, Bit 13):**
   - Registered as `GPIO_PROD_ID = 5` in `kernel.bin` (`SetVibrator_Timer`).
   - Hardware gate line enabling the motor drive transistor.
2. **SC2720 PMIC Motor LDO (`0x406081b8` / `0x40608180`):**
   - Disassembly at `0xbdf150` in `kernel.bin` configures channels `0x406081b8`, `0x406081bc`, and `0x406081c0` with intensity levels in `0x40608188`, `0x4060818c`, `0x4060819c`.
   - Master power gate controlled via `0x40608180`.

### CRITICAL: Battery Power Requirement (`VBAT`)
Unlike the low-power flashlight LED which can operate off the 5V USB bus (`VBUS`), the ERM vibration motor draws high peak current (~150mA to 300mA) and is wired directly to **`VBAT` (the physical battery spring terminals)**.

> [!IMPORTANT]
> When executing bare-metal payloads over USB with the battery removed, the motor receives **0.0 Volts**.
> **To test vibration:** Start the BootROM connection with the battery OUT. Once Stage 1 finishes and the payload starts running in DRAM, **pop the battery into the phone** while USB remains connected!

### Bare-Metal C Driver
```c
#define GPIO_BASE 0x40280000
#define VIB_GPIO  45

// Initialize AP GPIO 45 as Output
void vib_gpio_init(void) {
    // Bank 2 (45 / 16 = 2) offset = 2 * 0x80 = 0x100; Bit = 45 % 16 = 13
    uint32_t bank_offset = (45 >> 4) * 0x80;
    uint32_t pin_mask    = 1u << (45 & 0xF);

    *(volatile uint32_t *)(GPIO_BASE + bank_offset + 0x08) |= pin_mask; // DIR = Out
    *(volatile uint32_t *)(GPIO_BASE + bank_offset + 0x04) |= pin_mask; // DMSK = En
    *(volatile uint32_t *)(GPIO_BASE + bank_offset + 0x00) &= ~pin_mask; // Start Low
}

void vib_gpio_set(int state) {
    uint32_t bank_offset = (45 >> 4) * 0x80;
    uint32_t pin_mask    = 1u << (45 & 0xF);
    volatile uint32_t *reg = (volatile uint32_t *)(GPIO_BASE + bank_offset + 0x00);
    if (state) *reg |= pin_mask;
    else *reg &= ~pin_mask;
}

// SC2720 PMIC Motor Driver
void pmic_vib_set(int enable, int level) {
    if (level < 0) level = 0;
    if (level > 15) level = 15;
    uint32_t val8 = (level << 4) | 0x0f;

    if (enable) {
        adi_write(0x406081b8, (adi_read(0x406081b8) & ~0x3f) | 0x19);
        adi_write(0x406081bc, (adi_read(0x406081bc) & ~0x3f) | 0x19);
        adi_write(0x406081c0, (adi_read(0x406081c0) & ~0x3f) | 0x19);
        adi_write(0x40608188, (val8 << 8) | 0xff);
        adi_write(0x4060818c, (val8 << 8) | 0xff);
        adi_write(0x4060819c, (val8 << 8) | 0xff);
        adi_write(0x40608180, adi_read(0x40608180) | 0x0111);
    } else {
        adi_write(0x40608180, adi_read(0x40608180) & ~0x0111);
        adi_write(0x406081b8, adi_read(0x406081b8) & ~0x3f);
        adi_write(0x406081bc, adi_read(0x406081bc) & ~0x3f);
        adi_write(0x406081c0, adi_read(0x406081c0) & ~0x3f);
    }
}

// Master Haptic Pulse
void haptics_pulse(uint32_t duration_ms, int level) {
    pmic_vib_set(1, level);
    vib_gpio_set(1);
    sys_wait_ms(duration_ms);
    pmic_vib_set(0, level);
    vib_gpio_set(0);
}
```

---

## 4. Interactive Diagnostic Tools

Two dedicated, independent tools are provided in `tools/ram_payload/`:

### 1. Flashlight Test (`run_flashlight_test.sh`)
* Source: [`tools/ram_payload/src/flashlight_test/`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/tools/ram_payload/src/flashlight_test/)
* Binary: [`tools/ram_payload/bin/flashlight_test.bin`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/tools/ram_payload/bin/flashlight_test.bin)
* Controls:
  - `[1]`: Toggle Flashlight ON / OFF
  - `[2]`: Short Camera Flash (50 ms)
  - `[3]` / `[4]` / `[5]`: Timed Pulses (200 ms, 500 ms, 1.0 s)
  - `[6]` / `[7]` / `[8]`: Fast Strobe, Slow Strobe, Single Tick
  - `[9]`: **SOS Morse Code Optical Beacon** (`... --- ...`)
  - `[0]`: Full Torch ON (Level 15)
  - `[*]`: Turn OFF Torch immediately
  - `[Vol+]` / `[Vol-]`: Adjust Brightness Level (0..15) with visual feedback tick

```bash
# Launch on Main Screen (2.8" 240x320)
./tools/ram_payload/run_flashlight_test.sh

# Launch on Cover Screen (1.77" 128x160)
./tools/ram_payload/run_flashlight_test.sh cover
```

---

### 2. Haptics Test (`run_haptics_test.sh`)
* Source: [`tools/ram_payload/src/haptics_test/`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/tools/ram_payload/src/haptics_test/)
* Binary: [`tools/ram_payload/bin/haptics_test.bin`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/tools/ram_payload/bin/haptics_test.bin)
* Controls:
  - `[1]` / `[2]`: Tactile Click (40 ms) / Double Tick (60+60 ms)
  - `[3]` / `[4]`: Buzz (200 ms) / Long Buzz (400 ms)
  - `[5]`: Rumble (750 ms)
  - `[6]`: Heartbeat Pattern
  - `[7]` / `[8]`: Continuous Motor ON / Stop Motor
  - `[9]`: **SOS Morse Code Vibration** (`... --- ...`)
  - `[0]`: Heavy Blast (1.0 s)
  - `[Vol+]` / `[Vol-]`: Intensity Level (0..15)

```bash
# Launch on Main Screen (2.8" 240x320)
./tools/ram_payload/run_haptics_test.sh

# Launch on Cover Screen (1.77" 128x160)
./tools/ram_payload/run_haptics_test.sh cover
```
