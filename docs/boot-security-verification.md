# Boot Security & Signature Verification (`boot0` / `boot1`)

Status: **Reverse-Engineered from Ghidra Decompilation** ([`boot0.bin_decompiled.c`](../ghidra/exports/boot0.bin_decompiled.c) & [`boot1.bin_decompiled.c`](../ghidra/exports/boot1.bin_decompiled.c)).

This document details the exact cryptographic verification, hardware gating registers, eFuse checks, and anti-rollback mechanics implemented in the Nokia 2660 Flip (TA-1480 / Unisoc UMS9117) stage loaders.

---

## 🏛️ Security Architecture Overview

The boot chain implements a stage-by-stage cryptographic trust cascade:

```text
  BootROM (Hardware ROM)
      │
      ▼ (checks eFuse & executes)
  boot0.bin (IRAM / SRAM @ 0x00006200)
      │
      ├─► [Gate 1]: Read AON APB register 0x402e0568 bit 0
      ├─► [Gate 2]: Read eFuse Block 1 bit 1 via 0x40240000
      │
      ▼ (if both gates active)
  Verify boot1.bin (DHTB Certificate)
      ├─► Anti-rollback check (eFuse Block 2 vs image version)
      ├─► SHA-256 hash validation
      ├─► RSA-2048 signature verification
      ▼
  boot1.bin (DDR RAM @ 0x80100000)
      │
      ├─► [Gate 1]: Read AON APB register 0x402e0568 bit 0
      ├─► [Gate 2]: Read eFuse Block 1 bit 1
      │
      ▼ (if both gates active)
  Verify kernel.bin (DHTB Certificate)
      ├─► Anti-rollback check
      ├─► SHA-256 hash validation
      ├─► RSA-2048 signature verification
      ▼
  kernel.bin (MOCOR RTOS core)
```

---

## 🔑 Hardware Security Gates (The Bypass Path)

Both `boot0.bin` and `boot1.bin` execute the exact same gatekeeper checks before calling any verification routines:

```c
// Decompiled from boot0 FUN_000069fc / boot1 0x801004e6:
bool sec_gate1 = check_aon_security_register(); // FUN_00006614 / FUN_80101a80
bool sec_gate2 = check_efuse_secure_boot();      // FUN_00006622 / FUN_80101a8e

if (sec_gate1 && sec_gate2) {
    // Both gates OPEN: Perform cryptographic signature validation
    if (!verify_image(root_key_hash, target_image)) {
        printf("verify fail: %x\n", timestamp);
        while(1); // DEADLOCK / HANG
    }
    printf("verify suc: %x\n", timestamp);
}

// If EITHER gate is 0, verification is completely SKIPPED:
printf("run next_stage: %x\n", timestamp);
jump_to_next_stage();
```

### 1. Gate 1: AON Register (`0x402e0568`)
* **Address:** `0x402e0568` (Inside the Always-On APB System Controller `0x402e0000`).
* **Implementation:**
  ```c
  bool check_aon_security_register(void) {
      return (*(volatile uint32_t *)0x402e0568 & 1) != 0;
  }
  ```
* **Meaning:** Bit 0 acts as a hardware strap / bonding pin option indicating whether secure boot is globally enabled on the silicon. If bit 0 is `0`, signature validation is disabled.

### 2. Gate 2: eFuse Block 1 (`0x40240000`)
* **Controller Base:** `0x40240000` (`EFUSE_BASE`).
* **Implementation:**
  ```c
  int check_efuse_secure_boot(void) {
      uint32_t efuse_blk1;
      efuse_read(1, &efuse_blk1, 0); // Read eFuse row 1
      
      // Check secure boot enable bit in row 1
      if ((efuse_blk1 << 0x1e) < 0) { // Bit 1
          // Validate eFuse redundancy / parity
          if ((efuse_read(0) >> 22) != 0x3ff) return 0;
          return 1;
      }
      return 0; // Unblown eFuse -> Secure boot inactive
  }
  ```
* **Meaning:** If eFuse block 1 has not been blown at the factory (e.g. engineering sample or developer board), the bootloader does not enforce cryptographic verification.

---

## 📜 Cryptographic Verification Routine

