#!/usr/bin/env python3
"""
ums9117_emu.py — Unicorn-Based CPU & Peripheral Emulator for Unisoc UMS9117

Emulates the ARM Cortex-A7 (ARMv7-A) core of the Nokia 2660 Flip (TA-1480).
Used to test bare-metal RAM payloads, stage loaders (boot0, boot1, fdl1, fdl2),
and peripheral interactions without physical hardware.

Supported Simulated Hardware:
- Memory: IRAM (0x00006200), DDR RAM (0x80100000), Mailbox RAM (0x80700000)
- UART1 Serial Console: 0x70100000 (TX FIFO auto-emits to console/log)
- SC2720 PMIC & ADI Bus: 0xA1600000 / 0x40608000 (Flashlight, Vibrator, Power Key)
- Boot Key Pinmux: 0x402a0018 (Simulates holding Key 7 for UART, Key 3 for JTAG)
- SPI Display Controller: GC9307 240x320 framebuffer capture
- FDL1 Dispatcher Stub: 0x00006c22 (send_ack acknowledgment and clean return)
"""

import sys
import os
import argparse
import struct
import time

try:
    from unicorn import (
        Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_MODE_THUMB,
        UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE,
        UC_HOOK_MEM_UNMAPPED, UC_MEM_READ, UC_MEM_WRITE,
        UcError
    )
    from unicorn.arm_const import (
        UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
        UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
        UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
        UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
        UC_ARM_REG_CPSR
    )
except ImportError:
    sys.stderr.write("[-] Error: 'unicorn' library not found. Install via: pip install unicorn\n")
    sys.exit(1)

try:
    import capstone
    HAVE_CAPSTONE = True
except ImportError:
    HAVE_CAPSTONE = False


PAGE_SIZE = 4096

def align_down(addr):
    return addr & ~(PAGE_SIZE - 1)

def align_up(size):
    return (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)


