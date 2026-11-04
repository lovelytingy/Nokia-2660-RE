# Boot process

Status: **Validated** on real Nokia 2660 Flip (TA-1480 HW3.0, SW `10.02.17.00`, India series) hardware dumps and PAC binaries.

## The Complete Chain

```text
                    ┌─────────────────────────────────────────┐
   Power-on ───────►│ Unisoc BootROM (UMS9117 / Cortex-A7)   │
                    └───────────────────┬─────────────────────┘
                                        │
                 ┌──────────────────────┴──────────────────────┐
                 │                                             │
      Normal NAND Boot                                Download / BSL Mode (Hold '*')
                 │                                             │
                 ▼                                             ▼
          boot0.bin (IRAM)                              FDL1 (IRAM / DRAM)
        (Inits DDR, Reads boot1)                     (Inits DDR, USB handshake)
                 │                                             │
                 ▼                                             ▼
        [Verifies DHTB Cert]                          [Verifies FDL2 Cert]
                 │                                             │
                 ▼                                             ▼
          boot1.bin (DRAM)                              FDL2 (DRAM)
     (Inits FTL, Reads kernel)                      (NAND read/write protocol)
                 │                                             │
                 ▼                                             ▼
        [Verifies DHTB Cert]                           Flash / Dump Partitions
        (start verify kernel)                          or Direct RAM Execution
                 │
                 ▼
          kernel.bin (DRAM)
         (MOCOR OS Core)
                 │
                 ▼
      [start verify user img]
                 │
                 ▼
          user.bin (DRAM)
     (MMI Task, Apps, Opera, UI)
```

End goal relevance: download mode (FDL1→FDL2) is the practical path for **custom firmware experiments**; normal boot shows the **on-device trust chain** (certs / anti-rollback) that custom images must satisfy or bypass.

## Image wrapper: `DHTB`

All three loaders begin with the same Unisoc signed-image header:

| Offset | Field (observed) |
|--------|------------------|
| `0x00` | Magic `DHTB` (`44 48 54 42`) |
| `0x04` | Version `1` |
| `0x30` | Payload size (bytes after header) |
| `0x200+` | ARM bootstrap / CP15 bring-up stub (inside header region) |

- Header size on these files: **1204 bytes (`0x4B4`)** = `filesize − dword[0x30]`.
- Payload after the header is predominantly **ARM Thumb**.
- Stripped payloads: `firmware/extracted/v23_update_Nokia_2660/stripped/`.

| File | Payload size | File SHA-256 |
|------|-------------:|--------------|
| `fdl1.bin` | 31,392 | `2d1629db408dbfa08965eaa83cde41d963cf1d26a1aa09f6ab5b87e34b16c390` |
| `fdl2.bin` | 62,720 | `a3a695c3a02cf206448d60c7e7a32df809b928a8c9a3bbd79f8de8aa462ec2b0` |
| `boot0.bin` | 30,416 | `f9a446bb90d338a72275d338fcc6ddbcdf2b9cabf842babaa90814efde33b281` |

## Load / flash addresses

From embedded `UMS9117.xml` (flash tool scheme) — prefer these over PAC `FILE_T` addrs when they disagree:

| Image | XML `<Base>` | Meaning |
|-------|--------------|---------|
| FDL1 | `0x6200` | BootROM SRAM download address (“First nand flash download image”) |
| FDL2 | `0x80100000` | DDR (“Second nand flash download image”, type `NAND_FDL`) |
| BOOT0 | `0x80000001` | NAND slot / partition id style (not an SRAM vector) |
| BOOT1 | `0x80000002` | Same style |

PAC `FILE_T` also listed FDL @ `0x40000000` (likely remapped/alternate view). Ghidra imports currently used `0x40000000` (FDL1/boot0 payload) and `0x80000000` (FDL2 payload ≈ XML `0x80100000`). Re-base to XML addresses in GUI when doing detailed RE.

## FDL1 — first download loader

Role: first stage after BootROM in **USB/UART download mode**.

Evidence from strings / structure:

- Prints `fdl1 started!`, `Spreadtrum Boot Block version 1.1`, `FDL_PacketDoIdle`
- Host channel: USB **and** UART (`usb crc error`, `uart crc error`, `SEND_ERROR_RSP`)
- Initializes **LPDDR** (`DDR init start` … `DDR init OK`, `lpddr_powerup_init`, DMC training strings)
- **Verifies FDL2** before handoff: `start verify fdl2` / `verify fdl2 suc|fail`
- Secure-boot style failures: `cert type invalid`, `compare hash fail`, `key cert hash verify fail`, `content cert hash verify fail`, `antiroll_back error`, `cert empty`
- Mentions `boot0 UART/JTAG mode = %d` (debug strap/mode shared with boot0 lineage)

