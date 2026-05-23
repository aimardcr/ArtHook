// SPDX-License-Identifier: Apache-2.0
#include "trampoline/Trampoline.h"

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>

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

}  // namespace

TrampolinePage BuildTrampoline(void* target) {
    const size_t page = PageSize();
    const size_t tsize = TemplateSize();
    if (tsize == 0 || tsize > page) {
        LOGE("trampoline: bad template size %zu", tsize);
        return {};
    }

    void* mem = ::mmap(nullptr, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        LOGE("trampoline: mmap failed (%d)", errno);
        return {};
    }

    std::memcpy(mem, TemplateBegin(), tsize);

    if (kPatchIs64) {
        uint64_t v = reinterpret_cast<uint64_t>(target);
        std::memcpy(static_cast<uint8_t*>(mem) + kPatchOffset, &v, sizeof(v));
    } else {
        uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target));
        std::memcpy(static_cast<uint8_t*>(mem) + kPatchOffset, &v, sizeof(v));
    }

    // RX flip; if SELinux/W^X policy denies it, fall back to RWX.
    if (::mprotect(mem, page, PROT_READ | PROT_EXEC) != 0) {
        if (::mprotect(mem, page, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
            LOGE("trampoline: mprotect to RX failed (%d)", errno);
            ::munmap(mem, page);
            return {};
        }
        LOGW("trampoline: fell back to RWX page");
    }

    __builtin___clear_cache(static_cast<char*>(mem), static_cast<char*>(mem) + tsize);

    TrampolinePage out;
    out.base = mem;
    out.size = page;
    out.entry = mem;
    return out;
}

void FreeTrampoline(TrampolinePage page) {
    if (page.base) ::munmap(page.base, page.size);
}

}  // namespace arthook
