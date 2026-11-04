# Memory Map & Image Layout

Status: **Reverse Engineered & Validated** from TA-1480 hardware dumps and PAC XML scheme (`UMS9117.xml`).

---

## 1. Physical Memory Architecture

The Unisoc UMS9117 (Cortex-A7) integrates internal SRAM (IRAM), an LPDDR controller, and SPI NAND flash mapping:

| Memory Type | Physical Window | Typical Use |
|:---|:---|:---|
| **Internal BootROM / SRAM** | `0x00000000` – `0x00010000` | On-chip BootROM code & BootROM download address (`0x6200`) |
| **Internal SRAM (IRAM)** | `0x40000000` – `0x40020000` | `fdl1.bin` / `boot0.bin` execution window (~128 KB) |
| **External LPDDR DRAM** | `0x80000000` – `0x84000000` (64 MB) / `0x88000000` (128 MB) | Secondary bootloaders, RTOS kernel, application runtime |
| **Hardware Peripherals (APB/AHB)** | `0x50000000` – `0x7FFFFFFF` | Pin controller, GPIOs, LCD controller, UART, DMC, Timers |

---

## 2. Boot & Firmware Image Layout

All executable firmware images begin with the 1,204-byte Spreadtrum `DHTB` signed wrapper containing cryptographic certificates and an initial vector stub at offset `+0x200`.

| Image / Component | Partition ID / Base | DRAM / SRAM Load Address | Entry Point | Source File | Description |
|:---|:---:|:---:|:---:|:---|:---|
| **FDL1** | `0x6200` | `0x00006200` / `0x40000000` | `0x00006400` | `fdl1.bin` | USB BSL First Stage Loader (IRAM) |
| **FDL2** | `0x80100000` | `0x80100000` | `0x80100220` | `fdl2.bin` | USB BSL Second Stage Flasher (DRAM) |
| **BOOT0** | `0x80000001` | `0x40000000` (IRAM) | `0x40000220` | `boot0.bin` | First on-NAND stage; trains LPDDR |
| **BOOT1** | `0x80000002` | `0x80100000` (DRAM) | `0x80100220` | `boot1.bin` | Loads and cryptographically verifies kernel |
| **KERNEL** | `0x80000003` | **`0x80a05e00`** | **`0x80a06000`** (stub $\rightarrow$ `0x80a061f0`) | `kernel.bin` | MOCOR RTOS core, ThreadX scheduler, drivers |
| **USER_IMG** | `0x80000004` | **`0x88000000`** | **`0x88479f6f`** (`MS_MMI_Main`) | `user.bin` | Application framework, MMK GUI, native apps |
| **MMI_RES** | `0x90000004` | `0x90000004` (FS/ROM) | N/A (Resource bundle) | `mmi_res.bin` | UI theme assets, fonts, strings, bitmaps |
| **LTE_DSP** | `0x90000005` | Modem DSP RAM | Coprocessor Vector | `lte_dsp.bin` | LTE Cat-1 Modem DSP firmware |
| **GSM_DSP** | `0x90000006` | Modem DSP RAM | Coprocessor Vector | `gsm_dsp.bin` | 2G/GSM modem DSP firmware |
| **CM4_CODE** | `0x90000013` | CM4 IRAM | CM4 Reset Vector | `pm_cm4.bin` | Cortex-M4 Power Management MCU |

---

## 3. Kernel Execution Map (`kernel.bin`)

Base load address: **`0x80a05e00`** (Total size: ~12.3 MB)

| Address Range | Component / Subsystem | Execution Mode |
|:---|:---|:---:|
| `0x80a05e00` – `0x80a062b3` | DHTB Header (1,204 bytes) + Early bootstrap stub | ARM |
| `0x80a06000` | Header jump vector (`ldr pc, [pc]`) | ARM |
| `0x80a060b4` | Core mode stack initialization (FIQ, IRQ, ABT, UND, SYS, SVC) | ARM |
| `0x80a061f0` | CP15 bring-up (SCTLR low vectors, CP10/CP11 VFP/NEON enable) | ARM |
| `0x814c029c` | ARM C library `__scatterload` (BSS zeroing & data relocation) | Thumb-2 |
| `0x814eb0d4` | ARM C library `__rt_entry` | Thumb-2 |
| `0x80a140e0` | Kernel `main()` entry | Thumb-2 |
| `0x80a0e128` | RTOS scheduler entry (CPSR = `0x13`, Supervisor mode, IRQ enabled) | ARM |
| `0x80a0e144` | ThreadX / SCI preemptive idle & dispatch loop | ARM |
| `0x80b551e4` | System initialization main task (`init.c`) | Thumb-2 |
| `0x80b54b92` | Coprocessor firmware loader (`init.c`: GSM/LTE DSP & CM4) | Thumb-2 |
| `0x80b54e68` | Cryptographic verifier `verify_user_img` (`init.c`) | Thumb-2 |
| `0x80b654e8` | Foundational task table instantiation (`os_cfg.c`) | Thumb-2 |

---

## 4. Userland Application Map (`user.bin`)

Base load address: **`0x88000000`** (Total size: ~10.67 MB)

| Address Range | Component / Subsystem | Execution Mode |
|:---|:---|:---:|
| `0x88000000` – `0x88000493` | DHTB Header (1,172 bytes) | N/A |
| `0x88479f6e` | `MS_MMI_Main` in `mmimain.c` (`T_P_APP` entry point) | Thumb-2 |
| `0x880da4e0` | `MMI_GetPowerOnType` (Boot reason inspector) | Thumb-2 |
| `0x88000000` + offset | Native C Apps (Opera Mini, Snake X, Tetris, File Manager) | Thumb-2 |
| `0x88000000` + offset | MMK Window Manager & Event Dispatch Queue | Thumb-2 |

---

## 5. RTOS Static Task Table (`os_cfg.c`)

Instantiated during kernel bring-up using `SCI_CreateAppThread`:

| Task ID | Thread Name | Priority | Stack Size | Entry Address | Function |
|:---:|:---|:---:|:---:|:---:|:---|
| `0x00` | `T_CM` | `0xd7` (215) | 4 KB | `kernel.bin` | Communication Manager (modem multiplexing) |
| `0x01` | `T_DIAG` | `0xd8` (216) | 4 KB | `kernel.bin` | AT command diagnostics & serial PC protocol |
| `0x02` | `T_KPDSVR` | `0x63` (99) | 4 KB | `kernel.bin` | Hardware keypad scanning & debounce server |
| `0x04` | `T_AUDIO` | `0xc8` (200) | 8 KB | `kernel.bin` | Voice processing & hardware audio routing |
| `0x09` | `T_TIMESVR` | `0x44` (68) | 2 KB | `kernel.bin` | RTOS high-resolution software timers |
| `0x15` | `T_P_APP` | `0xe4` (228) | 50 KB | **`0x88479f6f`** | **MMI Application Layer (`MS_MMI_Main`)** |