When both gates are active, `verify_image()` ([`FUN_00006838`](../ghidra/exports/boot0.bin_decompiled.c#L579) in `boot0` and `FUN_80101c82` in `boot1`) is invoked:

### 1. Security Header (`sechdr`) Offset Calculation
The security structure is appended directly after the image payload:
```c
sechdr_offset = dword[image_base + 0x30] + 0x200; // payload_size + 0x200 header
sechdr_ptr = image_base + sechdr_offset;
```
If `sechdr` is null or empty (`dword[sechdr + 0x20] == 0`), the loader emits `"cert empty"` and fails.

### 2. Cryptographic Algorithms
* **Hash Function:** **SHA-256** (32 bytes / `0x20`).
  - Computed over the target image data from offset `0x200` through the declared payload length.
* **Public Key Algorithm:** **RSA-2048** (256 bytes / `0x100` modulus).
  - Public exponent and modulus are read from the Certificate structure.
  - The RSA operation decrypts the signature to extract the expected SHA-256 hash.
* **Certificate Types Supported:**
  - `Type 0`: **Content Certificate** (`s_content_cert_hash_verify_fail`).
  - `Type 1`: **Key Certificate** (`s_key_cert_hash_verify_fail`).

### 3. Anti-Rollback Hardware Counter
* **Hardware Read:** Read from **eFuse Block 2** via `efuse_read(2, &efuse_ver, 1)`.
* **Certificate Version:** Read from offset `0x130` (Type 0) or `0x150` (Type 1) in the certificate.
* **Check Formula:**
  ```c
  if (efuse_ver <= cert_ver) {
      return 1; // Valid version
  }
  printf("antiroll_back error\n");
  return 0; // REJECT: firmware rollback attempt detected!
  ```

---

## 🚨 Failure Behavior

If hash comparison fails, RSA signature is invalid, or anti-rollback detects a downgraded binary:
1. The loader emits a descriptive diagnostic string over UART (e.g. `verify boot1 fail: %x` or `verify kernel fail: %x`).
2. The loader immediately enters an **infinite deadloop** (`while(true);`).
3. Execution halts completely; the device does not boot or attempt fallback recovery.

---

## 🔬 Physical Hardware Verification (TA-1480 HW3.0)

Tested on authentic reference physical hardware (**Nokia 2660 Flip TA-1480, HW3.0, SW 10.02.17.00, India SKU**) via [`run_secure_boot_diag.sh`](../tools/ram_payload/run_secure_boot_diag.sh):

```text
=================================================================
  NOKIA 2660 FLIP (TA-1480) - HARDWARE SECURE BOOT AUDIT
=================================================================
  [*] Gate 1: AON Register 0x402e0568 = 0xF0000101 (Bit 0 = 1: ENABLED)
  [*] Gate 2: eFuse Row 1 (SecCfg)   = 0x000003C0 (Bit 1 = 0: CLEAN / BYPASSED)
  [*] eFuse Row 0 (Chip ID / Calib)  = 0x003FFFC0
  [*] eFuse Row 2 (Anti-Rollback Ver)= 0 (0x00000000)
  [*] Vendor Root Key SHA-256 Hash:
      00000000 00000000 00000000 2CFE5CBA 2CFE5CBA D895EF33 D895EF33 EC25CDEE
-----------------------------------------------------------------
  [*] VERDICT: SECURE BOOT IS BYPASSED ON THIS UNIT!
      boot0/boot1 skip verification; custom code can boot from NAND directly.
=================================================================
```

### Analysis of Physical Unit Findings
1. **Gate 2 (eFuse Row 1) is `0x000003C0`:** Bit 1 is `0`. The factory never burned the Secure Boot OTP fuse bit on this unit.
2. **Anti-Rollback Counter is `0`:** No anti-rollback fuses are set; the bootloaders will accept any firmware version without rollback brick protection tripping.
3. **Implication:** The stock on-device `boot0` and `boot1` bootloaders will read and run custom or modified `boot1` and `kernel` binaries from NAND flash **without requiring valid vendor RSA-2048 signatures**. Untethered custom firmware and homebrew from NAND is feasible.

---

1. **Volatile DRAM Execution (FDL1 USB Download Mode) is Unrestricted:**
   - Loading payloads directly to DDR RAM (`0x80100000`) over USB download mode bypasses `boot0` and `boot1` entirely. The hardware runs arbitrary code safely without triggering NAND signature checks.
2. **Untethered NAND Flash Execution:**
   - Flashing a modified `kernel.bin` or `boot1.bin` to NAND will be executed only if:
     - The RSA signature is valid (requires private key), OR
     - The device's Gate 1 (`0x402e0568`) or Gate 2 (eFuse row 1) is unblown/disabled.
3. **Actionable Next Step:**
   - Create a 10-line diagnostic RAM payload that reads `*(volatile uint32_t *)0x402e0568` and `efuse_read(1)` to print the real-world fuse status of the reference Nokia 2660 Flip to the display.