Implication: unsigned or wrong-key **FDL2** should fail verification inside FDL1 unless BootROM/FDL1 secure boot is disabled or keys are known.

## FDL2 — NAND programmer

Role: second-stage download agent in **DDR**; talks flash protocol to the host and programs NAND.

Evidence:

- `fdl2 started!`, `FDL_PacketDoIdle`, `FDL2_DataStart`, `FDL2_DataEnd`
- NAND path: `NANDCTL_ReadID`, `NANDCTL_Open`, `NandSpecTable`, `SCI_FTL_Load` / `Format` / `GetDevInfo`, `nand_flash_init success!`
- Rejects mismatched tables: `The Download partition table is inconsistent with the partition table in the device!`
- Post-flash: `wait usb plug out`, `usb has plug out, power off now`, `WDG_ResetMCU`

Implication: this is the component that must accept our images for **custom firmware flashing** via official BSL/download tooling (or a compatible open tool).

## boot0 — on-NAND first stage

Role: normal-boot counterpart that brings up DRAM/NAND enough to chain to **boot1**.

Evidence:

- `boot0 started!: %x`
- Reads NAND ID / params; loads boot1 with ECC handling (`read boot1 block %d,page %d…`, `load boot1 suc`)
- Verifies then runs boot1: `start verify boot1` → `verify boot1 suc` → `run boot1:%x` (or `verify boot1 fail`)
- Same cert/hash/anti-rollback string set as FDL1
- Shares many **DDR / LPDDR / DMC** diagnostic strings with FDL1 (common bring-up code lineage; payloads are not byte-identical)

Implication: even if NAND is rewritten, **boot0 will refuse an unverified boot1** under current secure-boot strings. Custom firmware likely needs a valid signature chain, an earlier-stage exploit, or a download-mode workflow that replaces more of the chain.

## boot1 — secondary bootloader & kernel launcher

Role: Second NAND boot stage running in **DDR** (load address `0x80100000`). It initializes the Flash Translation Layer (`SCI_FTL`), reads `kernel.bin`, cryptographically verifies the kernel DHTB certificate, and jumps to the kernel entry point.

### Evidence & Memory Map:
* **Load Address:** `0x80100000`
* **Entry Point (Reset Vector):** `0x80100220` (ARM mode bootstrap enabling CP10/CP11 VFP/NEON, sets stack pointer, branches to Thumb mode at `0x801003f2`).
* **Strings identified:**
  - `boot1 started!: %x`
  - `boot1 enter auto download` / `boot1 enter 7s reset mode`
  - `SCI_FTL_Load failed.`
  - `load kernel suc`
  - `start verify kernel: %x`
  - `verify kernel suc: %x`
  - `run kernel:%x`
  - `verify kernel fail: %x`

### Reverse-Engineered Verification Routine (`boot1.bin`):
Disassembly of the verification sequence at file offset `0x04f2` (Thumb mode):

```thumb
0x04f2: bl      #0x2880          ; get timestamp
0x04f6: mov     r1, r0
0x04f8: adr     r0, #0xf8        ; "start verify kernel: %x"
0x04fa: bl      #0x870           ; printf(...)
0x04fe: ldr     r5, [pc, #0x110] ; r5 = kernel destination ptr (0x000080a0)
0x0500: ldr     r0, [pc, #0x110] ; r0 = DHTB cert table ptr (0x80100000)
0x0502: mov     r1, r5
0x0504: bl      #0x1c82          ; Cryptographic verification routine
0x0508: cbz     r0, #0x53c       ; Branch to failure block if r0 == 0
0x050a: bl      #0x2880          ; Success path:
0x050e: mov     r1, r0
0x0510: adr     r0, #0x104       ; "verify kernel suc: %x"
0x0512: bl      #0x870
...
0x052e: adr     r0, #0x104       ; "run kernel:%x"
0x0530: bl      #0x870
0x0534: mov     r0, r4
0x0536: blx     #0x274           ; Branch to kernel entry point
0x053a: b       #0x53a           ; Halt

; Failure path:
0x053c: bl      #0x2880
0x0540: mov     r1, r0
0x0542: adr     r0, #0x100       ; "verify kernel fail: %x"
0x0544: bl      #0x870
0x0548: b       #0x548           ; Dead loop (device halts)
```

