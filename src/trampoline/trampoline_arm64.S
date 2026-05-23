// SPDX-License-Identifier: Apache-2.0
//
// arm64 trampoline template — 16 bytes.
//
//   ldr  x16, [pc, #8]   ; load patched literal into IP0 (scratch per AAPCS64)
//   br   x16             ; tail-call; LR untouched
//   .quad <patched>      ; replacement fn address, written at install time
//
// x0..x7 are passed through unchanged so the JNI-shaped args land in the
// replacement intact.

    .section .rodata
    .global  arthook_tramp_arm64_template
    .global  arthook_tramp_arm64_template_end
    .balign  8
arthook_tramp_arm64_template:
    .byte    0x50, 0x00, 0x00, 0x58        // ldr x16, [pc, #8]
    .byte    0x00, 0x02, 0x1f, 0xd6        // br  x16
    .quad    0xdeadbeefdeadbeef
arthook_tramp_arm64_template_end:

    .section .note.GNU-stack,"",%progbits
