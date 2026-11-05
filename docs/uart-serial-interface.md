# UART Serial Interface & Debug Console

Status: **Reverse Engineered & Validated from Bootloader Disassembly (`boot0.bin`, `fdl1.bin`, `kernel.bin`) and Linux `sprd_serial` Architecture**.

---

## 1. Architectural Overview

The **Unisoc UMS9117 / T107** SoC incorporates three independent hardware Universal Asynchronous Receiver-Transmitter (UART) controller blocks (`sprd_serial` architecture). 

The primary hardware serial console is **UART1**, used by `boot0.bin`, `boot1.bin`, and `kernel.bin` as the foundational **SIO Debug Port**:

```
+-----------------------------------------------------------------------------+
|                          Unisoc UMS9117 SoC                                 |
+-----------------------------------------------------------------------------+
|                                                                             |
|   +-----------------------+     +-----------------------+                   |
|   |   BootROM / boot0     |     |  MOCOR RTOS Kernel    |                   |
|   |  Early Diagnostic Log |     |  SIO Debug Port Task  |                   |
|   +-----------+-----------+     +-----------+-----------+                   |
|               |                             |                               |
|               v                             v                               |
|   +-----------------------------------------------------+                   |
|   |                 UART1 Hardware Controller           |                   |
|   |                 Base Address: 0x70100000            |                   |
|   |                 Clock: 26 MHz (26M_XTL)             |                   |
|   |                 Baud: 115200 8N1                    |                   |
|   +--------------------------+--------------------------+                   |
|                              |                                              |
|                              v                                              |
|   +-----------------------------------------------------+                   |
|   |         Pin Controller / IOMUX (0x402a0000)         |                   |
|   |  - Mode 0 (Key '7' held): 0x402a0018 |= 0x40000000  |                   |
|   |  - Mode 1 (Key '3' held): 0x402a0018 |= 0xE0000000  |                   |
|   +--------------------------+--------------------------+                   |
|                              |                                              |
+------------------------------|----------------------------------------------+
                               |
                               v
                     [1.8V TTL Serial Lines]
                  (PCB Test Points / USB MUX)
```

---

## 2. Hardware Register Map (`0x70100000`)

UART1 registers are memory-mapped on the internal APB peripheral bus at **`0x70100000`**:

| Offset | Name | Type | Reset | Description |
|:---:|:---|:---:|:---:|:---|
| `0x00` | `TXD` | WO | `0x00000000` | **Transmit Data FIFO Register:** Writing an 8-bit character pushes it into the hardware TX FIFO queue. |
| `0x04` | `RXD` | RO | `0x00000000` | **Receive Data FIFO Register:** Reading fetches the next received byte from the hardware RX FIFO. |
| `0x0C` | `STS1` | RO | `0x00000000` | **FIFO Status Register 1:**<br>• Bits [15:8]: `TX_FIFO_CNT` (Current byte count in TX FIFO; non-zero = busy/full)<br>• Bits [7:0]: `RX_FIFO_CNT` (Current byte count in RX FIFO) |
| `0x10` | `IEN` | R/W | `0x00000000` | **Interrupt Enable Mask:** Set to `0` for polled mode transmission. |
| `0x14` | `ICLR` | WO | `0x00000000` | **Interrupt Clear Register:** Acknowledges and clears hardware interrupt flags. |
| `0x18` | `LCR` | R/W | `0x00000000` | **Line Control Register:** Configures framing.<br>• Default: `0x1C` (28 decimal) $\to$ **8 data bits, no parity, 1 stop bit (8N1)**. |
| `0x1C` | `STS0` | RO | `0x00000000` | **Line Status Register 0:** Break detection, framing errors, parity errors. |
| `0x20` | `FCR` | R/W | `0x00000000` | **FIFO Control & Hardware Flow Control:** Configures CTS/RTS threshold. |
| `0x24` | `CLKD0` | R/W | `0x000000E2` | **Baud Rate Clock Divisor Low 16 bits:** Configures integer baud rate divisor. |
| `0x28` | `CLKD1` | R/W | `0x00000000` | **Baud Rate Clock Divisor High 16 bits:** Fraction / upper bits of divisor. |

---

## 3. Clock Source & Baud Rate Calculation

* **Clock Source:** 26 MHz primary crystal oscillator (**`26M_XTL`**, `26,000,000 Hz`).
* **APB Bus Clock Gating:** Controlled by register **`0x71300000`** (bits 13 & 14):
  ```c
  *(volatile uint32_t*)0x71300000 |= 0x6000; // Ungates UART1 APB clock
  ```
* **Baud Rate Formula:**
  $$\text{Divisor} = \frac{\text{Clock Frequency} + (\text{Baud} / 2)}{\text{Baud}}$$

For **115,200 bps** at 26 MHz:
$$\text{Divisor} = \frac{26000000 + 57600}{115200} = \frac{26057600}{115200} \approx 226 \quad (\text{0x00E2})$$

