# Nokia 2660 Flip (TA-1480) Audio & Haptics Subsystem

This document details the reverse-engineered hardware architecture, register maps, and bare-metal control methods for the **External Speaker Power Amplifier (PA)**, **SC2720 PMIC Haptic Vibrator Motor**, and **Voice Band Codec (VBC)** on the **Nokia 2660 Flip (TA-1480, Unisoc UMS9117 / T107)**.

---

## 1. Subsystem Overview

The audio and haptic feedback architecture of the Nokia 2660 Flip consists of three primary hardware domains:

```
                      +---------------------------------------+
                      |       Unisoc UMS9117 / T107 SoC       |
                      |                                       |
                      |  +---------------------------------+  |
                      |  | APB GPIO Controller             |  |
                      |  | Base: 0x40280000                |  |
                      |  | GPIO 31 (Bank 1, Bit 15)        |  |
                      |  +-----------------+---------------+  |
                      |                    |                  |
                      |  +-----------------v---------------+  |
                      |  | Voice Band Codec (VBC) / I2S    |  |
                      |  | Base: 0x40020000                |  |
                      |  | Clock Gate: 0x402e1000 bit 20   |  |
                      |  +---------------------------------+  |
                      |                    | ADI Serial Bus   |
                      +--------------------|------------------+
                                           |
                                           v
                      +---------------------------------------+
                      |         Unisoc SC2720 PMIC            |
                      |                                       |
                      |  +---------------------------------+  |
                      |  | Vibrator Motor Control LDO      |  |
                      |  | ADI Register: 0x40608df4        |  |
                      |  +-----------------+---------------+  |
                      |                    |                  |
                      |  +-----------------v---------------+  |
                      |  | Analog Audio Codec / Mixer      |  |
                      |  | Mixer Table: 0x81622a88         |  |
                      |  +-----------------+---------------+  |
                      +--------------------|------------------+
                                           |
                +--------------------------+--------------------------+
                |                                                     |
                v                                                     v
   +--------------------------+                         +--------------------------+
   |   External Speaker PA    |                         |  ERM Haptic Vib Motor    |
   |   (Driven via GPIO 31)   |                         |  (Driven via SC2720 LDO) |
   +--------------------------+                         +--------------------------+
```

---

## 2. External Speaker Power Amplifier (PA)

### Hardware Identification
The Nokia 2660 Flip utilizes an external high-efficiency Class-D speaker power amplifier driven by the Application Processor GPIO bus.

* **Control Line:** APB **GPIO 31** (Pin 31, Bank 1, Bit 15).
* **Firmware Function:** `codec_ext_pa_ctl` in `kernel.bin` at virtual address `0x80b34544`.
* **Kernel Reference:** References GPIO 31 configured in 1-wire pulse / square-wave mode to power and modulate the external speaker.

### Register Mapping: APB GPIO (Base `0x40280000`)
GPIO pins on the UMS9117 are grouped into banks of 16 pins (Bank 0: Pins 0..15, Bank 1: Pins 16..31).
Pin 31 resides in **Bank 1** with offset `(31 / 16) * 0x80 = 0x80` and bit position `31 % 16 = 15`.

| Register | Offset | Physical Address | Description |
|:---|:---:|:---:|:---|
| **`GPIO_PG_DATA`** | `0x80` | `0x40280080` | Bank 1 Pin Data (Bit 15 controls GPIO 31 state: 1=HIGH, 0=LOW) |
| **`GPIO_PG_DMSK`** | `0x84` | `0x40280084` | Bank 1 Data Write Mask (Bit 15: 1=Enable writes to Bit 15) |
| **`GPIO_PG_DIR`**  | `0x88` | `0x40280088` | Bank 1 Direction (Bit 15: 1=Output, 0=Input) |
| **`GPIO_PG_INEN`** | `0xA8` | `0x402800A8` | Bank 1 Input Buffer Enable (Bit 15: 0=Disable input buffer) |

