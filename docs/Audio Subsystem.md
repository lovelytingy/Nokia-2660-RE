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
                      |  | Analog Audio Codec / Mixer      |  |
                      |  | Mixer Table: 0x81622a88         |  |
                      |  | Differential Audio DAC Outputs  |  |
                      |  +-----------------+---------------+  |
                      +--------------------|------------------+
                                           | Audio Differential Signals (AU_HPL / AU_HPR)
                                           v
                             +--------------------------+
                             |   External Speaker PA    |
                             |   (Enable on GPIO 31)    |
                             |   Power: VBAT (~3.8V)    |
                             +--------------------------+

> [!NOTE]
> For the **Rear Flashlight / Torch LED** (`0x40608df4`) and **Haptic Vibration Motor** (`0x406081b8` / GPIO 45), see the dedicated reference [`docs/Flashlight & Haptics.md`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/docs/Flashlight%20%26%20Haptics.md).
```

---

## 2. External Speaker Power Amplifier (PA)

### Hardware Identification
The Nokia 2660 Flip utilizes an external high-efficiency Class-D speaker power amplifier (Awinic AW-series) driven by the Application Processor GPIO bus.

* **Control Line:** APB **GPIO 31** (Pin 31, Bank 1, Bit 15).
* **Firmware Function:** `codec_ext_pa_ctl` in `kernel.bin` at virtual address `0x80b34544`.
* **Kernel Reference:** References GPIO PROD ID 31 (`0x1f`) configured in 1-wire pulse mode to select gain and enable the amplifier.

> [!IMPORTANT]
> **Power Architecture & Battery Requirement (`VBAT`):**
> * **Power Rail:** The speaker Class-D PA draws high peak currents (up to 500mA–1A) and is connected directly to **`VBAT` (the physical battery spring contacts)**, completely isolated from the USB 5V (`VBUS`) rail to prevent brownouts.
> * **Without Battery:** If executing payloads over USB with the battery removed, the PA power rail has **0.0 Volts** and the amplifier cannot power the speaker.
> * **Safe Operation:** Begin USB connection with the battery removed, hold `*` to enter BootROM mode. Once the payload is executing in volatile DRAM, **pop the battery into the phone** to energize the amplifier rail!
> * **Analog vs. Enable:** GPIO 31 acts as an enable / gain select line. The actual sound waveform is routed differentially from the Unisoc Voice Band Codec (VBC) DAC into the amplifier's inputs.

### Register Mapping: APB GPIO (Base `0x40280000`)
GPIO pins on the UMS9117 are grouped into banks of 16 pins (Bank 0: Pins 0..15, Bank 1: Pins 16..31).
Pin 31 resides in **Bank 1** with offset `(31 / 16) * 0x80 = 0x80` and bit position `31 % 16 = 15`.

| Register | Offset | Physical Address | Description |
|:---|:---:|:---:|:---|
| **`GPIO_PG_DATA`** | `0x80` | `0x40280080` | Bank 1 Pin Data (Bit 15 controls GPIO 31 state: 1=HIGH, 0=LOW) |
| **`GPIO_PG_DMSK`** | `0x84` | `0x40280084` | Bank 1 Data Write Mask (Bit 15: 1=Enable writes to Bit 15) |
| **`GPIO_PG_DIR`**  | `0x88` | `0x40280088` | Bank 1 Direction (Bit 15: 1=Output, 0=Input) |
| **`GPIO_PG_INEN`** | `0xA8` | `0x402800A8` | Bank 1 Input Buffer Enable (Bit 15: 0=Disable input buffer) |

### Bare-Metal Initialization & 1-Wire Mode 2 Control
```c
#define GPIO_BASE 0x40280000
#define AUDIO_PA_GPIO 31

static int gpio_pin_set(unsigned id, unsigned off, int state) {
    uint32_t addr, tmp, shl = id & 0xf;
    if (id >= 0x80) return -1;
    addr = GPIO_BASE + ((id >> 4) << 7) + off;
    tmp = *(volatile uint32_t *)addr;
    if (state < 0) return (tmp >> shl) & 1;
    tmp &= ~(1u << shl);
    tmp |= (state & 1) << shl;
    *(volatile uint32_t *)addr = tmp;
    return 0;
}

// Enable External PA in Mode 2 (Speaker Mode)
// Re-engineered from MOCOR kernel.bin codec_ext_pa_ctl @ 0x115890:
// Sends 1 pulse (HIGH -> LOW) and then holds HIGH to keep the amplifier active.
void audio_pa_enable(void) {
    gpio_pin_set(AUDIO_PA_GPIO, 4, 1);    // GPIO_DMSK
    gpio_pin_set(AUDIO_PA_GPIO, 8, 1);    // GPIO_DIR = output
    gpio_pin_set(AUDIO_PA_GPIO, 0x28, 0); // GPIO_INEN = disabled

    // 1-wire Mode 2 sequence: HIGH (5us) -> LOW (5us) -> HIGH (stays active)
    gpio_pin_set(AUDIO_PA_GPIO, 0, 1);
    sys_wait_clk(250);
    gpio_pin_set(AUDIO_PA_GPIO, 0, 0);
    sys_wait_clk(250);
    gpio_pin_set(AUDIO_PA_GPIO, 0, 1);
    sys_wait_clk(250);
}

