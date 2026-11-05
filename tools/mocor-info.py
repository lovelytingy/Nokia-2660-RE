#!/usr/bin/env python3
"""
tools/mocor-info.py - Inspect Unisoc / Spreadtrum MOCOR firmware images & partitions.

Parses MOCOR OS headers, DHTB signatures, RSA certificate chains, RTOS task
registries, peripheral drivers, and firmware partition tables directly from
raw flash dumps or extracted PAC packages.

Supports:
  - Single binary files (kernel.bin, user.bin, boot0.bin, boot1.bin, pm_cm4.bin, etc.)
  - Complete firmware directories (firmware/dumps/... or firmware/extracted/...)
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import struct
import sys
from typing import Any, Dict, List, Optional, Set, Tuple


# ==============================================================================
# Known Partition Roles
# ==============================================================================

PARTITION_ROLES: Dict[str, str] = {
    "boot0": "1st Stage Bootloader (SRAM / NAND init / eFuse verify)",
    "boot1": "2nd Stage Bootloader (DRAM init & kernel authentication)",
    "fdl1": "USB Flash Download Loader 1 (SRAM / BootROM handoff)",
    "fdl2": "USB Flash Download Loader 2 (DRAM / Protocol engine)",
    "kernel": "MOCOR OS RTOS Kernel & Hardware Drivers",
    "exec_kernel_image": "MOCOR OS RTOS Kernel & Hardware Drivers",
    "user": "MOCOR UI / MMI Application Bundle",
    "exec_user_image": "MOCOR UI / MMI Application Bundle",
    "pm_cm4": "Cortex-M4 Power Management Core Firmware",
    "pm_nemol_cm4": "Cortex-M4 Power Management Core Firmware",
    "pm_nemol_cm4_ab": "Cortex-M4 Power Management Core Firmware (AB Slot)",
    "lte_dsp": "LTE-A / Cat-1 Modem Digital Signal Processor",
    "t117_cat1_ltea_dsp": "LTE-A / Cat-1 Modem Digital Signal Processor",
    "gsm_dsp": "GSM / 2G Modem Digital Signal Processor",
    "nemol_cat1_evs_dm_dsp": "EVS / Voice Modem Digital Signal Processor",
    "mmi_res": "UI Resource Pack (Icons, Bitmaps, Fonts, Audio)",
    "nvitem": "Non-Volatile RF Calibration & IMEI Parameters",
    "deltanv": "Delta NV Items Configuration",
    "apn": "Carrier APN Configuration Database",
    "ims_toggle": "IMS / VoLTE Configuration Toggles",
}


# ==============================================================================
# DHTB & Secure Boot Security Header Parser
# ==============================================================================

def parse_dhtb_header(data: bytes) -> Optional[Dict[str, Any]]:
    """Parse DHTB signed header and Unisoc Security Header."""
    if len(data) < 0x200 or data[:4] != b"DHTB":
        return None

    dhtb_ver = struct.unpack_from("<I", data, 4)[0]
    payload_size = struct.unpack_from("<I", data, 0x30)[0]
    header_size = len(data) - payload_size if len(data) >= payload_size else 0x200

    # Read vector table / bootstrap stub at offset 0x200
    bootstrap = data[0x200:0x220]
    vector_preview = bootstrap.hex()

    # Detect ARM exception vector pattern (0xEA000000 branch or 0xE59FF000 ldr pc)
    vector_desc = "Unknown / Non-ARM"
    if len(bootstrap) >= 4:
        first_word = struct.unpack_from("<I", bootstrap, 0)[0]
        if (first_word & 0xFF000000) == 0xEA000000:
            rel_target = (first_word & 0x00FFFFFF) << 2
            vector_desc = f"ARM Reset Branch -> +0x{rel_target + 8:X}"
        elif (first_word & 0xFFFFF000) == 0xE59FF000:
            offset = first_word & 0xFFF
            vector_desc = f"ARM Vector LDR PC, [PC, #0x{offset:X}]"
        elif (first_word & 0xFFFF0000) == 0x47700000 or (first_word & 0x0000FFFF) == 0xB510:
            vector_desc = "Thumb Execution Entry"

    result: Dict[str, Any] = {
        "magic": "DHTB",
        "version": dhtb_ver,
        "payload_size": payload_size,
        "header_size": header_size,
        "vector_preview": vector_preview,
        "vector_type": vector_desc,
        "has_sec_header": False,
    }

    # Security header location: 0x200 + payload_size
    sec_off = 0x200 + payload_size
    if sec_off + 0x40 <= len(data):
        sec_chunk = data[sec_off:]
        p_sz, hdr_off, cert_sz, total_sz = struct.unpack_from("<IIII", sec_chunk, 0x10)

        result["has_sec_header"] = True
        result["sec_offset"] = sec_off
        result["sec_payload_size"] = p_sz
        result["sec_hdr_offset"] = hdr_off
        result["cert_size"] = cert_sz
        result["total_signed_size"] = total_sz

        # Parse cert / RSA block at sec_chunk + 0x60
        if len(sec_chunk) >= 0x60 + 0x10:
            cert_type, key_bits, exp = struct.unpack_from("<III", sec_chunk, 0x60)
            if cert_type == 1:
                cert_desc = "Root / Vendor Key Certificate"
            elif cert_type == 0:
                cert_desc = "Content Certificate (Image Signature)"
            else:
                cert_desc = f"Certificate Type {cert_type}"

            result["cert_type"] = cert_desc
            result["key_bits"] = key_bits
            # public exponent
            exp_val = 65537 if exp in (0x10001, 0x1000100) else exp
            result["public_exponent"] = exp_val

            # RSA Modulus (256 bytes for 2048-bit key)
            if key_bits == 2048 and len(sec_chunk) >= 0x6C + 256:
                modulus = sec_chunk[0x6C:0x6C + 256]
                result["rsa_modulus_sha256"] = hashlib.sha256(modulus).hexdigest()
                result["rsa_modulus_hex"] = modulus.hex()
                result["rsa_modulus_preview"] = (
                    modulus[:16].hex() + " ... " + modulus[-16:].hex()
                )

            # Certificate digest
            if cert_sz > 0 and len(sec_chunk) >= 0x60 + cert_sz:
                cert_bytes = sec_chunk[0x60:0x60 + cert_sz]
                result["cert_sha256"] = hashlib.sha256(cert_bytes).hexdigest()

            # Signature block
            sig_off = 0x6C + 256
            if len(sec_chunk) >= sig_off + 256:
                sig_bytes = sec_chunk[sig_off:sig_off + 256]
                result["signature_size"] = len(sig_bytes)
                result["signature_sha256"] = hashlib.sha256(sig_bytes).hexdigest()

    return result


# ==============================================================================
# MOCOR OS Metadata Scanner
# ==============================================================================

def analyze_mocor_metadata(data: bytes) -> Dict[str, Any]:
    """Extract OS version, RTOS tasks, drivers, and runtime features."""
    # 1. Build string (e.g. MOCOR_20A_MP_W22.04.6_P4_Release)
    mocor_matches = re.findall(rb"MOCOR[A-Za-z0-9_.]+", data)
    build_strings = sorted(list(set(m.decode("ascii", "replace") for m in mocor_matches)))

    # 2. Software version (e.g. 10.02.17.00)
    ver_matches = re.findall(rb"\b\d{2}\.\d{2}\.\d{2}\.\d{2}\b", data)
    versions = sorted(list(set(v.decode("ascii") for v in ver_matches)))

    # 3. Compilation timestamps
    date_matches = re.findall(
        rb"\b(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)\s+\d{1,2}\s+\d{4}\b", data
    )
    compile_dates = sorted(list(set(d.decode("ascii") for d in date_matches)))

    # 4. Chipset target
    chipsets = set()
    for chip in (b"UMS9117", b"T117", b"UIS8910", b"SC7701", b"SC6531"):
        if chip in data:
            chipsets.add(chip.decode("ascii"))

    # 5. Product SKU / configuration tags
    product_tags = set()
    for tag_rx in (rb"\bFF618[A-Za-z0-9_]*\b", rb"\b13ARA80[A-Za-z0-9_]*\b", rb"\bHMD_SKU\d+\b"):
        for m in re.finditer(tag_rx, data):
            product_tags.add(m.group(0).decode("ascii"))

    # 6. RTOS Tasks & Threads (ending in _TASK or _THREAD or starting with T_P_)
    task_pattern = re.compile(rb"\b([A-Z0-9_]{2,28}(?:_TASK|_THREAD|_SVC|_SRV))\b")
    tasks = sorted(list(set(m.group(1).decode("ascii") for m in task_pattern.finditer(data))))

    # 7. Spreadtrum SCI OS API functions
    sci_pattern = re.compile(rb"\b(SCI_Create[A-Za-z0-9_]+)\b")
    sci_apis = sorted(list(set(m.group(1).decode("ascii") for m in sci_pattern.finditer(data))))

    # 8. Hardware LCD controller drivers
    lcd_drivers = set()
    for m in re.finditer(rb"\b(GC[0-9]{4}[A-Za-z0-9_]*|ST[0-9]{4}[A-Za-z0-9_]*|ILI[0-9]{4}[A-Za-z0-9_]*)\b", data):
        driver_name = m.group(1).decode("ascii")
        # filter out generic noise
        if any(kw in driver_name for kw in ("Init", "ReadID", "Close", "Sleep", "sublcd", "mainlcd", "driver")):
            lcd_drivers.add(driver_name)

    # 9. PMIC & Audio
    pmic_detected = [pm.decode("ascii") for pm in (b"SC2720", b"SC2721") if pm in data]
    audio_vbc = b"VBC" in data or b"SPRD_CODEC" in data

    # 10. Application Engines & Middleware
    middleware: Dict[str, int] = {}
    for engine in (
        b"Opera",
        b"MRE",
        b"VXP",
        b"Java",
        b"MIDP",
        b"KVM",
        b"JVM",
        b"NES",
        b"VoLTE",
        b"GPRS",
        b"Bluetooth",
    ):
        count = len(re.findall(rb"\b" + engine + rb"\b", data))
        if count > 0:
            middleware[engine.decode("ascii")] = count

    # 11. Supported File Extensions
    ext_pattern = re.compile(
        rb"\.(?:vxp|jar|jad|nes|mp3|wav|amr|aac|mp4|3gp|avi|flv|jpg|png|gif|bmp|txt|ini|dat)\b",
        re.IGNORECASE,
    )
    ext_counts: Dict[str, int] = {}
    for ext in ext_pattern.findall(data):
        k = ext.decode("ascii").lower()
        ext_counts[k] = ext_counts.get(k, 0) + 1

    # 12. Secret Dialer Codes
    dialer_matches = re.findall(rb"\*#[0-9*#]+#", data)
    dialer_codes = sorted(list(set(d.decode("ascii") for d in dialer_matches)))

    # 13. HMD FOTA update endpoints
    fota_urls = set()
    for m in re.finditer(rb"https?://[A-Za-z0-9_.-]*hmdglobal[A-Za-z0-9_./?&=%-]+", data):
        fota_urls.add(m.group(0).decode("ascii"))

    return {
        "build_strings": build_strings,
        "versions": versions,
        "compile_dates": compile_dates,
        "chipsets": sorted(list(chipsets)),
        "product_tags": sorted(list(product_tags)),
        "rtos_tasks": tasks,
        "sci_apis": sci_apis,
        "lcd_drivers": sorted(list(lcd_drivers)),
        "pmic": pmic_detected,
        "audio_dsp": "Spreadtrum VBC Audio Engine" if audio_vbc else None,
        "middleware": middleware,
        "file_extensions": ext_counts,
        "dialer_codes": dialer_codes,
        "fota_urls": sorted(list(fota_urls)),
    }


# ==============================================================================
# Single Binary Report Presentation
# ==============================================================================

def print_binary_report(filepath: str, data: bytes, verbose: bool = False) -> Dict[str, Any]:
    file_size = len(data)
    file_sha256 = hashlib.sha256(data).hexdigest()
    fname = os.path.basename(filepath)
    stem = fname.replace(".bin", "").lower()
    role = PARTITION_ROLES.get(stem, "Firmware Binary Image")

    dhtb = parse_dhtb_header(data)
    meta = analyze_mocor_metadata(data)

    print("=" * 72)
    print("  MOCOR FIRMWARE BINARY AUDIT")
    print(f"  File: {filepath}")
    print(f"  Role: {role}")
    print(f"  Size: {file_size:,} bytes | SHA-256: {file_sha256[:16]}...")
    print("=" * 72)

    # 1. Header & Secure Boot Structure
    if dhtb:
        print("\n[+] Unisoc DHTB Header & Secure Boot:")
        print(f"    DHTB Version:       {dhtb['version']}")
        print(f"    Payload Size:       {dhtb['payload_size']:,} bytes (0x{dhtb['payload_size']:08X})")
        print(f"    Header Size:        {dhtb['header_size']} bytes")
        print(f"    Execution Entry:    {dhtb['vector_type']}")
        print(f"    Vector Preview:     {dhtb['vector_preview']}")

        if dhtb["has_sec_header"]:
            print("    -------------------------------------------------------------")
            print(f"    Security Header:    Present at offset 0x{dhtb['sec_offset']:08X}")
            print(f"    Certificate Type:   {dhtb.get('cert_type', 'Unknown')}")
            print(f"    RSA Key Length:     {dhtb.get('key_bits', 'Unknown')} bits")
            print(f"    Public Exponent:    {dhtb.get('public_exponent', 'Unknown')}")
            if "rsa_modulus_sha256" in dhtb:
                print(f"    RSA Key Modulus:    SHA256: {dhtb['rsa_modulus_sha256']}")
                print(f"    Modulus Preview:    {dhtb['rsa_modulus_preview']}")
                if verbose:
                    print(f"    Full Modulus (hex): {dhtb['rsa_modulus_hex']}")
            if "cert_sha256" in dhtb:
                print(f"    Certificate Hash:   SHA256: {dhtb['cert_sha256']}")
            if "signature_size" in dhtb:
                print(f"    Signature Block:    {dhtb['signature_size']} bytes | SHA256: {dhtb['signature_sha256'][:16]}...")
    else:
        print("\n[-] DHTB Header: None (Raw unheadered partition binary)")

    # 2. Build & Target System
    print("\n[+] Build & System Identification:")
    if meta["build_strings"]:
        for s in meta["build_strings"]:
            print(f"    • MOCOR Banner:     {s}")
    else:
        print("    • MOCOR Banner:     Not explicitly identified")

    if meta["versions"]:
        for v in meta["versions"]:
            print(f"    • Software Version: {v}")
    if meta["compile_dates"]:
        print(f"    • Compile Dates:    {', '.join(meta['compile_dates'])}")
    if meta["chipsets"]:
        print(f"    • Chipset Target:   {', '.join(meta['chipsets'])}")
    if meta["product_tags"]:
        print(f"    • Product Tags:     {', '.join(meta['product_tags'])}")

    # 3. Hardware & Drivers
    print("\n[+] Hardware & Peripherals Detected:")
    if meta["lcd_drivers"]:
        print("    • Display Drivers:  " + ", ".join(meta["lcd_drivers"][:6]))
        if len(meta["lcd_drivers"]) > 6:
            print(f"                        ... and {len(meta['lcd_drivers']) - 6} more driver entrypoints")
    else:
        print("    • Display Drivers:  None directly referenced in this partition")

    if meta["pmic"]:
        print(f"    • PMIC Chip:        {', '.join(meta['pmic'])}")
    if meta["audio_dsp"]:
        print(f"    • Audio Subsystem:  {meta['audio_dsp']}")

    # 4. RTOS Tasks
    print(f"\n[+] RTOS Task Registry ({len(meta['rtos_tasks'])} tasks discovered):")
    if meta["rtos_tasks"]:
        sample_tasks = meta["rtos_tasks"] if verbose else meta["rtos_tasks"][:15]
        for t in sample_tasks:
            print(f"    • {t}")
        if len(meta["rtos_tasks"]) > 15 and not verbose:
            print(f"    ... and {len(meta['rtos_tasks']) - 15} more (use --verbose to view all)")
    else:
        print("    • No explicit static task tables in this partition")

    # 5. Middleware & Runtimes
    if meta["middleware"]:
        print("\n[+] Subsystems & Runtimes:")
        for eng, count in meta["middleware"].items():
            print(f"    • {eng:<12} ({count} occurrences)")

    # 6. Dialer & FOTA Endpoints
    if meta["dialer_codes"]:
        print(f"\n[+] Engineering & Dialer Codes ({len(meta['dialer_codes'])} found):")
        for dc in meta["dialer_codes"][:8]:
            print(f"    • {dc}")
        if len(meta["dialer_codes"]) > 8:
            print(f"    ... and {len(meta['dialer_codes']) - 8} more dialer codes")

    if meta["fota_urls"]:
        print("\n[+] HMD Global FOTA Update Endpoints:")
        for u in meta["fota_urls"]:
            print(f"    • {u}")

    print("=" * 72)

    return {
        "file": os.path.abspath(filepath),
        "size": file_size,
        "sha256": file_sha256,
        "role": role,
        "dhtb": dhtb,
        "metadata": meta,
    }


# ==============================================================================
# Firmware Directory Scanner Mode
# ==============================================================================

def analyze_firmware_directory(dirpath: str, verbose: bool = False) -> Dict[str, Any]:
    print("=" * 78)
    print("  FIRMWARE DIRECTORY & PARTITION MAP AUDIT")
    print(f"  Target Directory: {os.path.abspath(dirpath)}")
    print("=" * 78)

    # Check for pac_header.json
    pac_hdr_path = os.path.join(dirpath, "pac_header.json")
    pac_info: Optional[Dict[str, Any]] = None
    if os.path.isfile(pac_hdr_path):
        try:
            with open(pac_hdr_path, "r", encoding="utf-8") as f:
                pac_info = json.load(f)
            print("[+] PAC Container Header:")
            print(f"    Product Name:    {pac_info.get('product_name', 'N/A')}")
            print(f"    Product Version: {pac_info.get('product_version', 'N/A')}")
            print(f"    Flash Type:      {pac_info.get('flash_type', 'N/A')}")
            print(f"    Total Image Sz:  {pac_info.get('actual_size', 0):,} bytes")
            print("------------------------------------------------------------------------------")
        except Exception:
            pass

    # Scan all .bin files
    bin_files = sorted([f for f in os.listdir(dirpath) if f.endswith(".bin")])
    if not bin_files:
        print(f"[-] No .bin partition images found in {dirpath}", file=sys.stderr)
        return {}

    print(f"{'Partition Binary':<22} | {'Size':>12} | {'DHTB':<5} | {'SecBoot':<8} | {'Role'}")
    print("-" * 78)

    partitions: List[Dict[str, Any]] = []
    for fname in bin_files:
        fpath = os.path.join(dirpath, fname)
        sz = os.path.getsize(fpath)
        with open(fpath, "rb") as f:
            data = f.read()

        stem = fname.replace(".bin", "").lower()
        role = PARTITION_ROLES.get(stem, "Firmware Partition")
        dhtb = parse_dhtb_header(data)
        has_dhtb = dhtb is not None
        sec_status = "None"
        if dhtb and dhtb.get("has_sec_header"):
            c_type = dhtb.get("cert_type", "")
            sec_status = "RootCert" if "Root" in c_type else "Signed"

        print(f"{fname:<22} | {sz:>10,} B | {str(has_dhtb):<5} | {sec_status:<8} | {role[:25]}")

        partitions.append({
            "filename": fname,
            "path": fpath,
            "size": sz,
            "sha256": hashlib.sha256(data).hexdigest(),
            "role": role,
            "dhtb": dhtb,
            "has_dhtb": has_dhtb,
            "sec_status": sec_status,
        })

    print("=" * 78)

    # Now run deep inspection on key core partitions if present
    print("\n>>> DEEP INSPECTION OF CORE PARTITIONS <<<")
    key_targets = ["kernel.bin", "EXEC_KERNEL_IMAGE.bin", "user.bin", "EXEC_USER_IMAGE.bin", "boot0.bin", "boot1.bin"]
    inspected_count = 0
    for target in key_targets:
        target_path = os.path.join(dirpath, target)
        if os.path.isfile(target_path):
            with open(target_path, "rb") as f:
                tdata = f.read()
            print()
            print_binary_report(target_path, tdata, verbose=verbose)
            inspected_count += 1

    return {
        "directory": os.path.abspath(dirpath),
        "pac_info": pac_info,
        "partitions": partitions,
    }


# ==============================================================================
# CLI Entry Point
# ==============================================================================

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Inspect Unisoc / Spreadtrum MOCOR firmware images & partitions.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Inspect single partition image:
  python3 tools/mocor-info.py firmware/dumps/ta1480_10.02.17.00/kernel.bin

  # Audit full firmware dump directory:
  python3 tools/mocor-info.py firmware/dumps/ta1480_10.02.17.00/

  # Audit extracted PAC update package:
  python3 tools/mocor-info.py firmware/extracted/v23_update_Nokia_2660/

  # Export complete structured JSON audit:
  python3 tools/mocor-info.py firmware/dumps/ta1480_10.02.17.00/kernel.bin --json kernel_info.json
""",
    )
    parser.add_argument("target", help="Path to MOCOR binary (.bin) or firmware directory")
    parser.add_argument("--json", type=str, metavar="JSON_FILE", help="Export structured audit report as JSON")
    parser.add_argument("-v", "--verbose", action="store_true", help="Print all RTOS tasks and full RSA modulus")

    args = parser.parse_args()

    if not os.path.exists(args.target):
        print(f"[-] Target not found: {args.target}", file=sys.stderr)
        sys.exit(1)

    if os.path.isdir(args.target):
        audit_data = analyze_firmware_directory(args.target, verbose=args.verbose)
    else:
        with open(args.target, "rb") as f:
            data = f.read()
        audit_data = print_binary_report(args.target, data, verbose=args.verbose)

    if args.json:
        with open(args.json, "w", encoding="utf-8") as jf:
            json.dump(audit_data, jf, indent=2)
        print(f"\n[+] Exported structured JSON audit to {args.json}")


if __name__ == "__main__":
    main()
