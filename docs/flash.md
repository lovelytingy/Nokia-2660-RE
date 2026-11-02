# Flash

Status: **partial** — see [Flash Layout](Flash%20Layout.md) for PAC inventory.

## Summary

- PAC flash type: **NAND**
- Programmer path: FDL2 (`NANDCTL_*`, `SCI_FTL_*`)
- Partition ids in XML use `0x8000000x` / `0x9000000x` style bases

## Needed

- On-device partition table dump
- Page/block geometry from NAND ID strings in FDL2/boot0
