# Firmware 002 — Reference Device Dump (TA-1480, v10.02.17.00)

Complete, authentic partition dump read directly from the physical reference device via `spd_dump` in BootROM download mode.

## Device Identity & Build

| Field | Value |
|-------|-------|
| Phone Model | **Nokia 2660 Flip** |
| Type Designator | **TA-1480** |
| Hardware ID | **HW3.0** |
| NDT Code | **`MH_13ARA80IN00_0`** (India) |
| Software Version | **10.02.17.00** |
| OS Build String | **`MOCOR_20A_MP_W22.04.6_P4_Release`** |
| Dump Method | `spd_dump` (`read_flash`) over USB BSL mode via `tools/dump_device.sh` |
| Loaders Used | `fdl1.bin` (SRAM `0x6200`), `fdl2.bin` (DDR `0x80100000`) |

---

## Dumped Partitions Manifest

| Partition Name | Flash Base | Size (bytes) | SHA-256 Digest |
|---|---|---:|---|
| `boot0.bin` | `0x80000001` | 31,620 | `0e34e71578e94c9d42657128c93db9c11d68e9df85db0657ead4f07fd8389530` |
| `boot1.bin` | `0x80000002` | 67,748 | `0bfc01117898e01f27a8f1762637b15620d7f462fc3d41ac428bf23e0fc08e46` |
| `kernel.bin` | `0x80000003` | 12,945,748 | `8e332f60fbe9887410c03859379c240cf90f78ec87be04f098f50f7a2e509beb` |
| `user.bin` | `0x80000004` | 11,194,324 | `04f07e5426f3edd1eb1037b58f57c73325006ceda43c6d9b9018eb2e81556a2f` |
| `nvitem.bin` | `0x90000001` | 1,048,576 | `f1d1806fe8cb38c12b837a348bfec310d10d4d876d61282fcf4c87c8a1c6de2c` |
| `mmi_res.bin` | `0x90000004` | 8,912,896 | `b176e72221ab5119404873c51b31afa345cb3ce34a9c0db1c719ca17213d675c` |
| `lte_dsp.bin` | `0x90000005` | 3,670,016 | `427fca56b9f2d5aa21273806f0a1ee3939ca1aea75244820c64b24be34ecd7a6` |
| `gsm_dsp.bin` | `0x90000006` | 3,145,728 | `c7a84137b7a9199b655031244a669435cd99dd641eb5a17e5d10a290febc4eff` |
| `pm_cm4.bin` | `0x90000013` | 131,072 | `7bcec854a1c7b3b7ef3dca9cba34e998ada51a83b78b5576aa13ff4f4af73888` |

> [!NOTE]
> All binary `.bin` files remain local and gitignored per repository policy. Only the cryptographic hashes and manifests are checked into git.
