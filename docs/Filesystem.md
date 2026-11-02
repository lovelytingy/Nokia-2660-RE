# Filesystem

Status: **partially documented — verified from TA-1480 hardware mount**

MOCOR feature-phone internal flash storage is not an Android `ext4` or `erofs` image. It uses Unisoc FTL (Flash Translation Layer) managing raw NAND blocks, carving out FAT-compatible volumes and custom binary resources.

---

## 1. Internal User Disk (`PreloadUdisk` / `Erase FS`)

When the phone is connected via USB and "Phone memory" is chosen, the internal user data disk is mounted:

* **Partition Base:** `0x80000008` (`PreloadUdisk` in `UMS9117.xml`)
* **Format:** MS-DOS FAT12 (16 KB cluster allocation)
* **Total Size:** 32.4 MB (`32,385,024` bytes)
* **Volume Label:** `Nokia 2660F`

### Directory Structure & Purpose

| Directory | Contents | Purpose / Notes |
|-----------|----------|-----------------|
| `OperaMiniCustom/mini4/` | `speeddials.ini`, `history.ini`, `reksio.ini`, `enteredlinks.ini`, `trmkii.dat` | Built-in Opera Mini 4 browser config, persistent user cache, and bookmarks |
| `System/` | `envset.ini`, `mmslist.ini`, `mp3_history.dat`, `movie_historylist.dat` | Core MOCOR userland settings, ringtone environments, MMS store, and playback histories |
| `Filearray/` | `VIRTUALARRAY_*` | Binary virtual array databases used by MOCOR for contact/message indexes |
| `Browser/` | `cookie.dat` | Stock web browser cookies |
| `Alarm/` | User alarm audio files | User storage |
| `Audio/` | Recorded or transferred music/audio | User storage |
| `Ebook/` | `.txt` documents | E-book reader directory |
| `Photos/` | Camera captures | User storage |
| `Video/` | Recorded videos | User storage |
| `DRM_LRO/` | DRM rights objects | License storage |
| `vCalendar/`, `vCard/` | `.vcf`, `.vcs` files | Contact and calendar import/export |

---

## 2. External Storage (MicroSD)

* Supports FAT32 formatted MicroSD cards up to 32 GB.
* **Firmware Update Hook:** If `/bl_update/tfload.pac` is present on the SD card, dialing `#*8378#9#` on the phone initiates the TF update bootloader mode.
* **Custom Code Booting (`sdboot_t117`):** The community SD-card bootloader reads executable payloads (`fpbin/fpmain.bin`) and configuration files directly from the root of a FAT32 MicroSD card.

---

## 3. Flash Resources

* `mmi_res_240X320_MH.bin` (`0x90000004`): Packed UI resources (icons, fonts, localization strings for Indian region `MH`).
* `EXEC_USER_IMAGE.bin` (`0x80000004`): MOCOR userspace binary containing application code.
* `EXEC_KERNEL_IMAGE.bin` (`0x80000003`): Kernel, RTOS tasks, and drivers.
