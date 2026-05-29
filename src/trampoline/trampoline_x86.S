// SPDX-License-Identifier: Apache-2.0
//
// 32-bit x86 trampoline, 6 bytes. Position-independent push-imm32 / ret
// pattern (no rip-relative addressing available on i386):
//
//   68 ?? ?? ?? ??         push imm32      ; patched target
//   c3                     ret             ; pops it into EIP

    .section .rodata
    .global  arthook_tramp_x86_template
    .global  arthook_tramp_x86_template_end
    .balign  4
arthook_tramp_x86_template:
    .byte    0x68
    .long    0xdeadbeef
    .byte    0xc3
arthook_tramp_x86_template_end:

    .section .note.GNU-stack,"",%progbits
