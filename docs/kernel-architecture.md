# MOCOR RTOS Kernel Architecture (`kernel.bin`)

## 1. Overview & Execution Role

The **`kernel.bin`** binary (12,945,748 bytes / 12.3 MB) is the core monolithic operating system image for the Nokia 2660 Flip. Loaded into DDR RAM by the second-stage bootloader (`boot1.bin`), it contains:

1. **Hardware Initialization & Memory Management**: ARM Cortex-A7 MMU page tables, cache control, APB/AHB bus clocks, and DDR memory allocation.
2. **ThreadX RTOS Scheduler**: Spreadtrum SCI (Software Component Interface) RTOS environment managing multi-task scheduling, thread priority queues, mutexes, semaphores, and inter-task event queues.
3. **Peripheral Drivers**: Hardware drivers for dual SPI displays (GC9307, GC9104, GC9107, ST7735SV), SC2720 PMIC, keypad matrix scanner, VBC audio DMA, and hardware timers.
4. **Cellular Protocol Stack**: 2G/4G Non-Access Stratum (NAS), Radio Resource (RR), VoLTE IMS stack, and TCP/IP networking engine.
5. **Coprocessor Subsystem Loaders**: Firmwares for the Cortex-M4 power management core (`pm_cm4.bin`) and dual CEVA baseband DSPs (`gsm_dsp.bin`, `lte_dsp.bin`).

---

## 2. Binary Identification & Layout

| Attribute | Specification |
|:---|:---|
| **Binary Partition** | `kernel.bin` / `KERNEL` |
| **Partition ID** | `0x80000003` |
| **Raw Image Size** | 12,945,748 bytes |
| **Memory Range** | `0x80a05e00` – `0x8165e753` |
| **Verified Base Address** | **`0x80a05e00`** |
| **Header Format** | Unisoc DHTB Header v1 (512-byte header + RSA-2048 certificate) |
| **Reset Vector Table** | `0x80a06000` (file offset `0x200`) |
| **Reset Entry Point** | **`0x80a061f0`** (file offset `0x3f0`) |
| **Processor Architecture** | `ARM:LE:32:v7` (ARMv7-A Cortex-A7 with VFPv4/NEON) |
| **Build Banner** | `MOCOR_20A_MP_W22.04.6_P4_Release` |
| **Software Version** | `10.02.17.00` |
| **Decompiled Source** | [`ghidra/exports/kernel.bin_decompiled.c`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/ghidra/exports/kernel.bin_decompiled.c) |
| **Function Index** | [`ghidra/exports/kernel.bin_notes.txt`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/ghidra/exports/kernel.bin_notes.txt) |

---

## 3. Hardware Initialization & Reset Flow

At boot, execution jumps to the hardware entry point at **`0x80a061f0`**:

```armasm
/* Entry Point @ 0x80a061f0 */
mrc  p15, 0, r0, c1, c0, 0    ; Read CP15 System Control Register (SCTLR)
bic  r0, r0, #0x2000          ; Clear bit 13 (V bit) -> Map exception vectors to low address
mcr  p15, 0, r0, c1, c0, 0    ; Write back to SCTLR
ldr  sp, =0x81650000          ; Initialize primary kernel boot stack in DDR
bl   kernel_early_init        ; Configure clock gates, MMU translation tables, and SCI heap
```

1. **CP15 Control**: Clears the `V` bit to remap exception vectors to `0x80a06000`.
2. **MMU Translation**: Establishes identity-mapped L1 page tables (`0x80000000` DDR cached, `0x40000000` APB peripherals non-cached Device memory).
3. **Coprocessor Extraction**: Invokes `coprocessor_loader` (`init.c`) to unpack and copy `pm_cm4.bin` to IRAM (`0x00000000`) and the DSP images to `0xE0410000` and `0xE0490000`.
4. **RTOS Start**: Spawns the SCI system initialization thread (`T_SYS_INIT`), which launches device servers and transitions to userspace (`user.bin`).

---

## 4. Kernel Task Registry & Priorities

The kernel organizes multitasking into distinct ThreadX / SCI threads:

| Task Name | Queue Name | Type | Description |
|:---|:---|:---:|:---|
| `ASM_TASK` | `Q_ASM_TASK` | Power | Advanced Sleep Management / CPU clock gating |
| `AUDIO_HAL_TASK` | `Q_AUDIO_HAL_TASK` | Real-time | Low-level VBC audio DMA ringbuffer service |
| `CHGMNG_SRV` | `Q_CHGMNG_SRV` | Server | SC2720 PMIC battery charge controller & thermal monitor |
| `CONTROL_TASK` | — | System | Master power state machine (sleep/reboot/shutdown) |
| `DATA_TASK` | — | Network | Cellular data packet dispatch & GPRS/LTE buffers |
| `NAS_CONTROL_TASK`| — | Network | 3GPP Non-Access Stratum protocol stack |
| `SCM_TASK` | — | Driver | Smart Card / SIM card interface controller |
| `SPRD_CODEC_TASK` | `Q_SPRD_CODEC_TASK`| Audio | Software DSP vocoder & media codec decoder |
| `THERMAL_SRV` | `Q_THERMAL_SRV` | Server | Thermal ADC sensor monitoring & throttling |
| `WIQ_CAPTURE_TASK`| — | Diagnostic | Wireless IQ sample streaming for factory calibration |

---

## 5. Hardware Peripheral Drivers in Kernel

The kernel binary contains native hardware drivers for all board peripherals:

1. **Display Controllers**:
   - `GC9307`: Main 2.8" QVGA screen (`GC9307_Init`, `GC9307_EnterSleep`, `GC9307_Close`).
   - `GC9104` / `GC9107` / `ST7735SV`: Cover 1.77" QQVGA screen (`GC9104_Init`, `GC9104_sublcd_id`).
2. **Audio Subsystem**:
   - Spreadtrum VBC Audio Engine with memory-mapped DMA channels.
   - GPIO 31 external power amplifier (PA) gate control.
3. **Serial & Modem Interfaces**:
   - Multi-channel UART controller (`0x70100000`), APB clock gate `0x71300000`.
   - AT command parser supporting 560+ 3GPP and proprietary Unisoc diagnostic commands (`+SPADC`, `+SPAGPS`, `+SPAUDIOCONFIG`).
4. **PMIC Bus Controller (ADI)**:
   - Synchronous serial driver communicating with SC2720 PMIC at `0xA1600000`.
