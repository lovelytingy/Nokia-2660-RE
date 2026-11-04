# Hidden codes

Status: **partial** — India TA-1480 / software **10.02.17.00**

| Code | Effect | Build | Notes |
|------|--------|-------|-------|
| `*#0000#` | Product info | 10.02.17.00 | Shows name, TA-1480, NDT, HWID — see [device.md](device.md) |
| `*#06#` | IMEI display | Generic | Shows IMEI 1 & 2 + SV |
| `#*8378#9#` | TF Card Update | MOCOR / Unisoc | Looks for `/bl_update/tfload.pac` on FAT32 SD card (`8378` = `TEST`) |
| `*#2886#` | MMI / Factory Auto Test | MOCOR | Hardware self-test (LCD, keypad, audio, vibrator, camera) |
| `#*8378#0#` or `*#8378#` | Unisoc Engineer Mode | MOCOR / Unisoc | Diagnostic menus, band configuration, logging options |
| `*#7780#` | Soft Reset / Settings Reset | Generic | Default security lock code is usually `12345` |
| `*#7370#` | Full Factory Format | Generic | Wipes user data, requires security code `12345` |

## Needed

Other engineering / test / IMEI codes that work on the Indian 2660 Flip. Always note software version when testing.
