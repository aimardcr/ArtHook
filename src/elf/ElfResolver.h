// SPDX-License-Identifier: Apache-2.0
//
// In-process ELF symbol resolver for libart.so. Walks the in-memory .dynsym
// (and the on-disk .symtab as a fallback) since dlsym hides ART internals.

#ifndef ARTHOOK_ELF_ELFRESOLVER_H_
#define ARTHOOK_ELF_ELFRESOLVER_H_

#include <cstddef>

namespace arthook {

// Resolve an exact symbol name in libart.so. Returns nullptr if not found.
void* ResolveLibartSymbol(const char* name);

// Resolve the first symbol whose name starts with `prefix`. Useful for
// file-local mangled symbols (the `L`-prefixed variants) whose middle digits
// shift across versions.
void* ResolveLibartSymbolPrefix(const char* prefix);

// Resolve the first of `count` candidate names that exists.
void* ResolveLibartSymbolAny(const char* const* names, size_t count);

}  // namespace arthook

#endif  // ARTHOOK_ELF_ELFRESOLVER_H_
