# Cortex-M4 Power Management Subsystem (`pm_cm4.bin`)

## 1. Overview & Architectural Role

The **Unisoc UMS9117 (T117)** SoC contains a dedicated **ARM Cortex-M4 (ARMv7E-M)** auxiliary microcontroller core alongside the primary Cortex-A7 application processor and dual CEVA baseband DSPs. The Cortex-M4 core operates as an independent, ultra-low-power coprocessor responsible for:

1. **System Power Management**: Orchestrating deep sleep states, clock gating, and power domain collapse.
2. **DRAM Retention (`dmc_retention.c`)**: Keeping LPDDR memory in autonomous self-refresh while the Cortex-A7 core is powered down.
3. **PMIC Bus Control (`adi_phy.c`)**: Managing the **SC2720 PMIC** over the proprietary Analog Die Interface (ADI) serial bus.
4. **Inter-Core Messaging (`smsg_ch.c`)**: Providing low-latency shared-memory message channels between the main MOCOR RTOS kernel and low-power hardware state machines.
5. **Wakeup Event Handling (`eic_hal.c` / `gpio_hal.c`)**: Monitoring RTC alarms, flip hall-effect sensors, and keypad events to trigger cold or warm resume sequences.

```
       +-------------------------------------------------------+
       |             Unisoc UMS9117 SoC Boundary               |
       |                                                       |
       |   +-----------------------+   +-------------------+   |
       |   | Primary Cortex-A7 CPU |   |  CEVA Modem DSPs  |   |
       |   |  (MOCOR ThreadX RTOS) |   | (GSM/TD/LTE Cat1) |   |
       |   +-----------+-----------+   +---------+---------+   |
       |               |                         |             |
       |     Hardware Mailbox / Shared Ring Buffers (DDR)      |
       |               |                         |             |
       |   +-----------+-------------------------+---------+   |
       |   |             ARM Cortex-M4 (PM Core)           |   |
       |   |          Firmware: pm_cm4.bin (54.2 KB)       |   |
       |   |            RTOS: ThreadX Lite RTOS            |   |
       |   +-----------+-----------------------+-----------+   |
       |               |                       |               |
       |          ADI Controller          EIC / GPIO           |
       |           (0xA1600000)        (Wakeup Matrix)         |
       +---------------+-----------------------+---------------+
                       |                       |
               +-------v-------+       +-------v-------+
               |  Unisoc SC2720|       | Hall Sensor / |
               |     PMIC      |       |  Keypad / RTC |
               +---------------+       +---------------+
```

---

## 2. Firmware Identification & Properties

| Attribute | Specification |
|:---|:---|
| **Binary Partition** | `pm_cm4.bin` / `PM_nemol_cm4.bin` |
| **Partition ID** | `0x90000013` (`CM4CODE`) / Alt: `0x90000018` (`CM4CODE_AB`) |
| **Payload Size** | 55,498 bytes (0x0D8CA active payload; padded to 131,072 bytes / 128 KB) |
| **Core Architecture** | ARMv7E-M (Cortex-M4, Thumb-2, Little-Endian) |
| **Operating System** | Express Logic ThreadX Lite (`RTOS/source/src/c/threadx_os_lite.c`) |
| **Software Trunk** | `MOCOR_TRUNK_17A_RLS1_W21.38.6` |
| **Project Target** | `nemol_cm4` |
| **Build Timestamp** | `09-25-2021 22:42:21` |
| **Decompiled Source** | [`ghidra/exports/pm_cm4.bin_decompiled.c`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/ghidra/exports/pm_cm4.bin_decompiled.c) |

---

## 3. Physical Memory Map & Memory Protection (MPU)

The Cortex-M4 core executes from a dedicated on-chip 128 KB internal SRAM (IRAM) mapped at address `0x00000000`:

| Address Range | Size | Region Name | Description |
|:---|:---:|:---|:---|
| `0x00000000 - 0x0001FFFF` | 128 KB | **CM4 IRAM** | Code (`.text`), vectors, literal pools, and constants |
| `0x20000000 - 0x2000FFFF` | 64 KB | **CM4 SRAM** | ThreadX OS stacks, kernel heaps, static variables |
| `0x40000000 - 0x7FFFFFFF` | 1 GB | **SoC APB/AHB** | Peripheral bus (timers, mailbox, spinlocks, GPIO) |
| `0x80000000 - 0x8FFFFFFF` | 256 MB | **Shared DDR** | Inter-core IPC ringbuffers and logging queues |
| `0xA1600000 - 0xA160FFFF` | 64 KB | **ADI Controller** | Analog Die Interface hardware controller |
| `0xA1610000 - 0xA161FFFF` | 64 KB | **Analog Die Space** | Direct window to SC2720 PMIC internal registers |
| `0xE0000000 - 0xE00FFFFF` | 1 MB | **Cortex-M PPB** | System Control Space (NVIC, SysTick, MPU `0xE000ED90`) |

---

## 4. Analog Die Interface (ADI) & SC2720 PMIC Control

The Cortex-M4 core communicates with the SC2720 PMIC over a synchronous serial link managed by the hardware ADI bus controller. The driver implementation in `adi_phy.c` provides two low-level primitives:

### ADI Read Operation (`FUN_000086bc`)
```c
uint32_t ANA_Read(uint32_t addr)
{
    // 1. Verify target address is within Analog Die address range (0xA1610000 - 0xA1611000)
    if (addr + 0x5e9f0000 > 0x1000) {
        cm4_assert("ADI_IS_Analogdie_reg_addr", "adi_phy.c", 103);
    }

    // 2. Write address to ADI Read Command Register (0xA1600238)
    *(volatile uint32_t *)(0xA1600238) = addr;

    // 3. Poll ADI Read Data Register (0xA160023C) until busy bit 31 clears
    uint32_t val;
    do {
        val = *(volatile uint32_t *)(0xA160023C);
    } while (val & 0x80000000);

    // 4. Validate that returned address tag matches requested register address
    if (((addr & 0x1FFFF) >> 2) != (val >> 16)) {
        cm4_assert("ANA_Read tag mismatch", "adi_phy.c", 58);
    }

    // 5. Return 16-bit PMIC register contents
    return val & 0xFFFF;
}
```

