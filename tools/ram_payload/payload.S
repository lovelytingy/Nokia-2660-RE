/*
 * Bare-Metal RAM Payload for Nokia 2660 Flip (TA-1480 / Unisoc UMS9117)
 * 
 * Loaded into DRAM at 0x80000000 via FDL1 USB download mode.
 * Entry point is at offset +0x200 (0x80000200).
 * 100% Non-destructive: runs strictly in volatile RAM without NAND writes.
 */

.syntax unified
.arm
.global _start

.text
.org 0x0000

_start:
    /* Disable interrupts (IRQ and FIQ) */
    cpsid   if

    /* Initialize stack pointer to a safe region in DDR */
    ldr     sp, =0x80700000

    /* Mailbox address for test status output in DRAM */
    ldr     r0, =0x80700100

    /* Magic Marker 1: Nokia TA-1480 (0x14802660) */
    ldr     r1, =0x14802660
    str     r1, [r0, #0x00]

    /* Magic Marker 2: Execution Confirmed (0xCAFEBABE) */
    ldr     r1, =0xCAFEBABE
    str     r1, [r0, #0x04]

    /* Magic Marker 3: Payload Alive (0xDEADBEEF) */
    ldr     r1, =0xDEADBEEF
    str     r1, [r0, #0x08]

    /* Counter test loop: increments counter in mailbox */
    mov     r2, #0
    ldr     r3, =0x000FFFFF

count_loop:
    add     r2, r2, #1
    str     r2, [r0, #0x0C]
    subs    r3, r3, #1
    bne     count_loop

    /* Final Status: Execution Completed Success (0x00000001) */
    mov     r1, #1
    str     r1, [r0, #0x10]

halt:
    /* Low-power wait loop */
    wfi
    b       halt
