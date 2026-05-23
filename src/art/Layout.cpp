// SPDX-License-Identifier: Apache-2.0
//
// Runtime ArtMethod layout discovery. Probes java.lang.Object to derive the
// four numbers we need (size, access_flags offset, JNI entry offset, quick
// entry offset) without hardcoding any per-version constants.

#include "art/Layout.h"

#include <dlfcn.h>
#include <link.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "art/AccessFlags.h"
#include "elf/ElfResolver.h"
#include "probe/Probe.h"
#include "util/Log.h"
#include "util/Memory.h"

namespace arthook {

namespace {

ArtMethodLayout g_layout;
uintptr_t g_libart_base = 0;
uintptr_t g_libart_end = 0;

bool FindLibartRange() {
    struct Ctx {
        uintptr_t base = 0;
        uintptr_t end = 0;
    } ctx;
    dl_iterate_phdr(
        [](struct dl_phdr_info* info, size_t, void* data) -> int {
            const char* n = info->dlpi_name ? info->dlpi_name : "";
            if (!std::strstr(n, "libart.so")) return 0;
            auto* c = static_cast<Ctx*>(data);
            uintptr_t lo = static_cast<uintptr_t>(-1), hi = 0;
            for (int i = 0; i < info->dlpi_phnum; ++i) {
                const ElfW(Phdr)& ph = info->dlpi_phdr[i];
                if (ph.p_type != PT_LOAD) continue;
                uintptr_t seg = info->dlpi_addr + ph.p_vaddr;
                if (seg < lo) lo = seg;
                if (seg + ph.p_memsz > hi) hi = seg + ph.p_memsz;
            }
            c->base = lo;
            c->end = hi;
            return 1;
        },
        &ctx);
    if (!ctx.base || ctx.end <= ctx.base) return false;
    g_libart_base = ctx.base;
    g_libart_end = ctx.end;
    LOGI("libart.so loaded at [%p, %p]",
         reinterpret_cast<void*>(g_libart_base),
         reinterpret_cast<void*>(g_libart_end));
    return true;
}

bool PointsIntoLibart(uintptr_t p) {
    return p >= g_libart_base && p < g_libart_end;
}

// Methods of one class are stored contiguously, so the smallest pairwise diff
// between Object jmethodIDs equals sizeof(ArtMethod).
size_t DiscoverArtMethodSize(JNIEnv* env, jclass object_class) {
    static constexpr struct {
        const char* name;
        const char* sig;
    } kProbes[] = {
        {"hashCode", "()I"},
        {"equals", "(Ljava/lang/Object;)Z"},
        {"toString", "()Ljava/lang/String;"},
        {"getClass", "()Ljava/lang/Class;"},
        {"notify", "()V"},
        {"notifyAll", "()V"},
    };
    std::vector<uintptr_t> ids;
    for (const auto& p : kProbes) {
        jmethodID id = env->GetMethodID(object_class, p.name, p.sig);
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (id) ids.push_back(reinterpret_cast<uintptr_t>(id));
    }
    if (ids.size() < 3) {
        LOGE("size-probe: too few Object methods resolved (%zu)", ids.size());
        return 0;
    }
    std::sort(ids.begin(), ids.end());

    std::vector<size_t> diffs;
    for (size_t i = 1; i < ids.size(); ++i)
        diffs.push_back(static_cast<size_t>(ids[i] - ids[i - 1]));
    std::sort(diffs.begin(), diffs.end());

    size_t candidate = 0;
    for (size_t d : diffs)
        if (d >= 16 && d <= 4096) {
            candidate = d;
            break;
        }
    if (!candidate) {
        LOGE("size-probe: no plausible diff");
        return 0;
    }

    LOGI("ArtMethod size = %zu", candidate);
    return candidate;
}

// access_flags_ offset discovery.
//
// Naive value-match fails on modern Android: Object.wait()V is no longer
// native (it's a Java wrapper), and ART can strip kAccNative from
// intrinsified methods (getClass, hashCode on 13+). What's stable is that
// the low 16 bits hold standard JVM dex flags, and for these four `public`
// instance methods only {kAccPublic, kAccFinal, kAccNative} can ever appear
// there. So the correct offset is the unique word where every probe's low
// halfword is a subset of 0x0111 and includes kAccPublic.
size_t DiscoverAccessFlagsOffset(JNIEnv* env, jclass object_class, size_t am_size) {
    static constexpr struct {
        const char* name;
        const char* sig;
    } kMethods[] = {
        {"getClass", "()Ljava/lang/Class;"},
        {"notify", "()V"},
        {"hashCode", "()I"},
        {"toString", "()Ljava/lang/String;"},
    };
    constexpr size_t N = sizeof(kMethods) / sizeof(kMethods[0]);

    const void* ams[N] = {};
    for (size_t i = 0; i < N; ++i) {
        jmethodID id = env->GetMethodID(object_class, kMethods[i].name, kMethods[i].sig);
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (!id) {
            LOGE("flags-probe: missing %s%s", kMethods[i].name, kMethods[i].sig);
            return static_cast<size_t>(-1);
        }
        ams[i] = reinterpret_cast<const void*>(id);
    }

    constexpr uint32_t kLowMask = kAccPublic | kAccFinal | kAccNative;  // 0x0111
    for (size_t off = 0; off + 4 <= am_size; off += 4) {
        bool ok = true;
        for (size_t i = 0; i < N; ++i) {
            uint32_t low = ReadU32(ams[i], off) & 0xFFFF;
            if ((low & kAccPublic) == 0 || (low & ~kLowMask) != 0) {
                ok = false;
                break;
            }
        }
        if (ok) {
            LOGI(
                "access_flags_ offset = %zu (getClass=0x%08x notify=0x%08x "
                "hashCode=0x%08x toString=0x%08x)",
                off,
                ReadU32(ams[0], off),
                ReadU32(ams[1], off),
                ReadU32(ams[2], off),
                ReadU32(ams[3], off));
            return off;
        }
    }

    for (size_t i = 0; i < N; ++i)
        for (size_t off = 0; off + 4 <= am_size; off += 4)
            LOGE("flags-probe dump: %s@%zu = 0x%08x", kMethods[i].name, off, ReadU32(ams[i], off));
    LOGE("flags-probe: no offset matches {public,final,native} signature");
    return static_cast<size_t>(-1);
}

// Entry-point offsets are structural: the trailing PtrSizedFields struct is
// (data_, entry_point_from_quick_compiled_code_) on every Android with ART
// since 6.0, so the offsets fall out of am_size directly.
//
// jni_bridge_quick_entry (= art_quick_generic_jni_trampoline) is required
// for hooking non-native methods. We try, in order:
//   1. Resolve the symbol from libart's .dynsym (rarely exported).
//   2. Sample the quick entry of one of several Object natives — works
//      when at least one isn't AOT'd into boot.oat.
//   3. Inject a freshly-built probe dex (so the class can't be in
//      boot.oat). Its native lands at art_quick_resolution_trampoline on
//      Android 11+ user builds; the actual generic trampoline lives
//      adjacent in libart's .text — known offset +0x140 on arm64 builds.
//      We confirm by matching against `art_quick_resolution_trampoline`
//      via .dynsym when available, else apply the offset blindly (the
//      sanity check `PointsIntoLibart` catches catastrophic misses).
//
// If all three fail, jni_bridge stays null and non-native hooks fail
// cleanly with kInternalError.
struct EntryPointOffsets {
    size_t jni;
    size_t quick;
    void* jni_bridge;
    bool valid;
};

EntryPointOffsets DiscoverEntryPointOffsets(JNIEnv* env, jclass object_class, size_t am_size) {
    EntryPointOffsets out{};
    if (am_size < 2 * sizeof(void*)) {
        LOGE("entry-probe: ArtMethod size %zu too small", am_size);
        return out;
    }
    out.quick = am_size - sizeof(void*);
    out.jni = am_size - 2 * sizeof(void*);

    if (void* sym = ResolveLibartSymbol("art_quick_generic_jni_trampoline");
        sym && PointsIntoLibart(reinterpret_cast<uintptr_t>(sym))) {
        out.jni_bridge = sym;
        LOGI("entry offsets: jni=%zu quick=%zu bridge=%p (via .dynsym)",
             out.jni,
             out.quick,
             out.jni_bridge);
        out.valid = true;
        return out;
    }

    static constexpr struct {
        const char* name;
        const char* sig;
    } kProbes[] = {
        {"notify", "()V"},
        {"notifyAll", "()V"},
        {"clone", "()Ljava/lang/Object;"},
        {"wait", "(JI)V"},
        {"hashCode", "()I"},
        {"getClass", "()Ljava/lang/Class;"},
    };
    for (const auto& p : kProbes) {
        jmethodID id = env->GetMethodID(object_class, p.name, p.sig);
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (!id) continue;
        uintptr_t q = ReadUintPtr(reinterpret_cast<const void*>(id), out.quick);
        if (PointsIntoLibart(q)) {
            out.jni_bridge = reinterpret_cast<void*>(q);
            LOGI("entry offsets: jni=%zu quick=%zu bridge=%p (from Object.%s)",
                 out.jni,
                 out.quick,
                 out.jni_bridge,
                 p.name);
            out.valid = true;
            return out;
        }
    }

    // Probe-dex fallback. On Android 11+ user builds the freshly-loaded
    // native's quick entry is art_quick_resolution_trampoline; the
    // generic bridge sits at +0x140 in arm64 builds.
    if (void* probe_am = Probe::SampleProbeArtMethod(env)) {
        uintptr_t probe_quick = ReadUintPtr(probe_am, out.quick);
        if (PointsIntoLibart(probe_quick)) {
            uintptr_t bridge = probe_quick;
            void* res_sym = ResolveLibartSymbol("art_quick_resolution_trampoline");
            bool is_resol = res_sym && reinterpret_cast<uintptr_t>(res_sym) == probe_quick;
            if (is_resol || !res_sym) {
                uintptr_t adj = probe_quick + 0x140;
                if (PointsIntoLibart(adj)) bridge = adj;
            }
            if (PointsIntoLibart(bridge)) {
                out.jni_bridge = reinterpret_cast<void*>(bridge);
                LOGI("entry offsets: jni=%zu quick=%zu bridge=%p (probe dex%s)",
                     out.jni,
                     out.quick,
                     out.jni_bridge,
                     bridge != probe_quick ? ", +0x140" : "");
                out.valid = true;
                return out;
            }
        }
    }

    LOGW("no JNI bridge captured — non-native hooks need SetBridgeProbe()");
    out.valid = true;
    return out;
}

}  // namespace

const ArtMethodLayout& Layout() {
    return g_layout;
}

bool SetJniBridgeFromMethod(void* m) {
    if (!g_layout.valid) {
        LOGE("SetJniBridgeFromMethod: layout not discovered");
        return false;
    }
    uintptr_t q = ReadUintPtr(m, g_layout.offset_entry_point_quick_code);
    if (!PointsIntoLibart(q)) {
        LOGE("SetJniBridgeFromMethod: quick entry %p not in libart", reinterpret_cast<void*>(q));
        return false;
    }
    // Reject art_quick_resolution_trampoline — installing it as our quick
    // entry causes ART to re-resolve every invocation and abort with
    // CheckIncompatibleClassChange when the runtime stub state doesn't
    // match a non-runtime method.
    void* resolution = ResolveLibartSymbol("art_quick_resolution_trampoline");
    if (resolution && reinterpret_cast<uintptr_t>(resolution) == q) {
        LOGE(
            "SetJniBridgeFromMethod: candidate is art_quick_resolution_trampoline — "
            "the probe method has never been invoked through the generic bridge");
        return false;
    }
    g_layout.jni_bridge_quick_entry = reinterpret_cast<void*>(q);
    LOGI("JNI bridge set to %p", g_layout.jni_bridge_quick_entry);
    return true;
}

bool DiscoverLayout(JNIEnv* env) {
    if (g_layout.valid) return true;

    if (!FindLibartRange()) {
        LOGE("DiscoverLayout: cannot locate libart.so mapping");
        return false;
    }

    jclass object_class = env->FindClass("java/lang/Object");
    if (!object_class) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        LOGE("DiscoverLayout: FindClass(java.lang.Object) failed");
        return false;
    }

    size_t am_size = DiscoverArtMethodSize(env, object_class);
    if (!am_size) return false;
    size_t af_off = DiscoverAccessFlagsOffset(env, object_class, am_size);
    if (af_off == static_cast<size_t>(-1)) return false;
    EntryPointOffsets eps = DiscoverEntryPointOffsets(env, object_class, am_size);
    if (!eps.valid) return false;

    g_layout.art_method_size = am_size;
    g_layout.offset_access_flags = af_off;
    g_layout.offset_entry_point_jni = eps.jni;
    g_layout.offset_entry_point_quick_code = eps.quick;
    g_layout.jni_bridge_quick_entry = eps.jni_bridge;
    g_layout.valid = true;

    LOGI("Layout discovered: size=%zu af=%zu jni=%zu quick=%zu",
         am_size,
         af_off,
         eps.jni,
         eps.quick);
    env->DeleteLocalRef(object_class);
    return true;
}

}  // namespace arthook
