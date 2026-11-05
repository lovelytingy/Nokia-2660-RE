# Non-Volatile Parameter Architecture (`nvitem.bin`)

## 1. Overview & Storage Architecture

The **`nvitem.bin`** binary (1,048,576 bytes / 1.0 MB) represents the primary Non-Volatile Storage (NVRAM) partition for the Nokia 2660 Flip. Managed by the MOCOR kernel driver `nvitem.c`, this partition maintains all factory-calibrated radio frequency parameters, acoustic DSP tuning tables, hardware calibration constants, device identifiers, and carrier policy configurations.

In Unisoc architecture, NV storage is organized into distinct logical banks:
1. **Calibration Bank (`pNVItem_cali1`)**: Read-only factory calibration data written during manufacturing line test (RF power, crystal frequency offset, battery ADC).
2. **System Configurations (`pNVItem_sys1`, `sys2`, `sys3`)**: Modem operational settings, band support bitmasks, and audio routing modes.
3. **Protected NV Partition (`BML PROTECT NV Partition`)**: Non-volatile partition containing hardware identity (IMEIs) and SIMLock status.
4. **Delta NV Updates (`[DELTA NV]`)**: Dynamic overlay mechanism allowing runtime or OTA patching of specific NV item identifiers (`nvid`) without rewriting the entire partition.

---

## 2. NV Item Entry Structure

Items within `nvitem.bin` are stored sequentially in a type-length-value (TLV) binary format:

```c
typedef struct {
    uint16_t nvid;         // Non-Volatile Item Identifier
    uint16_t length;       // Payload size in bytes
    uint8_t  payload[];    // Parameter data
} __attribute__((packed)) nv_item_t;
```

* **Header Magic / Version**: `V1.0.1-B7 Spreadtrum Communication CO.` (found at offset `0x000013BA`).
* **Checksum & Flush Validation**: All NV items are protected by internal CRC-16 checks. Modifying parameters via AT commands (`+SPNVW`) triggers `EFS_NvitemWrite()`.

---

## 3. Acoustic & Audio DSP Profiles

The Nokia 2660 Flip implements extensive audio pre-processing and post-processing tuning. `nvitem.bin` contains **28 distinct acoustic scenario profiles**, each with tailored Finite Impulse Response (FIR) filter coefficients, Dynamic Range Compression (DRC), Automatic Gain Control (AGC), Acoustic Echo Cancellation (AEC), and side-tone attenuation:

### Acoustic Profiles Matrix

