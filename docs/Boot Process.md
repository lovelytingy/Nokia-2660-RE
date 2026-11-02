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

Role: The core operating system image (`kernel.bin`, partition `0x80000003`). Before launching userland MMI tasks, `kernel.bin` verifies `user.bin` (`0x80000004`), `lte_dsp.bin`, and `gsm_dsp.bin`:
* `start verify user img:%x`
* `userimg size:%x`
* `verify user img malloc fail`
* `Please download l_dsp image again`
* `Please download g_dsp image again`

This confirms that **every stage in the NAND boot chain cryptographically verifies the next stage**:
`BootROM` ➔ `boot0` ➔ `boot1` ➔ `kernel` ➔ `user`.

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