Configuration in `boot0.bin` (`FUN_00006ef0`) and bare-metal payload code:
```c
#define UART1_BASE 0x70100000

void uart_init(uint32_t baud) {
    // 1. Disable interrupts
    *(volatile uint32_t*)(UART1_BASE + 0x10) = 0;
    
    // 2. Ungate APB clock
    *(volatile uint32_t*)0x71300000 |= 0x6000;
    
    // 3. Compute and set baud divisor
    uint32_t div = (26000000 + (baud >> 1)) / baud;
    *(volatile uint32_t*)(UART1_BASE + 0x24) = div & 0xFFFF; // CLKD0 = 0x00E2
    *(volatile uint32_t*)(UART1_BASE + 0x28) = div >> 16;   // CLKD1 = 0x0000
    
    // 4. Line control: 8 data bits, 1 stop bit, no parity (8N1)
    *(volatile uint32_t*)(UART1_BASE + 0x18) = 0x1C; // 28 decimal
    *(volatile uint32_t*)(UART1_BASE + 0x1C) = 0;
    *(volatile uint32_t*)(UART1_BASE + 0x20) = 0;
}

void uart_putc(char c) {
    // Wait while TX FIFO is full (bits [15:8] of STS1 non-zero)
    while (*(volatile uint32_t*)(UART1_BASE + 0x0C) & 0xFF00);
    // Write byte to TXD
    *(volatile uint32_t*)(UART1_BASE + 0x00) = (uint8_t)c;
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}
```

---

## 4. Hardware Boot Key Multiplexing (UART / JTAG Modes)

`boot0.bin` inspects the physical keypad matrix at power-on to determine whether to enable external debug multiplexing (`FUN_0000745a` & `FUN_000074a6`):

```c
// Decompiled from boot0.bin:
int mode = kpd_sample_boot_keys();
printf("boot0 UART/JTAG mode = %d\n", mode);

if (mode != 2) {
    // Mode 0 (Key '7'): Route UART1 to external pads
    *(volatile uint32_t*)0x402a0018 |= 0x40000000; // Bit 30
    if (mode == 1) {
        // Mode 1 (Key '3'): Route ARM Cortex-A7 JTAG to external pads
        *(volatile uint32_t*)0x402a0018 |= 0x80000000; // Bit 31
        *(volatile uint32_t*)0x402a0018 |= 0x20000000; // Bit 29
    }
}
```

### Boot Key Selection Table:

| Key Held at Power-On | Matrix Coordinate | Mode | Result / Pinmux State |
|:---:|:---:|:---:|:---|
| **Key `7`** | Col 0, Row 1 (`0x10`) | **Mode 0** | **UART Debug Mode:** `0x402a0018 |= 0x40000000` (Bit 30). UART1 TX/RX enabled on external pins. |
| **Key `3`** | Col 1, Row 1 (`0x11`) | **Mode 1** | **JTAG Debug Mode:** `0x402a0018 |= 0xE0000000` (Bits 31, 30, 29). Cortex-A7 JTAG enabled. |
| *(None)* | — | **Mode 2** | **Retail Mode:** Normal pinmux, debug pins disabled. |
| **Key `*`** | Col 0, Row 0 (`0x00`) | BootROM BSL | **USB Download Mode:** Forces BootROM BSL protocol (`1782:4D00`). |

---

## 5. Bootloader UART Output Log Stream

When UART1 is active at 115200 baud, `boot0.bin` transmits the complete hardware bring-up sequence:

```text
boot0 started!: 0x40000220
boot0 UART/JTAG  mode = 0
boot0 nand flash ID 0xC8 0xD2!
sdram_clk_init_begin
DDR_dpll_clk_get_begin
dmc_dll_init_end
lpddr_powerup_init_end
dmc_init_post_setting_end
qos_init_end
load boot1 suc
start verify boot1: 0x80100000
verify boot1 suc: 0x80100000
run boot1: 0x80100220
```

If a hardware or verification error occurs:
- `verify boot1 fail: %x` (Signature / hash mismatch)
- `antiroll_back error` (eFuse counter mismatch)
- `key cert hash verify fail` (Root public key mismatch)
- `boot0 read Nand ID fail!` (NAND flash communication failure)
- `dmc_lpddr3_wde_training Failed` (LPDDR RAM timing training failure)
- `adi reg read timeout!` (SC2720 PMIC bus timeout)

---

## 6. Physical Interfacing & Voltage Levels

> [!WARNING]
> The Unisoc UMS9117 SoC uses **1.8V TTL logic levels** for all GPIO and UART lines. Connecting a standard 3.3V or 5V FTDI / USB-to-UART adapter directly to the phone pads will destroy the SoC I/O pins. Always use a **1.8V-capable USB-UART adapter** (such as an FTDI FT232H configured for 1.8V VCCIO or a bi-directional logic level converter).

### Physical Connection Methods:
1. **Motherboard Test Points (TP):** Unshielded gold circular pads on the PCB near the SoC shield. Look for test points adjacent to the PMIC/SoC labeled `TX`, `RX`, or test pads that show 1.8V idle HIGH state.
2. **USB D+/D- Multiplexer Mode:** The MOCOR kernel features an internal USB/UART hardware switch (`MUX_Create: Open UART/USB success`) allowing the physical micro-USB D+ and D- lines to carry raw serial UART traffic.
3. **Flashing Over Serial:** `spd_dump` supports direct flashing over serial UART via the `--tty` argument:
   ```bash
   ./tools/spd_dump --tty /dev/cu.usbserial-XXXX checkbaud 115200 fdl tools/ram_payload/data/fdl1.bin 0x40000000
   ```
