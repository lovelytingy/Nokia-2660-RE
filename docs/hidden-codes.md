# Hidden Codes & Engineering Menus

Status: **Verified from firmware binary analysis (`user.bin`) and hardware testing on TA-1480 (software 10.02.17.00, HW3.0)**.

The Nokia 2660 Flip (MOCOR OS / Unisoc UMS9117) contains several classes of dialer codes:
1. **Unisoc / Spreadtrum Factory & Engineering Diagnostic Menus**
2. **Device Information & Regulatory Compliance**
3. **Master Reset & Data Erasure**
4. **Direct Country Code / Language Switching Shortcuts**

---

## 1. Engineering & Diagnostic Modes

These codes access Unisoc's underlying factory test modes, RF diagnostic menus, and debug logging.

| Code | Name / Purpose | Binary Offset (`user.bin`) | Description / Behavior |
|------|----------------|----------------------------|------------------------|
| `*#*#372733#*#` | **Spreadtrum Engineer Mode** | Pattern `372733` | Opens the Unisoc Field Test / Engineering menu (`372733` spells `DSPREF` / `EPAPEE`). Provides cellular network parameters (RSRP, RSRQ, EARFCN, PCI, Band info), SIM diagnostics, audio parameter tuning, and battery ADC telemetry. |
| `*#8378*733425*1#` | **Enable Unisoc Factory Mode / RefLog** | `0x009B7148` (ASCII) | Enables internal Unisoc diagnostic and reference logging (`8378` = `TEST`, `733425` = `RELEASE` / `REFLOG`). Activates extra debug outputs over UART/USB. |
| `*#8378*733425*0#` | **Disable Unisoc Factory Mode / RefLog** | `0x009B715C` (ASCII) | Disables internal diagnostic logging and returns the device to retail logging state. |
| `*#2886#` | **MMI / Factory Auto Test** | MMI Dispatch | Hardware test loop (`2886` = `AUTO`). Sequentially tests Main LCD, Sub LCD, Keypad matrix, Earpiece, Speaker, Vibrator motor, Camera, Flashlight, and SD card slot. |
| `*#0886#` | **Auto-Test Menu Shortcut** | `0x00600BA4` (ASCII) | Alternative direct access to factory hardware self-test sequences. |
| `*#445566#` | **Unisoc Debug Menu Toggle** | `0x00600B88` (ASCII) | Spreadtrum internal test toggle for production line inspection. |
| `*#6774#` | **Production Test Mode A** | `0x00600B70` (ASCII) | Factory diagnostic menu used during assembly line verification. |
| `*#6789#` | **Production Test Mode B** | `0x00600B58` (ASCII) | Factory RF calibration and testing verification mode. |
| `#*8378#9#` | **TF Card Firmware Recovery** | MOCOR Dispatch | Direct PAC flasher stub. Scans the FAT32 microSD card for `/bl_update/tfload.pac` and attempts local firmware update if validly signed. |
| `#*8378#0#` or `*#8378#` | **Legacy Unisoc Test Shortcut** | MOCOR Dispatch | Quick shortcut to the top-level test menu. |

---

## 2. Device Information & Regulatory Codes

| Code | Name | Notes |
|------|------|-------|
| `*#0000#` | **Firmware Version & Hardware Info** | Displays marketing name (`Nokia 2660 Flip`), model (`TA-1480`), software version (`10.02.17.00`), build date, hardware revision (`HW3.0`), and NDT code (`MH_13ARA80IN00_0`). |
| `*#06#` | **IMEI & Software SV Display** | Shows IMEI 1 (SIM 1), IMEI 2 (SIM 2), and Software Version (SV) barcode/numbers. |
| `*#07#` | **SAR / Regulatory Compliance** | Displays Specific Absorption Rate (SAR) values, regional regulatory certifications, and compliance markings. |

---

## 3. Master Reset & Factory Restore

> [!CAUTION]
> The reset codes below will wipe user storage, contacts, and custom settings. Default phone lock code is typically `12345`.

| Code | Name | Binary Offset (`user.bin`) | Behavior |
|------|------|----------------------------|----------|
| `*#7370#` | **Nokia Full Factory Format** | `0x00600B68` (ASCII) | Full factory reset. Erases flash user partition, wipes call logs, SMS, phonebook, and resets all NVRAM parameters to factory defaults. Prompts for security lock code (default: `12345`). |
| `*#7780#` | **Soft Settings Reset** | MOCOR Dispatch | Restores system settings, wallpaper, and ringtones to defaults without wiping user personal files. Prompts for security lock code (default: `12345`). |

---

## 4. Language & Country Switching Shortcuts

The MOCOR user partition contains a contiguous lookup table of direct country dialing codes at offset `0x00600B94` – `0x00600C84` in [`firmware/dumps/ta1480_10.02.17.00/user.bin`](file:///Users/lovelytingy/Documents/Personal-Projects/Nokia-2660-RE/firmware/dumps/ta1480_10.02.17.00/user.bin). Entering these dialer codes immediately switches the phone's UI language to the specified country:

| Dialer Code | Language / Country Target | Binary Offset (`user.bin`) |
|-------------|---------------------------|----------------------------|
| `*#0044#` | English (United Kingdom) | `0x00600B94` |
| `*#0086#` | Simplified Chinese (China) | `0x00600B9C` |
| `*#0091#` | Hindi / English (India) | `0x00600BBC` |
| `*#0033#` | French (France) | `0x00600BB4` |
| `*#0049#` | German (Germany) | `0x00600C14` |
| `*#0034#` | Spanish (Spain) | `0x00600BEC` |
| `*#0039#` | Italian (Italy) | `0x00600C04` |
| `*#0007#` | Russian (Russia) | `0x00600BE4` |
| `*#0048#` | Polish (Poland) | `0x00600C34` |
| `*#0031#` | Dutch (Netherlands) | `0x00600C54` |
| `*#0045#` | Danish (Denmark) | `0x00600C24` |
| `*#0046#` | Swedish (Sweden) | `0x00600C5C` |
| `*#0047#` | Norwegian (Norway) | `0x00600C44` |
| `*#0358#` | Finnish (Finland) | `0x00600C3C` |
| `*#0351#` | Portuguese (Portugal) | `0x00600BDC` |
| `*#0030#` | Greek (Greece) | `0x00600C1C` |
| `*#0036#` | Hungarian (Hungary) | `0x00600BC4` |
| `*#0040#` | Romanian (Romania) | `0x00600C6C` |
| `*#0060#` | Malaysian (Malaysia) | `0x00600BD4` |
| `*#0062#` | Indonesian (Indonesia) | `0x00600BCC` |
| `*#0066#` | Thai (Thailand) | `0x00600BF4` |
| `*#0084#` | Vietnamese (Vietnam) | `0x00600BFC` |
| `*#0090#` | Turkish (Turkey) | `0x00600C0C` |
| `*#0359#` | Bulgarian (Bulgaria) | `0x00600C84` |
| `*#0385#` | Croatian (Croatia) | `0x00600C64` |
| `*#0386#` | Slovenian (Slovenia) | `0x00600C74` |
| `*#0420#` | Czech (Czech Republic) | `0x00600C2C` |
| `*#0421#` | Slovak (Slovakia) | `0x00600C4C` |
| `*#0966#` | Arabic (Saudi Arabia) | `0x00600BAC` |
| `*#0972#` | Hebrew (Israel) | `0x00600C7C` |

---

## 5. Verification Command

To extract and verify the entire table directly from your dumped firmware image:

```bash
python3 tools/extract_strings.py firmware/dumps/ta1480_10.02.17.00/user.bin -c dialer --offsets
```
