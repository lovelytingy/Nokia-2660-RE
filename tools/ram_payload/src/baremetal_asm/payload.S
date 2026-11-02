/*
 * Bare-Metal RAM Payload for Nokia 2660 Flip (TA-1480 / Unisoc UMS9117)
 * 
 * Loaded into DRAM at 0x80100000 via FDL1 USB download mode.
 * Entry point is at offset +0x200 (0x80100200).
 * 100% Non-destructive: runs strictly in volatile RAM without NAND writes.
 * Sends BSL_REP_ACK (0x80) back to host over USB and returns cleanly to FDL1.
 */

.syntax unified
.arm
.global _start

.text
.org 0x0000

_start:
    /* Preserve callee-saved registers and return address to FDL1 */
    push    {r4-r11, lr}

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

    /* Counter test loop: executes 1,000,000 iterations */
    mov     r2, #0
    ldr     r3, =0x000FFFFF

count_loop:
    add     r2, r2, #1
    str     r2, [r0, #0x0C]
    subs    r3, r3, #1
    bne     count_loop

    /* Final Status: Execution Completed Successfully (0x00000001) */
    mov     r1, #1
    str     r1, [r0, #0x10]

    /* Send BSL_REP_ACK (0x80) back to host via FDL1's send_ack at 0x00006c23 */
    mov     r0, #0x80
    ldr     r1, =0x00006c23
    blx     r1

    /* Return cleanly to FDL1 dispatcher */
    pop     {r4-r11, lr}
    bx      lr