### ADI Write Operation (`FUN_00008736`)
```c
int32_t ANA_Write(uint32_t *addr, uint32_t val)
{
    // 1. Verify target address range
    if ((uint32_t)addr + 0x5e9f0000 > 0x1000) {
        cm4_assert("ADI_IS_Analogdie_reg_addr", "adi_phy.c", 122);
    }

    // 2. Poll ADI Write FIFO status (0xA1600240) until write buffer is ready
    int timeout = 1000;
    while (*(volatile uint32_t *)(0xA1600240) & 1) {
        if (--timeout == 0) return -4; // Timeout error
    }

    // 3. Directly store value through memory-mapped analog window
    *addr = val;
    return 0; // Success
}
```

---

## 5. Inter-Processor Communication (IPC / SIPC)

Communication between the Cortex-A7 (running MOCOR RTOS) and the Cortex-M4 coprocessor is orchestrated using **Hardware Mailboxes** and **Shared Memory Queues** (`sipc_c`, `smsg_ch.c`):

1. **Hardware Mailbox Interrupts**: Located at APB base address `0x40000000` / `0x70000000`. Writing to the mailbox register triggers an Inter-Processor Interrupt (IPI) on the peer core.
2. **Channel Multiplexing (`smsg_ch.c`)**: Supports up to **24 distinct IPC channels** (`param_2 < 0x18`).
3. **Channel Handshake Protocol**:
   - Connection open packet: Magic `0xBEEE` + Channel Number (`u8`) + Direction Flag (`u8`).
   - Channel states: `SMSG_CH_CLOSED`, `SMSG_CH_OPENING`, `SMSG_CH_OPENED` (`0x5F`).
   - Synchronization primitives: ThreadX Mutexes (`s_ipc_channel_mutex`) and Event Flags (`s_ipc_event`).

### Active IPC Channel Assignments

| Channel ID | Subsystem Name | Direction | Description |
|:---:|:---|:---:|:---|
| `0x00` | `CH_SYS_CONTROL` | Bidirectional | Sleep request / Deep sleep handshake |
| `0x01` | `CH_PMIC_NOTIFY` | M4 → A7 | Battery low, charger plug/unplug, thermal alerts |
| `0x02` | `CH_RTC_ALARM` | M4 → A7 | Real-time clock alarm wakeup event |
| `0x03` | `CH_KEYPAD_EIC` | M4 → A7 | Power key long-press, flip open/closed state |
| `0x04` | `CH_AUDIO_PM` | Bidirectional | Audio codec power domain gating |
| `0x05` | `CH_DMC_CTRL` | A7 → M4 | LPDDR retention configuration and self-refresh timings |
| `0x06` | `CH_LOG_RELAY` | M4 → A7 | Relaying CM4 debug logs to AP serial logging daemon |

---

## 6. Deep Sleep & LPDDR Self-Refresh Retention (`dmc_retention.c`)

When the main Cortex-A7 system enters deep sleep, it hands over power management control to the Cortex-M4 via `LATCH_ALL_DEEP_SLEEP`:

1. **Bus Freeze**: Cortex-M4 executes `dmc_retention.c` to assert memory controller clock gating and lock the DDR controller state machine.
2. **LPDDR Self-Refresh**: The memory controller issues standard JEDEC auto-self-refresh commands to the embedded 48 MB / 64 MB LPDDR2/3 die.
3. **Power Rail Latching**: PMIC buck regulators for VDD_ARM (Cortex-A7) and VDD_MODEM (DSP) are disabled, while VDD_MEM and VDD_CORE (Cortex-M4) remain powered.
4. **Wakeup Assertion**: Upon an external interrupt event (Flip hall sensor `EIC_FLIP_DETECT` or Power key `EIC_KEY_DETECT`), the Cortex-M4 brings up the DDR clocks, exits self-refresh, restores DDR controller PHY registers, and asserts the Cortex-A7 warm reset entry pointer.

---

## 7. Crash Register Dumper & Fault Diagnostics

The firmware embeds a comprehensive exception handling vector table and register dump routine at file offset `0x0000BCC0` – `0x0000BE50`:

* **Core Registers**: `r0`, `r1`, `r2`, `r3`, `r12`, `lr`, `pc`, `xpsr`
* **Special Registers**: Stack Pointer (`sp`), Main Stack Pointer (`msp`), Process Stack Pointer (`psp`), Exception Return (`exe_return`)
* **MPU Registers**: `mpu_type`, `mpu_control`, `mpu_region`, `mpu_regbaseaddr`, `mpu_regattrsize`
* **Cortex-M Fault Status**:
  - Memory Management Fault Address (`mpu_mmar` / `0xE000ED34`)
  - Bus Fault Address (`bus_bfar` / `0xE000ED38`)
  - Configurable Fault Status (`fault_cfsr` / `0xE000ED28`)
  - Hard Fault Status (`fault_hfsr` / `0xE000ED2C`)
  - Interrupt Control and Status (`ICSR` / `0xE000ED04`)

When an assertion or fault occurs, register telemetry is written to a dedicated DDR ringbuffer (`g_data_buffer_arr.cm4_log_buffer_arr`), allowing the AP kernel to log the crash to `E:\Logsave\cm4log_*.log` or emit it over UART.
