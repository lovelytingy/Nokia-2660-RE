# Nokia-2660-RE

Community reverse-engineering notes for the **Nokia 2660 4G (TA-1480)** — Unisoc T107 / **UMS9117** / MOCOR feature phone.

**Goal:** understand boot + download modes, map firmware, and learn whether **custom firmware** is realistically possible. Contributions welcome — see [CONTRIBUTING.md](CONTRIBUTING.md).

## Current reference device

| Field | Value |
|-------|-------|
| Model | TA-1480 |
| Software | 10.02.17.00 |
| Hardware | HW2.0 |
| OS | `MOCOR_20A_MP_W22.04.5_P4_Release` |
| Chipset | Unisoc T107 |
| Platform | UMS9117 |
| USB (normal) | `1782:4D00` |

## Milestones

- [x] Identify device / USB / chipset
- [x] Obtain first MOCOR PAC sample (not yet confirmed TA-1480 match)
- [x] Identify firmware format (`BP_R1.0.0` PAC)
- [x] Draft boot/download chain (FDL1 → FDL2; boot0 → boot1) — [docs/Boot Process.md](docs/Boot%20Process.md)
- [ ] Confirmed TA-1480 firmware dump
- [ ] Find application loader
- [ ] Execute custom code safely

## Repo map

| Path | What belongs here |
|------|-------------------|
| [`docs/`](docs/) | Human findings (boot, flash, USB, hardware, …) |
| [`firmware/`](firmware/) | PAC metadata + **extracted manifests** (binaries are gitignored) |
| [`tools/`](tools/) | Scripts (`extract_pac.py`, …) |
| [`ghidra/`](ghidra/) | How to import; local Ghidra DBs are gitignored |
| [`captures/`](captures/) | Guidance for USB/UART/photos |
| [`scripts/`](scripts/) | One-off helpers |

## Quick start

```bash
git clone https://github.com/lovelytingy/Nokia-2660-RE.git
cd Nokia-2660-RE
python3 -m pip install -r requirements.txt   # optional (Capstone)

# Download the PAC (see firmware/readme.md), place as:
#   firmware/v23_update_Nokia_2660.pac
python3 tools/extract_pac.py firmware/v23_update_Nokia_2660.pac
```

Useful reading order:

1. [firmware/readme.md](firmware/readme.md) — sample hashes + extract  
2. [docs/firmware.md](docs/firmware.md) / [docs/Flash Layout.md](docs/Flash%20Layout.md)  
3. [docs/Boot Process.md](docs/Boot%20Process.md) / [docs/bootrom.md](docs/bootrom.md)  
4. [ghidra/readme.md](ghidra/readme.md) — load FDL/boot0 in Ghidra  

## Tools (recommended)

| Tool | Why |
|------|-----|
| Python 3 | PAC extract + triage |
| [Ghidra](https://ghidra-sre.org/) | ARM Thumb RE of loaders |
| Capstone (`requirements.txt`) | Quick disasm without Ghidra |
| binwalk (optional) | Scan larger images for embedded blobs |
| USB capture (Wireshark / usbmon) | Download-mode protocol |

```bash
# macOS examples
brew install binwalk          # optional
brew install --cask ghidra    # if not already installed
```

## Disclaimer

**No warranty. No guarantee. We are not responsible if your device bricks.**

This project is research/education only. Flashing or modifying phones can permanently destroy them, void warranties, or cause data loss. You accept **full responsibility** for anything you do with these notes or tools.

Full text: [DISCLAIMER.md](DISCLAIMER.md).

Do not share IMEI, calibration, or personal data from NV dumps.

## License

Code and documentation in this repository are released under the [MIT License](LICENSE).

Upstream firmware remains the property of its respective owners — **do not** redistribute proprietary PAC/images in git; link + hash only.
