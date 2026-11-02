# Reference device (India)

Captured from a real unit via dialer code **`*#0000#`** (product info).

| Field | Value |
|-------|-------|
| Product / marketing name | **Nokia 2660 Flip** |
| Type designator | **TA-1480** |
| Software version | **10.02.17.00** |
| NDT code | **`MH_13ARA80IN00_0`** |
| HWID | **HW3.0** |
| Market | **India-launched** series |

## Scope of this repo

Findings, PAC samples, and notes in this project are oriented toward the **Indian** 2660 Flip / TA-1480 lineage (NDT / SKU strings with `…IN…`). Other regions may use different NDT codes, NV, or PAC packages — treat cross-region firmware as untrusted until compared.

## Link to current PAC sample

NV image inside `v23_update_Nokia_2660.pac`:

`FF618V01_48X128_QVGA_CF8_HMD_SKU3_BASE_6325_nvitem_MH_13ARA80IN00.bin`

The **`MH_13ARA80IN00`** token matches the device NDT code prefix → strong evidence this PAC is built for the **same Indian SKU family** as the reference phone.

Still not a full proof of flash safety: PAC product version is `MOCOR_…W22.04.6_P5` while earlier OS string on device was `…W22.04.5_P4…`. Prefer a version-matched package before any write.

## Also recorded elsewhere

- USB (normal): `1782:4D00` — see [usb.md](usb.md)
- Chipset / platform: Unisoc T107 / UMS9117 — see [hardware.md](hardware.md)
