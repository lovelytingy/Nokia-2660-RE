#!/usr/bin/env python3
"""Build and package a bare-metal ARM RAM payload for Unisoc UMS9117 / Nokia 2660 Flip.

Uses system clang to assemble payload.S for ARMv7-A, extracts the raw binary,
and prepends a 512-byte DHTB header so FDL1 jumps directly to the entry point (+0x200).
"""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys


def assemble_arm(source_file: str, obj_file: str) -> None:
    cmd = [
        "clang",
        "-target",
        "armv7a-none-eabi",
        "-c",
        source_file,
        "-o",
        obj_file,
    ]
    print(f"[+] Assembling {source_file} with clang...")
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[-] Clang assembly failed:\n{res.stderr}", file=sys.stderr)
        sys.exit(1)


def extract_text_section(obj_file: str) -> bytes:
    with open(obj_file, "rb") as f:
        elf = f.read()

    # ELF32 header checks
    if elf[:4] != b"\x7fELF" or elf[4] != 1:
        raise ValueError("Not a valid 32-bit ELF file")

    e_shoff = struct.unpack_from("<I", elf, 0x20)[0]
    e_shentsize = struct.unpack_from("<H", elf, 0x2E)[0]
    e_shnum = struct.unpack_from("<H", elf, 0x30)[0]
    e_shstrndx = struct.unpack_from("<H", elf, 0x32)[0]

    # Section header string table
    shstr_hdr = e_shoff + e_shstrndx * e_shentsize
    shstr_offset = struct.unpack_from("<I", elf, shstr_hdr + 0x10)[0]

    for i in range(e_shnum):
        sh = e_shoff + i * e_shentsize
        sh_name = struct.unpack_from("<I", elf, sh)[0]
        name = elf[shstr_offset + sh_name :].split(b"\x00")[0].decode("ascii", "replace")
        sh_offset = struct.unpack_from("<I", elf, sh + 0x10)[0]
        sh_size = struct.unpack_from("<I", elf, sh + 0x14)[0]
        if name == ".text":
            print(f"[+] Extracted .text section: {sh_size} bytes")
            return elf[sh_offset : sh_offset + sh_size]

    raise RuntimeError(".text section not found in ELF file")


def create_dhtb_header(payload_len: int) -> bytes:
    header = bytearray(0x200)
    # DHTB magic
    header[0:4] = b"DHTB"
    # Version 1
    struct.pack_into("<I", header, 0x04, 1)
    # Payload size at 0x30
    struct.pack_into("<I", header, 0x30, payload_len)
    return bytes(header)


def verify_disassembly(code_bytes: bytes, base_addr: int = 0x80100200) -> None:
    try:
        import capstone

        md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
        print("\n[+] Disassembly of compiled payload:")
        for instr in md.disasm(code_bytes, base_addr):
            print(f"    0x{instr.address:08X}:  {instr.mnemonic:<8} {instr.op_str}")
        print()
    except ImportError:
        print("[*] Capstone not installed; skipping disassembly verification.")


def main():
    parser = argparse.ArgumentParser(description="Build bare-metal RAM payload for Nokia 2660 Flip.")
    parser.add_argument(
        "--source",
        default="tools/ram_payload/payload.S",
        help="Path to assembly source (default: tools/ram_payload/payload.S)",
    )
    parser.add_argument(
        "--output",
        default="tools/ram_payload/ram_payload.bin",
        help="Path to output binary (default: tools/ram_payload/ram_payload.bin)",
    )
    args = parser.parse_args()

    if not os.path.isfile(args.source):
        print(f"[-] Source file not found: {args.source}", file=sys.stderr)
        sys.exit(1)

    obj_file = "/tmp/ram_payload.o"
    assemble_arm(args.source, obj_file)
    code = extract_text_section(obj_file)
    verify_disassembly(code, base_addr=0x80100200)

    header = create_dhtb_header(len(code))
    full_image = header + code

    with open(args.output, "wb") as f:
        f.write(full_image)

    print(f"[+] Successfully built: {args.output}")
    print(f"    Total Size:   {len(full_image)} bytes (512-byte DHTB header + {len(code)} bytes code)")
    print(f"    Entry Point:  0x80100200 in DRAM")


if __name__ == "__main__":
    main()
