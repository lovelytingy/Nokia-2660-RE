# USB

Status: **partially documented — verified via community findings**

## Identifiers & Modes

### 1. Download / BootROM Mode (BSL)
* **VID:PID:** **`1782:4D00`** (Spreadtrum / Unisoc BootROM mode)
* **Trigger:** Remove battery, hold **`*`** (or **`*` + Hang Up**), insert USB.
* **Role:** Talks to FDL1/FDL2 flash loaders or tools like `spd_dump`.
* **Behavior:** Enumerates as a vendor-specific serial/bulk interface. If no handshake is initiated by host software within ~2-5 seconds, the BootROM times out.

### 2. Normal Mode (Powered On)
* **Charge Only:** Disables USB PHY data lines; device draws charging current only without enumerating on host.
* **Mass Storage ("Phone memory" / U-Disk):**
  * **VID:PID:** **`1782:4D01`**
  * **Product String:** `Generic Disk1.0`
  * **Manufacturer:** `Spreadtrum`
  * **Behavior:** Exposes the internal 32.4 MB FAT12 volume (`/Volumes/Nokia 2660F`) corresponding to partition `0x80000008` (`PreloadUdisk`).
* **Engineering/Diag Mode:** If enabled via hidden USSD code, exposes Spreadtrum Modem / Diag COM endpoints (`1782:4002` or similar).

## Host Connection on macOS

On macOS:
* Check USB presence instantly:
  ```bash
  ioreg -p IOUSB -l -w 0 | grep -E "idVendor|idProduct|1782"
  ```
  or:
  ```bash
  system_profiler SPUSBHostDataType
  ```
* `libusb` (e.g. from Homebrew) can claim the interface directly for `spd_dump`.

See also [USB Enumeration](USB%20Enumeration.md) and [bootrom.md](bootrom.md).