**Bypass observation:** The branch `0x0508: cbz r0, #0x53c` (`c0 b1` in Thumb opcode) is a 2-byte instruction. Patching this with `00 bf` (`nop`) bypasses the verification check and boots the kernel regardless of signature validity.

---

## kernel — MOCOR RTOS core & userland verification

Role: The core operating system image (`kernel.bin`, partition `0x80000003`). Linked at DRAM base address **`0x80a05e00`** with a standard 1,204-byte (`0x4B4`) DHTB header.

### 1. Low-Level CP15 & Stack Bring-Up (`0x80a061f0`)
`boot1.bin` jumps to the kernel reset vector at `0x80a06000` (`+0x200`), where `ldr pc, [pc]` transfers control to `0x80a061f0` in ARM mode:
1. **System Control Register (SCTLR)**: `mrc/mcr p15, #0, r0, c1, c0, #0` clears bit 13 (`0x2000`) to enforce low exception vectors (`0x00000000`).
2. **Coprocessor Access (CPACR & FPEXC)**: Enables CP10 and CP11 coprocessors via `mcr p15, #0, r0, c1, c0, #2` (`0xf00000`) and executes `vmsr fpexc, r0` (`0x40000000`) to activate the hardware VFP/NEON floating-point engine.
3. **Core Mode Stacks (`0x80a060b4`)**: Configures separate banked stack pointers across all ARM CPU execution modes:
   * `0xd1` (FIQ mode) → sets FIQ SP
   * `0xd2` (IRQ mode) → sets IRQ SP
   * `0xdb` (Abort mode) → sets ABT SP
   * `0xd7` (Undefined mode) → sets UND SP
   * `0xdf` (System mode) → sets SYS SP
   * `0xd3` (Supervisor mode) → restores SVC mode and initial execution stack.
4. **C Runtime Scatterloader**: Branches (`b 0x814eb0e8`) to the ARM C library `__scatterload` routine (`0x814c029c`), zeroing `.bss` and copying data sections into DRAM, then hands off to `__rt_entry` (`0x814eb0d4`).

### 2. RTOS Scheduler Startup & Idle Loop
`__rt_entry` calls `main()` at **`0x80a140e0`** (Thumb-2 mode):
* `0x80a140e2`: `bl #0x80cbefe8` — Initializes early PLLs, clock trees, and memory timings.
* `0x80a140e6`: `bl #0x80b12ef8` — Initializes hardware timers and board peripherals.
* `0x80a140ea`: `bl #0x810d475c` — Enters the RTOS scheduler initialization bridge (`0x80a06984` → `0x80a0e124`).

At **`0x80a0e124`**, the kernel switches from Thumb to ARM mode (`adr r0, #0; bx r0`):
```arm
0x80a0e128: mov  r0, #0x13
0x80a0e12c: msr  cpsr_c, r0     ; CPSR = 0x13 (Supervisor mode, IRQ & FIQ unmasked)
0x80a0e130: ldr  r0, [pc, #0xdc]
0x80a0e134: add  r0, r0, #0x320
0x80a0e138: mov  sp, r0         ; Establishes System Idle Thread stack
0x80a0e140: blx  #0x80dec452    ; Starts ThreadX / SCI core tick timers
0x80a0e144: ...                 ; Scheduler dispatch & idle execution loop
```
From this point onward, preemptive task scheduling is active under ThreadX / Spreadtrum Communication Interface (SCI).

### 3. Coprocessor & Modem Bring-Up (`init.c:0x80b551e4`)
The primary system initialization thread executes `init.c`:
1. **Modem DSP & Coprocessor Loading (`0x80b54b92`)**:
   Reads firmware partitions from NAND into reserved modem DRAM buffers:
   * `BML GSM DSP Code Partition` → `gsm_dsp.bin` (`NEMOL_CAT1_EVS_DM_DSP.bin`)
   * `BML LTE DSP Code Partition` → `lte_dsp.bin` (`T117_CAT1_LTEA_DSP.bin`)
   * `BML CM4 Code Download Partition` → `pm_cm4.bin` (Power management coprocessor)
   If partition reading fails, recovery messages are triggered:
   * `Please download g_dsp image again` (`init.c:275`)
   * `Please download l_dsp image again` (`init.c:287`)
   * `Please download cm4 image again` (`init.c:303`)