// Disable External PA (Shutdown Mode: hold LOW > 1ms)
void audio_pa_disable(void) {
    gpio_pin_set(AUDIO_PA_GPIO, 0, 0);
    sys_wait_ms(2);
}
```

### SC2720 PMIC Audio Codec & Line-Out Routing
The actual audio waveform originates as differential analog audio from the SC2720 PMIC Line-Out (LOUT) block:

| Register | Address | Function |
|:---|:---:|:---|
| **`ANA_CDC_04`** | `0x40608704` | Analog Routing (Bits [2:0] = `0x2` for LOUT enable) |
| **`ANA_CDC_08`** | `0x40608708` | Analog Gain Control (Bits [8:5] = `0x0` mute to `0xF` max gain) |
| **`ANA_CDC_18`** | `0x40608718` | Codec Bias & Power Reference (Bits [14:6] = `0x7fc0`) |
| **`ANA_CDC_28`** | `0x40608728` | LOUT Mute Control (Bit 3: `1` = Unmute / Active, `0` = Mute) |
| **`ANA_CDC_3C`** | `0x4060873c` | Bandgap Reference (Bits [6, 5, 0] = `0x61`) |
| **`CFG_CDC_38`** | `0x40608838` | Digital Audio Clock Gate (Bits [4, 1, 0] = `0x13`) |

### Bare-Metal Tone Playback
```c
void speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms, int gain) {
    if (freq_hz == 0 || duration_ms == 0) return;

    uint32_t half_period_us = 500000 / freq_hz;
    uint32_t total_cycles   = (freq_hz * duration_ms) / 1000;

    // 1. Energize External Class-D PA in Mode 2
    audio_pa_enable();

    // 2. Modulate PMIC Line-Out gain & mute bit to generate audio square wave
    uint32_t reg_08 = adi_read(0x40608708) & ~0x1e0;
    uint32_t val_hi = reg_08 | ((gain & 0xf) << 5);
    uint32_t val_lo = reg_08;
    uint32_t reg_28 = adi_read(0x40608728);

    for (uint32_t i = 0; i < total_cycles; i++) {
        // High phase
        adi_write(0x40608708, val_hi);
        adi_write(0x40608728, reg_28 | 0x8);
        sys_wait_us(half_period_us);

        // Low phase
        adi_write(0x40608708, val_lo);
        adi_write(0x40608728, reg_28 & ~0x8);
        sys_wait_us(half_period_us);
    }

    // 3. Power down PA to prevent idle hiss
    audio_pa_disable();
}
```

---

## 3. SC2720 PMIC Haptic / Vibrator Motor

The Nokia 2660 Flip contains an Eccentric Rotating Mass (ERM) haptic vibration motor powered and regulated directly by a dedicated Low-Dropout (LDO) regulator inside the **Spreadtrum / Unisoc SC2720 PMIC**.

The PMIC is accessed over the internal high-speed **ADI (Analog-Digital Interface)** serial bus mapped at memory address `0x40608000`.

> [!NOTE]
> Physical register `0x40608df4` is the **Flashlight / Rear Torch LED** controller. The dedicated **ERM Haptic Vibrator Motor** is powered by the `LDO_VIBR` rail controlled at **`0x40608e00`**. For full details, see [`docs/Flashlight & Haptics.md`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/docs/Flashlight%20%26%20Haptics.md).

### Register Layout: SC2720 Vibrator Control (`0x40608e00` - LDO_VIBR)
Disassembly of `kernel.bin` at `0x80be704c` (`analog_phy.c`) reveals the exact bitfield used by the official Unisoc driver:

| Bits | Field | Description |
|:---:|:---|:---|
| **[2:0]** | `LDO_VIBR_V` | **Motor Voltage / Intensity Level:** `0` (2.8V) to `7` (3.5V max rumble) |
| **[7:3]** | `RESERVED` | Preserved during read-modify-write operations |
| **[8]** | `LDO_VIBR_PD` | **Motor Power Gate:** `0` = Power ON (active), `1` = Power Down / Disable |
| **[9]** | `SLP_LDOVIBR_PD_EN` | **Sleep Power-Down Enable:** `0` = Active, `1` = Sleep Power Down |

### Bare-Metal Vibrator Driver
```c
#define PMIC_LDO_VIBR_REG 0x40608e00

uint32_t adi_read(uint32_t addr);
void adi_write(uint32_t addr, uint32_t val);

// Set vibrator enable state and voltage level (0..7 -> 2.8V..3.5V)
void vibrator_set(int enable, int level) {
    if (enable) {
        if (level < 0) level = 0;
        if (level > 7) level = 7;
        uint32_t val = adi_read(PMIC_LDO_VIBR_REG);
        val = (val & ~0x7) | (level & 0x7);
        val &= ~0x300; // Clear bit 8 (PD) and bit 9 (SLP_PD) -> Power ON
        adi_write(PMIC_LDO_VIBR_REG, val);
    } else {
        uint32_t val = adi_read(PMIC_LDO_VIBR_REG);
        val |= 0x300;  // Set bit 8 and bit 9 -> Power Down
        adi_write(PMIC_LDO_VIBR_REG, val);
    }
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
