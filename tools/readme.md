# Tools

Stable scripts, one-off helpers, and bare-metal RAM payloads.

## Scripts

| Script | Purpose |
|--------|---------|
| `extract_pac.py` | Unpack Unisoc/Spreadtrum `BP_R1.0.0` `.pac` → images + `manifest.csv` |
| `dump_device.sh` | Dump firmware partitions from Nokia 2660 Flip over USB using `spd_dump` |
| `extract_strings.py` | Extract, filter noise, and categorize strings from firmware binaries into reverse engineering domains |
| `mocor-info.py` | Parse Unisoc DHTB headers, RSA certificate chains, RTOS task tables, peripheral drivers, and partition layouts |
| [`emulator/ums9117_emu.py`](emulator/README.md) | Unicorn-based ARM Cortex-A7 CPU & peripheral hardware emulator for local payload and loader testing |

### Firmware Unpacking & Dumping

```bash
# Unpack official PAC update package:
python3 tools/extract_pac.py firmware/v23_update_Nokia_2660.pac
# writes firmware/extracted/<pac-stem>/

# Dump partitions from physical phone over USB:
./tools/dump_device.sh
# writes firmware/dumps/ta1480_<version>/
```

### String Extraction & Categorization (`extract_strings.py`)

Extracts ASCII and UTF-16LE strings, filters machine-code noise heuristics, and automatically categorizes strings into reverse-engineering domains (secret dialer codes `*#...#`, 3GPP and Spreadtrum AT commands, RTOS tasks, LCD & peripheral drivers, network/APN/IMS configs, security/certificates, file paths, URLs, and error diagnostics):

```bash
# High-level categorized summary overview:
python3 tools/extract_strings.py firmware/dumps/ta1480_10.02.17.00/user.bin

# Dump specific domain categories with file offsets:
python3 tools/extract_strings.py firmware/dumps/ta1480_10.02.17.00/user.bin -c dialer at_cmd --offsets

# Search for specific peripheral or driver terms:
python3 tools/extract_strings.py firmware/dumps/ta1480_10.02.17.00/kernel.bin -g "GC9307|ST7789"

# Export structured categorized database as JSON:
python3 tools/extract_strings.py firmware/dumps/ta1480_10.02.17.00/kernel.bin --json kernel_strings.json
```

### MOCOR OS & Header Inspection (`mocor-info.py`)

Parses Unisoc DHTB signed headers, RSA-2048 certificate chains, exception vector tables, RTOS task control lists, display controllers, and partition maps directly from raw partition dumps or PAC update trees:

```bash
# Inspect a single binary (kernel, user, boot0, boot1):
python3 tools/mocor-info.py firmware/dumps/ta1480_10.02.17.00/kernel.bin

# View full verbose details (all RTOS tasks, full RSA public key modulus):
python3 tools/mocor-info.py firmware/dumps/ta1480_10.02.17.00/boot0.bin -v

# Audit an entire firmware dump directory or extracted PAC:
python3 tools/mocor-info.py firmware/dumps/ta1480_10.02.17.00/
python3 tools/mocor-info.py firmware/extracted/v23_update_Nokia_2660/

# Export complete structured audit report to JSON:
python3 tools/mocor-info.py firmware/dumps/ta1480_10.02.17.00/kernel.bin --json kernel_audit.json
```

## RAM Payloads

See [`ram_payload/README.md`](ram_payload/README.md) for bare-metal DRAM injection payloads (hello world, keypad, flashlight, haptics, audio, hardware secure boot audit).

## External Dependencies

| Tool | Install | Notes |
|------|---------|-------|
| `spd_dump` | Build from [ilyakurdyukov/spreadtrum_flash](https://github.com/ilyakurdyukov/spreadtrum_flash) | Required for `dump_device.sh` and RAM payload injection |
| `arm-none-eabi-gcc` | `brew install arm-none-eabi-gcc` (macOS) | Required to build RAM payloads from source |

