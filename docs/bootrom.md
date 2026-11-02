# BootROM and download mode

Status: **inferred** from loader images + `UMS9117.xml`. BootROM dump from TA-1480 not obtained yet.

## Download chain (host → device)

1. Device enters Spreadtrum/Unisoc **download / BSL** mode (key combo, tool, or failed boot — exact TA-1480 method TBD).
2. BootROM enumerates USB (project device normal VID:PID `1782:4D00`; download-mode PID may differ — capture needed).
3. Host sends **FDL1** to SRAM base **`0x6200`** (XML).
4. FDL1 initializes DRAM, speaks FDL packet protocol (`FDL_PacketDoIdle`), verifies **FDL2**.
5. Host sends **FDL2** to DDR **`0x80100000`**.
6. FDL2 initializes NAND FTL and accepts partition download (`FDL2_DataStart` / `FDL2_DataEnd`).
7. After programming, FDL2 waits for USB unplug and can power off / reset.

## Channels

FDL1/FDL2 strings reference both **USB** and **UART** CRC errors → same protocol family on either link. UART/JTAG mode flag string appears in FDL1 and boot0.

## Relation to normal boot

BootROM (not in PAC) selects:

- **Download path** → FDL1/FDL2 (host-supplied, DHTB-wrapped), or
- **NAND path** → boot0 → verified boot1 → …

See [Boot Process](Boot%20Process.md).
