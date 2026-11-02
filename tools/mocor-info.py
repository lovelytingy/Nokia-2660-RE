#!/usr/bin/env python3
"""Inspect Unisoc / Spreadtrum MOCOR firmware images (DHTB headers, tasks, build info)."""

from __future__ import annotations

import argparse
import os
import re
import struct
import sys
from typing import Any, Dict


def parse_dhtb(data: bytes) -> Dict[str, Any] | None:
    if len(data) < 0x200 or data[:4] != b"DHTB":
        return None

    version = struct.unpack_from("<I", data, 4)[0]
    payload_size = struct.unpack_from("<I", data, 0x30)[0]
    header_size = len(data) - payload_size if len(data) >= payload_size else 1204

    # Check for bootstrap stub at +0x200
    bootstrap = data[0x200:0x220].hex()

    return {
        "magic": "DHTB",
        "version": version,
        "payload_size": payload_size,
        "estimated_header_size": header_size,
        "bootstrap_preview": bootstrap,
    }


def find_mocor_metadata(data: bytes) -> Dict[str, Any]:
    # Look for MOCOR build string
    mocor_matches = re.findall(rb"MOCOR[^\x00\r\n]{3,80}", data)
    build_strings = sorted(list(set(m.decode("ascii", "replace") for m in mocor_matches)))

    # Look for Software versions (e.g. 10.02.xx)
    ver_matches = re.findall(rb"\b\d{2}\.\d{2}\.\d{2}\.\d{2}\b", data)
    versions = sorted(list(set(v.decode("ascii") for v in ver_matches)))

    # Look for compiler timestamps
    date_matches = re.findall(
        rb"\b(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)\s+\d{1,2}\s+\d{4}\b", data
    )
    compile_dates = sorted(list(set(d.decode("ascii") for d in date_matches)))

    # Task and thread creation names (SCI_CreateThread / SCI_CreateAppThread)
    thread_matches = re.findall(rb"P_[A-Z0-9_]{3,24}", data)
    thread_candidates = sorted(list(set(t.decode("ascii", "replace") for t in thread_matches)))

    # Look for application / engine strings
    app_engines = {}
    for engine in (
        b"Opera",
        b"MRE",
        b"VXP",
        b"Java",
        b"MIDP",
        b"KVM",
        b"JVM",
        b"NES",
        b"CMMB",
        b"GPRS",
        b"VoLTE",
    ):
        matches = re.findall(rb"[^\x00\r\n]{0,20}" + engine + rb"[^\x00\r\n]{0,20}", data)
        if matches:
            app_engines[engine.decode("ascii")] = len(matches)

    # Look for file extensions supported
    extensions = re.findall(
        rb"\.(?:vxp|jar|jad|nes|mp3|wav|amr|aac|mp4|3gp|avi|jpg|png|gif|bmp|txt|ini|dat)\b",
        data,
        re.IGNORECASE,
    )
    ext_counts = {}
    for ext in extensions:
        k = ext.decode("ascii").lower()
        ext_counts[k] = ext_counts.get(k, 0) + 1

    return {
        "mocor_build_strings": build_strings,
        "versions": versions,
        "compile_dates": compile_dates,
        "sample_thread_names": thread_candidates[:15],
        "app_engines_detected": app_engines,
        "file_extensions": ext_counts,
    }


def main():
    parser = argparse.ArgumentParser(description="Analyze MOCOR binary images.")
    parser.add_argument("binary", help="Path to MOCOR binary (kernel.bin, user.bin, etc.)")
    args = parser.parse_args()

    if not os.path.isfile(args.binary):
        print(f"[-] File not found: {args.binary}", file=sys.stderr)
        sys.exit(1)

    print(f"Analyzing {args.binary} ({os.path.getsize(args.binary):,} bytes)...")

    with open(args.binary, "rb") as f:
        data = f.read()

    dhtb = parse_dhtb(data)
    if dhtb:
        print("\n[+] DHTB Signed Header Detected:")
        print(f"    Version:      {dhtb['version']}")
        print(f"    Payload Size: {dhtb['payload_size']:,} bytes")
        print(f"    Header Size:  {dhtb['estimated_header_size']} bytes")
        print(f"    Bootstrap:    {dhtb['bootstrap_preview']}")
    else:
        print("\n[-] No DHTB header detected (raw payload or non-DHTB image).")

    meta = find_mocor_metadata(data)

    print("\n[+] Build & Version Information:")
    for s in meta["mocor_build_strings"]:
        print(f"    • {s}")
    for v in meta["versions"]:
        print(f"    • Software Version: {v}")
    for d in meta["compile_dates"]:
        print(f"    • Compile Date: {d}")

    print("\n[+] Subsystems / Engines Detected:")
    for eng, count in meta["app_engines_detected"].items():
        print(f"    • {eng:<8} ({count} occurrences)")

    print("\n[+] File Extensions Referenced:")
    for ext, count in sorted(meta["file_extensions"].items(), key=lambda x: -x[1]):
        print(f"    • {ext:<8} ({count})")

    print("\n[+] Sample Thread / Task Symbols:")
    for th in meta["sample_thread_names"]:
        print(f"    • {th}")


if __name__ == "__main__":
    main()
