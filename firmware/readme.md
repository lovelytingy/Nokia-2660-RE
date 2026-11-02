# Firmware

This directory holds firmware used during analysis.

## Firmware 001 — MOCOR PAC sample

| Field | Value |
|-------|-------|
| Filename | `v23_update_Nokia_2660.pac` |
| Local path | `firmware/v23_update_Nokia_2660.pac` |
| Size | 41,385,109 bytes (39,439 KiB / ~39.45 MiB) |
| Type | Spreadtrum / Unisoc `.pac` update package |
| PAC struct version | `BP_R1.0.0` (UTF-16LE at offset 0) |
| Product ID in header | `UMS9117` |
| Product version in header | `MOCOR_MOCOR_20A_MP_W22.04.6_P5` |
| Flash type | NAND |
| File table | 29 entries @ offset 2124; magic `0xFFFAFFFA` |
| Origin | [mega.nz](https://mega.nz/file/UpwSxQgY#vc0N20ogCZmW9TNm-YRjc-oj83DHFc3pnbgk6o8CIZg) |
| Market / SKU | **India series** — NV basename contains `MH_13ARA80IN00` |
| Status | **SKU-linked** to reference TA-1480 NDT; software build still not an exact match |
| Platform hints | MOCOR; `UMS9117` / Unisoc T107 family |

Reference phone (`*#0000#`): Nokia 2660 Flip, TA-1480, software **10.02.17.00**, NDT **`MH_13ARA80IN00_0`**, HWID **HW3.0** (India).

PAC NV file uses the same NDT token (`…nvitem_MH_13ARA80IN00.bin`) → same **Indian** SKU family. PAC header version is still `MOCOR_…W22.04.6_P5` (device earlier also showed `…W22.04.5_P4…` in OS string) — treat as related, not automatically flash-safe.

### Hashes

| Algorithm | Digest |
|-----------|--------|
| SHA-256 | `c0f7118eec52c1425dc810d5727c20cc965edd7d9840f2957138f3eb6f6af34a` |
| SHA-1 | `4761a819c7313e7022379ef06acaae13d0e020b6` |
| MD5 | `d198053c9003e13b821980272758d3f7` |

### Extraction

```bash
python3 tools/extract_pac.py firmware/v23_update_Nokia_2660.pac
```

Output: `firmware/extracted/v23_update_Nokia_2660/`

| Kind | Count | Notes |
|------|------:|-------|
| Table entries | 29 | includes erase/ops-only slots |
| Payloads written | 16 | loaders, NV, boot, kernel/user, DSP, PM, XML |
| Ops / empty | 13 | no file data in this PAC |

Largest images:

- `EXEC_KERNEL_IMAGE.bin` — 12,976,468 bytes  
- `EXEC_USER_IMAGE.bin` — 11,834,228 bytes  
- `mmi_res_240X320_MH.bin` — 8,489,672 bytes (240×320 UI resources)

NV item basename: `FF618V01` / `HMD_SKU3` / `QVGA` / **`MH_13ARA80IN00`** (matches device NDT).

Inventory + full per-file SHA-256: `extracted/v23_update_Nokia_2660/manifest.csv`  
Flash-tool scheme: `extracted/v23_update_Nokia_2660/UMS9117.xml`  
Human summary: [`docs/Flash Layout.md`](../docs/Flash%20Layout.md), [`docs/firmware.md`](../docs/firmware.md)

### Notes

- This is an update PAC, not a raw NAND dump from the phone.
- India-series only unless you verify another region’s NDT/PAC.
- Do not flash until software/PAC version match is confirmed.
- **Git:** `.pac` / `.bin` files are gitignored. Commit manifests (`manifest.csv`, `pac_header.json`, `UMS9117.xml`) only; keep binaries local after extract.

---

## Firmware 002 — Reference Device Dump (TA-1480, v10.02.17.00)

Authentic partition dump read directly from the physical reference device via `spd_dump` in download mode:

* **Model:** Nokia 2660 Flip (TA-1480, HW3.0, India NDT `MH_13ARA80IN00_0`)
* **Software Version:** `10.02.17.00`
* **MOCOR Build:** `MOCOR_20A_MP_W22.04.6_P4_Release`
* **Directory:** [`firmware/dumps/ta1480_10.02.17.00/`](dumps/ta1480_10.02.17.00/)
* **Manifest:** [`dumps/ta1480_10.02.17.00/manifest.csv`](dumps/ta1480_10.02.17.00/manifest.csv)

Includes: `boot0`, `boot1`, `kernel`, `user`, `nvitem`, `mmi_res`, `lte_dsp`, `gsm_dsp`, `pm_cm4`.