2. **Coprocessor Power & Reset Release (`0x80b54b48`)**:
   Writes control flags to APB power domain registers (`str r1, [r0, #0x114]`) to release reset lines and supply clocks to the Cortex-M4 and DSPs.
3. **Inter-Processor Communication (SIPC)**:
   * Initializes shared-memory ring buffers and mailbox protocols via `0x80b64fb0` and `0x80b650ba`.
   * Enables hardware mailbox interrupts: `TB_MBOX_SRC_AP_INT` (`0x80b8e4d4`) and `TB_MBOX_TAR_AP_INT` (`0x80b8e4e8`).

### 4. Cryptographic Verification of Userland (`init.c:0x80b54e68`)
Before launching the userland interface, `kernel.bin` calls `verify_user_img` (`0x80b54e68`):
```text
start verify user img: %x -> read BML partition 0x80000004 -> compute SHA-256 blocks -> verify RSA/ECC DHTB cert -> verify suc: %x
```
* If verification succeeds, execution proceeds to task creation.
* If verification fails, it outputs `verify fail` and enters an assert halt dead loop (`init.c:895`).

### 5. Static Task Creation (`os_cfg.c:0x80b654e8`)
Tasks are instantiated via `SCI_CreateAppThread` (`0x810d19da`):

| Task ID | Name | Priority | Stack Size | Function |
|:---:|:---|:---:|:---:|:---|
| `0x00` | **`T_CM`** | `0xd7` (215) | 4 KB | Communication Manager (modem multiplexing) |
| `0x01` | **`T_DIAG`** | `0xd8` (216) | 4 KB | Diagnostic / AT command server |
| `0x02` | **`T_KPDSVR`** | `0x63` (99) | 4 KB | Keypad matrix scanner & debouncer |
| `0x04` | **`T_AUDIO`** | `0xc8` (200) | 8 KB | Audio driver & voice processing pipe |
| `0x09` | **`T_TIMESVR`**| `0x44` (68) | 2 KB | Software timer subsystem |
| `0x15` | **`T_P_APP`** | `0xe4` (228) | 50 KB | **Application Framework / MMI Task** |

For Task `0x15` (`T_P_APP`), `os_cfg.c` registers the entry point **`0x88479f6f`**, transferring control into `user.bin`.

---

## user — Application Framework Bring-Up (`user.bin`)

Role: The interactive userland image (`user.bin`, partition `0x80000004`). Linked at DRAM base address **`0x88000000`**.

### 1. Entry Point: `MS_MMI_Main` (`0x88479f6e`)
Located in `MS_MMI_Main/source/mmi_app/kernel/mmimain.c`:
1. **Dynamic Frequency Scaling**: Registers `s_mmi_chng_freq_req_handler` with the kernel power manager.
2. **Boot Reason Inspection (`MMI_GetPowerOnType` @ `0x880da4e0`)**:
   Inspects hardware wake source:
   * Normal power key press
   * USB charger insertion / charging animation
   * RTC alarm trigger
   * Factory / calibration test mode
3. **MMK Window Manager Bring-Up**:
   * Initializes display buffers and LCD drivers (`OpDevGDI.c`).
   * Hooks keypad event dispatching (`MMK_DispatchMSGKbd`).
   * Loads icon, font, and translation bundles from `mmi_res.bin` (partition `0x90000004`).
   * Spawns the root desktop / idle window handler using `MMK_CreateWin()`.
4. **Event Loop**:
   `T_P_APP` enters the main message pump (`MMK_SendMsg` / signal receiver), dispatching UI redraws and user input until device shutdown.

---

## Tooling used

| Tool | Use |
|------|-----|
| Custom PAC extractor | Isolate images |
| Manual DHTB parse + Capstone | Header size, Thumb vs ARM, stub at `+0x200` |
| Strings / entropy | Role classification (no binwalk installed) |
| Ghidra 12.1.4 headless | Project `ghidra/projects/Nokia2660_Loaders` with stripped payloads |

Open in Ghidra GUI: `ghidraRun` → open `ghidra/projects/Nokia2660_Loaders`.

## Next RE steps (toward custom FW)

1. Map FDL packet handlers (`FDL_PacketDoIdle`, DataStart/End) and command IDs.
2. Locate hash/cert verify routines; identify algorithm and key material source.
3. Analyze `boot1.bin` the same way (next chain link after boot0).
4. Capture USB download session from a real TA-1480 (BootROM ↔ FDL1 ↔ FDL2).
5. Confirm whether device secure boot is enforced in production NV / eFuse.
