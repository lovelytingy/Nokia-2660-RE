#!/usr/bin/env python3
"""
tools/extract_strings.py - Extract and categorize strings from firmware binaries.

Designed for reverse engineering Unisoc / Spreadtrum MOCOR firmware images
(Nokia 2660 Flip / UMS9117 and related feature phones).

Extracts ASCII and UTF-16LE strings, filters machine-code noise, and
automatically categorizes strings into reverse-engineering domains:
  - Secret USSD dialer codes (*#...#)
  - 3GPP & Unisoc proprietary AT modem commands (AT+, +C..., +SP...)
  - RTOS tasks and thread names (SCI_*, *_TASK, *_THREAD)
  - Hardware controllers & peripheral drivers (GC9307, SC2720, UMS9117, SPI...)
  - Network, VoLTE, IMS, APN, and SIM configurations
  - Security, certificate, RSA, and Secure Boot keywords
  - Storage drive paths and filesystem locations (C:\\..., D:\\..., /system/...)
  - Web URLs and endpoints (http://..., https://...)
  - Assertion, panic, and diagnostic error messages
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from typing import Any, Dict, List, Pattern, Set, Tuple


# ==============================================================================
# Domain Categorization Rules
# ==============================================================================

CATEGORY_DEFINITIONS: Dict[str, Tuple[str, Pattern[bytes]]] = {
    "dialer": (
        "Secret USSD & Engineering dialer codes",
        re.compile(rb"^\*#[0-9*#]+#$"),
    ),
    "at_cmd": (
        "3GPP & Unisoc proprietary AT modem commands",
        re.compile(rb"^(?:AT\+[A-Za-z0-9_\^]+|\+C[A-Z0-9_]{2,18}|\+(?:SP|SPD)[A-Za-z0-9_]{2,24})$"),
    ),
    "rtos": (
        "RTOS tasks, threads, and Spreadtrum SCI API calls",
        re.compile(
            rb"^(?:[A-Za-z0-9_]{2,28}(?:_TASK|_THREAD|_SVC|_SRV)|SCI_[A-Za-z0-9_]{3,32}|T_P_[A-Za-z0-9_]{2,28}|P_APP[A-Za-z0-9_]*)$"
        ),
    ),
    "hardware": (
        "Hardware peripherals, displays, PMIC, and chip drivers",
        re.compile(
            rb"(?i)\b(?:GC[0-9]{4}[A-Za-z0-9_]*|ST[0-9]{4}[A-Za-z0-9_]*|ILI[0-9]{4}[A-Za-z0-9_]*|SC27[0-9]{2}[A-Za-z0-9_]*|UMS91[0-9]{2}[A-Za-z0-9_]*|VBC[A-Za-z0-9_]*|GPIO[A-Za-z0-9_]*|SPI[0-9]?[A-Za-z0-9_]*|UART[0-9]?[A-Za-z0-9_]*|NAND[A-Za-z0-9_]*|eFuse[A-Za-z0-9_]*|AON[A-Za-z0-9_]*)\b"
        ),
    ),
    "network": (
        "Telecom, APN, IMS, VoLTE, PLMN, and SIM configurations",
        re.compile(
            rb"(?i)\b(?:APN|VoLTE|IMS|PLMN|PDP|MCC|MNC|GPRS|EDGE|LTE-A|EVS|AMR-WB|USIM|SIM[0-9]?)\b"
        ),
    ),
    "security": (
        "Secure Boot, RSA, SHA256, certificates, and eFuses",
        re.compile(
            rb"(?i)\b(?:cert(?:ificate)?|rsa|sha256|sha1|antiroll(?:back|_back)?|secure[\s_]?boot|signature|modulus|exponent|efuse|sechdr|public[\s_]?key|root[\s_]?key)\b"
        ),
    ),
    "paths": (
        "Storage drive paths and filesystem locations",
        re.compile(
            rb"^(?:[A-Za-z]:[/\\][A-Za-z0-9_./\\ -]{2,100}|/(?:system|etc|data|nand|card|app|res|sys|temp|sdcard)[A-Za-z0-9_./\\ -]{2,100})$"
        ),
    ),
    "urls": (
        "Web URLs and online endpoints",
        re.compile(rb"^https?://[A-Za-z0-9_./\?#&=%:~-]+$"),
    ),
    "errors": (
        "Assertions, panics, errors, and failure diagnostics",
        re.compile(
            rb"(?i)\b(?:assert(?:ion)?|panic|fatal|overflow|corrupt|timeout|fail(?:ed|ure)?|abort)\b"
        ),
    ),
}


# ==============================================================================
# String Extraction & Heuristic Noise Filtering
# ==============================================================================

def is_noise(text: str) -> bool:
    """Heuristic check to weed out ARM opcode bytes that form random ASCII."""
    # Strings with spaces or word structure are rarely random code
    if " " in text:
        return False

    n = len(text)
    # Check for excessive unprintable/punctuation characters
    alnum = sum(1 for c in text if c.isalnum() or c in ("_", "-", ".", "/"))
    ratio = alnum / n

    if n <= 6 and ratio < 0.75:
        return True

    # Obvious machine opcode patterns (backticks, tildes, carats, pipe symbols)
    if any(c in text for c in "`~^|"):
        return True

    # 4 consecutive punctuation characters
    if re.search(r"[!@#$%^&*()_+=\[\]{};':\",.<>/?\\-]{4,}", text):
        return True

    return False


def extract_strings(
    data: bytes,
    min_len: int = 4,
    encoding: str = "both",
    raw: bool = False,
) -> List[Tuple[int, str, str]]:
    """Extract (offset, encoding, text) tuples from binary data."""
    results: List[Tuple[int, str, str]] = []

    # ASCII strings
    if encoding in ("ascii", "both"):
        ascii_pattern = re.compile(rb"[\x20-\x7e]{" + str(min_len).encode() + rb",}")
        for match in ascii_pattern.finditer(data):
            try:
                text = match.group().decode("latin-1")
                if raw or not is_noise(text):
                    results.append((match.start(), "ASCII", text))
            except Exception:
                pass

    # UTF-16LE strings
    if encoding in ("utf16", "both"):
        utf16_pattern = re.compile(rb"(?:[\x20-\x7e]\x00){" + str(min_len).encode() + rb",}")
        for match in utf16_pattern.finditer(data):
            try:
                text = match.group().decode("utf-16le")
                if raw or not is_noise(text):
                    results.append((match.start(), "UTF-16LE", text))
            except Exception:
                pass

    results.sort(key=lambda x: x[0])
    return results


def categorize_strings(
    strings: List[Tuple[int, str, str]],
) -> Dict[str, List[Tuple[int, str, str]]]:
    """Group extracted strings into recognized domain categories."""
    categorized: Dict[str, List[Tuple[int, str, str]]] = {cat: [] for cat in CATEGORY_DEFINITIONS}
    categorized["uncategorized"] = []

    for offset, enc, text in strings:
        raw_bytes = text.encode("utf-8", "ignore")
        matched = False
        for cat_name, (_, rx) in CATEGORY_DEFINITIONS.items():
            if rx.search(raw_bytes):
                categorized[cat_name].append((offset, enc, text))
                matched = True
        if not matched:
            categorized["uncategorized"].append((offset, enc, text))

    return categorized


# ==============================================================================
# Presentation & Summary Reports
# ==============================================================================

def print_summary(
    filepath: str,
    filesize: int,
    total_strings: int,
    categorized: Dict[str, List[Tuple[int, str, str]]],
) -> None:
    print("=" * 72)
    print("  FIRMWARE STRING TRIAGE REPORT")
    print(f"  Target: {filepath} ({filesize:,} bytes)")
    print(f"  Total Strings Extracted: {total_strings:,}")
    print("=" * 72)
    print(f"{'Category':<12} | {'Matches':<8} | {'Description'}")
    print("-" * 72)

    for cat_name, (desc, _) in CATEGORY_DEFINITIONS.items():
        count = len(categorized[cat_name])
        print(f"{cat_name:<12} | {count:<8,} | {desc}")

    uncat_count = len(categorized["uncategorized"])
    print(f"{'uncat':<12} | {uncat_count:<8,} | Other general text and symbols")
    print("=" * 72)

    # Print top findings per domain
    for cat_name, (desc, _) in CATEGORY_DEFINITIONS.items():
        items = categorized[cat_name]
        if not items:
            continue
        print(f"\n[+] {cat_name.upper()} ({len(items):,} matches - {desc}):")
        # Deduplicate while preserving order
        seen: Set[str] = set()
        unique_samples = []
        for off, enc, txt in items:
            cleaned = txt.strip()
            if cleaned not in seen:
                seen.add(cleaned)
                unique_samples.append((off, enc, cleaned))
            if len(unique_samples) >= 12:
                break

        for off, enc, txt in unique_samples:
            preview = txt if len(txt) <= 65 else txt[:62] + "..."
            print(f"    0x{off:08X} [{enc:<8}] {preview}")

        if len(seen) > len(unique_samples):
            print(f"    ... and {len(seen) - len(unique_samples):,} more unique {cat_name} strings")


# ==============================================================================
# CLI Entry Point
# ==============================================================================

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract and categorize strings from firmware binaries.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Show categorized summary overview:
  python3 tools/extract_strings.py firmware/dumps/ta1480_10.02.17.00/user.bin

  # Dump all secret dialer codes and AT commands with offsets:
  python3 tools/extract_strings.py firmware/dumps/ta1480_10.02.17.00/user.bin -c dialer at_cmd --offsets

  # Search for specific terms:
  python3 tools/extract_strings.py firmware/dumps/ta1480_10.02.17.00/kernel.bin -g "GC9307|ST7789"

  # Export complete categorized JSON database:
  python3 tools/extract_strings.py firmware/dumps/ta1480_10.02.17.00/kernel.bin --json kernel_strings.json
""",
    )
    parser.add_argument("binary", help="Path to binary firmware image")
    parser.add_argument("-n", "--min-len", type=int, default=4, help="Minimum string length (default: 4)")
    parser.add_argument(
        "-e",
        "--encoding",
        choices=["ascii", "utf16", "both"],
        default="both",
        help="Encoding to extract (default: both)",
    )
    parser.add_argument(
        "-c",
        "--category",
        nargs="+",
        metavar="CAT",
        help="Filter by category (dialer, at_cmd, rtos, hardware, network, security, paths, urls, errors, all)",
    )
    parser.add_argument(
        "--list-categories",
        action="store_true",
        help="List available categories and exit",
    )
    parser.add_argument("-g", "--grep", type=str, default=None, help="Filter strings matching regex pattern")
    parser.add_argument("-o", "--output", type=str, default=None, help="Write output lines to file")
    parser.add_argument("--json", type=str, metavar="JSON_FILE", help="Export structured categorized JSON")
    parser.add_argument("--offsets", action="store_true", help="Print hexadecimal file offsets")
    parser.add_argument("--raw", action="store_true", help="Disable heuristic noise filtering")
    parser.add_argument(
        "--summary",
        action="store_true",
        help="Show high-level category summary (default if no -c or -g given)",
    )

    args = parser.parse_args()

    if args.list_categories:
        print("Available String Categories:")
        for k, (desc, _) in CATEGORY_DEFINITIONS.items():
            print(f"  {k:<12}: {desc}")
        sys.exit(0)

    if not os.path.isfile(args.binary):
        print(f"[-] File not found: {args.binary}", file=sys.stderr)
        sys.exit(1)

    file_size = os.path.getsize(args.binary)
    with open(args.binary, "rb") as f:
        data = f.read()

    strings = extract_strings(data, min_len=args.min_len, encoding=args.encoding, raw=args.raw)
    categorized = categorize_strings(strings)

    # Save JSON if requested
    if args.json:
        json_obj: Dict[str, Any] = {
            "binary": os.path.abspath(args.binary),
            "size": file_size,
            "total_strings": len(strings),
            "categories": {},
        }
        for cat_name in CATEGORY_DEFINITIONS:
            json_obj["categories"][cat_name] = [
                {"offset": off, "encoding": enc, "text": txt}
                for off, enc, txt in categorized[cat_name]
            ]
        with open(args.json, "w", encoding="utf-8") as jf:
            json.dump(json_obj, jf, indent=2)
        print(f"[+] Exported categorized JSON database to {args.json} ({len(strings):,} strings)")
        if not args.category and not args.grep and not args.summary:
            return

    # If no filtering or explicit summary requested, run summary
    should_run_summary = args.summary or (args.category is None and args.grep is None)
    if should_run_summary and not args.output:
        print_summary(args.binary, file_size, len(strings), categorized)
        return

    # Specific category or grep filter requested
    selected_strings: List[Tuple[int, str, str]] = []
    if args.category:
        cats = args.category
        if "all" in cats:
            cats = list(CATEGORY_DEFINITIONS.keys())
        for c in cats:
            if c in categorized:
                selected_strings.extend(categorized[c])
            else:
                print(f"[!] Warning: Unknown category '{c}'. Available: {list(CATEGORY_DEFINITIONS.keys())}", file=sys.stderr)
        selected_strings.sort(key=lambda x: x[0])
    else:
        selected_strings = strings

    if args.grep:
        rx = re.compile(args.grep, re.IGNORECASE)
        selected_strings = [item for item in selected_strings if rx.search(item[2])]

    # Format output
    out_lines: List[str] = []
    for off, enc, text in selected_strings:
        if args.offsets:
            out_lines.append(f"0x{off:08X} [{enc:<8}] {text}")
        else:
            out_lines.append(text)

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            for line in out_lines:
                f.write(line + "\n")
        print(f"[+] Wrote {len(out_lines):,} strings to {args.output}")
    else:
        for line in out_lines:
            print(line)


if __name__ == "__main__":
    main()
