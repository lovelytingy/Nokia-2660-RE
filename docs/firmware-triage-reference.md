# Firmware Triage & Static Analysis Reference

Status: **Reverse Engineered & Verified from TA-1480 Stock Firmware Dumps (`kernel.bin`, `user.bin`, `boot0.bin`, `boot1.bin`)**.

This document compiles technical findings discovered through automated string triage ([`tools/extract_strings.py`](../tools/extract_strings.py)) and MOCOR metadata inspection ([`tools/mocor-info.py`](../tools/mocor-info.py)).

---

## 1. Domain Findings Matrix

| Domain Category | Discovered Items | Primary Source Binary | Key Technical Significance |
|-----------------|------------------|-----------------------|----------------------------|
| **`dialer`** | 38 secret codes | `user.bin` (`0x00600B58`) | Unisoc Engineer Mode (`*#*#372733#*#`), Factory Logging toggles (`*#8378*733425*1#`), MMI Auto-Test (`*#2886#`), 30+ language shortcuts. |
| **`at_cmd`** | 560+ AT commands | `user.bin` & `kernel.bin` | 3GPP standards (`AT+CMUX`, `+CBC`, `+CREG`) & Spreadtrum proprietary (`+SPADC`, `+SPAGPS`, `+SPAUDIOCONFIG`). |
| **`hardware`** | 140+ driver symbols | `kernel.bin` & `user.bin` | Dual LCD drivers (`GC9307` main, `GC9107`/`GC9104`/`ST7735SV` sub), PMIC `SC2720`, VBC audio, eFuse controllers. |
| **`rtos`** | 30+ tasks & SCI APIs | `kernel.bin` & `user.bin` | Nucleus/ThreadX task architecture, priority queues (`Q_...`), static task registry (`*_TASK`, `*_THREAD`, `*_SRV`). |
| **`security`** | 140+ crypto strings | `boot0.bin` & `user.bin` | Anti-rollback checks, eFuse SPK read/write, RSA-2048 modulus, Secure Boot certificate chain verification. |
| **`network`** | 300+ cellular configs | `user.bin` & modem DSP | VoLTE / IMS stack, EVS voice codec parameters, SIM/PLMN routing, MCC/MNC tracking. |
| **`paths`** | 90+ filesystem paths | `user.bin` | Drive mappings (`D:\` U-Disk, `E:\` SD card, `c:\reset`), Gameloft demo storage (`c:\od_game\...`). |
| **`urls`** | 74 network endpoints | `user.bin` | Official HMD Global FOTA update endpoints, Opera Mini proxy servers. |
| **`errors`** | 800+ diagnostics | `boot0.bin` & `kernel.bin` | NAND ID probing, DDR3 training diagnostics, ADI interconnect timeouts, boot verification asserts. |

---

## 2. RTOS Architecture & Task Registry

The Nokia 2660 Flip runs Unisoc's **MOCOR RTOS**, an operating system environment built upon a preemptive real-time kernel (derived from Nucleus PLUS and ThreadX primitives) wrapped with Spreadtrum's **SCI** (System Control Interface) API layer.

### A. Kernel RTOS Tasks (`kernel.bin`)
These tasks run in privileged ARM supervisor mode (`0x13`) and manage hardware peripherals, communications, and power:

| Task Symbol | Task Queue | Description / Responsibilities |
|-------------|------------|--------------------------------|
| `AUDIO_HAL_TASK` | `Q_AUDIO_HAL_TASK` | Low-level Voice Band Codec (VBC) hardware audio driver, DAC/ADC DMA buffer feeding. |
| `SPRD_CODEC_TASK` | `Q_SPRD_CODEC_TASK` | Spreadtrum software audio codec engine (MP3, AAC, AMR-WB, WAV decoders). |
| `CONTROL_TASK` | — | Core hardware state controller, power state transitions, sleep negotiation. |
| `DATA_TASK` | — | Packet data router between cellular baseband DSP and TCP/IP stack. |
| `NAS_CONTROL_TASK`| — | Non-Access Stratum cellular protocol control (mobility, attachment). |
| `SCM_TASK` | — | Smart Card / SIM card interface manager (USIM command handling). |
| `TIMER_TASK` | — | Kernel high-resolution tick timer service. |
| `WIQ_CAPTURE_TASK`| — | Wireless IQ diagnostic data capture task for RF calibration. |
| `ASM_TASK` | — | Advanced Sleep Management / CPU clock gating coordinator. |
| `CHGMNG_SRV` | `Q_CHGMNG_SRV` | SC2720 PMIC battery charge management and temperature monitoring service. |
| `THERMAL_SRV` | `Q_THERMAL_SRV` | Thermal sensor polling and CPU throttling governor. |
| `MAX_GAP_TASK` | — | Radio frequency GAP profile manager. |
| `T_P_APP` | `Q_P_APP` | **The MMI application container task** (entry point `0x88479f6f` launching `MS_MMI_Main`). |

### B. Userland Application Tasks (`user.bin`)
Launched by `user.bin` to handle user-facing features asynchronously:

| Task Symbol | Task Queue | Description / Responsibilities |
|-------------|------------|--------------------------------|
| `T_P_APP_FILE_TASK` | `Q_P_APP_FILE_TASK` | Asynchronous file I/O operations (FAT filesystem read/write, photo saving). |
| `T_P_APP_MP3_DEL_TASK` | `Q_P_APP_MP3_DEL_TASK` | Background media file deletion and ID3 tag parsing. |
| `T_P_APP_ENG_FMSCAN_TASK` | `Q_P_APP_ENG_FMSCAN_TASK` | Background FM Radio frequency scanning and RSSI signal measurement. |
| `FAT_XFIND_TASK` | — | Fast file search and directory tree traversal across internal and external storage. |
| `FAT_XMOV_TASK` | — | High-speed FAT file moving and cluster defragmentation. |
| `MMIPB_PB_TASK` | `Q_MMIPB_PB_TASK` | Phonebook index database search and SIM contact synchronization. |
| `SDIO0_TASK` | — | High-speed MicroSD card transfer controller. |
| `T_P_IKEYBACKUP` | — | Contact and SMS backup / restore database worker. |
| `T_P_DRM_IO` | — | DRM Rights Object file decryption and license validation. |
| `SPTEST_TASK` | — | Production factory self-test thread runner. |

### C. Spreadtrum SCI API Functions
Both `kernel.bin` and `user.bin` interact through standardized SCI system calls:
- `SCI_CreateAppThread` / `SCI_CreateThread`: Task instantiation with priority, stack size, and queue allocation.
- `SCI_CreateEvent` / `SCI_SetEvent`: Inter-thread event flags and synchronization.
- `SCI_CreatePeriodTimer` / `SCI_CreateTimer`: High-precision timer callbacks.
- `SCI_CreateMemPool` / `SCI_ALLOC_APP`: Thread-safe memory allocation pools.

---

## 3. Display Subsystem & Kernel GC Drivers

Static analysis of [`firmware/dumps/ta1480_10.02.17.00/kernel.bin`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/firmware/dumps/ta1480_10.02.17.00/kernel.bin) reveals the full suite of LCD driver functions compiled into the kernel.

```
       +------------------------------------------------------+
       |                Kernel Display Subsystem              |
       +--------------------------+---------------------------+
                                  |
            [Main LCD]            |           [Sub LCD]
       GalaxyCore GC9307          |     GC9107 / GC9104 / ST7735SV
       (Internal 2.8" QVGA)       |     (Cover 1.77" QQVGA)
       +----------------------+   |     +----------------------+
       | GC9307_Init          |   |     | GC9107_Init          |
       | GC9307_ReadID        |   |     | GC9104_Init          |
       | GC9307_mainlcd_id    |   |     | ST7735SV_Init        |
       | GC9307_set_window    |   |     | GC9107_sublcd_id     |
       | GC9307_ESD_driver    |   |     | GC9104_sublcd_id     |
       | GC9307_EnterSleep    |   |     | GC9107_set_window    |
       +----------------------+   |     +----------------------+
```

### Complete Driver Function Inventory

#### 1. GalaxyCore GC9307 (Main Internal Display — 240×320 QVGA)
- `GC9307_Init`: Sends hardware reset and initialization register sequences over SPI1.
- `GC9307_ReadID`: Reads 24-bit panel ID (telemetry confirms `0x009307`).
- `GC9307_mainlcd_id`: Panel identification matching handler.
- `GC9307_set_display_window`: Configures active RAM window (`0x2A` column, `0x2B` row).
- `GC9307_EnterSleep` / `GC9307_Close`: Powers down internal charge pumps and puts LCD driver IC into deep sleep.
- `GC9307_ESD_driver`: Electrostatic discharge detection and automated display reset handler.
- `GC9307_ESD_Init_check_0` / `GC9307_ESD_Init_check_1`: Multi-point ESD status register validators.

#### 2. GalaxyCore GC9107 (Sub External Cover Display — 128×160 QQVGA)
- `GC9107_Init`: Primary initialization sequence for external cover display.
- `GC9107_ReadID`: Probes GC9107 chip identifier.
- `GC9107_sublcd_id`: Sub-screen hardware presence validator.
- `GC9107_set_display_window`: Window configuration for 128×160 frame.
- `GC9107_EnterSleep` / `GC9107_Close`: Closes SPI session and enters sleep mode when flip is open.
- `GC9107_ESD_Init_check_0` / `GC9107_ESD_Init_check_1`: Sub-display ESD health monitor.

#### 3. GalaxyCore GC9104 (Sub External Cover Display — Alternate)
- `GC9104_Init`, `GC9104_ReadID`, `GC9104_sublcd_id`, `GC9104_set_display_window`, `GC9104_EnterSleep`, `GC9104_Close`, `GC9104_ESD_Init_check_0`, `GC9104_ESD_Init_check_1`.

#### 4. Sitronix ST7735SV (Sub External Cover Display — Alternate)
- `ST7735SV_Init`, `ST7735SV_EnterSleep`, `ST7735SV_Close`.

---

## 4. Modem Protocol & AT Command Set

Over 560 AT commands are recognized across `user.bin` and the modem subsystem when communicating over USB CDC-ACM (Modem Interface):

### A. Spreadtrum Proprietary Diagnostic Commands (`+SP...`)
Unisoc implements vendor-specific extensions prefixed with `+SP`:

| Command | Purpose / Functionality |
|---------|-------------------------|
| `+SPADC` / `+SPADCVS` | Reads on-chip Analog-to-Digital Converter channels, including internal battery voltage, charger voltage, and thermal thermistors. |
| `+SPAGPSENABLE` / `+SPAGPSRESET` | Controls the Assisted-GPS engine state machine. |
| `+SPAGPSSTATE` / `+SPAGPSREPORT` | Polls A-GPS positioning solutions and satellite constellation status. |
| `+SPAGPSOTDOACAP` / `+SPAGPSOTDOACONFIG` | Configures LTE OTDOA (Observed Time Difference of Arrival) cellular positioning. |
| `+SPAUDIOCONFIG` / `+SPAUDVOLTYPE` | Sets audio gain tables, equalizer curves, and routing paths for earpiece, speaker, and headset. |
| `+SPAUDRECORD` | Triggers voice call recording or microphone sampling. |
| `+SPACTCARD` / `+SPACTCFUN` | Configures multi-SIM power state and dual standby RF activation. |
| `+SPATASSERT` | Triggers diagnostic software assertion, generating a memory crash dump. |
| `+SPPOWERFB` / `+SPPOWERBFCOM` | Controls baseband power state and RF front-end calibration feedback. |

### B. Standard 3GPP Modem Commands
- `AT+CMUX`: GSM 07.10 multiplexing protocol used to run multiple virtual channels over USB.
- `+CBC`: Battery charge status and percentage readout.
- `+CSQ`: Cellular signal quality (RSSI and bit error rate).
- `+CREG` / `+CCGREG` / `+CEREG`: Network registration status (GSM, GPRS, and LTE).
- `+CIMI`: International Mobile Subscriber Identity (IMSI) retrieval.
- `+CGMR`: Baseband firmware revision identifier.
- `+CPMS` / `+CMGD`: Short Message Service (SMS) preferred memory storage and deletion.

---

## 5. Security & Cryptographic Verifications

Strings extracted from `boot0.bin`, `boot1.bin`, and `kernel.bin` identify the operational security checks enforced by the Unisoc boot chain:

```
[boot0 / boot1 Security Logic]
  1. "input of get_sechdr_Addr err"       -> Checks DHTB header boundary validity
  2. "cert empty"                         -> Validates that a security header exists
  3. "cert type invalid"                  -> Rejects unknown certificate structures
  4. "compare hash fail"                  -> SHA-256 calculation mismatch
  5. "key cert hash verify fail"          -> Root public key does not match eFuse hash
  6. "antiroll_back error"                -> Cert version < eFuse anti-rollback counter
  7. "content cert hash verify fail"      -> Image signature verification failure
  8. "verify boot1 fail: %x"              -> Terminal boot abort
```

### eFuse Runtime Telemetry in Userspace
Strings in `user.bin` (`SECURITY_DATA: ...`) reveal that userland contains security services interacting with the hardware eFuses:
- `SECURITY_DATA: efuse read spk not in thread context!`
- `SECURITY_DATA: Efuse read spk failed!`
- `SECURITY_DATA: Efuse first read spk suc!`
- `SECURITY_DATA: spk efuse: 0x%x`
- `SECURITY_DATA: get rsa data from nv failed`
- `SECURITY_DATA: rsa pk can't be saved to nv!`
- `SECURITY_DATA: rsa encrypt failed, ret: %d`
- `update vesion:efuse version is %x, cert version is %x`

This confirms the phone's software updates verify that the certificate version in the update PAC equals or exceeds the burned hardware anti-rollback eFuse counter before permitting execution.

---

## 6. Remote Endpoints & Server Infrastructure

Extracted URLs in `user.bin` reveal the backend cloud infrastructure used by the Nokia 2660 Flip:

### A. HMD Global Firmware & FOTA Servers
- **`https://productsapi.hmdglobal.com/arae/SWOTAData`**: Primary HMD Global Software Over-The-Air (SWOTA) update check API.
- **`https://productsapi.hmdglobal.com/FPOTA/getBinFiles`**: Feature Phone Over-The-Air (FPOTA) direct binary payload download service.
- **`https://dapi.hmdglobal.net.cn/api/v1/npsfp/feedback`**: Device analytics and customer feedback API (China endpoint).
- **`https://dapi.hmdglobal.net/api/v1/npsfp/feedback`**: Device analytics and customer feedback API (Global endpoint).

### B. Opera Mini 4.4 Cloud Proxies
The embedded Opera Mini browser routes web traffic through Opera's compression transcoders:
- `http://server4.operamini.com:80`
- `http://mini4cn.opera-mini.net:80`
- `http://startup.oupeng.com/mini/stat.php`

---

## 7. Storage Drive Mappings & File Paths

MOCOR maps flash partitions and peripheral storage to DOS-style drive letters:

| Drive / Path | Physical Storage Media | Usage & Discovered Paths |
|--------------|------------------------|--------------------------|
| **`D:\`** | Internal NAND User Disk (`0x80000008`) | `D:\UDISKTEST`, `D:\Audio\`, `D:\Video\`, `D:\System\Dhcps\lease.dat`, `D:\DRM_LRO` |
| **`E:\`** | External MicroSD Card | `E:\SDTEST\`, `E:\apr.ini`, `E:\sd.txt`, `E:\Audio\`, `E:\Video\test.flv` |
| **`c:\reset`** | Internal System NV / State Marker | Created during master reset sequence (`*#7370#`) to signal full partition formatting on reboot. |
| **`c:\od_game\...`** | Internal Demo Games | `c:\od_game\od_new_racing_attack\bronze\`, `silver\`, `golden\` (Gameloft trial games directory). |

---

## 8. Hardware Diagnostics & Panic Signatures

Diagnostic messages preserved in the bootloader binaries give insight into hardware failure modes:

- **NAND Flash Probing**:
  - `boot0 read Nand ID fail!`
  - `boot0 nand flash ID %#0x%0x!`
  - `boot0 nand set param fail!`
  - `fdl2 not fand NandFlash ID in NandSpecTable, %#0x %#0x!`
  - `Nand MC Wrong Pos %d`
- **LPDDR DRAM Training**:
  - `dmc_lpddr3_wde_training Failed` (Write Data Eye training failure)
  - `dmc_lpddr3_rde_training_pos failed` (Read Data Eye positive edge failure)
  - `dmc_lpddr3_rde_training_neg failed` (Read Data Eye negative edge failure)
- **PMIC & Bus Interconnect**:
  - `adi reg read timeout!` / `adi reg write timeout!` (Analog Die Interconnect bus timeout between SoC and SC2720 PMIC)
  - `MUX_Create: Open UART/USB success`
