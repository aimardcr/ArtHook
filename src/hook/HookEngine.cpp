// SPDX-License-Identifier: Apache-2.0
//
// HookEngine: install/remove ArtMethod-level hooks.
//
// Threading: only Hook/HookReflected/Unhook take the mutex. Hooked-method
// invocation is lock-free — the trampoline contains no synchronization,
// and ArtMethod entry writes are word-sized, naturally aligned, and
// single-copy atomic on arm64/x86, so live hot-swaps are safe.

#include "hook/HookEngine.h"

#include <jni.h>

#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

#include "art/AccessFlags.h"
#include "art/ArtMethod.h"
#include "art/Layout.h"
#include "trampoline/Trampoline.h"
#include "util/Log.h"

namespace arthook {

namespace {

struct HookEntry {
    ArtMethodPtr target = nullptr;
    void* backup_storage = nullptr;
    uint32_t original_flags = 0;
    void* original_quick_entry = nullptr;
    void* original_jni_entry = nullptr;
    TrampolinePage trampoline = {};
};

bool g_initialized = false;
std::mutex g_mutex;
std::unordered_map<ArtMethodPtr, HookEntry> g_hooks;

void* AllocateArtMethodSlot() {
    void* p = nullptr;
    if (posix_memalign(&p, 16, Layout().art_method_size) != 0) return nullptr;
    return p;
}

void FreeArtMethodSlot(void* p) {
    std::free(p);
}

// Install a hook on `target`. Caller must hold g_mutex and have verified
// g_initialized + non-null replacement. Returns kOk on success and stores
// the backup handle in *backup_out (fn pointer for native targets,
// jmethodID-shaped pointer to the backup ArtMethod for non-native).
Status InstallHookLocked(ArtMethodPtr target, void* replacement, void** backup_out) {
    if (g_hooks.find(target) != g_hooks.end()) return Status::kAlreadyHooked;

    HookEntry e;
    e.target = target;
    e.original_flags = GetAccessFlags(target);
    e.original_quick_entry = GetEntryPointFromQuickCompiledCode(target);
    e.original_jni_entry = GetEntryPointFromJni(target);

    e.backup_storage = AllocateArtMethodSlot();
    if (!e.backup_storage) return Status::kInternalError;
    CopyArtMethod(e.backup_storage, target);

    e.trampoline = BuildTrampoline(replacement);
    if (!e.trampoline.entry) {
        FreeArtMethodSlot(e.backup_storage);
        return Status::kTrampolineAllocFailed;
    }

    // Force the target into a plain `private native` shape with
    // CompileDontBother set; clearing kAccHookClearMask is what gets the
    // generic JNI bridge to dispatch object args correctly on Android 13+
    // (see AccessFlags.h for the per-bit rationale).
    const bool was_native = (e.original_flags & kAccNative) != 0;
    uint32_t flags = e.original_flags;
    flags &= ~kAccHookClearMask;
    flags |= kAccPrivate;
    flags |= kAccNative;
    flags |= kAccCompileDontBother;
    SetAccessFlags(target, flags);

    // The replacement is JNI-shaped (JNIEnv*, jobject|jclass, args...) and
    // must be entered with the JNI calling convention. For native targets
    // the existing quick entry already converts; for non-native targets
    // install the sampled bridge as the new quick entry.
    SetEntryPointFromJni(target, e.trampoline.entry);
    if (!was_native) {
        if (!Layout().jni_bridge_quick_entry) {
            LOGE("InstallHookLocked: no JNI bridge sampled — cannot hook non-native method");
            SetEntryPointFromJni(target, e.original_jni_entry);
            SetAccessFlags(target, e.original_flags);
            FreeTrampoline(e.trampoline);
            FreeArtMethodSlot(e.backup_storage);
            return Status::kInternalError;
        }
        SetEntryPointFromQuickCompiledCode(target, Layout().jni_bridge_quick_entry);
    }

    if (backup_out) {
        // Native targets: the original JNI entry is already a callable C fn
        // with JNI ABI — hand it back so the user can invoke it directly.
        // Non-native targets: no such C fn exists, so hand back the backup
        // ArtMethod as a jmethodID for use via env->CallNonvirtual*Method.
        *backup_out = was_native ? e.original_jni_entry : ArtMethodToMethodId(e.backup_storage);
    }

    g_hooks.emplace(target, std::move(e));
    return Status::kOk;
}

}  // namespace

Status HookEngine::Initialize(JNIEnv* env) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_initialized) return Status::kOk;
    if (!DiscoverLayout(env)) return Status::kLayoutDiscoveryFailed;
    g_initialized = true;
    LOGI("HookEngine initialized");
    return Status::kOk;
}

bool HookEngine::IsInitialized() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_initialized;
}

