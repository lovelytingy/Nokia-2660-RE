# USB

Status: **partial**

## Observed (normal mode)

- VID:PID **`1782:4D00`** (Spreadtrum / Unisoc)

## Needed

- Full `lsusb -v` / Windows USB descriptors for **normal** and **download/BSL** modes
- Whether download mode uses a different PID
- Interface / endpoint layout (CDC, bulk-only, …)

See also [USB Enumeration](USB%20Enumeration.md) and [bootrom.md](bootrom.md).
