// SPDX-License-Identifier: Apache-2.0
//
// Per-arch trampoline pages: tiny RX-mapped code that jumps to a target
// function pointer. Installed as the JNI entry of a hooked ArtMethod.

#ifndef ARTHOOK_TRAMPOLINE_TRAMPOLINE_H_
#define ARTHOOK_TRAMPOLINE_TRAMPOLINE_H_

#include <cstddef>

namespace arthook {

struct TrampolinePage {
    void* base = nullptr;   // mmap base, pass to munmap
    size_t size = 0;        // page-rounded mmap size
    void* entry = nullptr;  // first instruction
};

// Returns an invalid page (base == nullptr) on failure.
TrampolinePage BuildTrampoline(void* target);

void FreeTrampoline(TrampolinePage page);

}  // namespace arthook

#endif  // ARTHOOK_TRAMPOLINE_TRAMPOLINE_H_
