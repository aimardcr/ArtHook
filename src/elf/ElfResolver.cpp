// SPDX-License-Identifier: Apache-2.0
//
// ELF symbol resolution for libart.so without depending on dlsym. We walk
// the in-memory .dynsym first; on a miss we mmap libart.so on disk and walk
// .symtab. The disk fallback only helps on userdebug/eng builds — release
// libart.so is fully stripped — but it costs little to try.

#include "elf/ElfResolver.h"

#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "util/Log.h"

#if defined(__LP64__)
using Elf_Ehdr = Elf64_Ehdr;
using Elf_Shdr = Elf64_Shdr;
using Elf_Sym = Elf64_Sym;
using Elf_Dyn = Elf64_Dyn;
using Elf_Phdr = Elf64_Phdr;
#else
using Elf_Ehdr = Elf32_Ehdr;
using Elf_Shdr = Elf32_Shdr;
using Elf_Sym = Elf32_Sym;
using Elf_Dyn = Elf32_Dyn;
using Elf_Phdr = Elf32_Phdr;
#endif

namespace arthook {

namespace {

struct LibartInfo {
    bool initialized = false;
    uintptr_t base = 0;
    std::string path;
    const Elf_Sym* dynsym = nullptr;
    const char* dynstr = nullptr;
    size_t dynsym_count = 0;
};

LibartInfo g_info;

int IteratePhdrCb(struct dl_phdr_info* info, size_t, void* data) {
    const char* name = info->dlpi_name ? info->dlpi_name : "";
    if (!std::strstr(name, "libart.so")) return 0;

    auto* out = static_cast<LibartInfo*>(data);
    out->base = info->dlpi_addr;
    out->path = name;

    const Elf_Dyn* dyn = nullptr;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type == PT_DYNAMIC) {
            dyn = reinterpret_cast<const Elf_Dyn*>(info->dlpi_addr + ph.p_vaddr);
            break;
        }
    }
    if (!dyn) return 1;

    const Elf_Sym* dynsym = nullptr;
    const char* dynstr = nullptr;
    size_t syment = 0;
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

    // SysV-hash exposes nchain directly. GNU-hash needs a chain walk. With
    // no hash table at all we estimate from the strtab gap, which holds for
    // every ART build we've inspected.
    if (sysv_hash) {
        out->dynsym_count = sysv_hash[1];
    } else if (gnu_hash) {
        uint32_t nbuckets = gnu_hash[0];
        uint32_t symoffset = gnu_hash[1];
        uint32_t bloom_size = gnu_hash[2];
        const uint32_t* buckets = gnu_hash + 4 + bloom_size * (sizeof(size_t) / 4);
        const uint32_t* chain = buckets + nbuckets;
        uint32_t last = symoffset;
        for (uint32_t i = 0; i < nbuckets; ++i)
            if (buckets[i] > last) last = buckets[i];
        if (last >= symoffset) {
            while (!(chain[last - symoffset] & 1)) ++last;
            ++last;
        }
        out->dynsym_count = last;
    } else {
        uintptr_t span = reinterpret_cast<uintptr_t>(dynstr) - reinterpret_cast<uintptr_t>(dynsym);
        out->dynsym_count = std::min<size_t>(span / sizeof(Elf_Sym), 200000);
    }
    return 1;
}

bool EnsureInit() {
    if (g_info.initialized) return g_info.dynsym != nullptr;
    g_info.initialized = true;
    dl_iterate_phdr(&IteratePhdrCb, &g_info);
    if (!g_info.base) {
        LOGE("ElfResolver: libart.so not found in process");
        return false;
    }
    return g_info.dynsym != nullptr;
}

void* LookupInDynsym(const char* name) {
    if (!g_info.dynsym || !g_info.dynstr) return nullptr;
    for (size_t i = 0; i < g_info.dynsym_count; ++i) {
        const Elf_Sym& s = g_info.dynsym[i];
        if (s.st_name == 0 || s.st_value == 0) continue;
        if (std::strcmp(g_info.dynstr + s.st_name, name) == 0)
            return reinterpret_cast<void*>(g_info.base + s.st_value);
    }
    return nullptr;
}

void* LookupInFileSymtab(const char* name) {
    static const char* const kCandidates[] = {
#if defined(__LP64__)
        "/apex/com.android.art/lib64/libart.so",
        "/apex/com.android.runtime/lib64/libart.so",
        "/system/lib64/libart.so",
#else
        "/apex/com.android.art/lib/libart.so",
        "/apex/com.android.runtime/lib/libart.so",
        "/system/lib/libart.so",
#endif
        nullptr,
    };

    int fd = -1;
    const char* path = g_info.path.c_str();
    if (*path && path[0] == '/') fd = ::open(path, O_RDONLY);
    for (int i = 0; fd < 0 && kCandidates[i]; ++i) fd = ::open(kCandidates[i], O_RDONLY);
    if (fd < 0) {
        LOGW("ElfResolver: cannot open libart.so on disk");
        return nullptr;
    }

    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(Elf_Ehdr))) {
        ::close(fd);
        return nullptr;
    }
    void* map = ::mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (map == MAP_FAILED) {
        LOGW("ElfResolver: mmap libart.so failed");
        return nullptr;
    }

    void* result = nullptr;
    auto* base = static_cast<const uint8_t*>(map);
    const auto& eh = *reinterpret_cast<const Elf_Ehdr*>(base);
    if (std::memcmp(eh.e_ident, ELFMAG, SELFMAG) == 0) {
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
        if (symtab && strtab) {
            const auto* syms = reinterpret_cast<const Elf_Sym*>(base + symtab->sh_offset);
            const auto* strs = reinterpret_cast<const char*>(base + strtab->sh_offset);
            size_t n = symtab->sh_size / sizeof(Elf_Sym);
            for (size_t i = 0; i < n; ++i) {
                if (syms[i].st_name == 0 || syms[i].st_value == 0) continue;
                if (std::strcmp(strs + syms[i].st_name, name) == 0) {
                    result = reinterpret_cast<void*>(g_info.base + syms[i].st_value);
                    break;
                }
            }
        }
    }

    ::munmap(map, static_cast<size_t>(st.st_size));
    return result;
}

}  // namespace

void* ResolveLibartSymbol(const char* name) {
    if (!EnsureInit()) return nullptr;
    if (void* p = LookupInDynsym(name)) return p;
    return LookupInFileSymtab(name);
}

}  // namespace arthook
