# Nokia-2660-RE

Community reverse-engineering notes for the **Nokia 2660 Flip (TA-1480)** — India series — Unisoc T107 / **UMS9117** / MOCOR feature phone.

**Goal:** understand boot + download modes, map firmware, and learn whether **custom firmware** is realistically possible. Contributions welcome — see [CONTRIBUTING.md](CONTRIBUTING.md).

## Current reference device

From dialer **`*#0000#`** on a real unit (India):

| Field | Value |
|-------|-------|
| Name | Nokia 2660 Flip |
| Type designator | TA-1480 |
| Software version | 10.02.17.00 |
| NDT code | `MH_13ARA80IN00_0` |
| HWID | **HW3.0** |
| Market | India-launched |
| Chipset / platform | Unisoc T107 / UMS9117 |
| USB (normal) | `1782:4D00` |

Full device page: [docs/device.md](docs/device.md).

**Note:** PAC / NV work here targets the **Indian** series (`MH_13ARA80IN00…`). Other countries may differ.

## Milestones

- [x] Identify device / USB / chipset
- [x] Obtain MOCOR PAC sample (NV name matches Indian NDT `MH_13ARA80IN00`)
- [x] Identify firmware format (`BP_R1.0.0` PAC)
- [x] Draft boot/download chain (FDL1 → FDL2; boot0 → boot1) — [docs/Boot Process.md](docs/Boot%20Process.md)
- [ ] Version-matched TA-1480 dump / confirmed-safe flash package
- [ ] Find application loader
- [ ] Execute custom code safely

## Repo map

| Path | What belongs here |
|------|-------------------|
| [`docs/`](docs/) | Human findings (device, boot, flash, USB, hardware, …) |
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

1. [docs/device.md](docs/device.md) — reference phone (`*#0000#`)  
2. [docs/community_findings.md](docs/community_findings.md) — boot key, dialer codes & safe dump instructions  
3. [firmware/readme.md](firmware/readme.md) — sample hashes + extract  
4. [docs/firmware.md](docs/firmware.md) / [docs/Flash Layout.md](docs/Flash%20Layout.md)  
5. [docs/Boot Process.md](docs/Boot%20Process.md) / [docs/bootrom.md](docs/bootrom.md)  
6. [ghidra/readme.md](ghidra/readme.md) — load FDL/boot0 in Ghidra  

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

## Host Environment & OS Compatibility

> [!NOTE]
> **Primary Host Environment: macOS**  
> This project is actively developed and tested on **macOS** (Apple Silicon / Darwin).
>
> Please note if you are using another operating system:
> - **Script & Command Differences:** Some shell snippets, USB polling scripts, and system utilities (such as `ioreg` or `system_profiler`) are macOS-specific and will need to be adapted for Linux (e.g. `lsusb`, `udev`) or Windows (PowerShell, Device Manager).
> - **OS-Specific Tools & Drivers:**
>   - **macOS:** Uses `libusb` (`brew install libusb pkg-config`). No kernel extensions needed for BootROM mode.
>   - **Linux:** Requires `udev` rules for Spreadtrum/Unisoc vendor ID `1782` and building tools like `spreadtrum_flash` via native `make`.
>   - **Windows:** Requires Spreadtrum USB / SCI / Jungo drivers (`SciU2S` driver package) and Windows builds of utilities (e.g. `spd_dump64.exe`, ResearchDownload, or SPD Upgrade Tool).
> - **Contributions:** PRs adding or improving cross-platform compatibility for Linux and Windows are very welcome!

## Disclaimer

**No warranty. No guarantee. We are not responsible if your device bricks.**

This project is research/education only. Flashing or modifying phones can permanently destroy them, void warranties, or cause data loss. You accept **full responsibility** for anything you do with these notes or tools.

Full text: [DISCLAIMER.md](DISCLAIMER.md).

Do not share IMEI, calibration, or personal data from NV dumps.

## License

Code and documentation in this repository are released under the [MIT License](LICENSE).

Upstream firmware remains the property of its respective owners — **do not** redistribute proprietary PAC/images in git; link + hash only.
