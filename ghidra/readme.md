# Ghidra

## Project: Nokia2660_Loaders

Path: `ghidra/projects/Nokia2660_Loaders`

Imported (stripped DHTB payloads from PAC sample):

| Program | File | Loader base used | Recommended rebase |
|---------|------|------------------|--------------------|
| `fdl1_payload.bin` | FDL1 payload | `0x40000000` | XML FDL1 `0x6200` (confirm if base includes DHTB) |
| `fdl2_payload.bin` | FDL2 payload | `0x80000000` | XML FDL2 `0x80100000` |
| `boot0_payload.bin` | boot0 payload | `0x40000000` | SRAM base TBD on-device; PAC/XML flash id `0x80000001` |

Language: `ARM:LE:32:v7` (Thumb-heavy payloads).

Raw images: `firmware/extracted/v23_update_Nokia_2660/`  
Stripped payloads: `firmware/extracted/v23_update_Nokia_2660/stripped/`  
Headless helper script: `ghidra/scripts/ExportLoaderNotes.java` (optional)

```bash
# Example re-import at XML FDL2 address
analyzeHeadless ghidra/projects Nokia2660_Loaders \
  -import firmware/extracted/v23_update_Nokia_2660/stripped/fdl2_payload.bin \
  -processor ARM:LE:32:v7 \
  -loader BinaryLoader -loader-baseAddr 0x80100000
```

Firmware binaries themselves are large; keep analysis notes in `docs/`.
