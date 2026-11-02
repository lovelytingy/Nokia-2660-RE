# USB Enumeration

Status: **verified from real TA-1480 hardware (HW3.0, SW 10.02.17.00)**

---

## Mode Comparison

| Mode | Trigger | USB VID:PID | Manufacturer / Product | Class / Protocol | Notes |
|------|---------|-------------|------------------------|------------------|-------|
| **Charge Only** | Plug in while on, select "Charge only" | *None* | *None* | Disables data PHY | Draws VBUS charging power only; does not enumerate on host |
| **Normal (Phone Memory / U-Disk)** | Plug in while on, select "Phone memory" | **`1782:4D01`** | `Spreadtrum` / `Generic Disk1.0` | `08:06:50` (Mass Storage, SCSI, Bulk-Only) | Mounts internal FAT12 volume (~32.4 MB) |
| **Download / BSL Mode** | Battery out, hold `*`, plug USB | **`1782:4D00`** | `Spreadtrum` / `SPRD BootROM` | Vendor-Specific (FDL protocol) | Bootloader flashing interface; times out in ~2-5s if no FDL1 handshake |

---

## 1. Normal Mode (Mass Storage: "Phone memory" / U-Disk)

Captured on macOS (`system_profiler SPUSBHostDataType` & `ioreg`):

```text
Generic Disk1.0:
  Location ID:            0x03110000
  Connection Type:        Removable
  Manufacturer:           Spreadtrum
  Serial Number:          EFF7318C6310
  Link Speed:             480 Mb/s (USB 2.0 High-Speed)
  USB Vendor ID:          0x1782 (Spreadtrum Communications Inc.)
  USB Product ID:         0x4d01
  USB Product Version:    0x0001 (bcdDevice 1)
  bcdUSB:                 0x0200 (USB 2.0)
  bDeviceClass:           0x00 (Defined at interface level)
  bDeviceSubClass:        0x00
  bDeviceProtocol:        0x00
  bMaxPacketSize0:        64 bytes
  bNumConfigurations:     1
```

### Interface 0: Mass Storage
* **Class:** `0x08` (Mass Storage)
* **SubClass:** `0x06` (SCSI transparent command set)
* **Protocol:** `0x50` (Bulk-Only Transport)
* **Endpoints:** 2 (Bulk IN, Bulk OUT)
* **SCSI Inquiry String:** `Generic LOCAL DISK`

### Storage Volume Mount
* **Device Node:** `/dev/disk4`
* **Volume Name:** `Nokia 2660F`
* **Mount Point:** `/Volumes/Nokia 2660F`
* **File System:** MS-DOS FAT12 (Allocation block size: 16 KB)
* **Total Size:** 32.4 MB (`32,385,024` bytes / 63,252 sectors of 512 bytes)
* **PAC Partition Match:** Matches `<Base>0x80000008</Base>` (`PreloadUdisk` / `Erase FS` in `UMS9117.xml`).

### Volume Directory Tree
```text
/Volumes/Nokia 2660F/
├── Alarm/
├── Audio/
├── Browser/
│   └── cookie.dat
├── DRM_LRO/
├── Ebook/
├── Filearray/
│   └── VIRTUALARRAY_* (MOCOR database files)
├── OperaMiniCustom/
│   └── mini4/
│       ├── enteredlinks.ini
│       ├── history.ini
│       ├── iconstorage.ini
│       ├── keychain.ini
│       ├── reksio.ini
│       ├── speeddials.ini
│       └── trmkii.dat
├── Others/
├── Photos/
├── System/
│   ├── Audio/ (mp3_history.dat)
│   ├── MMS/ (mmslist.ini)
│   ├── Mp3_res/ (*.lst playlists)
│   ├── Video/ (movie_historylist.dat)
│   └── moreringset/ (envset.ini)
├── Video/
├── vCalendar/
└── vCard/
```

