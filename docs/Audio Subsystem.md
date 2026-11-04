# Nokia 2660 Flip (TA-1480) Loudspeaker & Audio Subsystem

This document details the reverse-engineered hardware architecture, register maps, and bare-metal control methods for the **External Speaker Power Amplifier (PA)**, **SC2720 PMIC Line-Out Audio Codec**, and **Voice Band Codec (VBC)** on the **Nokia 2660 Flip (Model TA-1480, Unisoc UMS9117 / T107 SoC, SC2720 PMIC)**.

> [!NOTE]
> For the **Rear Flashlight / Torch LED** (`0x40608df4`), see [`docs/Flashlight.md`](/docs/Flashlight.md).  
> For the **Haptic Vibration Motor** (`0x40608e00`), see [`docs/Haptics.md`](/docs/Haptics.md).

> [!WARNING]
> **Hardware Status:** The audio test program (`audio_test.bin`) is currently **not outputting audio via the physical speaker**. While the diagnostic payload, PMIC clock un-gating, and UI function properly in volatile DRAM, sound output through the physical loudspeaker is not currently working and is under active investigation.

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

## 3. SC2720 PMIC Audio Codec (Analog Front-End & Line-Out)

The Unisoc SC2720 PMIC houses the mixed-signal Audio Codec block comprising digital clock gating, analog bias references, bandgaps, DACs, and differential Line-Out / Headphone output stages.

### Why Register Dumps Previously Returned `0x0000`
On cold boot or under Spreadtrum FDL1 / BootROM execution, the entire Audio Codec domain inside the SC2720 PMIC is **clock-gated and power-gated by default**.
* **Global Clock Gate:** Register **`0x40608c08`** (`MODULE_CLK_EN`), **Bit 4 (`0x10`)** is the Master Audio Codec Clock Gate. When bit 4 is `0`, all ADI read commands to `0x40608700`..`0x406088ff` return `0x0000`, and all register writes are silently ignored by the unclocked silicon.
* **Audio Routing Verification (Headphone vs. Speaker):**
  - **Headphone Jack (3.5mm):** Handled by `__sprd_codec_ap_hp` (`0x801367e0`) for Audio Device ID `2`.
  - **Loudspeaker (Handsfree):** Handled by `__sprd_codec_ap_lout` (`0x801368d0`) for Audio Device ID `1`, routing analog sound into the external Awinic Class-D PA via Line-Out (`LOUT`).
  - Audio is **NOT** routed to the headphone jack during speaker tests; rather, the entire DAC and analog stages were previously unpowered.

### Reverse-Engineered PMIC Codec Register Map

| Register | Address | Description & Verified Bitfields |
|:---|:---|:---|
| **`MODULE_CLK`** | `0x40608c08` | PMIC Master Module Clock Enable (**Bit 4 (`0x10`): Audio Codec Clock En**) |
| **`ANA_CDC_00`** | `0x40608700` | Codec Analog Status / Diagnostics |
| **`ANA_CDC_04`** | `0x40608704` | Analog Routing Control (Bits [2:0] = `0x2`: LOUT Select) |
| **`ANA_CDC_08`** | `0x40608708` | Line-Out Volume / Gain (Bits [8:5] = `0..15` Gain Level) |
| **`ANA_CDC_18`** | `0x40608718` | Analog Bias & Power Gates (Bits [9:4] = `0x3f0`) |
| **`ANA_CDC_24`** | `0x40608724` | Line-Out Driver Enable (**Bit 13 (`0x2000`) & Bit 0 (`0x0001`)**: LOUT Active / Unmute) |
| **`ANA_CDC_30`** | `0x40608730` | Analog Bias Reference (Bits 11, 9, 1 = `0x0a02`) |
| **`ANA_CDC_34`** | `0x40608734` | Bandgap & Voltage Reference (Bits [15:14]=3, [12:11]=1, [3:2]=1, [1:0]=1 = `0xc805`) |
| **`ANA_CDC_38`** | `0x40608738` | Analog Core Power Reference (Bits [10:7]=0xa, [6:5]=1, bit 4=1, bit 2=1 = `0x0534`) |
| **`ANA_CDC_3C`** | `0x4060873c` | Bandgap Reference Control (Bit 0 & Bit 5..6 = `0x61`) |
| **`CFG_CDC_38`** | `0x40608838` | Digital Audio Clock Gate (Bits [5:2] = `0x3c`) |
| **`CFG_CDC_3C`** | `0x4060883c` | Digital Clock Logic Gate (Bit 0 = `0x01`) |

