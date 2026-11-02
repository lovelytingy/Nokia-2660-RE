# Flash layout (from PAC file table & UMS9117.xml)

Derived from India-oriented `v23_update_Nokia_2660.pac` (`UMS9117` / NAND; NV token `MH_13ARA80IN00`) and the embedded `UMS9117.xml` flashing scheme.

## Partition Base Map (for `read_flash` / `spd_dump`)

These IDs correspond to the `<Base>` address tags recognized by FDL2 during read/write commands:

| Partition ID / Base | PAC ID | File | Size (bytes) | Description |
|---------------------|--------|------|-------------:|-------------|
| `0x80000001` | BOOT0 | `boot0.bin` | 31,620 | First NAND stage bootloader |
| `0x80000002` | BOOT1 | `boot1.bin` | 161,364 | Second NAND stage bootloader |
| `0x80000003` | KernelImg | `EXEC_KERNEL_IMAGE.bin` | 12,976,468 | MOCOR OS kernel |
| `0x80000004` | UserImg | `EXEC_USER_IMAGE.bin` | 11,834,228 | User space / application layer |
| `0x80000008` | PreloadUdisk | *(empty in PAC)* | - | Erase File System / user disk |
| `0x90000001` / `0x9000000C` | NV | `FF618V01_…_nvitem_….bin` | 841,860 | Download-NV items (RF cal, IMEI, SKU config) |
| `0x90000002` | PhaseCheck | *(empty in PAC)* | - | Production phase checks |
| `0x90000003` | EraseRunNV | *(empty in PAC)* | - | Running NV erase |
| `0x90000004` | MMIRes | `mmi_res_240X320_MH.bin` | 8,489,672 | 240×320 UI resource pack |
| `0x90000005` | LTEDSPCODE | `T117_CAT1_LTEA_DSP.bin` | 3,145,728 | LTE DSP modem firmware |
| `0x90000006` | GSMDSPCODE | `NEMOL_CAT1_EVS_DM_DSP.bin` | 2,654,208 | GSM/2G DSP modem firmware |
| `0x90000007` | Preload_Hidden | *(empty in PAC)* | - | Hidden disk C section |
| `0x90000008` | OmadmFota | *(empty in PAC)* | - | FOTA upgrade binary |
| `0x9000000E` | RomDisk | *(empty in PAC)* | - | Auto-run driver disk |
| `0x90000013` | CM4CODE | `PM_nemol_cm4.bin` | 55,500 | Cortex-M4 power management firmware |
| `0x90000014` | WIFICODE | *(empty in PAC)* | - | Wi-Fi BSP code (unused on TA-1480) |
| `0x90000016` | XFILE | *(empty in PAC)* | - | XFILE dump |
| `0x90000017` | AUTOSMS | *(empty in PAC)* | - | Auto SMS section |
| `0x90000018` | CM4CODE_AB | `PM_nemol_cm4_AB.bin` | 465,384 | Cortex-M4 alt / secondary image |
| `0x90000019` | DeltaNV | `…_deltanv.bin` | 56 | Delta NV updates |
| `0x9000001A` | EraseUserNV | *(empty in PAC)* | - | User NV erase |
| `0x9000001B` | APN | `apn.bin` | 538,534 | Cellular APN database |
| `0x9000001D` | IMSToggle | `ims_toggle.bin` | 256 | VoLTE / IMS toggle settings |

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
