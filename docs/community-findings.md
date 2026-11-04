# Community Research & Tooling Findings

Documenting online findings, tested procedures, and open-source tooling for the **Nokia 2660 Flip (TA-1480)** and **Unisoc T107 / UMS9117** before physical connection to host.

---

## 1. Boot / Download Mode (BSL)

* **Physical Boot Key:** **`*` (Asterisk)** key (or **`*` + Hang Up / Power**).
* **Connection Procedure:**
  1. Unplug USB cable and remove the battery from the phone.
  2. Press and hold the **`*`** key on the keypad.
  3. While holding the key, plug the USB cable into the computer and phone.
  4. The phone enumerates over USB as **VID:PID `1782:4D00`** (*Spreadtrum / Unisoc BootROM mode*).
  5. The BootROM waits for host handshake commands (FDL1). If no handshake is received within ~2-5 seconds, the connection drops.
* **USB ID Clarification:** `1782:4D00` is **specifically the BootROM / BSL download mode**, not normal OS mode.

---

## 2. Hidden & Engineering Dialer Codes

Tested / known on MOCOR on Unisoc UMS9117:

| Code | Purpose | Details |
|------|---------|---------|
| `*#0000#` | Device & Firmware Info | Confirmed on TA-1480: shows name, SW version (`10.02.17.00`), NDT (`MH_13ARA80IN00_0`), HWID (`HW3.0`). |
| `*#06#` | IMEI display | Shows IMEI 1 & 2 + SV. |
| `#*8378#9#` | **TF Card Firmware Update** | Triggers SD card recovery flash mode without PC! Expects `/bl_update/tfload.pac` on FAT32 MicroSD (`8378` spells `TEST`). |
| `*#2886#` | MMI Auto Test | Factory hardware diagnostics (LCD, keypad, speakers, mic, vibrator, camera). |
| `#*8378#0#` / `*#8378#` | Unisoc Engineer Mode | Diagnostics, radio frequency band configuration, and logging options. |
| `*#7780#` / `*#7370#` | Factory Reset | Soft reset / Full wipe (default security PIN: `12345`). |

---

## 3. Flash & Dump Tooling: `spreadtrum_flash` / `spd_dump`

Community developer Ilya Kurdyukov maintains tools in `ilyakurdyukov/spreadtrum_flash`. In **Issue #44**, user `BrownCoatJustice` confirmed successful partition dumping on a **Nokia 2660 Flip (TA-1480 / UMS9117)**.

> [!NOTE]
> **Host OS Differences:** The project repository primarily documents and runs tests on **macOS**. Users on Linux or Windows will need to obtain or build the toolchain for their respective OS as outlined below.

### Building / Running `spd_dump` by OS

* **macOS (active project environment):**
  ```bash
  brew install libusb pkg-config
  git clone https://github.com/xun404/spd_dump-macos
  cd spd_dump-macos
  ./scripts/build-macos.sh
  ```
* **Linux:**
  ```bash
  sudo apt install libusb-1.0-0-dev pkg-config build-essential
  git clone https://github.com/ilyakurdyukov/spreadtrum_flash
  cd spreadtrum_flash
  make
  # Configure udev rule so root isn't required for 1782:4d00:
  echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1782", MODE="0666"' | sudo tee /etc/udev/rules.d/99-unisoc.rules
  sudo udevadm control --reload-rules && sudo udevadm trigger
  ```
* **Windows:**
  - Download precompiled `spd_dump64.exe` from `ilyakurdyukov/spreadtrum_flash/releases`.
  - Install the Unisoc/Spreadtrum USB driver package (e.g. *SciU2S* / Jungo driver) so the COM/BootROM device `1782:4D00` is recognized.

### Non-Destructive Partition Dump Command
To dump partition images directly from the phone over USB without modifying NAND:
```bash
./spd_dump \
  keep_charge 1 \
  fdl firmware/extracted/v23_update_Nokia_2660/fdl1.bin 0x6200 \
  blk_size 0x1000 \
  fdl firmware/extracted/v23_update_Nokia_2660/fdl2.bin 0x80100000 \
  read_flash 0x80000001 0 auto dump_boot0.bin \
  read_flash 0x80000002 0 auto dump_boot1.bin \
  read_flash 0x80000003 0 auto dump_kernel.bin \
  read_flash 0x80000004 0 auto dump_user.bin \
  read_flash 0x90000001 0 auto dump_nv.bin \
  read_flash 0x90000004 0 auto dump_mmi_res.bin
```

> [!IMPORTANT]
> - `fdl1.bin` and `fdl2.bin` from `v23_update_Nokia_2660.pac` (already unpacked in this repo) work for dumping across software versions.
> - Always perform a full read dump of the on-device partitions (`boot0`, `boot1`, `kernel`, `user`, `nv`) before attempting any write or flash operations.

---

## 4. Custom Code Execution: `fpdoom` & `sdboot_t117`

The project milestone of *“Execute custom code safely”* is proven feasible by community work:

1. **RAM Execution via USB:**
   Custom bare-metal or ported code can be executed in DRAM via `spd_dump` RAM loading (`./spd_dump fdl fdl1.bin 0x6200 fdl custom.bin ram`) without touching NAND flash.
2. **SD Card Bootloader (`sdboot_t117`):**
   `sdboot_t117` from `ilyakurdyukov/fpdoom` provides a dual-boot hook:
   * Sits in free space in firmware.
   * If special keys are held on power-on, it loads binaries (`fpbin/fpmain.bin`) and configs from a FAT32 MicroSD card.
3. **Hardware Display GPIO Config for Nokia 2660 Flip:**
   * **Main (inner) screen:** GPIO `-0x28,0x5f`, backlight `rgb=0x19`
   * **Cover (outer) screen:** GPIO `0x28,-0x5f`, backlight `w=0x2d`

---

## 5. Host Connection on macOS

* **Detection without specialized drivers:**
  ```bash
  # Check if device is enumerated in BootROM mode:
  ioreg -p IOUSB -l -w 0 | grep -E "idVendor|idProduct|1782"
  ```
* **Polling Loop:**
  ```bash
  while true; do
    ioreg -p IOUSB -l -w 0 | grep -i "1782" && break
    sleep 0.2
  done
  echo "Spreadtrum BootROM detected!"
  ```