class UMS9117Emulator:
    def __init__(self, trace=False, verbose=False):
        self.trace = trace
        self.verbose = verbose
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)

        if HAVE_CAPSTONE:
            self.cs_arm = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
            self.cs_thumb = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)

        # Emulation state & statistics
        self.insn_count = 0
        self.uart_output = []
        self.torch_enabled = False
        self.torch_level = 0
        self.haptics_enabled = False
        self.haptics_level = 0
        self.boot_key_mode = 0x00000000 # Default normal

        # Virtual 240x320 RGB565 Framebuffer
        self.fb_width = 240
        self.fb_height = 320
        self.framebuffer = bytearray(self.fb_width * self.fb_height * 2)
        self.on_frame_callback = None

        # Simulated Peripheral Registers
        self.mmio_storage = {}
        self.mapped_intervals = []

        # Initialize Base Memory Layout
        self._init_memory()
        self._init_hooks()

    def _map_region(self, base, size):
        start = align_down(base)
        end = align_up(base + size)
        if end <= start:
            return
        curr = start
        while curr < end:
            found = False
            for s, e in self.mapped_intervals:
                if s <= curr < e:
                    curr = e
                    found = True
                    break
                elif curr < s:
                    map_end = min(s, end)
                    self.uc.mem_map(curr, map_end - curr)
                    self.mapped_intervals.append([curr, map_end])
                    self.mapped_intervals.sort()
                    curr = map_end
                    found = True
                    break
            if not found:
                self.uc.mem_map(curr, end - curr)
                self.mapped_intervals.append([curr, end])
                self.mapped_intervals.sort()
                curr = end

        # Merge adjacent intervals
        merged = []
        for interval in self.mapped_intervals:
            if not merged or merged[-1][1] < interval[0]:
                merged.append(interval)
            else:
                merged[-1][1] = max(merged[-1][1], interval[1])
        self.mapped_intervals = merged

    def _init_memory(self):
        # 1. IRAM / SRAM (128 KB at 0x00000000)
        self._map_region(0x00000000, 128 * 1024)

        # 2. Main DRAM (32 MB at 0x80000000 - 0x82000000)
        self._map_region(0x80000000, 32 * 1024 * 1024)

        # 3. Mailbox Test Region (0x80700000, 64 KB)
        self._map_region(0x80700000, 64 * 1024)

        # 4. FDL1 Ack stub at 0x00006C22 (Thumb "bx lr" = 0x4770)
        # Allows payloads returning to FDL1 to acknowledge cleanly
        self.uc.mem_write(0x00006C22, b'\x70\x47')

        # 5. Return target halt instruction at 0x80100FF0 ("b ." = 0xeafffffe)
        self.halt_address = 0x80100FF0
        self.uc.mem_write(self.halt_address, b'\xfe\xff\xff\xea')

        # 6. Map common MMIO peripheral blocks
        # LCD Controller: 0x20800000
        self._map_region(0x20800000, 0x2000)
        # AHB Clock & Power: 0x20E00000
        self._map_region(0x20E00000, 0x4000)
        # Hardware 1ms Timer: 0x40230000
        self._map_region(0x40230000, 0x1000)
        # Keypad Matrix: 0x40250000
        self._map_region(0x40250000, 0x1000)
        # AON / Pinmux: 0x402A0000
        self._map_region(0x402A0000, 0x2000)
        # Secure Boot & System Control: 0x402E0000
        self._map_region(0x402E0000, 0x2000)
        # PMIC ADI Base: 0x40600000
        self._map_region(0x40600000, 0x1000)
        # PMIC EIC & APB: 0x40608000
        self._map_region(0x40608000, 0x4000)
        # UART1: 0x70100000
        self._map_region(0x70100000, 0x2000)
        # APB Clock Gates: 0x71300000
        self._map_region(0x71300000, 0x1000)
        # ADI Controller: 0xA1600000
        self._map_region(0xA1600000, 0x2000)
        # Analog Die Window: 0xA1610000
        self._map_region(0xA1610000, 0x2000)

    def _init_hooks(self):
        # Memory Read / Write hooks on peripherals
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self._hook_mem_write)
        self.uc.hook_add(UC_HOOK_MEM_READ, self._hook_mem_read)
        self.uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._hook_mem_unmapped)

        if self.trace:
            self.uc.hook_add(UC_HOOK_CODE, self._hook_code)

    def set_boot_key(self, mode):
        """Simulate holding hardware boot keys at power-on."""
        if mode == 'uart':
            self.boot_key_mode = 0x40000000 # Key 7
        elif mode == 'jtag':
            self.boot_key_mode = 0xE0000000 # Key 3
        else:
            self.boot_key_mode = 0x00000000

    def _hook_mem_write(self, uc, access, address, size, value, user_data):
        self.mmio_storage[address] = (value, size)

        # 1. UART1 Transmit FIFO (0x70100000)
        if address == 0x70100000:
            ch = chr(value & 0xFF)
            self.uart_output.append(ch)
            sys.stdout.write(ch)
            sys.stdout.flush()
            return

        # 2. PMIC Torch LED (0x40608df4)
        if address == 0x40608df4:
            enabled = bool(value & 0x8000)
            level = value & 0x0F
            if enabled != self.torch_enabled or level != self.torch_level:
                self.torch_enabled = enabled
                self.torch_level = level
                state = f"ON (level {level}/15)" if enabled else "OFF"
                print(f"[EMU:PMIC] Flashlight Torch: {state}")
            return

        # 3. PMIC Vibrator Motor LDO (0x40608e00)
        if address == 0x40608e00:
            enabled = bool(value & 0x8000)
            level = value & 0x07
            if enabled != self.haptics_enabled or level != self.haptics_level:
                self.haptics_enabled = enabled
                self.haptics_level = level
                state = f"ON (level {level}/7)" if enabled else "OFF"
                print(f"[EMU:PMIC] Haptic Vibrator Motor: {state}")
            return

        # 4. ADI Read Command Register (0xA1600238)
        if address == 0xA1600238:
            # Prepare ADI response at 0xA160023C with matching address tag
            tag = (value & 0x1FFFF) >> 2
            mock_val = self.mmio_storage.get(value, (0x0000, 2))[0] & 0xFFFF
            resp = (tag << 16) | mock_val # bit 31=0 (not busy)
            uc.mem_write(0xA160023C, struct.pack('<I', resp))
            return

        # 5. LCD Controller Control Register (0x20800000)
        if address == 0x20800000:
            if value & 8: # Start Refresh bit
                y_base = self.mmio_storage.get(0x20800024, (0, 4))[0]
                fb_addr = y_base << 2
                if fb_addr != 0:
                    try:
                        frame_data = uc.mem_read(fb_addr, self.fb_width * self.fb_height * 2)
                        self.framebuffer[:] = frame_data
                        if self.on_frame_callback:
                            self.on_frame_callback(frame_data)
                    except Exception as e:
                        if self.verbose:
                            print(f"[EMU:LCDC] Framebuffer read error: {e}")
                # Set LCDC IRQ raw bit 0 (refresh done) so sys_wait_refresh returns!
                uc.mem_write(0x2080011C, struct.pack('<I', 1))
            return

        # 6. LCD Controller IRQ Clear (0x20800114)
        if address == 0x20800114:
            uc.mem_write(0x2080011C, struct.pack('<I', 0))
            return

        # 7. Keypad Matrix IRQ Clear (0x40250010)
        if address == 0x40250010:
            uc.mem_write(0x40250008, struct.pack('<I', 0))
            return

        # 8. PMIC ADI Base (0x40600000) Command Register (0x40600028)
        if address == 0x40600028:
            reg_addr = value
            mock_val = self.mmio_storage.get(reg_addr, (0x0000, 2))[0] & 0xFFFF
            # Bit 31=0 indicates ready / not busy
            uc.mem_write(0x4060002C, struct.pack('<I', mock_val))
            return

    def _hook_mem_read(self, uc, access, address, size, value, user_data):
        # 1. UART1 Status Register (0x7010000C) — bit 0=0 (TX FIFO not full / ready)
        if address == 0x7010000C:
            uc.mem_write(address, b'\x00\x00\x00\x00')
            return

        # 2. Boot Key Pinmux (0x402A0018)
        if address == 0x402A0018:
            uc.mem_write(address, struct.pack('<I', self.boot_key_mode))
            return

        # 3. Secure Boot Status (0x402E0568) — Gates 1 & 2 active
        if address == 0x402E0568:
            uc.mem_write(address, struct.pack('<I', 0x00000003))
            return

        # 4. ADI Status Register (0xA1600240) — bit 0=0 (write FIFO ready)
        if address == 0xA1600240:
            uc.mem_write(address, b'\x00\x00\x00\x00')
            return

        # 5. PMIC EIC Power Key (0x40608280) — bit 1=1 (Power key not pressed)
        if address == 0x40608280:
            uc.mem_write(address, struct.pack('<I', 0x00000002))
            return

        # 6. System Timer ms (0x4023000C) — 1ms free-running counter
        if address == 0x4023000C:
            ms = int(time.time() * 1000) & 0xFFFFFFFF
            uc.mem_write(address, struct.pack('<I', ms))
            return

        # 7. UMS9117 Chip ID (0x402E00FC)
        if address == 0x402E00FC:
            uc.mem_write(address, struct.pack('<I', 0x91170000))
            return

        # 8. PMIC ADI Write Status (0x40600030) — bit 11 (0x800) = 0 (write FIFO ready)
        if address == 0x40600030:
            uc.mem_write(address, struct.pack('<I', 0x00000000))
            return

    def _hook_mem_unmapped(self, uc, access, address, size, value, user_data):
        """Auto-map unknown peripheral ranges gracefully instead of crashing."""
        page = align_down(address)
        if self.verbose:
            pc = uc.reg_read(UC_ARM_REG_PC)
            acc_str = "WRITE" if access == UC_MEM_WRITE else "READ"
            print(f"[EMU:MMIO] Auto-mapping unmapped page at 0x{page:08x} (triggered by {acc_str} @ PC=0x{pc:08x})")
        self._map_region(page, PAGE_SIZE)
        return True

    def _hook_code(self, uc, address, size, user_data):
        self.insn_count += 1
        if not HAVE_CAPSTONE or not self.trace:
            return

        # Disassemble instruction
        code_bytes = uc.mem_read(address, size)
        cpsr = uc.reg_read(UC_ARM_REG_CPSR)
        is_thumb = bool(cpsr & 0x20)
        cs = self.cs_thumb if is_thumb else self.cs_arm

        for i in cs.disasm(code_bytes, address):
            mode = "THUMB" if is_thumb else "ARM  "
            print(f"[{mode}] 0x{i.address:08x}:  {i.mnemonic:<8} {i.op_str}")

    def load_binary(self, path, base_addr):
        with open(path, 'rb') as f:
            data = f.read()
        self._map_region(base_addr, len(data))
        self.uc.mem_write(base_addr, data)
        return len(data)

    def run(self, entry_addr, max_insn=5000000, timeout_ms=5000):
        # Set default stack pointer in DRAM
        self.uc.reg_write(UC_ARM_REG_SP, 0x80705000)
        # Set return address to halt stub
        self.uc.reg_write(UC_ARM_REG_LR, self.halt_address)

        # Check Thumb vs ARM entry point
        is_thumb = bool(entry_addr & 1)
        real_entry = entry_addr & ~1

        cpsr = self.uc.reg_read(UC_ARM_REG_CPSR)
        if is_thumb:
            cpsr |= 0x20 # Set T bit
        else:
            cpsr &= ~0x20 # Clear T bit
        self.uc.reg_write(UC_ARM_REG_CPSR, cpsr)

        start_time = time.time()
        try:
            self.uc.emu_start(real_entry, self.halt_address, timeout=timeout_ms * 1000, count=max_insn)
        except UcError as err:
            pc = self.uc.reg_read(UC_ARM_REG_PC)
            print(f"\n[-] Emulation halted with exception: {err} @ PC=0x{pc:08x}")
            return False

        elapsed = time.time() - start_time
        print(f"\n[+] Emulation finished in {elapsed:.3f}s ({self.insn_count} instructions executed)")
        return True

    def read_mailbox(self, addr=0x80700100):
        """Read 5 words of payload test status from mailbox."""
        try:
            data = self.uc.mem_read(addr, 20)
            return struct.unpack('<5I', data)
        except Exception:
            return None


    def press_key(self, key_name):
        """Simulate a keypad button press event in the emulated hardware."""
        key_codes = {
            '1': 0x00, '2': 0x01, '3': 0x02,
            '4': 0x08, '5': 0x09, '6': 0x0A,
            '7': 0x10, '8': 0x11, '9': 0x12,
            '*': 0x18, '0': 0x19, '#': 0x1A,
            'LSK': 0x03, 'RSK': 0x04,
            'CALL': 0x0B, 'OK': 0x13,
            'UP': 0x0C, 'DOWN': 0x14,
            'LEFT': 0x0D, 'RIGHT': 0x15
        }
        if key_name in ('END', 'POWER'):
            # SC2720 PMIC EIC Power/End key active low (bit 1 = 0)
            self.uc.mem_write(0x40608280, struct.pack('<I', 0x00000000))
            return

        code = key_codes.get(key_name, 0x00)
        self.uc.mem_write(0x40250008, struct.pack('<I', 0x00000001))
        self.uc.mem_write(0x4025002C, struct.pack('<I', code))

    def release_key(self, key_name):
        """Simulate releasing a keypad button."""
        if key_name in ('END', 'POWER'):
            self.uc.mem_write(0x40608280, struct.pack('<I', 0x00000002))
            return
        self.uc.mem_write(0x40250008, struct.pack('<I', 0x00000000))


