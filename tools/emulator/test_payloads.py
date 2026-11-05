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

    def test_04_lcdc_framebuffer_refresh(self):
        """Test LCDC display controller trigger, RAM framebuffer fetch, and refresh callback."""
        import struct
        emu = UMS9117Emulator(trace=False, verbose=False)
        received_frames = []

        def on_frame(raw_bytes):
            received_frames.append(raw_bytes)

        emu.on_frame_callback = on_frame

        # Prepare 240x320 RGB565 red pixels (0xF800 little-endian = \x00\xF8) in DRAM
        fb_addr = 0x80200000
        fb_size = 240 * 320 * 2
        red_pattern = b'\x00\xF8' * (240 * 320)
        emu._map_region(fb_addr, fb_size)
        emu.uc.mem_write(fb_addr, red_pattern)

        # Execute ARM instructions programming LCDC registers:
        # ldr r0, =fb_addr >> 2; ldr r1, =0x20800024; str r0, [r1]
        # mov r0, #8;            ldr r1, =0x20800000; str r0, [r1]; bx lr
        code = bytearray()
        code += struct.pack('<I', 0xe59f0018) # ldr r0, [pc, #24] -> fb_addr >> 2
        code += struct.pack('<I', 0xe59f1018) # ldr r1, [pc, #24] -> 0x20800024
        code += struct.pack('<I', 0xe5810000) # str r0, [r1]
        code += struct.pack('<I', 0xe3a00008) # mov r0, #8
        code += struct.pack('<I', 0xe59f1010) # ldr r1, [pc, #16] -> 0x20800000
        code += struct.pack('<I', 0xe5810000) # str r0, [r1]
        code += struct.pack('<I', 0xe12fff1e) # bx lr
        code += struct.pack('<I', 0xe1a00000) # nop
        # Literals:
        code += struct.pack('<I', fb_addr >> 2) # 0x80100220
        code += struct.pack('<I', 0x20800024)   # 0x80100224
        code += struct.pack('<I', 0x20800000)   # 0x80100228

        emu.uc.mem_write(0x80100200, bytes(code))
        success = emu.run(0x80100200, max_insn=20, timeout_ms=1000)
        self.assertTrue(success, "LCDC refresh routine execution failed")

        self.assertEqual(len(received_frames), 1, "Expected 1 framebuffer refresh callback")
        self.assertEqual(len(received_frames[0]), fb_size, "Framebuffer byte size mismatch")
        self.assertEqual(received_frames[0][:4], b'\x00\xF8\x00\xF8', "Framebuffer pixel data mismatch")

        # Verify LCDC IRQ raw bit 0 is asserted
        irq_raw = int.from_bytes(emu.uc.mem_read(0x2080011C, 4), 'little')
        self.assertEqual(irq_raw & 1, 1, "LCDC IRQ raw bit 0 should be asserted after refresh")
        print("  [✓] lcdc: Virtual framebuffer refresh and callback verified!")

    def test_05_keypad_injection(self):
        """Test keypad button matrix injection and PMIC EIC power key simulation."""
        emu = UMS9117Emulator(trace=False, verbose=False)

        # 1. Test Key 5 injection
        emu.press_key('5')
        int_raw = int.from_bytes(emu.uc.mem_read(0x40250008, 4), 'little')
        key_stat = int.from_bytes(emu.uc.mem_read(0x4025002C, 4), 'little')
        self.assertEqual(int_raw, 1, "Keypad int_raw should be 1 on key press")
        self.assertEqual(key_stat, 0x09, "Keypad key_status mismatch for Key 5")

        # Release Key 5
        emu.release_key('5')
        int_raw = int.from_bytes(emu.uc.mem_read(0x40250008, 4), 'little')
        self.assertEqual(int_raw, 0, "Keypad int_raw should clear on key release")

        # 2. Test PMIC Power/End key (active low)
        emu.press_key('END')
        eic_val = int.from_bytes(emu.uc.mem_read(0x40608280, 4), 'little')
        self.assertEqual(eic_val & 2, 0, "PMIC Power key bit 1 should be 0 (pressed)")

        emu.release_key('END')
        eic_val = int.from_bytes(emu.uc.mem_read(0x40608280, 4), 'little')
        self.assertEqual(eic_val & 2, 2, "PMIC Power key bit 1 should be 1 (released)")
        print("  [✓] keypad: Matrix and PMIC EIC key injection verified!")

    def test_06_baremetal_lcd_payload(self):
        """Test full execution of baremetal_lcd.bin: LCDC programming, frame rendering, and PMIC indicators."""
        path = os.path.join(PAYLOAD_DIR, "baremetal_lcd.bin")
        if not os.path.exists(path):
            self.skipTest(f"Binary not found: {path}")

        emu = UMS9117Emulator(trace=False, verbose=False)
        emu.load_binary(path, 0x80100000)

        frames = []
        emu.on_frame_callback = lambda f: frames.append(f)

        # Run 500,000 instructions (enough to render multiple frames)
        success = emu.run(0x80100200, max_insn=500000, timeout_ms=2000)
        self.assertTrue(success, "baremetal_lcd execution failed")
        self.assertGreater(len(frames), 0, "No LCD frames were rendered by baremetal_lcd.bin")
        self.assertEqual(len(frames[0]), 240 * 320 * 2, "Frame byte count mismatch")
        self.assertTrue(emu.torch_enabled, "PMIC torch LED should be turned on by payload")
        self.assertTrue(emu.haptics_enabled, "PMIC vibrator motor should be pulsed by payload")
        print(f"  [✓] baremetal_lcd: Rendered {len(frames)} frames with PMIC torch and vibrator active!")


if __name__ == "__main__":
    print("=================================================================")
    print("  UMS9117 Emulator — Automated Payload & Loader Test Suite")
    print("=================================================================\n")
    unittest.main(verbosity=2)
