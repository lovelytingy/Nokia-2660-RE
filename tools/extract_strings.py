#!/usr/bin/env python3
"""Extract ASCII and UTF-16LE strings from firmware binaries."""

import argparse
import os
import re
import sys


def extract_strings(data: bytes, min_len: int = 4, encoding: str = "both"):
    results = []

    # ASCII / Latin-1 printable characters (0x20 - 0x7E)
    if encoding in ("ascii", "both"):
        ascii_pattern = re.compile(rb"[\x20-\x7e]{" + str(min_len).encode() + rb",}")
        for match in ascii_pattern.finditer(data):
            try:
                text = match.group().decode("latin-1")
                results.append((match.start(), "ASCII", text))
            except Exception:
                pass

    # UTF-16LE printable characters
    if encoding in ("utf16", "both"):
        utf16_pattern = re.compile(rb"(?:[\x20-\x7e]\x00){" + str(min_len).encode() + rb",}")
        for match in utf16_pattern.finditer(data):
            try:
                text = match.group().decode("utf-16le")
                results.append((match.start(), "UTF-16LE", text))
            except Exception:
                pass

    results.sort(key=lambda x: x[0])
    return results


def main():
    parser = argparse.ArgumentParser(description="Extract ASCII and UTF-16LE strings from firmware binaries.")
    parser.add_argument("binary", help="Path to binary file")
    parser.add_argument("-n", "--min-len", type=int, default=4, help="Minimum string length (default: 4)")
    parser.add_argument("-e", "--encoding", choices=["ascii", "utf16", "both"], default="both", help="Encoding")
    parser.add_argument("-g", "--grep", type=str, default=None, help="Filter strings matching regex")
    parser.add_argument("-o", "--output", type=str, default=None, help="Write output to file")
    parser.add_argument("--offsets", action="store_true", help="Print hexadecimal file offsets")
    args = parser.parse_args()

    if not os.path.isfile(args.binary):
        print(f"[-] File not found: {args.binary}", file=sys.stderr)
        sys.exit(1)

    with open(args.binary, "rb") as f:
        data = f.read()

    strings = extract_strings(data, min_len=args.min_len, encoding=args.encoding)

    filter_re = re.compile(args.grep, re.IGNORECASE) if args.grep else None

    out_lines = []
    for offset, enc, text in strings:
        if filter_re and not filter_re.search(text):
            continue
        if args.offsets:
            out_lines.append(f"0x{offset:08X} [{enc:<8}] {text}")
        else:
            out_lines.append(text)

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            for line in out_lines:
                f.write(line + "\n")
        print(f"[+] Wrote {len(out_lines)} strings to {args.output}")
    else:
        for line in out_lines:
            print(line)


if __name__ == "__main__":
    main()
