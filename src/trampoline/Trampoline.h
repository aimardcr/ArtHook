// SPDX-License-Identifier: Apache-2.0
//
// Per-arch trampolines: tiny executable thunks that jump to a target function
// pointer, packed into pooled RWX pages and installed as the JNI entry of a
// hooked ArtMethod.

#ifndef ARTHOOK_TRAMPOLINE_TRAMPOLINE_H_
#define ARTHOOK_TRAMPOLINE_TRAMPOLINE_H_

namespace arthook {

struct TrampolinePage {
    void* entry = nullptr;  // executable trampoline slot
};

// Returns an invalid handle (entry == nullptr) on failure.
TrampolinePage BuildTrampoline(void* target);

// No-op: pooled slots are never reclaimed (see Trampoline.cpp).
void FreeTrampoline(TrampolinePage page);

}  // namespace arthook

#endif  // ARTHOOK_TRAMPOLINE_TRAMPOLINE_H_
