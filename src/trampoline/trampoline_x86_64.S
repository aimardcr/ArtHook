// SPDX-License-Identifier: Apache-2.0
//
// x86_64 trampoline, 14 bytes.
//
//   ff 25 00 00 00 00      jmp qword ptr [rip+0]    ; indirect via literal
//   <8 bytes>              patched target

    .section .rodata
    .global  arthook_tramp_x86_64_template
    .global  arthook_tramp_x86_64_template_end
    .balign  8
arthook_tramp_x86_64_template:
    .byte    0xff, 0x25, 0x00, 0x00, 0x00, 0x00
    .quad    0xdeadbeefdeadbeef
arthook_tramp_x86_64_template_end:

    .section .note.GNU-stack,"",%progbits
