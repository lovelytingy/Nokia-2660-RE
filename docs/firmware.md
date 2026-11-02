# Firmware

## Market scope

This project tracks **India-launched** Nokia 2660 Flip (TA-1480) firmware / PAC material (NDT family `MH_13ARA80IN00…`). See [device.md](device.md).

## Sample: v23_update_Nokia_2660.pac

| Field | Value |
|-------|-------|
| Path | `firmware/v23_update_Nokia_2660.pac` |
| PAC version | `BP_R1.0.0` |
| Product | `UMS9117` |
| PAC product version | `MOCOR_MOCOR_20A_MP_W22.04.6_P5` |
| Flash type | NAND |
| Entries | 29 (16 with payloads) |
| Magic | `0xFFFAFFFA` |
| SHA-256 (PAC) | `c0f7118eec52c1425dc810d5727c20cc965edd7d9840f2957138f3eb6f6af34a` |
| SKU link | NV name contains **`MH_13ARA80IN00`** (= device NDT prefix) |

Reference device software from `*#0000#`: **10.02.17.00**. PAC MOCOR string is **W22.04.6_P5** — related Indian package, not proven identical to the running build.

Extracted tree: `firmware/extracted/v23_update_Nokia_2660/`

See also: [Flash Layout](Flash%20Layout.md), [firmware/readme.md](../firmware/readme.md).
