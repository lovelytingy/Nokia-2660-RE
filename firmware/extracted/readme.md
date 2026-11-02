# Extracted firmware

Contents here are unpacked from PAC samples under `firmware/`.

## v23_update_Nokia_2660

Source: `../v23_update_Nokia_2660.pac`

Re-extract:

```bash
python3 tools/extract_pac.py firmware/v23_update_Nokia_2660.pac
```

Artifacts:

- `pac_header.json` — PAC header summary
- `manifest.json` / `manifest.csv` — full file table + SHA-256
- `UMS9117.xml` — embedded flash scheme
- `*.bin` — partition / loader payloads
