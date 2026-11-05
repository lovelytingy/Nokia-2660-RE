# MMI Resource Pack Architecture (`mmi_res.bin`)

## 1. Overview & Partition Purpose

The **`mmi_res.bin`** binary (8,912,896 bytes / 8.5 MB) is the primary user interface asset archive for the Nokia 2660 Flip. It is mounted by the MOCOR MMI application framework (`user.bin`) and read at boot to render:

1. **Boot & Power-off Splash Screens**: Pre-rendered 16-bit bitmaps for both the main QVGA display and the external cover display.
2. **GUI Theme & Graphics Assets (`tres0001`)**: Menu icons, softkey navigation bars, status indicators (cellular signal, battery meter, Bluetooth, VoLTE icons), wallpaper backgrounds, and dialog boxes.
3. **Localization & Fonts**: Language dictionaries, multi-lingual string indices, and font glyph raster data.
4. **Customer Configuration (`cscs`)**: Operator customizations, default MMS/WAP settings, and custom theme overrides.

---

## 2. Partition Header Layout (`sres0001`)

The image begins with the Unisoc **`sres0001`** (Spreadtrum Resource Pack v1) master container header:

| File Offset | Size (Bytes) | Field / Value | Description |
|:---:|:---:|:---|:---|
| `0x00000000` | 8 | `sres0001` | Magic container signature |
| `0x00000008` | 4 | `0x00000010` (16) | Master header descriptor length |
| `0x0000000C` | 4 | `0x00010500` | Format revision & resource count mask |
| `0x00000010` | 4 | `0x00000158` (344) | Byte offset to `tres0001` theme resource archive |
| `0x00000014` | 4 | `0x00794FA0` (7.58 MB)| Total length of active theme resource data |
| `0x00000018` | 260 | `0x00` (Padding) | Reserved header extension space |
| `0x0000011C` | 60 | **Resource Table** | 5× 12-byte partition descriptors (logos, lang, cscs) |
| `0x00000158` | — | **`tres0001` Data** | Theme resources, font assets, and GUI elements |

---

## 3. Top-Level Resource Directory Table

At file offset **`0x0000011C`**, a fixed array of 12-byte entries registers standalone system assets:

| Index | Tag | Offset | Size | Format | Description |
|:---:|:---:|:---:|:---:|:---:|:---|
| `0` | `logo` | `0x007950F8` | 4,108 bytes | BMP (RGB565) | **Main Display Boot Splash** (240×320 Nokia Connecting Hands) |
| `1` | `logo` | `0x00796110` | 2,230 bytes | BMP (RGB565) | **Cover Display Boot Splash** (128×160 Sub-Screen Nokia logo) |
| `2` | `logo` | `0x007969D4` | 4,108 bytes | BMP (RGB565) | **Main Display Power-Off Splash** (240×320 Shutdown animation) |
| `3` | `lang` | `0x007979EC` | 316 bytes | Binary Index | **Language Pack Descriptor** (English, Hindi, Urdu, etc.) |
| `4` | `cscs` | `0x00797B28` | 22,540 bytes | Custom XML/Bin | **Customer Specific Configuration** (Theme string tables) |

---

## 4. Boot Splash Image Structures

Unlike encrypted modern smartphones, the boot splashes inside `mmi_res.bin` are stored as **standard Windows BMP files** with 16-bit RGB565 pixel encoding:

### Main Display Boot Logo (`0x007950F8`)
* **Magic**: `42 4D` (`BM`)
* **Header Size**: 40 bytes (`BITMAPINFOHEADER`)
* **Dimensions**: **240 × 320** pixels (QVGA)
* **Color Depth**: **16 bits per pixel** (`0x0010`, BI_BITFIELDS / RGB565)
* **Raw BMP Header**:
  ```hex
  42 4d 0c 10 00 00 00 00 00 00 42 00 00 00 28 00 00 00
  f0 00 00 00 40 01 00 00 01 00 10 00 03 00 00 00
  ```
  - Width: `0x000000F0` (240 pixels)
  - Height: `0x00000140` (320 pixels)

### Cover Sub-Display Boot Logo (`0x00796110`)
* **Magic**: `42 4D` (`BM`)
* **Dimensions**: **128 × 160** pixels
* **Color Depth**: **16 bits per pixel** (RGB565)
* **Raw BMP Header**:
  ```hex
  42 4d b6 08 00 00 00 00 00 00 42 00 00 00 28 00 00 00
  80 00 00 00 a0 00 00 00 01 00 10 00 03 00 00 00
  ```
  - Width: `0x00000080` (128 pixels)
  - Height: `0x000000A0` (160 pixels)

> [!TIP] Custom Splash Customization
> Because these images are standard uncompressed RGB565 bitmaps at fixed flash offsets, custom boot logos can be injected directly into a firmware dump by replacing the payload bytes between `0x007950F8` – `0x00796103` without breaking filesystem integrity.

---

## 5. Theme Resource Container (`tres0001`)

The core user interface bundle resides in the `tres0001` sub-archive starting at offset **`0x00000158`**:

* **Signature**: `tres0001` (8 bytes)
* **Header Size**: 16 bytes (`0x10`)
* **Archive Size**: 7,950,240 bytes (`0x00794FA0`)
* **Resource Group Count**: **792 indexed groups** (`0x00000318`)
* **Offset Table (`0x00000168` – `0x00000DC8`)**: A contiguous array of 792 32-bit pointers referencing each asset chunk:
  - `0x003EDB9C`: System font table (Latin glyphs, Devnagari glyphs)
  - `0x004E4CBC`: Main menu icon matrix (Phonebook, Messages, Call History, Camera)
  - `0x005175A4`: Battery and antenna signal gauge animations
  - `0x00528F4C`: Status bar indicator icons (Bluetooth, Alarm, Silent, Headset)
  - `0x0053D834`: In-call UI controls and volume sliders

---

## 6. Language & Localization Table (`lang`)

Offset **`0x007979EC`** contains the multilingual language selection table utilized by the Settings menu:

```
0000: 00 00 00 00 00 00 00 00 45 6e 67 6c 69 73 68 00   ........English.
0010: 00 00 00 00 00 00 00 00 01 00 00 00 48 69 6e 64   ............Hind
0020: 69 00 00 00 00 00 00 00 02 00 00 00 55 72 64 75   i...........Urdu
```

Each language entry defines:
- **Language ID (`uint32`)**: Numeric identifier mapped to ISO 639-1 language code.
- **Name String (`char[16]`)**: ASCII display string rendered in language selection menus.
- **String Bank Pointer**: Memory offset into the string table containing translated UI strings.