### Bare-Metal Initialization & Square-Wave Generation
```c
#define GPIO_BASE 0x40280000

static inline void write32(uint32_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t read32(uint32_t addr) {
    return *(volatile uint32_t *)addr;
}

// Initialize GPIO 31 as Output for Speaker PA
void audio_pa_init(void) {
    uint32_t bank_offset = (31 >> 4) * 0x80; // 0x80 (Bank 1)
    uint32_t pin_mask    = 1u << (31 & 0xF);  // 1 << 15 (0x8000)

    // Set Pin 31 direction to OUTPUT
    write32(GPIO_BASE + bank_offset + 0x08, read32(GPIO_BASE + bank_offset + 0x08) | pin_mask);
    // Enable Pin 31 write mask
    write32(GPIO_BASE + bank_offset + 0x04, read32(GPIO_BASE + bank_offset + 0x04) | pin_mask);
    // Ensure PA starts LOW (muted/idle)
    write32(GPIO_BASE + bank_offset + 0x00, read32(GPIO_BASE + bank_offset + 0x00) & ~pin_mask);
}

// Drive square wave of frequency `freq_hz` for duration `duration_ms`
void speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    if (freq_hz == 0 || freq_hz > 20000) return;

    uint32_t half_period_us = 500000 / freq_hz;
    uint32_t total_cycles   = (freq_hz * duration_ms) / 1000;
    uint32_t pin_mask       = 1u << 15;
    uint32_t data_reg       = GPIO_BASE + 0x80;

    for (uint32_t i = 0; i < total_cycles; i++) {
        // High
        write32(data_reg, read32(data_reg) | pin_mask);
        sys_wait_us(half_period_us);
        // Low
        write32(data_reg, read32(data_reg) & ~pin_mask);
        sys_wait_us(half_period_us);
    }
}
```

---

## 3. SC2720 PMIC Haptic / Vibrator Motor

The Nokia 2660 Flip contains an Eccentric Rotating Mass (ERM) haptic vibration motor powered and regulated directly by a dedicated Low-Dropout (LDO) regulator inside the **Spreadtrum / Unisoc SC2720 PMIC**.

The PMIC is accessed over the internal high-speed **ADI (Analog-Digital Interface)** serial bus mapped at memory address `0x40608000`.

### Register Layout: SC2720 Vibrator Control (`0x40608df4`)
Disassembly of `kernel.bin` at `0x8085f1c4` reveals the exact bitfield used by the official Unisoc driver:

| Bits | Field | Description |
|:---:|:---|:---|
| **[15]** | `VIB_EN` | **Motor Power Gate:** `1` = Enable power LDO, `0` = Power off / brake |
| **[14:4]** | `RESERVED` | Preserved during read-modify-write operations |
| **[3:0]** | `VIB_LEVEL` | **Motor Voltage / Intensity Level:** `0x0` (Weakest) to `0xF` (Strongest, ~3.3V) |

### Bare-Metal Vibrator Driver
```c
#define PMIC_VIB_REG 0x40608df4

uint32_t adi_read(uint32_t addr);
void adi_write(uint32_t addr, uint32_t val);

// Set vibrator enable state and intensity level (0..15)
void vibrator_set(int enable, int level) {
    uint32_t cur = adi_read(PMIC_VIB_REG);
    if (enable) {
        if (level < 0)  level = 0;
        if (level > 15) level = 15;
        // Keep reserved bits, set bit 15 (enable), apply intensity in [3:0]
        cur = (cur & ~0x800F) | 0x8000 | (level & 0xF);
    } else {
        // Clear bit 15 to disable motor
        cur &= ~0x8000;
    }
    adi_write(PMIC_VIB_REG, cur);
}

// Generate timed vibration pulse in milliseconds
void vibrator_pulse(uint32_t duration_ms, int level) {
    vibrator_set(1, level);
    sys_wait_ms(duration_ms);
    vibrator_set(0, 0);
}
```

