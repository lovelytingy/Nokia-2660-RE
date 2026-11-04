# Captures

Raw data collected directly from hardware during reverse engineering.

## What belongs here

- USB protocol captures (`.pcapng`, Wireshark)
- USB descriptor dumps (text files)
- UART / serial logs (`.log`, `.txt`)
- Boot logs and console output
- Photos of the motherboard, test points, and silkscreen markings
- Screenshots of tools (ResearchDownload, SPD Upgrade Tool, etc.)

## File naming convention

Use descriptive names with the device model and context:

```
ta1480_usb_bootrom_handshake.pcapng
ta1480_uart_boot_log_hw3.0.txt
ta1480_motherboard_top.jpg
ta1480_motherboard_bottom.jpg
ta1480_testpoints_annotated.jpg
```

## Git policy

> [!NOTE]
> Large binary captures (`.pcap`, `.pcapng`, `.zip`, `.7z`) are **gitignored** by default.
> Commit only small text-based logs and representative screenshots.
> For large captures, document SHA-256 hashes and upload location instead.

## Related docs

- [USB Enumeration](../docs/usb-enumeration.md) — verified USB VID/PID and descriptor captures
- [Boot Process](../docs/boot-process.md) — what to expect on UART during boot
- [Community Findings](../docs/community-findings.md) — safe dump instructions
