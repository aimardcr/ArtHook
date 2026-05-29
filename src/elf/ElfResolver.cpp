// SPDX-License-Identifier: Apache-2.0
//
// ELF symbol resolution for libart.so without dlsym. Walks the in-memory
// .dynsym first; on a miss, mmaps libart.so on disk and walks .symtab (only
// useful on userdebug/eng, release libart is stripped). All table offsets
// are bounds-checked against the mapping so a malformed/obfuscated libart
// can't trigger an out-of-bounds read.

#include "elf/ElfResolver.h"

#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

#include "util/Log.h"

#if defined(__LP64__)
using Elf_Ehdr = Elf64_Ehdr;
using Elf_Shdr = Elf64_Shdr;
using Elf_Sym = Elf64_Sym;
using Elf_Dyn = Elf64_Dyn;
using Elf_Phdr = Elf64_Phdr;
constexpr int kElfClass = ELFCLASS64;
#else
using Elf_Ehdr = Elf32_Ehdr;
using Elf_Shdr = Elf32_Shdr;
using Elf_Sym = Elf32_Sym;
using Elf_Dyn = Elf32_Dyn;
using Elf_Phdr = Elf32_Phdr;
constexpr int kElfClass = ELFCLASS32;
#endif

namespace arthook {

namespace {

struct LibartInfo {
    uintptr_t base = 0;
    uintptr_t map_lo = 0;  // loaded PT_LOAD span, bounds for all reads
    uintptr_t map_hi = 0;
    std::string path;
    const Elf_Sym* dynsym = nullptr;
    const char* dynstr = nullptr;
    size_t dynsym_count = 0;
    size_t dynstr_size = 0;  // DT_STRSZ, 0 if absent
};

LibartInfo g_info;

inline bool InRange(uintptr_t p, uintptr_t lo, uintptr_t hi) {
    return p >= lo && p < hi;
}

inline bool NameMatches(const char* s, const char* q, bool prefix) {
    return prefix ? std::strncmp(s, q, std::strlen(q)) == 0 : std::strcmp(s, q) == 0;
}

// Overflow-safe: is [off, off+len) within [0, size)?
inline bool FitsIn(uint64_t off, uint64_t len, uint64_t size) {
    return off <= size && size - off >= len;
}

// Largest dynstr offset we'll trust, plus a guarantee the string is
// NUL-terminated within it. Returns false if `st_name` is out of bounds.
bool SafeStr(const char* strtab, size_t st_name, size_t strmax, const char** out) {
    if (st_name >= strmax) return false;
    const char* s = strtab + st_name;
    size_t avail = strmax - st_name;
    if (strnlen(s, avail) >= avail) return false;  // not terminated in bounds
    *out = s;
    return true;
}

int IteratePhdrCb(struct dl_phdr_info* info, size_t, void* data) {
    const char* name = info->dlpi_name ? info->dlpi_name : "";
    if (!std::strstr(name, "libart.so")) return 0;

    auto* out = static_cast<LibartInfo*>(data);
    out->base = info->dlpi_addr;
    out->path = name;

    // PT_LOAD span and PT_DYNAMIC in one pass.
    const Elf_Dyn* dyn = nullptr;
    uintptr_t lo = static_cast<uintptr_t>(-1), hi = 0;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type == PT_DYNAMIC) {
            dyn = reinterpret_cast<const Elf_Dyn*>(info->dlpi_addr + ph.p_vaddr);
        } else if (ph.p_type == PT_LOAD) {
            uintptr_t seg = info->dlpi_addr + ph.p_vaddr;
            if (seg < lo) lo = seg;
            if (seg + ph.p_memsz > hi) hi = seg + ph.p_memsz;
        }
    }
    if (lo < hi) {
        out->map_lo = lo;
        out->map_hi = hi;
    }
    if (!dyn) return 1;

    const Elf_Sym* dynsym = nullptr;
    const char* dynstr = nullptr;
    size_t syment = 0, strsz = 0;
    const uint32_t* gnu_hash = nullptr;
    const uint32_t* sysv_hash = nullptr;

