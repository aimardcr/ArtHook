// SPDX-License-Identifier: Apache-2.0
//
// In-process ELF symbol resolver for libart.so. Walks the in-memory .dynsym
// (and the on-disk .symtab as a fallback) since dlsym hides ART internals.

#ifndef ARTHOOK_ELF_ELFRESOLVER_H_
#define ARTHOOK_ELF_ELFRESOLVER_H_

namespace arthook {

// Resolve a symbol in libart.so. Returns nullptr if not found.
void* ResolveLibartSymbol(const char* name);

}  // namespace arthook

#endif  // ARTHOOK_ELF_ELFRESOLVER_H_