### Pre-Programmed Haptic Effects
In the `audio_test` payload, several haptic patterns are implemented:
1. **Haptic Click:** 50 ms pulse at Level 12 (tactile UI feedback).
2. **Double Buzz:** Two 80 ms pulses separated by 60 ms gap (notification chime).
3. **SOS Morse Code:** `... --- ...` vibration pattern (3 short dots, 3 long dashes, 3 short dots).
4. **Heavy Rumble:** 400 ms continuous pulse at max level 15.

---

## 4. Voice Band Codec (VBC) & PMIC Audio Codec

For full PCM/WAV playback beyond square-wave generation, the UMS9117 includes a hardware **Voice Band Codec (VBC)** peripheral connected via I2S/TDM to the SC2720 PMIC's internal DAC and mixer.

### Hardware Address Map
* **VBC Base Address:** `0x40020000`
* **APB Clock Gate Register:** `0x402e1000`
  * **Bit 20 (`0x00100000`):** `CLK_VBC_EN` (Must be enabled to clock the audio DSP pipeline).
* **PMIC Audio Mixer & Codec Entry Table:** Discovered in `kernel.bin` at virtual range `0x81622a88` – `0x81623160`.
  - Configures DAC gains, internal headphone vs. external handsfree routing, and sidetone cancellation.

### Unisoc PWM Controller
The SoC also includes an independent 4-channel Pulse Width Modulation controller:
* **PWM Base Address:** `0x402b0000`
* **Channels:** 4 independent channels spaced at `0x20` byte increments (`0x402b0000`, `0x402b0020`, `0x402b0040`, `0x402b0060`).

---

## 5. Interactive Audio & Haptics Diagnostic Payload (`audio_test.bin`)

The standalone test tool [`tools/ram_payload/src/audio_test/`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/tools/ram_payload/src/audio_test/) provides a complete, interactive hardware testbench that renders a real-time status dashboard on either screen while listening for keypad input.

### Keypad Mapping Table

| Key | Hex Code | Action | Hardware Details |
|:---:|:---:|:---|:---|
| **`1`** | `0x31` | **440 Hz (Note A4)** | Plays 300 ms concert pitch tone on Speaker PA |
| **`2`** | `0x32` | **880 Hz (Note A5)** | Plays 300 ms high octave tone on Speaker PA |
| **`3`** | `0x33` | **1000 Hz Reference Beep** | Standard 1 kHz test beep |
| **`4`** | `0x34` | **2000 Hz High Pitch** | High-frequency acoustic response test |
| **`5`** | `0x35` | **Classic Nokia Ringtone** | Full iconic 13-note melody (*Gran Vals*) |
| **`6`** | `0x36` | **Super Mario Coin Sound** | Two-tone (B5 + E6) authentic coin chime |
| **`7`** | `0x37` | **Audio Chirp Sweep** | Frequency ramp from 300 Hz to 2500 Hz |
| **`8`** | `0x38` | **Haptic Click (50 ms)** | Tactile feedback pulse via SC2720 PMIC |
| **`9`** | `0x39` | **SOS Morse Code Vibration** | Complete `... --- ...` emergency haptic pattern |
| **`0`** | `0x30` | **Heavy Rumble (400 ms)** | Maximum intensity vibration test |
| **`#`** | `0x23` | **Super Mario Theme Intro** | Iconic game intro jingle |
| **`*`** | `0x2A` | **Mute / Stop All Audio** | Immediately disables PA and motor |
| **`Vol+`** | `0x24` | **Increase Haptic Strength** | Steps motor level up (0 to 15) with feedback tick |
| **`Vol-`** | `0x25` | **Decrease Haptic Strength** | Steps motor level down (15 to 0) |
| **`Power / End`** | `0x02` | **Clean Exit** | Hold for ~1 second to terminate payload cleanly |

---

## 6. How to Run

Launch the diagnostic tool over USB into volatile DRAM:

```bash
# Test on Inner Main Screen (2.8" 240x320)
./tools/ram_payload/run_audio_test.sh

# Test on Outer Cover Screen (1.77" 128x160)
./tools/ram_payload/run_audio_test.sh cover
```
