# MMI Userspace Application Framework (`user.bin`)

## 1. Overview & Framework Architecture

The **`user.bin`** binary (11,194,324 bytes / 10.7 MB) comprises the complete user-facing application runtime and Graphical User Interface (GUI) environment for the Nokia 2660 Flip. Loaded into DDR RAM at address **`0x88000000`**, it operates as a cooperative userspace layer atop the MOCOR kernel (`kernel.bin`).

The application framework is structured into three primary tiers:

```
       +-------------------------------------------------------+
       |             Native Applications Layer                 |
       |  Phone, Contacts, Messages, Camera, Opera Mini, Media |
       +---------------------------+---------------------------+
                                   |
       +---------------------------v---------------------------+
       |             Spreadtrum MMK GUI Framework              |
       |  Window Manager, Control Trees, Form Layouts, Events  |
       +---------------------------+---------------------------+
                                   |
       +---------------------------v---------------------------+
       |          System Service Thunks (0x810xxxxx)           |
       |    Calls directly into Kernel Space (kernel.bin)      |
       +-------------------------------------------------------+
```

---

## 2. Binary Identification & Layout

| Attribute | Specification |
|:---|:---|
| **Binary Partition** | `user.bin` / `USER_IMG` |
| **Partition ID** | `0x80000004` |
| **Raw Image Size** | 11,194,324 bytes |
| **Memory Range** | `0x88000000` – `0x88aacfd3` |
| **Verified Base Address** | **`0x88000000`** |
| **Header Format** | Unisoc DHTB Header v1 with Content Certificate (`0x00AACD40`) |
| **Early Thumb Init** | **`0x880004ba`** (`push {r3-r7, lr}`) |
| **Main MMI Entry Point** | **`0x88479f6f`** (`MS_MMI_Main` in Task `0x15` / `T_P_APP`) |
| **Processor Architecture** | `ARM:LE:32:v7` (Thumb-2 heavy execution) |
| **Compile Timestamp** | `Jul 26 2021` |
| **Software Version** | `10.02.17.00` |
| **Product Identifier** | `FF618M02_D_CF3_SKU3_V21_240X320_EN_20220716_191458` |
| **Decompiled Source** | [`ghidra/exports/user.bin_decompiled.c`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/ghidra/exports/user.bin_decompiled.c) |
| **Function Index** | [`ghidra/exports/user.bin_notes.txt`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/ghidra/exports/user.bin_notes.txt) |

---

## 3. Userspace Task Registry

The userspace image spawns and manages dedicated worker threads:

| Task Symbol | Task Queue | Priority | Stack | Function / Purpose |
|:---|:---|:---:|:---:|:---|
| `0x15` (`T_P_APP`) | `Q_P_APP` | `0xe4` (228) | 50 KB | **Main MMI Event Loop (`MS_MMI_Main`)** |
| `T_P_APP_FILE_TASK` | `Q_P_APP_FILE_TASK`| User | 16 KB | Asynchronous file I/O & SD card operations |
| `FAT_XFIND_TASK` | — | User | 8 KB | Fast FAT filesystem directory and file index search |
| `FAT_XMOV_TASK` | — | User | 8 KB | Background file copy and move operations |
| `MMIPB_PB_TASK` | `Q_MMIPB_PB_TASK` | User | 16 KB | Phonebook database engine & SIM contact synchronization |
| `T_P_APP_ENG_FMSCAN_TASK` | `Q_P_APP_ENG_FMSCAN_TASK` | User | 8 KB | FM Radio background frequency sweep & RSSI scoring |
| `T_P_APP_MP3_DEL_TASK` | `Q_P_APP_MP3_DEL_TASK` | User | 8 KB | Media library file deletion and playlist pruning |
| `SDIO0_TASK` | — | Driver | 8 KB | High-speed SDIO bus transfer handler for MicroSD |

---

## 4. Key Subsystems & Application Engines

1. **Spreadtrum MMK (MMI Kernel)**:
   - Window stack managing hierarchical screens (`MMK_CreateWin`, `MMK_CloseWin`, `MMK_SendMsg`).
   - Widget controls: Buttons, text input boxes, list menus, softkey labels, popups, and status icons.
   - Resource loader consuming `mmi_res.bin` theme packages (`tres0001`).
2. **Web Browser & Network Runtimes**:
   - **Opera Mini**: Embedded proxy-based web browser optimized for low-memory feature phones.
   - **Java ME Virtual Machine (KVM / MIDP 2.0)**: Embedded Java runtime for running `.jar` micro-apps.
3. **Dialer & Secret Code Dispatcher**:
   - Intercepts keypad sequences in the call dialer before issuing standard 3GPP AT commands.
   - Handles Spreadtrum Engineer Mode (`*#*#372733#*#`), Factory Logging toggles (`*#8378*733425*1#` / `*0#`), Hardware Diagnostics (`*#2886#`), and Master Reset (`*#7370#`).
4. **Bluetooth Application Profiles**:
   - Hands-Free Profile (HFP 1.6), Advanced Audio Distribution (A2DP), Phone Book Access (PBAP), and Object Push Profile (OPP) for file transfer.
5. **HMD FOTA Update Engine**:
   - Firmware Over-The-Air client communicating with HMD production servers:
     - `https://productsapi.hmdglobal.com/FPOTA/getBinFiles`
     - `https://productsapi.hmdglobal.com/arae/SWOTAData`
