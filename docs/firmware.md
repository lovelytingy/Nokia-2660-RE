# Firmware

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

Device under study reports `MOCOR_20A_MP_W22.04.5_P4_Release` — **near** this PAC (`W22.04.6_P5`), but TA-1480 match is still unconfirmed.

NV payload filename inside the PAC references `FF618V01`, `QVGA`, `HMD_SKU3` (HMD Global / Nokia feature-phone lineage).

Extracted tree: `firmware/extracted/v23_update_Nokia_2660/`

See also: [Flash Layout](Flash%20Layout.md), [firmware/readme.md](../firmware/readme.md).
