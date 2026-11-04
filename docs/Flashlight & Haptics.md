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
The Nokia 2660 Flip contains an Eccentric Rotating Mass (ERM) vibration motor powered and regulated directly by a dedicated Low-Dropout (LDO) regulator inside the **Spreadtrum / Unisoc SC2720 PMIC**:

* **PMIC Register:** **`0x40608e00`** (`PMIC_LDO_VIBR_REG`)
* **Firmware Reference:** Disassembly of MOCOR `kernel.bin` at `0x80be704c` (`analog_phy.c`):
  - `0x80be7050: ldr r5, =0x40608e00` -> PMIC LDO_VIBR address
  - `0x80be70a8: add r1, r4, #0x1c` -> Calculates voltage: `28 + level` (2.8V to 3.5V)
  - `0x80be70ac: add r0, pc, #0x170` -> Prints `"Vibrator vlaue  = %d V,POWER_ON!!!!!!!!!!!!!!!"`
  - `0x80be70e0: add r0, pc, #0x16c` -> Prints `"Vibrator vlaue  = %d,POWER_DOWN!!!!!!!!!!!!!!!"`

### Register Bit Layout: `PMIC_LDO_VIBR_REG` (`0x40608e00`)

| Bits | Field | Description |
|:---:|:---|:---|
| **[2:0]** | `LDO_VIBR_V` | **LDO Output Voltage / Strength:** `0` (2.8V) to `7` (3.5V max rumble) |
| **[7:3]** | `RESERVED` | Preserved during read-modify-write |
| **[8]** | `LDO_VIBR_PD` | **Power-Down Control:** `0` = Power ON (active), `1` = Power Down (off) |
| **[9]** | `SLP_LDOVIBR_PD_EN` | **Sleep Power-Down Enable:** `0` = Active, `1` = Sleep Power Down |

> [!NOTE]
> Registers `0x40608180`, `0x40608188`, and `0x406081b8` belong to the **LCD Backlight Current Sink / RGB Driver**. Writing to them modulates the display backlight rather than the vibrator motor. The dedicated ERM motor regulator is exclusively controlled at `0x40608e00`.

### CRITICAL: Battery Power Requirement (`VBAT`)
Unlike the low-power flashlight LED which can operate off the 5V USB bus (`VBUS`), the ERM vibration motor draws high peak current (~150mA to 300mA) and is wired directly to **`VBAT` (the physical battery spring terminals)**.

> [!IMPORTANT]
> When executing bare-metal payloads over USB with the battery removed, the motor receives **0.0 Volts**.
> **To test vibration:**
> * **Method A (Recommended):** Put the battery inside the phone while powered OFF, hold `*` (Asterisk), and plug in the USB cable to enter BootROM mode.
> * **Method B:** If the phone boots normally when holding `*` with battery in, start with battery OUT, hold `*`, plug in USB, and once the DRAM payload is executing and the LCD lights up, gently slide the battery into place.

### Bare-Metal C Driver
```c
#define PMIC_LDO_VIBR_REG 0x40608e00

uint32_t adi_read(uint32_t addr);
void adi_write(uint32_t addr, uint32_t val);

// Enable or disable vibrator motor; level = 0..7 (2.8V .. 3.5V)
void pmic_vib_set(int enable, int level) {
    if (enable) {
        if (level < 0) level = 0;
        if (level > 7) level = 7;
        uint32_t val = adi_read(PMIC_LDO_VIBR_REG);
        // Configure voltage level in bits [2:0]
        val = (val & ~0x7) | (level & 0x7);
        // Clear bit 8 (LDO_VIBR_PD) and bit 9 (SLP_LDOVIBR_PD_EN) to POWER ON
        val &= ~0x300;
        adi_write(PMIC_LDO_VIBR_REG, val);
    } else {
        uint32_t val = adi_read(PMIC_LDO_VIBR_REG);
        // Set bit 8 and bit 9 to POWER DOWN
        val |= 0x300;
        adi_write(PMIC_LDO_VIBR_REG, val);
    }
}

// Master Haptic Pulse
void haptics_pulse(uint32_t duration_ms, int level) {
    pmic_vib_set(1, level);
    sys_wait_ms(duration_ms);
    pmic_vib_set(0, level);
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
