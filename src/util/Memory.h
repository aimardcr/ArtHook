// SPDX-License-Identifier: Apache-2.0
//
// memcpy-based offset reads/writes, avoids UB from type-punning through
// unrelated pointer types.

#ifndef ARTHOOK_UTIL_MEMORY_H_
#define ARTHOOK_UTIL_MEMORY_H_

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace arthook {

inline uintptr_t ReadUintPtr(const void* base, size_t offset) {
    uintptr_t v;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + offset, sizeof(v));
    return v;
}

inline uint32_t ReadU32(const void* base, size_t offset) {
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + offset, sizeof(v));
    return v;
}

inline void WriteUintPtr(void* base, size_t offset, uintptr_t value) {
    std::memcpy(static_cast<uint8_t*>(base) + offset, &value, sizeof(value));
}

inline void WriteU32(void* base, size_t offset, uint32_t value) {
    std::memcpy(static_cast<uint8_t*>(base) + offset, &value, sizeof(value));
}

}  // namespace arthook

#endif  // ARTHOOK_UTIL_MEMORY_H_
