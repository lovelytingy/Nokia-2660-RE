# Nokia 2660 Flip (TA-1480) Loudspeaker & Audio Subsystem

This document details the reverse-engineered hardware architecture, register maps, and bare-metal control methods for the **External Speaker Power Amplifier (PA)**, **SC2720 PMIC Line-Out Audio Codec**, and **Voice Band Codec (VBC)** on the **Nokia 2660 Flip (Model TA-1480, Unisoc UMS9117 / T107 SoC, SC2720 PMIC)**.

> [!NOTE]
> For the **Rear Flashlight / Torch LED** (`0x40608df4`), see [`docs/Flashlight.md`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/docs/Flashlight.md).  
> For the **Haptic Vibration Motor** (`0x40608e00`), see [`docs/Haptics.md`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/docs/Haptics.md).

---

## 1. Subsystem Overview

The audio architecture of the Nokia 2660 Flip consists of three primary hardware blocks:

```
                      +---------------------------------------+
                      |       Unisoc UMS9117 / T107 SoC       |
                      |                                       |
                      |  +---------------------------------+  |
                      |  | APB GPIO Controller             |  |
                      |  | Base: 0x40280000                |  |
                      |  | GPIO 31 (Bank 1, Bit 15)        |  |
                      |  | -> Awinic PA 1-Wire Mode Select |  |
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
                      |  | Line-Out DAC (AU_HPL / AU_HPR)  |  |
                      |  | Registers: 0x40608704 - 873C    |  |
                      |  +-----------------+---------------+  |
                      +--------------------|------------------+
                                           | Differential Audio (IN+ / IN-)
                                           v
                              +--------------------------+
                              |   External Speaker PA    |
                              |   (Awinic AW87xx Class-D)|
                              |   Mode Select: GPIO 31   |
                              |   Power: VBAT (~3.8V)    |
                              +--------------------------+
                                           |
                                           v
                              +--------------------------+
                              |   Loudspeaker Transducer |
                              +--------------------------+
```

---

## 2. External Speaker Power Amplifier (PA)

### Hardware Identification
The Nokia 2660 Flip utilizes an external high-efficiency Class-D speaker power amplifier (Awinic AW87xx series) driven by the Application Processor GPIO bus:

* **Control Line:** APB **GPIO 31** (Pin 31, Bank 1, Bit 15).
* **Firmware Function:** `codec_ext_pa_ctl` in `kernel.bin` at offset `0x115890` (virtual address `0x80b34544`).
* **Kernel Reference:** References GPIO PROD ID 31 (`0x1f`) configured in 1-wire pulse mode to select gain mode and enable the amplifier.
* **Operational Mode:** **Mode 2 (Speaker Mode)** is selected by sending 1 short pulse (HIGH $\to$ LOW $\to$ HIGH) and then holding GPIO 31 HIGH.

