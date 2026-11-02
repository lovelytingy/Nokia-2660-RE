# Memory map

Status: **stub — help wanted**

## Known anchors

| Region | Address | Source |
|--------|---------|--------|
| FDL1 load (download) | `0x6200` | `UMS9117.xml` |
| FDL2 load (download) | `0x80100000` | `UMS9117.xml` |
| FDL PAC FILE_T hint | `0x40000000` | PAC table (may be remapped view) |

## Needed

- Full IRAM / DRAM / NAND window map for UMS9117 / T107
- Peripheral base addresses used by FDL/boot0
- Confirmation from datasheet or BootROM RE