### Verified PMIC Codec Initialization Sequence
Extracted from `_sprd_codec_sc2720_init` (`0x80157540`) and `__sprd_codec_ap_lout` (`0x801368d0`) in `kernel.bin`:

```c
void pmic_audio_codec_init(int initial_gain) {
    // 1. Force Vibrator Motor LDO strictly OFF (so it never vibrates during audio test)
    adi_write(0x40608e00, adi_read(0x40608e00) | 0x300);

    // 2. Un-gate PMIC Master Audio Codec Clock
    adi_write_or(0x40608c08, 0x10);

    // 3. Enable Codec Digital Clocks
    adi_write_or(0x40608838, 0x3c);
    adi_write_or(0x4060883c, 0x01);

    // 4. Power on Analog Bias & References
    adi_write_or(0x40608718, 0x03f0);
    adi_write_or(0x40608730, 0x0a02);
    adi_write_or(0x40608734, 0xc805);
    adi_write_or(0x40608738, 0x0534);
    adi_write_or(0x4060873c, 0x0061);

    // 5. Configure Line-Out Route & Volume Gain
    adi_write(0x40608704, (adi_read(0x40608704) & ~0x7) | 0x2);
    adi_write(0x40608708, (adi_read(0x40608708) & ~0x1e0) | ((initial_gain & 0xf) << 5));

    // 6. Unmute Line-Out on 0x40608724
    adi_write_or(0x40608724, 0x2001);
}
```

### Acoustic Sound Generation Modes
The Nokia 2660 Flip supports two complementary bare-metal audio generation techniques:

1. **Direct Acoustic PA Tone (GPIO 31 Modulation):**
   Toggling APB GPIO 31 at the desired acoustic frequency ($f$) directly modulates the Awinic Class-D output stage across the speaker voice coil. This produces loud, guaranteed square-wave sound directly from the physical loudspeaker without depending on the PMIC DAC or DMA pipeline.
2. **PMIC Line-Out DAC Modulation (1-Wire Mode 2):**
   Enables the PA continuously in Mode 2 via 1-wire pulse and modulates the PMIC Line-Out stage (`0x40608724` / `0x40608708`).

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

> [!WARNING]
> **Current Status (WIP - No Speaker Output):** The audio test program is currently **not outputting audio via the physical speaker**. While the payload successfully runs in volatile DRAM, handles screen rendering, decodes keypad input, un-gates PMIC codec clocks, and writes to the amplifier registers, audio output through the loudspeaker is currently non-functional and under active investigation.

The standalone test tool [`tools/ram_payload/src/audio_test/`](/tools/ram_payload/src/audio_test/) provides a complete, interactive hardware testbench that renders a real-time status dashboard on either screen while listening for keypad input.

### Keypad Mapping Table

| Key | Hex Code | Action | Hardware Details |
|:---:|:---:|:---|:---|
| **`1`** | `0x31` | **440 Hz Beep (Note A4)** | Direct PA Acoustic Tone on GPIO 31 (instant square wave) |
| **`2`** | `0x32` | **880 Hz Tone (Note A5)** | Direct PA Acoustic Tone on GPIO 31 (high octave tone) |
| **`3`** | `0x33` | **1000 Hz Reference Beep** | Standard 1 kHz test beep via Speaker PA |
| **`4`** | `0x34` | **2000 Hz High Pitch** | High-frequency acoustic response test |
| **`5`** | `0x35` | **Classic Nokia Ringtone** | Full iconic 13-note melody (*Gran Vals*) on Loudspeaker |
| **`6`** | `0x36` | **Super Mario Coin Sound** | Two-tone (B5 + E6) authentic coin chime |
| **`7`** | `0x37` | **Audio Chirp Sweep** | Frequency ramp from 300 Hz to 2500 Hz |
| **`8`** | `0x38` | **Speaker Siren & Cone Click** | Acoustic warble sweep + voice coil pulse test |
| **`9`** | `0x39` | **Comprehensive Codec Dump** | Dumps PMIC Global, Codec CFG & ANA registers to UART |
| **`0`** | `0x30` | **PMIC Line-Out DAC Test** | Tests PMIC Codec Mode 2 Line-Out modulation (`0x40608724`) |
| **`#`** | `0x23` | **Super Mario Theme Intro** | Iconic game intro jingle |
| **`*`** | `0x2A` | **Mute / Stop All Audio** | Immediately disables PA and silences output |
| **`Vol+`** | `0x24` | **Increase Volume Gain** | Steps Line-Out gain up (0 to 15) with acoustic feedback blip |
| **`Vol-`** | `0x25` | **Decrease Volume Gain** | Steps Line-Out gain down (15 to 0) with acoustic feedback blip |
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