| Profile Identifier | Audio Path | Sample Rate | Network Protocol | Special Processing |
|:---|:---|:---:|:---:|:---|
| `Handset` | Internal Earpiece | 8 kHz | 2G GSM (AMR-NB) | Sidetone + standard DRC |
| `Handsfree` | Rear Loudspeaker | 8 kHz | 2G GSM (AMR-NB) | Loudness boosting + AEC |
| `Headset4P` | 3.5mm TRRS (with mic) | 8 kHz | 2G GSM (AMR-NB) | External mic gain curve |
| `Headset3P` | 3.5mm TRS (stereo) | 8 kHz | 2G GSM (AMR-NB) | Device internal mic used |
| `HandsetTD` | Earpiece | 8 kHz | TD-SCDMA | TD frame jitter buffers |
| `HandsfreeTD` | Loudspeaker | 8 kHz | TD-SCDMA | Acoustic echo suppressor |
| `Headset4PTD` | 3.5mm TRRS | 8 kHz | TD-SCDMA | External mic gain curve |
| `Headset3PTD` | 3.5mm TRS | 8 kHz | TD-SCDMA | High-gain internal mic |
| `HandsetWB` | Earpiece | 16 kHz | AMR-WB (Wideband) | 50 Hz – 7 kHz bandpass |
| `HandsfreeWB` | Loudspeaker | 16 kHz | AMR-WB (Wideband) | High-fidelity speaker EQ |
| `Headset4PWB` | 3.5mm TRRS | 16 kHz | AMR-WB (Wideband) | Full-spectrum speech |
| `Headset3PWB` | 3.5mm TRS | 16 kHz | AMR-WB (Wideband) | Full-spectrum speech |
| `HandsetLTENB` | Earpiece | 8 kHz | 4G VoLTE (AMR-NB) | VoLTE packet loss concealment |
| `HandsfreeLTENB`| Loudspeaker | 8 kHz | 4G VoLTE (AMR-NB) | Low-latency echo cancellation |
| `Headset4PLTENB`| 3.5mm TRRS | 8 kHz | 4G VoLTE (AMR-NB) | Inline mic filtering |
| `Headset3PLTENB`| 3.5mm TRS | 8 kHz | 4G VoLTE (AMR-NB) | Internal mic filtering |
| `HandsetLTEWB` | Earpiece | 16 kHz | 4G VoLTE (AMR-WB / EVS) | EVS-WB crystal voice processing |
| `HandsfreeLTEWB`| Loudspeaker | 16 kHz | 4G VoLTE (AMR-WB / EVS) | EVS-SWB 32 kHz upsampling |
| `Headset4PLTEWB`| 3.5mm TRRS | 16 kHz | 4G VoLTE (AMR-WB / EVS) | High-definition voice profile |
| `Headset3PLTEWB`| 3.5mm TRS | 16 kHz | 4G VoLTE (AMR-WB / EVS) | High-definition voice profile |
| `BTHSD8K` | Bluetooth SCO | 8 kHz | Bluetooth Headset | CVSD vocoder pass-through |
| `BTHSNRECD8K` | Bluetooth SCO | 8 kHz | Bluetooth (Noise Red.) | Ambient noise suppression |
| `BTHSD16K` | Bluetooth mSBC | 16 kHz | Bluetooth Wideband | mSBC wideband speech codec |
| `BTHSNRECD16K` | Bluetooth mSBC | 16 kHz | Bluetooth (Noise Red.) | Dual-mic noise reduction filter |
| `BTHSD8KLTE` | Bluetooth SCO | 8 kHz | VoLTE over Bluetooth | VoLTE-to-Bluetooth transcode |
| `BTHSNRECD8KLTE`| Bluetooth SCO | 8 kHz | VoLTE over BT (NR) | VoLTE-to-BT noise filter |
| `BTHSD16KLTE` | Bluetooth mSBC | 16 kHz | VoLTE over Bluetooth | Full wideband VoLTE-to-BT |
| `BTHSNRECD16KLTE`| Bluetooth mSBC | 16 kHz | VoLTE over BT (NR) | HD VoLTE Bluetooth profile |

---

## 4. Radio Frequency (RF) Calibration Tables

The calibration section stores hardware compensation tables read by the baseband DSP (`lte_dsp.bin` / `gsm_dsp.bin`):

1. **Automatic Frequency Control (AFC)**: Crystal oscillator frequency error calibration (compensation values for 26 MHz DCXO thermal drift).
2. **Power Amplifier (PA) Power Ramp Tables**: Transmit power leveling curves across 16 power control levels (PCL) for GSM900 / DCS1800 and LTE PRB allocations.
3. **Automatic Gain Control (AGC)**: Receiver dynamic range step tables mapping RSSI (dBm) to RF gain states.
4. **IQ Imbalance Correction**: Quadrature phase error and DC offset correction coefficients for LTE Cat-1 receiver chains.
5. **Supported Frequency Bands**:
   - **2G**: GSM 900 MHz, DCS 1800 MHz
   - **4G LTE Cat-1**: FDD Bands 1, 3, 5, 7, 8, 20, 28; TDD Bands 38, 40, 41

---

## 5. Hardware Identifiers & Carrier Configuration

| NV Item Function | Internal Identifier | Format / Location | Description |
|:---|:---|:---|:---|
| **Primary IMEI** | `NV_IMEI1` | 8 bytes (BCD) | Primary SIM slot hardware identifier |
| **Secondary IMEI** | `NV_IMEI2` | 8 bytes (BCD) | Secondary SIM slot hardware identifier |
| **Bluetooth MAC** | `NV_BT_ADDR` | 6 bytes (Binary) | Factory programmed Bluetooth physical address |
| **ADC Battery Cal** | `NV_ADC_CAL` | 16-bit constants | ADC voltage calibration slopes (battery meter) |
| **SIMLock Control** | `NV_SIMLOCK_STATUS`| Bitmask + Hashes | Network PIN unlock keys (NCK/SPCK) |
| **Operator APNs** | `NV_DEFAULT_APN` | ASCII / Binary | Default VoLTE IMS and internet APN profiles |
