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
| Status | **Not confirmed** compatible with Nokia 2660 4G TA-1480 |
| Platform hints | MOCOR; matches README platform `UMS9117` / Unisoc T107 family |

Device under study: `MOCOR_20A_MP_W22.04.5_P4_Release` — close to this package (`W22.04.6_P5`), still unverified as TA-1480.

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

NV item basename references `FF618V01` / `HMD_SKU3` / `QVGA`.

Inventory + full per-file SHA-256: `extracted/v23_update_Nokia_2660/manifest.csv`  
Flash-tool scheme: `extracted/v23_update_Nokia_2660/UMS9117.xml`  
Human summary: [`docs/Flash Layout.md`](../docs/Flash%20Layout.md), [`docs/firmware.md`](../docs/firmware.md)

### Notes

- This is an update PAC, not a raw NAND dump from a TA-1480.
- Do not flash until model / version match is confirmed.
- **Git:** `.pac` / `.bin` files are gitignored. Commit manifests (`manifest.csv`, `pac_header.json`, `UMS9117.xml`) only; keep binaries local after extract.
