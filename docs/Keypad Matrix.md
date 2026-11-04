# Keypad Matrix Controller & Physical Button Mapping

Hardware register documentation, matrix decoding formulas, and reverse-engineered physical keymap for the **Nokia 2660 Flip (TA-1480 / Unisoc T107 / UMS9117)** running software version `10.02.17.00`.

---

## 1. Architectural Overview

The keypad subsystem on the Unisoc UMS9117 / T107 SoC combines a dedicated **APB-attached Hardware Keypad Controller** for matrix keys and an **SC2720 PMIC EIC (External Interrupt Controller)** line for the Power/End button.

```
                           +----------------------------------------+
                           |       Unisoc UMS9117 (Cortex-A7)       |
                           +----------------------------------------+
                               |                                |
                      APB Bus (0x40250000)             ADI Bus (0x40608000)
                               |                                |
                               v                                v
                   +------------------------+      +------------------------+
                   | Keypad Matrix Scanner  |      |   SC2720 PMIC EIC      |
                   | 8 Columns x 8 Rows     |      | Pin 1: Power/End Key   |
                   +------------------------+      +------------------------+
                               |                                |
                               v                                v
                   +--------------------------------------------------------+
                   |         Nokia 2660 Flip Physical Keypad Panel          |
                   |  Digits 0-9, *, #, D-Pad 5-way, Softkeys, Volume, SOS  |
                   +--------------------------------------------------------+
```

### Key Hardware Boundaries:
1. **Matrix Scanning (`0x40250000`):** Drives a 5×5 physical active grid (out of an 8×8 theoretical maximum) to scan 23 physical keys (numeric keypad, D-pad, softkeys, call key, volume rocker, and SOS button).
2. **Dedicated Power/End Key:** The red Power / End key is **not** part of the matrix. It routes directly to PMIC EIC Pin 1 (`0x40608280`, bit 1) to enable hardware power-on, reset, and emergency shutdown even when the APB bus or RTOS is asleep.
3. **Power Domain & Clock:** Before accessing the matrix controller, software must enable:
   - APB Power Domain: `*(volatile uint32_t*)0x402e1000 |= 0x100` (`APB_PWR_ON`)
   - Keypad Clock: `*(volatile uint32_t*)0x402e1010 |= 0x002` (`CLK_KPD_EN`)

---

## 2. Hardware Register Map (`0x40250000`)

The keypad matrix controller base address is `0x40250000` on the APB peripheral bus (verified in `kernel.bin` module `kpd_phy.c` at `0x80cd4224`).

| Offset | Name | Type | Reset | Description |
|:---:|:---|:---:|:---:|:---|
| `0x00` | `kpd_ctrl` | R/W | `0x00000000` | **Control Register:**<br>• Bit 0: Scanner Enable (`1` = active)<br>• Bit 1: Sleep Mode (`0` = awake)<br>• Bit 2: Long press detection enable<br>• Bits [15:8]: Column enable mask (TA-1480: `0x1F`)<br>• Bits [23:16]: Row enable mask (TA-1480: `0x1F`) |
| `0x04` | `kpd_int_en` | R/W | `0x00000000` | **Interrupt Enable Mask:** Write `0x0FFF` to enable all keypress and release events. |
| `0x08` | `kpd_int_raw` | RO | `0x00000000` | **Raw Event Status:**<br>• Bits [3:0]: Key DOWN events (1 bit per simultaneous key)<br>• Bits [7:4]: Key UP (release) events |
| `0x0C` | `kpd_int_mask` | RO | `0x00000000` | Masked interrupt status (`int_raw & int_en`). |
| `0x10` | `kpd_int_clr` | WO | `0x00000000` | **Interrupt Clear Register:** Write `0x0FFF` to acknowledge and clear pending events. |
| `0x18` | `kpd_polarity` | R/W | `0x0000FFFF` | Keypad line polarity (`0xFFFF` for standard active-low matrix switches). |
| `0x1C` | `kpd_debounce` | R/W | `0x00000010` | Hardware debounce filter duration (default: `16` cycles). |
| `0x20` | `kpd_long_key` | R/W | `0x00000000` | Long press threshold timer counter. |
| `0x24` | `kpd_sleep_cnt`| R/W | `0x00000000` | Automatic sleep timer counter. |
| `0x28` | `kpd_clk_div` | R/W | `0x00000001` | Scanner clock divider ratio (default: `1`). |
| `0x2C` | `kpd_key_status` | RO | `0x00000000` | **Real-Time Key Coordinate Register:** Packed 8-bit slices for up to 4 pressed keys simultaneously. |

---

## 3. Key Matrix Coordinate Extraction

When `kpd_int_raw` signals an event, `kpd_key_status` holds the physical row and column coordinates.

### Bit Layout of `kpd_key_status` (`0x2C`):
Each 8-bit slice represents one active matrix key:
* **Slice 0:** Bits [7:0]
* **Slice 1:** Bits [15:8]
* **Slice 2:** Bits [23:16]
* **Slice 3:** Bits [31:24]

