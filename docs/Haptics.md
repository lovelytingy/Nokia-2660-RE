# Nokia 2660 Flip (TA-1480) Haptics Subsystem

This document details the reverse-engineered hardware architecture, register maps, power requirements, and bare-metal drivers for the **Eccentric Rotating Mass (ERM) Haptic Vibration Motor** on the **Nokia 2660 Flip (Model TA-1480, Unisoc UMS9117 / T107 SoC, SC2720 PMIC)**.

> [!NOTE]
> For the **Rear Flashlight / Torch LED** (`0x40608df4`), see [`docs/Flashlight.md`](/docs/Flashlight.md).  
> For the **External Speaker PA & Audio Codec**, see [`docs/Audio Subsystem.md`](/docs/Audio%20Subsystem.md).

---

## 1. Hardware Architecture

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
                      |  | Dedicated LDO_VIBR Regulator    |  |
                      |  | ADI Register: 0x40608e00        |  |
                      |  | Voltage Range: 2.8V - 3.5V      |  |
                      |  | Power Rail: VBAT (~3.8V Battery)|  |
                      |  +-----------------+---------------+  |
                      +--------------------|------------------+
                                           |
                                           v
                              +--------------------------+
                              |   ERM Vibration Motor    |
                              |  (REQUIRES BATTERY/VBAT) |
                              +--------------------------+
```

### Hardware Discovery
Disassembly of MOCOR `kernel.bin` at virtual address `0x80be704c` (`analog_phy.c`) reveals the dedicated vibrator regulator routine:

```arm
0x80be7050:  ldr   r5, =0x40608e00       ; PMIC LDO_VIBR register address
...
0x80be70a8:  add   r1, r4, #0x1c         ; 28 + level (2.8V to 3.5V)
0x80be70ac:  add   r0, pc, #0x170        ; "Vibrator vlaue  = %d V,POWER_ON!!!!!!!!!!!!!!!"
...
0x80be70e0:  add   r0, pc, #0x16c        ; "Vibrator vlaue  = %d,POWER_DOWN!!!!!!!!!!!!!!!"
```

* **Controller:** SC2720 PMIC integrated `LDO_VIBR` linear regulator.
* **Interface:** High-speed Analog-Digital Interface (ADI) serial bus mapped at `0x40608000`.
* **Motor Type:** Eccentric Rotating Mass (ERM) motor mounted inside the lower chassis.

> [!CAUTION]
> **Avoid Backlight Register Confusion:**  
> Registers `0x40608180`, `0x40608188`, and `0x406081b8` belong to the **LCD Backlight Current Sink / RGB Driver**. Writing to those addresses flashes the LCD panel rather than exciting the vibration motor. The dedicated ERM motor regulator is exclusively controlled at register **`0x40608e00`**.

---

## 2. Power Architecture & Battery Requirement (`VBAT`)

Unlike low-power digital peripherals and the rear flashlight LED (which runs off the 5V `VBUS` USB power rail), the ERM vibration motor draws high peak transient currents (~150mA to 300mA) when starting up.

To protect the USB controller from voltage sag and sudden brownouts, Unisoc routes the `LDO_VIBR` input rail directly to **`VBAT` (the physical battery spring terminals)**.

> [!IMPORTANT]
> **Without Battery:** If executing bare-metal payloads over USB with the battery removed, the motor receives **0.0 Volts** and cannot physically spin.
> 
> **How to execute bare-metal payloads with battery:**
> * **Method A (Recommended):** Put the battery inside the phone while powered OFF, hold `*` (Asterisk key), and plug in the USB cable to enter BootROM download mode.
> * **Method B:** If holding `*` with the battery inside triggers a standard power-on boot, start with the battery REMOVED, hold `*`, plug in USB to connect to BootROM. Once the DRAM payload is downloaded and executing (screen lights up), gently slide the battery into the phone spring terminals.

---

## 3. Register Bitfield: `PMIC_LDO_VIBR` (`0x40608e00`)

| Bits | Field | Description |
|:---:|:---|:---|
| **[2:0]** | `LDO_VIBR_V` | **LDO Output Voltage / Strength:** `0` (2.8V) to `7` (3.5V max rumble) |
| **[7:3]** | `RESERVED` | Preserved during read-modify-write |
| **[8]** | `LDO_VIBR_PD` | **Power-Down Control:** `0` = Power ON (active), `1` = Power Down (off) |
| **[9]** | `SLP_LDOVIBR_PD_EN` | **Sleep Power-Down Enable:** `0` = Active, `1` = Sleep Power Down |

* **Power ON:** Clear bit 8 and bit 9 (`val &= ~0x300`), set voltage bits `[2:0]`.
* **Power OFF:** Set bit 8 and bit 9 (`val |= 0x300`).

---

## 4. Bare-Metal C Driver

```c
#define PMIC_LDO_VIBR_REG 0x40608e00

