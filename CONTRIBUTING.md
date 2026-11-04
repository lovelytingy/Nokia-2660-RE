# Contributing

Thanks for helping reverse-engineer the **Nokia 2660 Flip (TA-1480), India series**. More eyes make this much easier.

Primary reference: software `10.02.17.00`, NDT `MH_13ARA80IN00_0`, HWID `HW3.0` — see [`docs/device.md`](docs/device.md).

## What we are trying to do

Document hardware and firmware, map boot/download modes, understand the MOCOR OS, and learn whether **safe custom firmware** is possible. Prefer evidence (dumps, captures, hashes) over guesses.

## Ground rules

- Read [DISCLAIMER.md](DISCLAIMER.md): **no warranty**; contributors are **not responsible** for bricks or damage.
- **Do not** flash unconfirmed packages to a daily-driver phone.
- **Do not** commit large binaries (`.pac`, `.bin`, pcaps). Document SHA-256 + where to get them instead.
- Prefer small, focused PRs (one topic: docs, tool, analysis note).
- Say what you observed vs what you inferred.
- If you dump NV or calibration data, **redact IMEI / secrets** before sharing.

## Quick start

```bash
git clone https://github.com/lovelytingy/Nokia-2660-RE.git
cd Nokia-2660-RE

# optional analysis deps
python3 -m pip install -r requirements.txt

# place the PAC outside git or under firmware/ (gitignored)
# then:
python3 tools/extract_pac.py firmware/v23_update_Nokia_2660.pac
```

Firmware details and hashes: [`firmware/readme.md`](firmware/readme.md).

## Good first contributions

| Area | Ideas |
|------|--------|
| Docs | Fill empty pages under `docs/` (`hardware`, `usb-enumeration`, `memory-map`, …) |
| Device | USB descriptors in download vs normal mode; hidden codes; UART pins |
| Firmware | String notes on `boot1`, kernel/user images; partition map confirmation |
| Tools | Improve `tools/extract_pac.py`; add helpers for DHTB / strings / FDL |
| Ghidra | Share symbol maps or annotated notes (not the whole project DB) |
| Cross-Platform | Port macOS-specific shell snippets (`ioreg`) to Linux (`lsusb`/`udev`) or Windows |

## Host OS & Cross-Platform Note

The primary environment currently used for testing and development in this repository is **macOS** (Darwin / Apple Silicon). 

Please keep in mind:
- Some scripts, commands, and one-liners in `docs/` and `tools/` may use macOS utilities (like `ioreg` or `system_profiler`) or Homebrew paths.
- Contributors on **Linux** or **Windows** will need tools and drivers tailored to their OS (e.g. Spreadtrum `SciU2S` drivers on Windows, `udev` rules and `libusb-1.0` on Linux).
- Contributions that test, adapt, and document tools for Linux and Windows are highly encouraged!

## Repo layout

```text
docs/        findings (human-readable)
firmware/    PAC notes + extracted manifests (binaries gitignored)
tools/       scripts, helpers, and bare-metal RAM payloads
ghidra/      import notes + optional scripts
captures/    how to store USB/UART/photos (raw files usually ignored)
```

## Analysis tips

1. Extract the PAC; trust `manifest.csv` hashes.
2. Loaders use a `DHTB` header (`0x4B4` bytes) then Thumb code — see `docs/boot-process.md`.
3. Prefer load addresses from `UMS9117.xml` (FDL1 `0x6200`, FDL2 `0x80100000`).
4. Ghidra: import stripped payloads; keep `.gpr` projects local (gitignored).

## Commit style

Short imperative subjects, focused on why:

- `docs: record FDL1 verify-fdl2 chain`
- `tools: extract PAC file table + hashes`

## Questions / coordination

Open a GitHub issue for:

- “I have a TA-1480 on build X”
- “Download-mode PID is …”
- “UART TX is test point …”

Link evidence (hashes, truncated logs, photos) when you can.
