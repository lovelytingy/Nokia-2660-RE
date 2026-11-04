# Tools

Stable scripts, one-off helpers, and bare-metal RAM payloads.

## Scripts

| Script | Purpose |
|--------|---------|
| `extract_pac.py` | Unpack Unisoc/Spreadtrum `BP_R1.0.0` `.pac` → images + `manifest.csv` |
| `dump_device.sh` | Dump firmware partitions from Nokia 2660 Flip over USB using `spd_dump` |

```bash
python3 tools/extract_pac.py firmware/v23_update_Nokia_2660.pac
# writes firmware/extracted/<pac-stem>/
```

## RAM Payloads

See [`ram_payload/README.md`](ram_payload/README.md) for bare-metal DRAM injection payloads (hello world, keypad, flashlight, haptics, audio).

## External Dependencies

| Tool | Install | Notes |
|------|---------|-------|
| `spd_dump` | Build from [ilyakurdyukov/spreadtrum_flash](https://github.com/ilyakurdyukov/spreadtrum_flash) | Required for `dump_device.sh` and RAM payload injection |
| `arm-none-eabi-gcc` | `brew install arm-none-eabi-gcc` (macOS) | Required to build RAM payloads from source |

## Stubs — Help Wanted

These scripts are placeholders awaiting implementation:

| Script | Intended Purpose |
|--------|-----------------|
| `extract_strings.py` | Extract and categorize human-readable strings from firmware images |
| `mocor-info.py` | Parse and display MOCOR OS metadata from kernel/user images |

Contributions welcome — see [CONTRIBUTING.md](/CONTRIBUTING.md).