uint32_t adi_read(uint32_t addr);
void adi_write(uint32_t addr, uint32_t val);
void sys_wait_ms(uint32_t ms);

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
    pmic_vib_set(0, 0);
}

// Double-Buzz Notification Pattern
void haptics_double_buzz(int level) {
    haptics_pulse(60, level);
    sys_wait_ms(50);
    haptics_pulse(60, level);
}

// Heartbeat Tactile Pattern (lub-dub)
void haptics_heartbeat(int level) {
    haptics_pulse(80, level);
    sys_wait_ms(100);
    haptics_pulse(120, level);
}

// Emergency SOS Morse Code Vibration: ... --- ...
void haptics_morse_sos(int level) {
    // S: 3 short dots (80ms)
    for (int i = 0; i < 3; i++) {
        haptics_pulse(80, level);
        sys_wait_ms(100);
    }
    sys_wait_ms(250);
    // O: 3 long dashes (250ms)
    for (int i = 0; i < 3; i++) {
        haptics_pulse(250, level);
        sys_wait_ms(100);
    }
    sys_wait_ms(250);
    // S: 3 short dots (80ms)
    for (int i = 0; i < 3; i++) {
        haptics_pulse(80, level);
        sys_wait_ms(100);
    }
}
```

---

## 5. Interactive Diagnostic Tool (`haptics_test.bin`)

A dedicated diagnostic tool is provided in `tools/ram_payload/`:

* Source: [`tools/ram_payload/src/haptics_test/`](/tools/ram_payload/src/haptics_test/)
* Binary: [`tools/ram_payload/bin/haptics_test.bin`](/tools/ram_payload/bin/haptics_test.bin)
* Runner Script: [`tools/ram_payload/run_haptics_test.sh`](/tools/ram_payload/run_haptics_test.sh)

### Keypad Controls

| Key | Mode / Action | Description |
|:---:|:---|:---|
| **`[1]`** | Tactile Click | 40 ms sharp UI click |
| **`[2]`** | Double Tick | 60 ms + 60 ms notification chime |
| **`[3]`** | Buzz | 200 ms medium buzz |
| **`[4]`** | Long Buzz | 400 ms long buzz |
| **`[5]`** | Heavy Rumble | 750 ms sustained vibration |
| **`[6]`** | Heartbeat | Dual-pulse "lub-dub" tactile pattern |
| **`[7]`** | Continuous Motor ON | Runs motor continuously at current level |
| **`[8]`** | Motor OFF | Stops vibration motor immediately |
| **`[9]`** | **SOS Morse Code** | Tactical silent emergency beacon (`... --- ...`) |
| **`[0]`** | Heavy Blast | 1.0 second max strength pulse |
| **`[*]`** | Instant Stop | Immediate shut-off |
| **`[Vol+]`** | Increase Strength | Steps level up (`0` to `7`, 2.8V to 3.5V) with tactile tick |
| **`[Vol-]`** | Decrease Strength | Steps level down (`0` to `7`) with tactile tick |
| **Hold `[End]`** | Clean Exit | Restores idle state and returns to bootloader/FDL |

### Launch Commands

```bash
# Launch on Main Screen (2.8" 240x320)
./tools/ram_payload/run_haptics_test.sh

# Launch on Cover Screen (1.77" 128x160)
./tools/ram_payload/run_haptics_test.sh cover
```