    for (const Elf_Dyn* d = dyn; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_SYMTAB:
                dynsym = reinterpret_cast<const Elf_Sym*>(info->dlpi_addr + d->d_un.d_ptr);
                break;
            case DT_STRTAB:
                dynstr = reinterpret_cast<const char*>(info->dlpi_addr + d->d_un.d_ptr);
                break;
            case DT_SYMENT:
                syment = d->d_un.d_val;
                break;
            case DT_STRSZ:
                strsz = d->d_un.d_val;
                break;
            case DT_GNU_HASH:
                gnu_hash = reinterpret_cast<const uint32_t*>(info->dlpi_addr + d->d_un.d_ptr);
                break;
            case DT_HASH:
                sysv_hash = reinterpret_cast<const uint32_t*>(info->dlpi_addr + d->d_un.d_ptr);
                break;
            default:
                break;
        }
    }

    if (!dynsym || !dynstr || syment != sizeof(Elf_Sym)) return 1;
    out->dynsym = dynsym;
    out->dynstr = dynstr;
    out->dynstr_size = strsz;

    const uintptr_t mlo = out->map_lo, mhi = out->map_hi;
    const size_t max_syms =
        (mhi > reinterpret_cast<uintptr_t>(dynsym)) ? (mhi - reinterpret_cast<uintptr_t>(dynsym)) / sizeof(Elf_Sym) : 0;
    auto gap_estimate = [&]() -> size_t {
        uintptr_t span = reinterpret_cast<uintptr_t>(dynstr) - reinterpret_cast<uintptr_t>(dynsym);
        return std::min<size_t>(span / sizeof(Elf_Sym), 200000);
    };

    // SysV hash gives nchain; GNU hash needs a bounded walk; else estimate from strtab gap.
    if (sysv_hash) {
        out->dynsym_count = std::min<size_t>(sysv_hash[1], max_syms ? max_syms : sysv_hash[1]);
    } else if (gnu_hash && mlo < mhi) {
        uint32_t nbuckets = gnu_hash[0];
        uint32_t symoffset = gnu_hash[1];
        uint32_t bloom_size = gnu_hash[2];
        const uint32_t* buckets = gnu_hash + 4 + bloom_size * (sizeof(size_t) / 4);
        const uint32_t* chain = buckets + nbuckets;
        size_t count = 0;
        if (nbuckets < (1u << 20) && InRange(reinterpret_cast<uintptr_t>(buckets), mlo, mhi) &&
            InRange(reinterpret_cast<uintptr_t>(chain), mlo, mhi)) {
            uint32_t last = symoffset;
            for (uint32_t i = 0; i < nbuckets; ++i)
                if (buckets[i] > last) last = buckets[i];
            if (last >= symoffset) {
                while (static_cast<size_t>(last - symoffset) < max_syms &&
                       InRange(reinterpret_cast<uintptr_t>(&chain[last - symoffset]), mlo, mhi) &&
                       !(chain[last - symoffset] & 1))
                    ++last;
                ++last;
            }
            count = std::min<size_t>(last, max_syms);
        }
        out->dynsym_count = count ? count : gap_estimate();
    } else {
        out->dynsym_count = gap_estimate();
    }
    return 1;
}

bool EnsureInit() {
    static std::once_flag once;
    std::call_once(once, [] {
        dl_iterate_phdr(&IteratePhdrCb, &g_info);
        if (!g_info.base) LOGE("ElfResolver: libart.so not found in process");
    });
    return g_info.base != 0;  // located, file fallback works even if dynsym is null
}

void* LookupInDynsym(const char* name, bool prefix) {
    if (!g_info.dynsym || !g_info.dynstr) return nullptr;
    size_t strmax = g_info.dynstr_size;
    if (strmax == 0 && g_info.map_hi > reinterpret_cast<uintptr_t>(g_info.dynstr))
        strmax = g_info.map_hi - reinterpret_cast<uintptr_t>(g_info.dynstr);
    for (size_t i = 0; i < g_info.dynsym_count; ++i) {
        const Elf_Sym& s = g_info.dynsym[i];
        if (s.st_name == 0 || s.st_value == 0) continue;
        const char* str = nullptr;
        if (!SafeStr(g_info.dynstr, s.st_name, strmax, &str)) continue;
        if (NameMatches(str, name, prefix))
            return reinterpret_cast<void*>(g_info.base + s.st_value);
    }
    return nullptr;
}

