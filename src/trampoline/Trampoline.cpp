// SPDX-License-Identifier: Apache-2.0
#include "trampoline/Trampoline.h"

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <mutex>

#include "util/Log.h"
#include "util/Memory.h"

extern "C" {
#if defined(__aarch64__)
extern const unsigned char arthook_tramp_arm64_template[];
extern const unsigned char arthook_tramp_arm64_template_end[];
#elif defined(__arm__)
extern const unsigned char arthook_tramp_arm_template[];
extern const unsigned char arthook_tramp_arm_template_end[];
#elif defined(__x86_64__)
extern const unsigned char arthook_tramp_x86_64_template[];
extern const unsigned char arthook_tramp_x86_64_template_end[];
#elif defined(__i386__)
extern const unsigned char arthook_tramp_x86_template[];
extern const unsigned char arthook_tramp_x86_template_end[];
#endif
}

namespace arthook {

namespace {

#if defined(__aarch64__)
constexpr size_t kPatchOffset = 8;
constexpr bool kPatchIs64 = true;
#elif defined(__arm__)
constexpr size_t kPatchOffset = 8;
constexpr bool kPatchIs64 = false;
#elif defined(__x86_64__)
constexpr size_t kPatchOffset = 6;
constexpr bool kPatchIs64 = true;
#elif defined(__i386__)
constexpr size_t kPatchOffset = 1;
constexpr bool kPatchIs64 = false;
#else
#error "Unsupported architecture for ArtHook trampoline"
#endif

const unsigned char* TemplateBegin() {
#if defined(__aarch64__)
    return arthook_tramp_arm64_template;
#elif defined(__arm__)
    return arthook_tramp_arm_template;
#elif defined(__x86_64__)
    return arthook_tramp_x86_64_template;
#elif defined(__i386__)
    return arthook_tramp_x86_template;
#endif
}

const unsigned char* TemplateEnd() {
#if defined(__aarch64__)
    return arthook_tramp_arm64_template_end;
#elif defined(__arm__)
    return arthook_tramp_arm_template_end;
#elif defined(__x86_64__)
    return arthook_tramp_x86_64_template_end;
#elif defined(__i386__)
    return arthook_tramp_x86_template_end;
#endif
}

size_t TemplateSize() {
    return static_cast<size_t>(TemplateEnd() - TemplateBegin());
}

size_t PageSize() {
    long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? static_cast<size_t>(ps) : 4096;
}

// 16-byte aligned slot, so the arm64 8-byte literal stays aligned and slots
// don't share a cache line awkwardly.
size_t SlotSize() {
    return (TemplateSize() + 15u) & ~size_t(15);
}

// Slots are packed into RWX pages: a page stays writable while it still has
// free slots (we must write future slots into it while earlier ones execute,
// so W and X coexist). Allocation allowed on Android via execmem.
std::mutex g_pool_mu;
uint8_t* g_page = nullptr;
size_t g_page_size = 0;
size_t g_page_off = 0;

}  // namespace

TrampolinePage BuildTrampoline(void* target) {
    const size_t page = PageSize();
    const size_t tsize = TemplateSize();
    const size_t slot = SlotSize();
    if (tsize == 0 || slot == 0 || slot > page) {
        LOGE("trampoline: bad template size %zu", tsize);
        return {};
    }

    std::lock_guard<std::mutex> lk(g_pool_mu);

    if (!g_page || g_page_off + slot > g_page_size) {
        // The current page is full and needs no more writes, so try to drop W
        // (RWX -> RX). Removing W from a page whose other slots are executing
        // is safe (X is kept). Best-effort: if the platform forbids the
        // transition we leave it RWX and carry on silently.
        if (g_page) ::mprotect(g_page, g_page_size, PROT_READ | PROT_EXEC);

        void* mem = ::mmap(nullptr, page, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) {
            LOGE("trampoline: RWX mmap failed (%d)", errno);
            return {};
        }
        g_page = static_cast<uint8_t*>(mem);
        g_page_size = page;
        g_page_off = 0;
    }

    uint8_t* s = g_page + g_page_off;
    g_page_off += slot;

    // Written under g_pool_mu so a concurrent fill can't downgrade this page
    // to RX before the write lands.
    std::memcpy(s, TemplateBegin(), tsize);
    if (kPatchIs64) {
        uint64_t v = reinterpret_cast<uint64_t>(target);
        std::memcpy(s + kPatchOffset, &v, sizeof(v));
    } else {
        uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target));
        std::memcpy(s + kPatchOffset, &v, sizeof(v));
    }
    __builtin___clear_cache(reinterpret_cast<char*>(s), reinterpret_cast<char*>(s) + tsize);

    TrampolinePage out;
    out.entry = s;
    return out;
}

void FreeTrampoline(TrampolinePage) {
    // No-op. Slots live in shared pooled pages, so we can't munmap one. And
    // invocation is lock-free, so a thread may still be executing a slot after
    // Unhook; reusing it would be a use-after-free. We therefore never reclaim
    // a slot. Install-failure rollback leaks one slot (rare).
}

}  // namespace arthook
