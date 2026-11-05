.syntax unified
.arm
.section .text.entry, "ax"
.global _start

_start:
    /* Set stack pointer in DRAM scratch area */
    movw    sp, #0x5000
    movt    sp, #0x8070
    /* Jump directly to main_entry in payload.c */
    b       main_entry