void* LookupInFileSymtab(const char* name, bool prefix) {
    static const char* const kCandidates[] = {
#if defined(__LP64__)
        "/apex/com.android.art/lib64/libart.so",
        "/apex/com.android.runtime/lib64/libart.so",
        "/system/lib64/libart.so",
        "/system_ext/lib64/libart.so",
#else
        "/apex/com.android.art/lib/libart.so",
        "/apex/com.android.runtime/lib/libart.so",
        "/system/lib/libart.so",
        "/system_ext/lib/libart.so",
#endif
        nullptr,
    };

    // Only the loaded module's own file rebases correctly onto base; track
    // whether we opened g_info.path so we never rebase from a candidate.
    int fd = -1;
    bool opened_real = false;
    const char* path = g_info.path.c_str();
    if (*path && path[0] == '/') {
        fd = ::open(path, O_RDONLY);
        opened_real = (fd >= 0);
    }
    for (int i = 0; fd < 0 && kCandidates[i]; ++i) fd = ::open(kCandidates[i], O_RDONLY);
    if (fd < 0) {
        LOGW("ElfResolver: cannot open libart.so on disk");
        return nullptr;
    }
    if (!opened_real) {
        // A candidate may be a different build; can't trust its st_value.
        ::close(fd);
        return nullptr;
    }

    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(Elf_Ehdr))) {
        ::close(fd);
        return nullptr;
    }
    const uint64_t size = static_cast<uint64_t>(st.st_size);
    void* map = ::mmap(nullptr, static_cast<size_t>(size), PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (map == MAP_FAILED) {
        LOGW("ElfResolver: mmap libart.so failed");
        return nullptr;
    }

    void* result = nullptr;
    auto* base = static_cast<const uint8_t*>(map);
    const auto& eh = *reinterpret_cast<const Elf_Ehdr*>(base);
    if (std::memcmp(eh.e_ident, ELFMAG, SELFMAG) == 0 && eh.e_ident[EI_CLASS] == kElfClass &&
        FitsIn(eh.e_shoff, static_cast<uint64_t>(eh.e_shnum) * sizeof(Elf_Shdr), size)) {
        const auto* shdrs = reinterpret_cast<const Elf_Shdr*>(base + eh.e_shoff);
        const Elf_Shdr* symtab = nullptr;
        const Elf_Shdr* strtab = nullptr;
        for (int i = 0; i < eh.e_shnum; ++i) {
            if (shdrs[i].sh_type == SHT_SYMTAB) {
                symtab = &shdrs[i];
                if (shdrs[i].sh_link < eh.e_shnum) strtab = &shdrs[shdrs[i].sh_link];
                break;
            }
        }
        if (symtab && strtab && FitsIn(symtab->sh_offset, symtab->sh_size, size) &&
            FitsIn(strtab->sh_offset, strtab->sh_size, size)) {
            const auto* syms = reinterpret_cast<const Elf_Sym*>(base + symtab->sh_offset);
            const char* strs = reinterpret_cast<const char*>(base + strtab->sh_offset);
            size_t n = symtab->sh_size / sizeof(Elf_Sym);
            for (size_t i = 0; i < n; ++i) {
                if (syms[i].st_name == 0 || syms[i].st_value == 0) continue;
                const char* str = nullptr;
                if (!SafeStr(strs, syms[i].st_name, strtab->sh_size, &str)) continue;
                if (NameMatches(str, name, prefix)) {
                    result = reinterpret_cast<void*>(g_info.base + syms[i].st_value);
                    break;
                }
            }
        }
    }

    ::munmap(map, static_cast<size_t>(size));
    return result;
}

}  // namespace

void* ResolveLibartSymbol(const char* name) {
    if (!EnsureInit()) return nullptr;
    if (g_info.dynsym) {
        if (void* p = LookupInDynsym(name, false)) return p;
    }
    return LookupInFileSymtab(name, false);
}

void* ResolveLibartSymbolPrefix(const char* prefix) {
    if (!EnsureInit() || !prefix || !*prefix) return nullptr;
    if (g_info.dynsym) {
        if (void* p = LookupInDynsym(prefix, true)) return p;
    }
    return LookupInFileSymtab(prefix, true);
}

void* ResolveLibartSymbolAny(const char* const* names, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (names[i]) {
            if (void* p = ResolveLibartSymbol(names[i])) return p;
        }
    }
    return nullptr;
}

}  // namespace arthook
