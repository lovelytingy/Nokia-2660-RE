# Hardware Architecture

Status: **Verified via hardware runtime diagnostics (`secure_boot_diag`), DRAM injection telemetry, and kernel binary analysis (`kernel.bin`)**.

Reference Unit: **Nokia 2660 Flip (TA-1480, India SKU, HW3.0)**.

---

## 1. System Specifications & Components

| Component | Identifier / Detail | Verification Source |
|-----------|---------------------|---------------------|
| **Marketing Name** | Nokia 2660 Flip | Product branding / `*#0000#` |
| **Model / SKU** | TA-1480 (Related regional: TA-1469, TA-1474, TA-1711) | `*#0000#` & PAC manifests |
| **Hardware Revision** | **HW3.0** | `*#0000#` & BootROM telemetry |
| **NDT Code** | `MH_13ARA80IN00_0` | `*#0000#` / NVRAM |
| **SoC / Chipset** | **Unisoc UMS9117 / T107** (ARM Cortex-A7 @ 1.0 GHz) | Chip ID register `0x98180001` |
| **Auxiliary Cores** | 1× ARM Cortex-M4 (Power Management), 2× Modem DSPs | `pm_cm4.bin`, `lte_dsp.bin`, `gsm_dsp.bin` |
| **Internal RAM** | 48 MB LPDDR (PoP package on SoC) | Heap size: 66,025,536 bytes (~64 MB space) |
| **Flash Storage** | 128 MB NAND flash | Partition map & `boot0.bin` NAND driver |
| **PMIC** | **Unisoc SC2720** Power Management IC | `kernel.bin` driver symbols |
| **Audio Subsystem** | Spreadtrum VBC (Voice Band Codec) + SPRD_CODEC | `kernel.bin` audio HAL & task registry |
| **Battery Model** | BP-4L (1450 mAh / 3.7V removable Li-ion) | Physical label & battery ADC |

---

## 2. Display Subsystem (Dual Screens)

The Nokia 2660 Flip features two separate LCD displays driven over SPI interfaces:

```
              +---------------------------------------+
              |           Unisoc UMS9117 SoC          |
              +-------------------+-------------------+
                                  |
                   SPI1 Bus       |        SPI Bus
             +--------------------+--------------------+
             |                                         |
             v                                         v
   +-------------------+                     +-------------------+
   |   Main LCD (2.8") |                     |  Cover LCD (1.77")|
   | GalaxyCore GC9307 |                     | GC9107 / ST7735SV |
   |     240 × 320     |                     |     128 × 160     |
   | Read ID: 0x009307 |                     |   Cover Display   |
   +-------------------+                     +-------------------+
```

### Main Display (Internal 2.8" QVGA)
- **Controller**: GalaxyCore **`GC9307`** (240 × 320 pixels)
- **Interface**: SPI1 (SPI bus 1)
- **Telemetry Readout**: Device ID `0x009307`, pin configuration `0x30`
- **GPIO / Backlight Control**: Control pins `-0x28, 0x5f`, Backlight `rgb=0x19`
- **Kernel Drivers**: Exported in `kernel.bin`:
  - `GC9307_Init`
  - `GC9307_ReadID`
  - `GC9307_mainlcd_id`
  - `GC9307_set_display_window`
  - `GC9307_EnterSleep`
  - `GC9307_Close`
  - `GC9307_ESD_driver`

### Sub Display (External Cover 1.77" QQVGA)
- **Controller Support**: Multi-driver fallback implemented in `kernel.bin`:
  - GalaxyCore **`GC9107`** (`GC9107_Init`, `GC9107_sublcd_id`, `GC9107_ReadID`, `GC9107_Close`)
  - GalaxyCore **`GC9104`** (`GC9104_Init`, `GC9104_sublcd_id`, `GC9104_ReadID`, `GC9104_Close`)
  - Sitronix **`ST7735SV`** (`ST7735SV_Init`, `ST7735SV_EnterSleep`, `ST7735SV_Close`)
- **Resolution**: 128 × 160 pixels
- **GPIO / Backlight Control**: Control pins `0x28, -0x5f`, Backlight `w=0x2d`

---

## 3. Power Management Subsystem

The device offloads low-power sleep management, real-time clock (RTC), battery charging, and thermal policies to a dedicated **ARM Cortex-M4** core:
- **Firmware Image**: `pm_cm4.bin` / `PM_nemol_cm4.bin` (55 KB – 131 KB)
- **OS Release**: `MOCOR_TRUNK_17A_RLS1_W21.38.6`
- **PMIC Companion**: Unisoc `SC2720` communicating via ADI (Analog Die Interconnect) bus
- **Thermal & Charge Services**: `CHGMNG_SRV`, `THERMAL_SRV`

---

## 4. Hardware Security & Secure Boot Gate Architecture

Hardware Secure Boot status on physical TA-1480 HW3.0 units:

```
[Power On / Reset]
       |
       v
+-------------------------------+
| Gate 1: AON Security Register | ----(Bit 0 == 0)----> [BYPASSED]
|         0x402e0568            |
+-------------------------------+
       | (Bit 0 == 1: ENABLED)
       v
+-------------------------------+
| Gate 2: eFuse Row 0 Lock Mask | ----(Bits 6..21 != 0xFFFF)----> [BYPASSED]
|         Bits 6..21            |
+-------------------------------+
       | (Bits 6..21 == 0xFFFF: ENFORCED)
       v
+-------------------------------------------------------------+
| BootROM & boot0 enforce RSA-2048 against Root Key Hash     |
| eFuse Rows 6..10: SHA-256 Digest of Nokia Vendor Root Key  |
+-------------------------------------------------------------+
```

1. **AON Register Gate 1 (`0x402e0568`)**:
   - Telemetry read: `0xF0000101` (Bit 0 = 1: Hardware Secure Boot Enabled).
2. **eFuse Row 0 Lock Mask Gate 2 (`0x003FFFC0`)**:
   - `(0x003FFFC0 >> 6) & 0xFFFF == 0xFFFF` (Lock mask bits fully blown).
3. **eFuse Vendor Root Key Hash (Rows 6–10)**:
   - Contains SHA-256 fingerprint of the Nokia/HMD Root Vendor Key (`31004171...`).
   - Validates `boot0.bin` and `boot1.bin` RSA-2048 signatures during NAND boot.
   - Volatile execution over USB (FDL1 at `0x40000000` / `0x80100000` DRAM) remains completely unrestricted for custom payloads.

---

## 5. Serial Debug & Hardware Console (UART1)

* **Controller**: Unisoc `sprd_serial` (UART1 at `0x70100000`, 115200 8N1).
* **Clock Source**: 26 MHz (`26M_XTL`), APB clock enable via `0x71300000 |= 0x6000`.
* **Hardware Boot Keys**:
  * Hold **Key `7`** at power-on $\to$ Enables **UART Debug Mode** (PIN_CTL `0x402a0018 |= 0x40000000`).
  * Hold **Key `3`** at power-on $\to$ Enables **JTAG Debug Mode** (PIN_CTL `0x402a0018 |= 0xE0000000`).
* **Logic Voltage**: **1.8V TTL** (requires 1.8V level shifter; 3.3V/5V will damage SoC).

See [`docs/uart-serial-interface.md`](uart-serial-interface.md) for complete register maps, baud rate formulas, and C initialization code.

---

## 6. Needed from Contributors

- Motherboard PCB high-resolution photos (front and back shielding removed)
- Exact flash memory IC part number (e.g. Winbond / Macronix / GigaDevice NAND)
- Dedicated UART / JTAG hardware test point mapping on the PCB
