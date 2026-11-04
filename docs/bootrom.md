# BootROM and download mode

Status: **verified via community research** (Tested on TA-1480 / UMS9117, see `ilyakurdyukov/spreadtrum_flash` Issue #44).

## How to Enter Download / BSL Mode

* **Boot Key:** **`*` (Asterisk)** key (or **`*` + Hang Up / Power**).
* **Connection Sequence:**
  1. Unplug USB and remove the battery from the phone.
  2. Press and hold the **`*`** key on the keypad.
  3. While holding the key, plug the USB cable into the computer and phone.
  4. The phone enumerates on USB as **VID:PID `1782:4D00`** (Spreadtrum / Unisoc BootROM mode).
  5. The BootROM waits for host handshake commands (FDL1). If no handshake is received within a short timeout, the connection drops.

## Download chain (host → device)

1. Device enters Spreadtrum/Unisoc **download / BSL** mode using the `*` boot key.
2. BootROM enumerates USB as **`1782:4D00`**.
3. Host sends **FDL1** (`fdl1.bin`) to SRAM base **`0x6200`** (XML).
4. FDL1 initializes LPDDR, speaks FDL packet protocol (`FDL_PacketDoIdle`), verifies **FDL2**.
5. Host sends **FDL2** (`fdl2.bin`) to DDR **`0x80100000`**.
6. FDL2 initializes NAND FTL and accepts partition read / write commands (`read_flash` / `FDL2_DataStart` / `FDL2_DataEnd`).
7. After operations, FDL2 waits for USB unplug or can reset the MCU (`WDG_ResetMCU`).

## Verified Dump Command (`spd_dump`)

The open-source tool `spd_dump` (from `ilyakurdyukov/spreadtrum_flash`, and macOS port `xun404/spd_dump-macos`) has been tested on Nokia 2660 Flip TA-1480 to dump partitions without flashing:

```bash
./spd_dump \
  keep_charge 1 \
  fdl firmware/extracted/v23_update_Nokia_2660/fdl1.bin 0x6200 \
  blk_size 0x1000 \
  fdl firmware/extracted/v23_update_Nokia_2660/fdl2.bin 0x80100000 \
  read_flash 0x80000001 0 auto boot0_dump.bin \
  read_flash 0x80000002 0 auto boot1_dump.bin \
  read_flash 0x80000003 0 auto kernel_dump.bin \
  read_flash 0x80000004 0 auto user_dump.bin
```

> [!NOTE]
> Community testing confirmed that `fdl1.bin` and `fdl2.bin` extracted from the older v23 PAC firmware work reliably to bootstrap FDL communication on newer software versions as well.

## Channels

FDL1/FDL2 strings reference both **USB** and **UART** CRC errors → same protocol family on either link. UART/JTAG mode flag string appears in FDL1 and boot0.

## Relation to normal boot

BootROM selects:

- **Download path** → FDL1/FDL2 (host-supplied, DHTB-wrapped), or
- **NAND path** → boot0 → verified boot1 → …

See [Boot Process](boot-process.md).