Within each 8-bit slice `S`:
$$\text{Row} = \frac{S \ \& \ \text{0x70}}{16} = (S \gg 4) \ \& \ 7$$
$$\text{Column} = S \ \& \ 7$$

### Linear Matrix Index:
MOCOR OS and FPDoom index the 64-entry (8×8) key table as:
$$\text{Key Index} = (\text{Column} \times 8) + \text{Row}$$

In assembly (`kpd_phy.c` @ `0x80cd408e`):
```c
uint32_t status = KEYPAD_BASE->key_status;
unsigned k = status & 0xFF;
unsigned row = (k >> 4) & 7;
unsigned col = k & 7;
unsigned index = (col << 3) | row;
```

---

## 4. Nokia 2660 Flip Physical Keymap

The keymap table was extracted directly from `kernel.bin` at file offset `0xaefcc4` (DRAM address `0x814f5ac4`). It is a 64-entry table of 16-bit little-endian signed integers (`tools/ram_payload/data/keymap.bin`).

### Matrix Grid (8 Columns × 8 Rows):

| Matrix Row | Col 0 | Col 1 | Col 2 | Col 3 | Col 4 | Col 5..7 |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **Row 0** | `*` (`0x2A`) | `0` (`0x30`) | `SHORTCUT` (`0x1F`) | `#` (`0x23`) | `L-SEL` (`0x08`) | — |
| **Row 1** | `7` (`0x37`) | `3` (`0x33`) | `LEFT` (`0x06`) | `9` (`0x39`) | `R-SEL` (`0x09`) | — |
| **Row 2** | `1` (`0x31`) | `2` (`0x32`) | `8` (`0x38`) | `6` (`0x36`) | `SOS` (`0x29`) | — |
| **Row 3** | `CALL` (`0x01`)| `DOWN` (`0x05`)| `CENTER` (`0x0D`)| `RIGHT` (`0x07`)| `VOL +` (`0x24`) | — |
| **Row 4** | `4` (`0x34`) | `5` (`0x35`) | `UP` (`0x04`) | `BACK` (`0x0E`) | `VOL -` (`0x25`) | — |
| **Row 5..7**| — | — | — | — | — | — |

---

## 5. Physical Button Reference Table

| Physical Button | Official Label | Hex Code | Col | Row | Pin Source | Description & Function |
|:---|:---|:---:|:---:|:---:|:---|:---|
| **0** | Key 0 / Space | `0x30` | 1 | 0 | Matrix (`0x40250000`) | Standard numeric input / space character |
| **1** | Key 1 | `0x31` | 0 | 2 | Matrix (`0x40250000`) | Standard numeric input / punctuation |
| **2** | Key 2 | `0x32` | 1 | 2 | Matrix (`0x40250000`) | Standard numeric input / ABC |
| **3** | Key 3 | `0x33` | 1 | 1 | Matrix (`0x40250000`) | Standard numeric input / DEF |
| **4** | Key 4 | `0x34` | 0 | 4 | Matrix (`0x40250000`) | Standard numeric input / GHI |
| **5** | Key 5 | `0x35` | 1 | 4 | Matrix (`0x40250000`) | Standard numeric input / JKL |
| **6** | Key 6 | `0x36` | 3 | 2 | Matrix (`0x40250000`) | Standard numeric input / MNO |
| **7** | Key 7 | `0x37` | 0 | 1 | Matrix (`0x40250000`) | Standard numeric input / PQRS |
| **8** | Key 8 | `0x38` | 2 | 2 | **PMIC EIC Pin 9** (`0x40608280`) | Standard numeric input / TUV; routed to PMIC EIC Pin 9 |
| **9** | Key 9 | `0x39` | 3 | 1 | Matrix (`0x40250000`) | Standard numeric input / WXYZ |
| **`*` (Asterisk)** | Star Key | `0x2A` | 0 | 0 | Matrix (`0x40250000`) | Held at USB plug-in for BootROM download |
| **`#` (Hash)** | Hash Key | `0x23` | 3 | 0 | Matrix (`0x40250000`) | Standard symbol / lock toggle |
| **Green Call / Dial** | Call Key | `0x01` | 0 | 3 | Matrix (`0x40250000`) | Answers incoming calls / opens call log |
| **Red Power / End** | Power / End Key | `0x02` | — | — | **PMIC EIC Pin 1** (`0x40608280`) | Ends calls, exits apps, powers phone on/off (active-low) |
| **D-Pad Up** | Scroll Up | `0x04` | 2 | 4 | Matrix (`0x40250000`) | Navigates upwards |
| **D-Pad Down** | Scroll Down | `0x05` | 1 | 3 | Matrix (`0x40250000`) | Navigates downwards |
| **D-Pad Left** | Scroll Left | `0x06` | 2 | 1 | Matrix (`0x40250000`) | Navigates leftwards |
| **D-Pad Right** | Scroll Right | `0x07` | 3 | 3 | Matrix (`0x40250000`) | Navigates rightwards |
| **D-Pad Center (OK)** | Scroll Center | `0x0D` | 2 | 3 | Matrix (`0x40250000`) | Selects / confirms menu items |
| **Left Selection Key** | L-Selection Key | `0x08` | 4 | 0 | Matrix (`0x40250000`) | Left upper menu key (Options / Menu) |
| **Right Selection Key** | R-Selection Key | `0x09` | 4 | 1 | Matrix (`0x40250000`) | Right upper menu key (Contacts / Clear) |
| **Shortcut Key** | Shortcut Key | `0x1F` | 2 | 0 | Matrix (`0x40250000`) | Positioned beside Call key; quick access to assigned features |
| **Back Key** | Back Key | `0x0E` | 3 | 4 | Matrix (`0x40250000`) | Positioned beside End key; steps back through menus |
| **Volume Up** | Volume + | `0x24` | 4 | 3 | Matrix (`0x40250000`) | Side upper rocker button |
| **Volume Down** | Volume - | `0x25` | 4 | 4 | Matrix (`0x40250000`) | Side lower rocker button |
| **Emergency / SOS** | SOS Key | `0x29` | 4 | 2 | Matrix (`0x40250000`) | Dedicated hardware emergency / ICE assistance button |

