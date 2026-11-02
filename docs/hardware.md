# Hardware

Status: **partially documented — verified via community findings and PAC manifests**

## Known (India reference unit)

| Item | Value |
|------|-------|
| Marketing name | Nokia 2660 Flip |
| Type designator | TA-1480 (related regional SKUs: TA-1469, TA-1474, TA-1711) |
| FCC ID | `2AJOTTA-1480` / `2AJOTTA-1469` (HMD Global Oy) |
| HWID (`*#0000#`) | **HW3.0** |
| NDT code | `MH_13ARA80IN00_0` |
| Market | India |
| Chipset / SoC | Unisoc T107 (UMS9117 / T117 family; ARM Cortex-A7) |
| Auxiliary Cores | ARM Cortex-M4 power management core (`PM_nemol_cm4.bin`), DSP (`T117_CAT1_LTEA_DSP.bin`) |
| Flash Type | NAND (128 MB internal typical for UMS9117 feature phones) |
| RAM | 48 MB LPDDR (integrated / PoP) |
| Main Display | 2.8" QVGA (240×320) LCD (GPIO control: `-0x28, 0x5f`, backlight: `rgb=0x19`) |
| Cover Display | 1.77" QQVGA (128×160) LCD (GPIO control: `0x28, -0x5f`, backlight: `w=0x2d`) |
| Battery Model | BP-4L (1450 mAh removable) |

See [device.md](device.md) for the full `*#0000#` capture.

## Needed from contributors

- Motherboard photos (both sides), silkscreen IDs
- NAND / RAM package markings
- UART / test-point map
- Battery / charging IC notes
- Photos confirming board silkscreen vs **HW3.0**
