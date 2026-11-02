# USB

Status: **partially documented — verified via community findings**

## Identifiers & Modes

### 1. Download / BootROM Mode (BSL)
* **VID:PID:** **`1782:4D00`** (Spreadtrum / Unisoc BootROM mode)
* **Trigger:** Remove battery, hold **`*`** (or **`*` + Hang Up**), insert USB.
* **Role:** Talks to FDL1/FDL2 flash loaders or tools like `spd_dump`.
* **Behavior:** Enumerates as a vendor-specific serial/bulk interface. If no handshake is initiated by host software within ~2-5 seconds, the BootROM times out.

### 2. Normal Mode (Powered On)
* When plugged in while powered on, MOCOR prompts on the screen:
  * **Mass Storage:** Exposes FAT-formatted MicroSD card as USB Mass Storage (MSC).
  * **Charge Only:** Disables data endpoints or charges only.
  * **Engineering/Diag Mode:** If enabled via hidden USSD code, exposes Spreadtrum Modem / Diag COM endpoints (`1782:4002` or similar).

## Host Connection on macOS

On macOS:
* Check USB presence instantly:
  ```bash
  ioreg -p IOUSB -l -w 0 | grep -E "idVendor|idProduct|1782"
  ```
  or:
  ```bash
  system_profiler SPUSBDataType
  ```
* `libusb` (e.g. from Homebrew) can claim the interface directly for `spd_dump`.

See also [USB Enumeration](USB%20Enumeration.md) and [bootrom.md](bootrom.md).