Status HookEngine::Hook(JNIEnv* env,
                        jclass clazz,
                        const char* name,
                        const char* signature,
                        void* replacement,
                        void** backup_out) {
    if (!g_initialized) return Status::kNotInitialized;
    if (!env || !clazz || !name || !signature || !replacement) {
        return Status::kInternalError;
    }
    ArtMethodPtr target = ArtMethodFromJniBinding(env, clazz, name, signature);
    if (!target) return Status::kMethodNotFound;

    std::lock_guard<std::mutex> lk(g_mutex);
    Status s = InstallHookLocked(target, replacement, backup_out);
    if (s == Status::kOk) {
        LOGI("Hooked %s%s (target=%p replacement=%p backup=%p)",
             name,
             signature,
             target,
             replacement,
             backup_out ? *backup_out : nullptr);
    }
    return s;
}

Status HookEngine::HookReflected(JNIEnv* env,
                                 jobject reflected,
                                 void* replacement,
                                 void** backup_out) {
    if (!g_initialized) return Status::kNotInitialized;
    if (!env || !reflected || !replacement) return Status::kInternalError;
    ArtMethodPtr target = ArtMethodFromReflected(env, reflected);
    if (!target) return Status::kMethodNotFound;

    std::lock_guard<std::mutex> lk(g_mutex);
    Status s = InstallHookLocked(target, replacement, backup_out);
    if (s == Status::kOk) {
        LOGI("Hooked reflected method (target=%p backup=%p)",
             target,
             backup_out ? *backup_out : nullptr);
    }
    return s;
}

Status HookEngine::Unhook(JNIEnv* env, jclass clazz, const char* name, const char* signature) {
    if (!g_initialized) return Status::kNotInitialized;
    if (!env || !clazz || !name || !signature) return Status::kInternalError;
    ArtMethodPtr target = ArtMethodFromJniBinding(env, clazz, name, signature);
    if (!target) return Status::kMethodNotFound;

    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_hooks.find(target);
    if (it == g_hooks.end()) return Status::kNotHooked;

    HookEntry& e = it->second;
    SetEntryPointFromQuickCompiledCode(target, e.original_quick_entry);
    SetEntryPointFromJni(target, e.original_jni_entry);
    SetAccessFlags(target, e.original_flags);

    // backup_storage is intentionally leaked: callers may still hold the
    // jmethodID we returned. Trampoline memory IS freed — no live ArtMethod
    // references it after the writes above land.
    FreeTrampoline(e.trampoline);
    e.trampoline = {};

    g_hooks.erase(it);
    LOGI("Unhooked %s%s (target=%p)", name, signature, target);
    return Status::kOk;
}

// ---- Public API forwarders -------------------------------------------------

Status Initialize(JNIEnv* env) {
    return HookEngine::Initialize(env);
}
bool IsInitialized() {
    return HookEngine::IsInitialized();
}

bool HasJniBridge() {
    return Layout().jni_bridge_quick_entry != nullptr;
}

Status SetBridgeProbe(JNIEnv* env, jclass clazz, const char* name, const char* signature) {
    if (!HookEngine::IsInitialized()) return Status::kNotInitialized;
    if (!env || !clazz || !name || !signature) return Status::kInternalError;
    // Prefer the bridge captured by Initialize — a user-supplied probe
    // could still be at art_quick_resolution_trampoline if its method
    // hasn't been resolved yet.
    if (HasJniBridge()) return Status::kOk;
    ArtMethodPtr m = ArtMethodFromJniBinding(env, clazz, name, signature);
    if (!m) return Status::kMethodNotFound;
    return SetJniBridgeFromMethod(m) ? Status::kOk : Status::kInternalError;
}

Status Hook(JNIEnv* env,
            jclass clazz,
            const char* name,
            const char* signature,
            void* replacement,
            void** backup_out) {
    return HookEngine::Hook(env, clazz, name, signature, replacement, backup_out);
}

Status HookReflected(JNIEnv* env, jobject reflected_method, void* replacement, void** backup_out) {
    return HookEngine::HookReflected(env, reflected_method, replacement, backup_out);
}

Status Unhook(JNIEnv* env, jclass clazz, const char* name, const char* signature) {
    return HookEngine::Unhook(env, clazz, name, signature);
}

const char* StatusToString(Status s) {
    switch (s) {
        case Status::kOk:
            return "kOk";
        case Status::kNotInitialized:
            return "kNotInitialized";
        case Status::kLayoutDiscoveryFailed:
            return "kLayoutDiscoveryFailed";
        case Status::kMethodNotFound:
            return "kMethodNotFound";
        case Status::kTrampolineAllocFailed:
            return "kTrampolineAllocFailed";
        case Status::kAlreadyHooked:
            return "kAlreadyHooked";
        case Status::kNotHooked:
            return "kNotHooked";
        case Status::kInternalError:
            return "kInternalError";
    }
    return "<unknown>";
}

}  // namespace arthook
