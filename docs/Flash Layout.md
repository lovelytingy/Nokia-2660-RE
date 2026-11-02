# Flash layout (from PAC file table)

Derived from `v23_update_Nokia_2660.pac` (`UMS9117` / NAND). Addresses below are download/load hints from the PAC `FILE_T` entries when present; physical NAND mapping still needs confirmation on-device.

## Payloads extracted

| ID | File | Size (bytes) | SHA-256 (prefix) |
|----|------|-------------:|------------------|
| FDL | `fdl1.bin` | 32,596 | `2d1629db408dbfa0…` |
| FDL2 | `fdl2.bin` | 63,924 | `a3a695c3a02cf206…` |
| NV | `FF618V01_…_nvitem_….bin` | 841,860 | `a4a1d1f89d04fcbe…` |
| BOOT0 | `boot0.bin` | 31,620 | `f9a446bb90d338a7…` |
| BOOT1 | `boot1.bin` | 161,364 | `84198efccac0a34a…` |
| UserImg | `EXEC_USER_IMAGE.bin` | 11,834,228 | `00da688f51fc66d8…` |
| MMIRes | `mmi_res_240X320_MH.bin` | 8,489,672 | `14f31f4f1f5fee88…` |
| GSMDSPCODE | `NEMOL_CAT1_EVS_DM_DSP.bin` | 2,654,208 | `5515ec5b9f557422…` |
| LTEDSPCODE | `T117_CAT1_LTEA_DSP.bin` | 3,145,728 | `ab1c100cae509d2e…` |
| KernelImg | `EXEC_KERNEL_IMAGE.bin` | 12,976,468 | `84fe0d7a5226d766…` |
| CM4CODE | `PM_nemol_cm4.bin` | 55,500 | `e708f6f0e27b140d…` |
| CM4CODE_AB | `PM_nemol_cm4_AB.bin` | 465,384 | `fe4edf664575d484…` |
| DeltaNV | `…_deltanv.bin` | 56 | `1ba805a592351d31…` |
| APN | `apn.bin` | 538,534 | `19ddcc30d1211d98…` |
| IMSToggle | `ims_toggle.bin` | 256 | `3d6876a0146de857…` |
| (xml) | `UMS9117.xml` | 16,767 | `a831ff0b9668c873…` |

Full digests: `firmware/extracted/v23_update_Nokia_2660/manifest.csv`.

## Operation-only / empty entries

No payload in this PAC (flash-tool operations or optional slots):

`OmadmFota`, `Preload_Hidden`, `PreloadUdisk`, `RomDisk`, `ERASE HIDDEN_DISK_C`, `EraseRunNV`, `XFILE`, `AUTOSMS`, `EraseDU`, `PhaseCheck`, `WIFICODE`, `EraseUserNV`, `Erase FS`.

## Likely analysis targets (priority)

1. `fdl1.bin` / `fdl2.bin` — download-mode loaders  
2. `boot0.bin` / `boot1.bin` — on-device boot chain  
3. `EXEC_KERNEL_IMAGE.bin` — MOCOR kernel  
4. `EXEC_USER_IMAGE.bin` — user/application image  
5. `mmi_res_240X320_MH.bin` — UI resources (240×320)  