> [!IMPORTANT]
> **Power Architecture & Battery Requirement (`VBAT`):**
> * **Power Rail:** The speaker Class-D PA draws high peak currents (up to 500mA–1A) and is connected directly to **`VBAT` (the physical battery spring contacts)**, completely isolated from the USB 5V (`VBUS`) rail to prevent brownouts.
> * **Without Battery:** If executing payloads over USB with the battery removed, the PA power rail has **0.0 Volts** and the amplifier cannot power the speaker cone.
> * **Safe Operation:** Begin USB connection with the battery removed, hold `*` to enter BootROM mode. Once the payload is executing in volatile DRAM and the screen turns on, **gently slide the battery into the phone** to energize the amplifier rail!
> * **Analog vs. Enable:** GPIO 31 is strictly a 1-wire control line (NOT an audio coil line). Bit-banging GPIO 31 at audio frequencies repeatedly shuts down the amplifier. The actual sound waveform is routed differentially from the SC2720 PMIC Line-Out (LOUT) DAC into the amplifier's analog inputs.

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
// Sends 1 pulse (HIGH -> LOW -> HIGH) and then holds HIGH to keep the amplifier active.
void audio_pa_enable(void) {
    gpio_pin_set(AUDIO_PA_GPIO, 4, 1);    // GPIO_DMSK: enable write
    gpio_pin_set(AUDIO_PA_GPIO, 8, 1);    // GPIO_DIR: output
    gpio_pin_set(AUDIO_PA_GPIO, 0x28, 0); // GPIO_INEN: input disabled

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

---

## 3. SC2720 PMIC Audio Codec & Line-Out Routing

The actual audio waveform originates as differential analog audio from the SC2720 PMIC Line-Out (LOUT) block:

| Register | Address | Function |
|:---|:---:|:---|
| **`ANA_CDC_04`** | `0x40608704` | Analog Routing (Bits [2:0] = `0x2` for Line-Out DAC enable) |
| **`ANA_CDC_08`** | `0x40608708` | Analog Gain Control (Bits [8:5] = `0x0` mute to `0xF` max gain) |
| **`ANA_CDC_18`** | `0x40608718` | Codec Bias & Power Reference (Bits [14:6] = `0x7fc0`) |
| **`ANA_CDC_28`** | `0x40608728` | LOUT Mute Control (Bit 3: `1` = Unmute / Active, `0` = Mute) |
| **`ANA_CDC_3C`** | `0x4060873c` | Bandgap Reference (Bits [6, 5, 0] = `0x61`) |
| **`CFG_CDC_38`** | `0x40608838` | Digital Audio Clock Gate (Bits [4, 1, 0] = `0x13`) |

### PMIC Codec Setup
```c
void pmic_audio_codec_init(void) {
    // 1. Enable digital audio clock gate in PMIC
    uint32_t clk = adi_read(0x40608838);
    adi_write(0x40608838, clk | 0x13);

    // 2. Power on analog bias references
    uint32_t bias = adi_read(0x40608718);
    adi_write(0x40608718, bias | 0x7fc0);

    // 3. Enable bandgap reference
    uint32_t bg = adi_read(0x4060873c);
    adi_write(0x4060873c, bg | 0x61);

    // 4. Configure Line-Out routing to DAC
    adi_write(0x40608704, 0x0002);

    // 5. Set default volume gain (level 10 of 15)
    adi_write(0x40608708, (adi_read(0x40608708) & ~0x1e0) | (10 << 5));

    // 6. Unmute Line-Out
    uint32_t mute = adi_read(0x40608728);
    adi_write(0x40608728, mute | 0x8);
}
```

### Tone Generation into Active Class-D PA
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

## 4. Voice Band Codec (VBC) & DSP Pipeline

For full PCM/WAV playback beyond square-wave generation, the UMS9117 includes a hardware **Voice Band Codec (VBC)** peripheral connected via I2S/TDM to the SC2720 PMIC's internal DAC and mixer.

### Hardware Address Map
* **VBC Base Address:** `0x40020000`
* **APB Clock Gate Register:** `0x402e1000`
  * **Bit 20 (`0x00100000`):** `CLK_VBC_EN` (Must be enabled to clock the audio DSP pipeline).
* **PMIC Audio Mixer & Codec Entry Table:** Discovered in `kernel.bin` at virtual range `0x81622a88` – `0x81623160`.
  - Configures DAC gains, internal headphone vs. external handsfree routing, and sidetone cancellation.

---

## 5. Interactive Loudspeaker Diagnostic Payload (`audio_test.bin`)

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
| **`8`** | `0x38` | **PA Cone Click Test** | Single 50µs pulse to verify voice coil / acoustic PA response |
| **`9`** | `0x39` | **PMIC Codec Dump** | Dumps audio registers (0x40608704, 8708, 8728, 8838) to UART |
| **`0`** | `0x30` | **Continuous 1 kHz Tone** | Toggles continuous 1 kHz tone generator ON / OFF |
| **`#`** | `0x23` | **Super Mario Theme Intro** | Iconic game intro jingle |
| **`*`** | `0x2A` | **Mute / Stop All Audio** | Immediately disables PA and silences output |
| **`Vol+`** | `0x24` | **Increase Volume Gain** | Steps Line-Out gain up (0 to 15) |
| **`Vol-`** | `0x25` | **Decrease Volume Gain** | Steps Line-Out gain down (15 to 0) |
| **Hold `End`** | `0x02` | **Clean Exit** | Restores idle state and returns to bootloader/FDL |

---

## 6. How to Run

Launch the diagnostic tool over USB into volatile DRAM:

```bash
# Test on Inner Main Screen (2.8" 240x320)
./tools/ram_payload/run_audio_test.sh

# Test on Outer Cover Screen (1.77" 128x160)
./tools/ram_payload/run_audio_test.sh cover
```