def main():
    parser = argparse.ArgumentParser(description="Unisoc UMS9117 CPU & Peripheral Emulator")
    parser.add_argument("binary", help="Path to raw binary payload or loader (.bin)")
    parser.add_argument("--base", type=lambda x: int(x, 0), default=0x80100000, help="Base load address (hex)")
    parser.add_argument("--entry", type=lambda x: int(x, 0), default=None, help="Execution entry point (hex)")
    parser.add_argument("--profile", choices=["payload", "boot0", "boot1", "fdl1", "fdl2"], help="Predefined memory profile")
    parser.add_argument("--boot-key", choices=["none", "uart", "jtag"], default="none", help="Hardware boot key to simulate")
    parser.add_argument("--trace", action="store_true", help="Print disassembly trace of every instruction")
    parser.add_argument("--verbose", action="store_true", help="Verbose peripheral logging")
    parser.add_argument("--max-insn", type=int, default=5000000, help="Max instruction count")
    parser.add_argument("--timeout", type=int, default=5000, help="Timeout in ms")
    parser.add_argument("--dump-mailbox", action="store_true", help="Inspect test mailbox at 0x80700100 on exit")

    args = parser.parse_args()

    if args.profile == "payload":
        base = 0x80100000
        entry = 0x80100200
    elif args.profile == "boot0":
        base = 0x00006200
        entry = 0x00006420
    elif args.profile == "boot1":
        base = 0x80100000
        entry = 0x80100220
    elif args.profile == "fdl1":
        base = 0x00006200
        entry = 0x00006420
    elif args.profile == "fdl2":
        base = 0x80100000
        entry = 0x80100220
    else:
        base = args.base
        entry = args.entry if args.entry is not None else base

    if not os.path.isfile(args.binary):
        sys.stderr.write(f"[-] Error: File not found: {args.binary}\n")
        sys.exit(1)

    print("=================================================================")
    print("  Unisoc UMS9117 (Nokia 2660 Flip) CPU & Peripheral Emulator")
    print(f"  Target Binary: {args.binary}")
    print(f"  Load Address:  0x{base:08x}")
    print(f"  Entry Point:   0x{entry:08x}")
    print("=================================================================\n")

    emu = UMS9117Emulator(trace=args.trace, verbose=args.verbose)
    emu.set_boot_key(args.boot_key)

    size = emu.load_binary(args.binary, base)
    print(f"[*] Loaded {size} bytes into virtual memory @ 0x{base:08x}")
    print(f"[*] Starting execution @ 0x{entry:08x}...")

    success = emu.run(entry, max_insn=args.max_insn, timeout_ms=args.timeout)

    if args.dump_mailbox or args.profile == "payload":
        mb = emu.read_mailbox(0x80700100)
        if mb:
            m1, m2, m3, counter, status = mb
            print("\n=== Mailbox Test Diagnostics (0x80700100) ===")
            print(f"  Marker 1: 0x{m1:08x} {'(Nokia 2660 TA-1480 OK)' if m1 == 0x14802660 else ''}")
            print(f"  Marker 2: 0x{m2:08x} {'(Exec Confirmed OK)' if m2 == 0xcafebabe else ''}")
            print(f"  Marker 3: 0x{m3:08x} {'(Payload Alive OK)' if m3 == 0xdeadbeef else ''}")
            print(f"  Counter:  {counter}")
            print(f"  Status:   0x{status:08x} {'(SUCCESS)' if status == 1 else ''}")

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
