# Baseband Modem DSP Subsystems (`gsm_dsp.bin` & `lte_dsp.bin`)

## 1. Overview & Architecture

The **Unisoc UMS9117 (T117)** SoC incorporates two dedicated **CEVA Digital Signal Processor (DSP)** cores to handle high-throughput physical layer (PHY) digital baseband modulation, channel estimation, vocoding, and radio protocol processing:

1. **`gsm_dsp.bin` (XDSP0)**: Dedicated to 2G GSM, GPRS, EDGE, and TD-SCDMA modem layer 1 processing and legacy speech vocoders.
2. **`lte_dsp.bin` (XDSP1)**: Dedicated to 3GPP LTE Release 13 Cat-1 (10 Mbps DL / 5 Mbps UL) signal processing, OFDMA/SC-FDMA modulation, and VoLTE/EVS voice codecs.

```
       +---------------------------------------------------------------+
       |                   Unisoc UMS9117 Baseband Core                |
       |                                                               |
       |   +-----------------------+       +-----------------------+   |
       |   |      XDSP0 Core       |       |      XDSP1 Core       |   |
       |   |    (CEVA-TeakLite)    |       |       (CEVA-XC)       |   |
       |   |   2G / TD-SCDMA PHY   |       |    LTE Cat-1 PHY      |   |
       |   |  Firmware: gsm_dsp.bin|       |  Firmware: lte_dsp.bin|   |
       |   +-----------+-----------+       +-----------+-----------+   |
       |               |                               |               |
       |       Memory Mapped RAM               Memory Mapped RAM       |
       |     0xE0410000 - 0xE0448000         0xE0490000 - 0xE04C8000   |
       |               |                               |               |
       |               +---------------+---------------+               |
       |                               |                               |
       |                  Hardware Mailbox & Shared IPC                |
       |                               |                               |
       |                   +-----------v-----------+                   |
       |                   | Primary ARM Cortex-A7 |                   |
       |                   |  MOCOR RTOS Protocol  |                   |
       |                   +-----------------------+                   |
       +---------------------------------------------------------------+
```

---

## 2. Firmware Partition Specifications

| Attribute | GSM / 2G DSP (`gsm_dsp.bin`) | LTE Cat-1 DSP (`lte_dsp.bin`) |
|:---|:---|:---|
| **Partition ID** | `0x90000006` (`GSM_DSP`) | `0x90000005` (`LTE_DSP`) |
| **Image Size** | 3,145,728 bytes (3.0 MB) | 3,670,016 bytes (3.5 MB) |
| **DSP Core Architecture**| CEVA-TeakLite / CEVA-X1643 | CEVA-XC / CEVA-X Vector DSP |
| **Header Signature** | `V1GGE_Bugid__990513_` / `versionTD1.0001` | `0x005A5A5A` (`.ZZZ`) / `T117_CAT1_LTEA_DSP` |
| **Physical Standards** | 2G GSM, GPRS, EDGE, TD-SCDMA | 3GPP Rel 13 LTE Cat-1 (FDD/TDD) |
| **Peak Throughput** | 236.8 kbps (EDGE) | 10.3 Mbps DL / 5.2 Mbps UL |
| **Audio Vocoders** | FR, HR, EFR, AMR-NB | AMR-WB, EVS (Enhanced Voice Services) |

---

## 3. Hardware Memory Mapping & Data Windows

The primary Cortex-A7 processor interacts with the DSPs via memory-mapped coprocessor SRAM windows mapped into the system address space:

### GSM / 2G DSP Memory Windows (`XDSP0`)
* **`XDSP_D0_RAM` (`0xE0410000`)**: Code & shared control parameter memory.
* **`XDSP_D1_RAM` (`0xE0440000`)**: IQ sample buffer & Viterbi channel decoding RAM.
* **`XDSP_D2_RAM` (`0xE0448000`)**: Speech vocoder circular frame buffers.

### LTE Cat-1 DSP Memory Windows (`XDSP1`)
* **`XDSP1_D0_RAM` (`0xE0490000`)**: LTE firmware execution space & MAC-to-PHY command interface.
* **`XDSP1_D1_RAM` (`0xE04C0000`)**: FFT/IFFT OFDM subcarrier matrix & MIMO channel estimation data.
* **`XDSP1_D2_RAM` (`0xE04C8000`)**: HARQ retransmission buffers & VoLTE jitter buffers.

---

## 4. Host - DSP Mailbox & Protocol Stack

Communication between the MOCOR kernel (L1/L2 protocol tasks) and the DSP cores is managed by dedicated RTOS tasks and hardware interrupts:

1. **Host-to-DSP Mailbox Register**: Triggers hardware FIQ/IRQ interrupts on the DSP cores to schedule slot-level transmission.
2. **DSP-to-Host Interrupt Handler (`DSP_FiqHandler`)**: Fast interrupt handler on the Cortex-A7 acknowledging frame reception, paging events, and measurement reports.
3. **Task Queues**:
   - `WXDSP Task` / `WXDSP Q`: Manages 2G/TD DSP scheduling and sleep gating.
   - `WIQ_CAPTURE_TASK`: Extracts raw IQ samples from the DSP buffers during factory calibration (`+SPAGPS` / RF calibration).
4. **Fault & Overrun Supervision**:
   - `DSP OVERRUN: 0x%X`: Emitted if the DSP fails to complete symbol demodulation within a radio subframe (1 ms for LTE, 4.615 ms for GSM).
   - `DSP did not send interrupt to mcu during 30s!!`: Hardware watchdog reset trigger indicating baseband coprocessor lockup.
   - `L1 handover search not response by dsp!`: Frequency inter-RAT handover search timeout.
