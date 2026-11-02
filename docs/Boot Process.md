# Boot process

Status: **partial** — inferred from `fdl1.bin` / `fdl2.bin` / `boot0.bin` in PAC `v23_update_Nokia_2660` (UMS9117). Not yet validated on TA-1480 silicon.

## Two paths

```text
                    ┌─────────────────────────────────────┐
   Power-on ───────►│ Unisoc / Spreadtrum BootROM         │
                    └──────────────┬──────────────────────┘
                                   │
                 ┌─────────────────┴──────────────────┐
                 │                                    │
         normal boot                          download / BSL mode
                 │                                    │
                 ▼                                    ▼
            boot0 (NAND)                         host sends FDL1
                 │                                    │
                 ▼                                    ▼
         verify + load boot1                     FDL1: DDR init,
                 │                               verify + run FDL2
                 ▼                                    │
              boot1 …                                 ▼
         (kernel / user later)                   FDL2: NAND FTL,
                                                 flash partitions
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
