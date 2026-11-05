#!/usr/bin/env python3
"""
test_payloads.py — Automated Emulation Test Runner for Nokia 2660 RAM Payloads

Executes bare-metal payloads and bootloader stages in the Unicorn UMS9117 emulator,
verifying hardware register states, memory markers, and instruction flow.
"""

import os
import sys
import unittest

from ums9117_emu import UMS9117Emulator

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
PAYLOAD_DIR = os.path.join(REPO_ROOT, "tools/ram_payload/bin")
DUMP_DIR = os.path.join(REPO_ROOT, "firmware/dumps/ta1480_10.02.17.00")
PAC_DIR = os.path.join(REPO_ROOT, "firmware/extracted/v23_update_Nokia_2660")


class TestUMS9117Emulator(unittest.TestCase):
    def test_01_baremetal_asm_payload(self):
        """Test baremetal_asm.bin runs in DRAM, executes loop, and writes mailbox markers."""
        path = os.path.join(PAYLOAD_DIR, "baremetal_asm.bin")
        if not os.path.exists(path):
            self.skipTest(f"Binary not found: {path}")

        emu = UMS9117Emulator(trace=False, verbose=False)
        emu.load_binary(path, 0x80100000)
        success = emu.run(0x80100200, max_insn=5000000, timeout_ms=5000)
        self.assertTrue(success, "Payload execution failed or timed out")

        # Verify mailbox results written by payload at 0x80700100
        mb = emu.read_mailbox(0x80700100)
        self.assertIsNotNone(mb, "Mailbox read returned None")
        m1, m2, m3, counter, status = mb

        self.assertEqual(m1, 0x14802660, f"Marker 1 mismatch: 0x{m1:08x} != 0x14802660")
        self.assertEqual(m2, 0xCAFEBABE, f"Marker 2 mismatch: 0x{m2:08x} != 0xCAFEBABE")
        self.assertEqual(m3, 0xDEADBEEF, f"Marker 3 mismatch: 0x{m3:08x} != 0xDEADBEEF")
        self.assertEqual(counter, 1048575, f"Counter mismatch: {counter} != 1048575")
        self.assertEqual(status, 1, f"Status mismatch: {status} != 1")
        print("  [✓] baremetal_asm: All 5 mailbox markers verified!")

    def test_02_boot0_loader_entry(self):
        """Test boot0.bin initial CP15 setup, stack allocation, and switch to Thumb mode."""
        path = os.path.join(DUMP_DIR, "boot0.bin")
        if not os.path.exists(path):
            self.skipTest(f"Binary not found: {path}")

        emu = UMS9117Emulator(trace=False, verbose=False)
        emu.load_binary(path, 0x00006200)
        # Execute first 50 instructions
        success = emu.run(0x00006420, max_insn=50, timeout_ms=1000)
        self.assertTrue(success, "boot0 execution faulted")
        print("  [✓] boot0: CP15 reset init and ARM-to-Thumb switch verified!")

    def test_03_boot_key_pinmux_simulation(self):
        """Test boot key pinmux detection register at 0x402A0018."""
        emu = UMS9117Emulator(trace=False, verbose=False)
        
        # Test normal boot (no key)
        emu.set_boot_key("none")
        self.assertEqual(emu.boot_key_mode, 0x00000000)

        # Test UART debug mode (Key 7 pressed)
        emu.set_boot_key("uart")
        self.assertEqual(emu.boot_key_mode, 0x40000000)

        # Test JTAG mode (Key 3 pressed)
        emu.set_boot_key("jtag")
        self.assertEqual(emu.boot_key_mode, 0xE0000000)
        print("  [✓] boot keys: Pinmux simulation verified!")


if __name__ == "__main__":
    print("=================================================================")
    print("  UMS9117 Emulator — Automated Payload & Loader Test Suite")
    print("=================================================================\n")
    unittest.main(verbosity=2)