---

## 6. PMIC EIC Power Button & Key 8 Special Routing

The Unisoc UMS9117 matrix controller is physically wired as a 5×5 grid (25 crosspoints). Because the Nokia 2660 Flip features 26 distinct input lines (keypad, D-pad, dual SOS keys, volume rocker, and flip hall sensor), **two keys are routed through the SC2720 PMIC External Interrupt Controller (EIC)** over Spreadtrum's ADI serial bus:
1. **Power / End Key:** Routed to **PMIC EIC Pin 1** (`EIC_PB`).
2. **Numeric Key 8:** Routed to **PMIC EIC Pin 9** (`EIC_PIN9`).

### Reading the Power Button (and Avoiding the Bitmask Bug):
The debounced EIC data register is located at address **`0x40608280`** (`EIC_DBNC_DATA`).

```c
#define EIC_PB 1 // Bit 1 corresponds to Power Button

// CRITICAL FIX: Must mask with (& 1) before inversion!
// An unmasked shift ((val >> 1) ^ 1) leaves Bit 8 (shifted from EIC Pin 9 / Key 8) 
// intact, causing any Key 8 press to falsely trigger a Power/End event!
static inline int read_power_button(void) {
    uint32_t val = adi_read(0x40608280);
    return ((val >> EIC_PB) & 1) ^ 1;
}
```

### Enabling EIC in PMIC Registers:
```c
static void eic_enable(void) {
    adi_write_or(0x406080c8, 0x10);  // Enable EIC clock gate
    adi_write_or(0x40608c08, 0x08);  // Power domain ungate
    adi_write_or(0x40608e40, 0x01);  // Unmask EIC power interrupt
    adi_write_or(0x40608c10, 0x08);  // Set debouncer filter enable
    eic_mask_or(1 << EIC_PB);         // Enable PB interrupt mask
}
```

---

## 7. MOCOR OS Keypad Driver Architecture (`T_KPDSVR`)

Inside stock MOCOR OS (`kernel.bin`):
1. **Interrupt Registration:** At boot, `kpd_phy.c:KPD_Init()` registers interrupt handler `KPD_INT` (`0x80cd421e`) on IRQ line 35.
2. **Task Queue:** When an interrupt occurs, the ISR packs the event into a message and posts it to the task queue `Q_KPDSVR`.
3. **Event Dispatch:** Task `T_KPDSVR` (`0x80b7602b`, priority `0x23`) unblocks, decodes long presses and repeats, and forwards `KPD_DOWN_MSG` / `KPD_UP_MSG` to the window manager task `T_P_APP` (`MS_MMI_Main`).

---

## 8. Bare-Metal Keypad Diagnostic Tool (`keypad_test`)

The custom RAM payload toolkit (`tools/ram_payload/src/keypad_test/keypad_test_main.c`) includes a complete bare-metal polling and visual diagnostic driver launched via [`tools/ram_payload/run_keypad_test.sh`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/tools/ram_payload/run_keypad_test.sh):

1. **Non-blocking Matrix Draining:** Calls `sys_event(&key)` in a loop each frame to drain all keypresses and releases from the hardware FIFO.
2. **Dedicated EIC Polling:** Inspects `read_power_button()` each frame with isolated `((eic_read() >> 1) & 1) ^ 1` bitmask to capture standalone Power/End keypresses and combo chords (`PWR + <key>`) without Key 8 interference.
3. **Real-Time Display:** Renders the key name, hex code, matrix column/row, total event counter, and recent event history directly on screen.
4. **Dual Display Support:** Works on both the inner main screen (`240x320`) and the outer cover screen (`128x160` via `./run_keypad_test.sh cover`).
5. **Clean Exit:** Holding the red Power / End key for ~1 second gracefully frees the framebuffer and returns execution cleanly.
