#!/usr/bin/env python3
"""Extract images from a Spreadtrum / Unisoc BP_R1.0.0 (.pac) package."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import struct
import sys
from pathlib import Path

PAC_HEADER_SIZE = 2124
FILE_HDR_SIZE = 2580
FILE_HDR_FMT = "<I512s512s504sIIIIIIII5I996s"
FLASH_TYPES = {0: "NOR", 1: "NAND", 2: "eMMC"}


def u16le(buf: bytes, off: int, nbytes: int) -> str:
    return buf[off : off + nbytes].decode("utf-16le", "replace").split("\0", 1)[0].strip()


def parse_header(data: bytes) -> dict:
    if len(data) < PAC_HEADER_SIZE:
        raise ValueError("file too small to be a PAC")

    version = u16le(data, 0, 48)
    if version not in ("BP_R1.0.0", "BP_R2.0.1"):
        raise ValueError(f"unsupported PAC version: {version!r}")

    lo_size = struct.unpack_from("<I", data, 0x30)[0]
    product = u16le(data, 0x34, 512)
    prd_version = u16le(data, 0x234, 512)
    nfiles = struct.unpack_from("<I", data, 0x434)[0]
    file_off = struct.unpack_from("<I", data, 0x438)[0]
    mode = struct.unpack_from("<I", data, 0x43c)[0]
    flash_type = struct.unpack_from("<I", data, 0x440)[0]
    nand_strategy = struct.unpack_from("<I", data, 0x444)[0]
    is_nv_backup = struct.unpack_from("<I", data, 0x448)[0]
    magic = struct.unpack_from("<I", data, PAC_HEADER_SIZE - 8)[0]
    crc1, crc2 = struct.unpack_from("<HH", data, PAC_HEADER_SIZE - 4)

    return {
        "version": version,
        "declared_size": lo_size,
        "actual_size": len(data),
        "product_name": product,
        "product_version": prd_version,
        "file_count": nfiles,
        "file_table_offset": file_off,
        "mode": mode,
        "flash_type": FLASH_TYPES.get(flash_type, str(flash_type)),
        "nand_strategy": nand_strategy,
        "is_nv_backup": is_nv_backup,
        "magic": hex(magic),
        "crc1": crc1,
        "crc2": crc2,
    }


def parse_entries(data: bytes, header: dict) -> list[dict]:
    assert struct.calcsize(FILE_HDR_FMT) == FILE_HDR_SIZE
    entries: list[dict] = []
    for i in range(header["file_count"]):
        off = header["file_table_offset"] + i * FILE_HDR_SIZE
        fields = struct.unpack_from(FILE_HDR_FMT, data, off)
        part_name = fields[1].decode("utf-16le", "replace").split("\0", 1)[0].strip()
        file_name = fields[2].decode("utf-16le", "replace").split("\0", 1)[0].strip()
        hi_size, hi_data_off, lo_size, n_file_flag, n_check_flag, lo_data_off, can_omit, addr_num = (
            fields[4:12]
        )
        addrs = fields[12:17]
        size = (hi_size << 32) | lo_size
        data_offset = (hi_data_off << 32) | lo_data_off
        entries.append(
            {
                "index": i,
                "hdr_length": fields[0],
                "id": part_name,
                "file_name": file_name or f"{part_name or f'part_{i}'}.bin",
                "size": size,
                "data_offset": data_offset,
                "file_flag": n_file_flag,
                "check_flag": n_check_flag,
                "can_omit": can_omit,
                "addr_num": addr_num,
                "addrs": [hex(a) for a in addrs[:addr_num]],
            }
        )
    return entries


def unique_name(name: str, used: set[str]) -> str:
    name = name.replace("\\", "_").replace("/", "_") or "part.bin"
    if name not in used:
        used.add(name)
        return name
    stem, suf = Path(name).stem, Path(name).suffix or ".bin"
    n = 1
    while True:
        cand = f"{stem}_{n}{suf}"
        if cand not in used:
            used.add(cand)
            return cand
        n += 1


def extract(pac_path: Path, out_dir: Path) -> tuple[dict, list[dict]]:
    data = pac_path.read_bytes()
    header = parse_header(data)
    entries = parse_entries(data, header)
    out_dir.mkdir(parents=True, exist_ok=True)

    used: set[str] = set()
    manifest: list[dict] = []
    for e in entries:
        out_name = unique_name(e["file_name"], used)
        rec = dict(e)
        rec["extracted_as"] = out_name
        if e["size"] == 0 or e["file_flag"] == 0:
            rec["sha256"] = None
            rec["note"] = "no payload (operation-only or empty)"
            print(f"[{e['index']:02d}] {e['id']:<20} (skip, no payload)")
        else:
            blob = data[e["data_offset"] : e["data_offset"] + e["size"]]
            if len(blob) != e["size"]:
                rec["note"] = f"truncated: got {len(blob)} of {e['size']}"
            (out_dir / out_name).write_bytes(blob)
            rec["sha256"] = hashlib.sha256(blob).hexdigest()
            print(
                f"[{e['index']:02d}] {e['id']:<20} -> {out_name:<40} "
                f"{e['size']:>10} @ 0x{e['data_offset']:x}"
            )
        manifest.append(rec)

    (out_dir / "pac_header.json").write_text(json.dumps(header, indent=2) + "\n")
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    with (out_dir / "manifest.csv").open("w", newline="") as f:
        fields = [
            "index",
            "id",
            "file_name",
            "extracted_as",
            "size",
            "data_offset",
            "file_flag",
            "check_flag",
            "can_omit",
            "addrs",
            "sha256",
            "note",
        ]
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for m in manifest:
            row = {k: m.get(k, "") for k in fields}
            row["addrs"] = " ".join(m.get("addrs") or [])
            w.writerow(row)
    return header, manifest


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("pac", type=Path, help="path to .pac file")
    ap.add_argument(
        "-o",
        "--out",
        type=Path,
        help="output directory (default: firmware/extracted/<pac-stem>)",
    )
    args = ap.parse_args()
    if not args.pac.is_file():
        print(f"not found: {args.pac}", file=sys.stderr)
        return 1
    out = args.out or Path("firmware/extracted") / args.pac.stem
    header, manifest = extract(args.pac, out)
    payload = sum(1 for m in manifest if m.get("sha256"))
    print(
        f"\n{header['product_name']} {header['product_version']}: "
        f"{payload} payloads / {header['file_count']} entries -> {out}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
