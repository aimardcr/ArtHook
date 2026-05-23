// SPDX-License-Identifier: Apache-2.0
//
// 32-bit ARM (armeabi-v7a) trampoline — 12 bytes. ARM mode, not Thumb, so it
// matches the mode of ART's other quick trampolines.
//
//   ldr  ip, [pc, #0]    ; PC reads as current+8 in ARM, so this loads
//                        ;   from offset +8 — the literal slot
//   bx   ip              ; tail-call (LR untouched); switches to Thumb if
//                        ;   the patched address has bit 0 set
//   .word <patched>

    .arm
    .section .rodata
    .global  arthook_tramp_arm_template
    .global  arthook_tramp_arm_template_end
    .balign  4
arthook_tramp_arm_template:
    .byte    0x00, 0xc0, 0x9f, 0xe5        // ldr ip, [pc, #0]
    .byte    0x1c, 0xff, 0x2f, 0xe1        // bx  ip
    .word    0xdeadbeef
arthook_tramp_arm_template_end:

    .section .note.GNU-stack,"",%progbits
