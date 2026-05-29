// SPDX-License-Identifier: Apache-2.0
//
// HookEngine: install/remove ArtMethod-level hooks.
//
// Threading: install/remove take g_mutex; invocation is lock-free (entry
// writes are word-sized and aligned). Because invocation is lock-free,
// Unhook leaks the trampoline page rather than risk freeing one a thread is
// still executing.

#include "hook/HookEngine.h"

#include <jni.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

#include "art/AccessFlags.h"
#include "art/ArtMethod.h"
#include "art/Layout.h"
#include "jvm/ClassLookup.h"
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
    jclass declaring_class_ref = nullptr;  // GlobalRef pinning the class
};

// Atomic: read unlocked at the top of Hook/Unhook, written under g_mutex.
std::atomic<bool> g_initialized{false};
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

// Install a hook on `target`. Caller holds g_mutex. `declaring` (may be null)
// is pinned via a GlobalRef so its LinearAlloc (holding `target`) survives
// the hook. backup_out gets the native fn pointer, or the backup ArtMethod as
// a jmethodID for non-native targets.
Status InstallHookLocked(
    JNIEnv* env, jclass declaring, ArtMethodPtr target, void* replacement, void** backup_out) {
    if (g_hooks.find(target) != g_hooks.end()) return Status::kAlreadyHooked;

    HookEntry e;
    e.target = target;
    e.original_flags = GetAccessFlags(target);
    e.original_quick_entry = GetEntryPointFromQuickCompiledCode(target);
    e.original_jni_entry = GetEntryPointFromJni(target);

    e.backup_storage = AllocateArtMethodSlot();
    if (!e.backup_storage) return Status::kOutOfMemory;
    CopyArtMethod(e.backup_storage, target);

    e.trampoline = BuildTrampoline(replacement);
    if (!e.trampoline.entry) {
        FreeArtMethodSlot(e.backup_storage);
        return Status::kTrampolineAllocFailed;
    }

    // Reshape to `private native` + CompileDontBother (see AccessFlags.h).
    const bool was_native = (e.original_flags & kAccNative) != 0;
    uint32_t flags = e.original_flags;
    flags &= ~kAccHookClearMask;
    flags |= kAccPrivate;
    flags |= kAccNative;
    flags |= kAccCompileDontBother;
    SetAccessFlags(target, flags);

    // Enter via the JNI ABI: native keeps its quick entry; non-native gets
    // the sampled bridge as its quick entry.
    SetEntryPointFromJni(target, e.trampoline.entry);
    if (!was_native) {
        void* bridge = Layout().jni_bridge_quick_entry;
        if (!bridge) {
            LOGE("InstallHookLocked: no JNI bridge sampled, cannot hook non-native method");
            SetEntryPointFromJni(target, e.original_jni_entry);
            SetAccessFlags(target, e.original_flags);
            FreeTrampoline(e.trampoline);
            FreeArtMethodSlot(e.backup_storage);
            return Status::kNoJniBridge;
        }
        SetEntryPointFromQuickCompiledCode(target, bridge);
        // Re-store if a concurrent JIT compile clobbered it (best effort).
        if (GetEntryPointFromQuickCompiledCode(target) != bridge) {
            SetEntryPointFromQuickCompiledCode(target, bridge);
        }
    }

    // Pin the declaring class (last, so earlier failures need no ref rollback).
    if (env && declaring) {
        e.declaring_class_ref = static_cast<jclass>(env->NewGlobalRef(declaring));
        if (!e.declaring_class_ref) {
            SetEntryPointFromQuickCompiledCode(target, e.original_quick_entry);
            SetEntryPointFromJni(target, e.original_jni_entry);
            SetAccessFlags(target, e.original_flags);
            FreeTrampoline(e.trampoline);
            FreeArtMethodSlot(e.backup_storage);
            return Status::kOutOfMemory;
        }
    }

    if (backup_out) {
        // Native: the original JNI fn pointer. Non-native: the backup
        // ArtMethod as a jmethodID (for env->CallNonvirtual*Method).
        *backup_out = was_native ? e.original_jni_entry : ArtMethodToMethodId(e.backup_storage);
    }

    g_hooks.emplace(target, std::move(e));
    return Status::kOk;
}

// Resolve the declaring class of a reflected Method/Constructor as a local
// ref (caller deletes), nullptr on failure with exceptions cleared.
jclass DeclaringClassOfReflected(JNIEnv* env, jobject reflected) {
    jclass mcls = env->GetObjectClass(reflected);
    if (!mcls) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }
    jmethodID m = env->GetMethodID(mcls, "getDeclaringClass", "()Ljava/lang/Class;");
    env->DeleteLocalRef(mcls);
    if (!m) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }
    jobject cls = env->CallObjectMethod(reflected, m);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return static_cast<jclass>(cls);
}

}  // namespace

Status HookEngine::Initialize(JNIEnv* env) {
    if (!env) return Status::kInvalidArgument;
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_initialized) return Status::kOk;
    if (!DiscoverLayout(env)) return Status::kLayoutDiscoveryFailed;
    g_initialized = true;  // publish under g_mutex; readers acquire it before g_layout
    LOGI("HookEngine initialized");
    return Status::kOk;
}

bool HookEngine::IsInitialized() {
    return g_initialized.load();
}

Status HookEngine::Hook(JNIEnv* env,
                        jclass clazz,
                        const char* name,
                        const char* signature,
                        void* replacement,
                        void** backup_out) {
    if (!g_initialized) return Status::kNotInitialized;
    if (!env || !clazz || !name || !signature || !replacement) {
        return Status::kInvalidArgument;
    }
    ArtMethodPtr target = ArtMethodFromJniBinding(env, clazz, name, signature);
    if (!target) return Status::kMethodNotFound;

    std::lock_guard<std::mutex> lk(g_mutex);
    Status s = InstallHookLocked(env, clazz, target, replacement, backup_out);
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
    if (!env || !reflected || !replacement) return Status::kInvalidArgument;
    ArtMethodPtr target = ArtMethodFromReflected(env, reflected);
    if (!target) return Status::kMethodNotFound;

    // Pin the declaring class (best-effort) so the method's storage survives.
    jclass declaring = DeclaringClassOfReflected(env, reflected);

    std::lock_guard<std::mutex> lk(g_mutex);
    Status s = InstallHookLocked(env, declaring, target, replacement, backup_out);
    if (declaring) env->DeleteLocalRef(declaring);
    if (s == Status::kOk) {
        LOGI("Hooked reflected method (target=%p backup=%p)",
             target,
             backup_out ? *backup_out : nullptr);
    }
    return s;
}

Status HookEngine::Unhook(JNIEnv* env, jclass clazz, const char* name, const char* signature) {
    if (!g_initialized) return Status::kNotInitialized;
    if (!env || !clazz || !name || !signature) return Status::kInvalidArgument;
    ArtMethodPtr target = ArtMethodFromJniBinding(env, clazz, name, signature);
    if (!target) return Status::kMethodNotFound;

    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_hooks.find(target);
    if (it == g_hooks.end()) return Status::kNotHooked;

    HookEntry& e = it->second;
    SetEntryPointFromQuickCompiledCode(target, e.original_quick_entry);
    SetEntryPointFromJni(target, e.original_jni_entry);
    SetAccessFlags(target, e.original_flags);

    // Leak backup_storage and the trampoline page: invocation is lock-free,
    // so a thread may still be in the thunk, freeing it would be a UAF.
    // One page + slot per unhook, so avoid tight hook/unhook loops.
    if (e.declaring_class_ref) env->DeleteGlobalRef(e.declaring_class_ref);

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
    std::lock_guard<std::mutex> lk(g_mutex);
    return Layout().jni_bridge_quick_entry != nullptr;
}

static Status SetBridgeProbeImpl(
    JNIEnv* env, jclass clazz, const char* name, const char* signature, bool force) {
    if (!HookEngine::IsInitialized()) return Status::kNotInitialized;
    if (!env || !clazz || !name || !signature) return Status::kInvalidArgument;
    ArtMethodPtr m = ArtMethodFromJniBinding(env, clazz, name, signature);
    if (!m) return Status::kMethodNotFound;
    std::lock_guard<std::mutex> lk(g_mutex);
    // Non-force keeps the auto-captured bridge (read directly, we hold
    // g_mutex). SetJniBridgeFromMethod re-validates even the force path.
    if (!force && Layout().jni_bridge_quick_entry) return Status::kOk;
    return SetJniBridgeFromMethod(m) ? Status::kOk : Status::kInvalidArgument;
}

Status SetBridgeProbe(JNIEnv* env, jclass clazz, const char* name, const char* signature) {
    return SetBridgeProbeImpl(env, clazz, name, signature, /*force=*/false);
}

Status ForceBridgeProbe(JNIEnv* env, jclass clazz, const char* name, const char* signature) {
    return SetBridgeProbeImpl(env, clazz, name, signature, /*force=*/true);
}

Status Hook(JNIEnv* env,
            jclass clazz,
            const char* name,
            const char* signature,
            void* replacement,
            void** backup_out) {
    return HookEngine::Hook(env, clazz, name, signature, replacement, backup_out);
}

Status Hook(JNIEnv* env,
            const char* class_name,
            const char* name,
            const char* signature,
            void* replacement,
            void** backup_out) {
    if (!env || !class_name) return Status::kInvalidArgument;
    jclass clazz = FindClassByName(env, class_name);
    if (!clazz) return Status::kMethodNotFound;
    Status s = HookEngine::Hook(env, clazz, name, signature, replacement, backup_out);
    env->DeleteLocalRef(clazz);
    return s;
}

Status HookReflected(JNIEnv* env, jobject reflected_method, void* replacement, void** backup_out) {
    return HookEngine::HookReflected(env, reflected_method, replacement, backup_out);
}

Status Unhook(JNIEnv* env, jclass clazz, const char* name, const char* signature) {
    return HookEngine::Unhook(env, clazz, name, signature);
}

Status Unhook(JNIEnv* env, const char* class_name, const char* name, const char* signature) {
    if (!env || !class_name) return Status::kInvalidArgument;
    jclass clazz = FindClassByName(env, class_name);
    if (!clazz) return Status::kMethodNotFound;
    Status s = HookEngine::Unhook(env, clazz, name, signature);
    env->DeleteLocalRef(clazz);
    return s;
}

bool IsHooked(JNIEnv* env, jclass clazz, const char* name, const char* signature) {
    if (!g_initialized || !env || !clazz || !name || !signature) return false;
    ArtMethodPtr target = ArtMethodFromJniBinding(env, clazz, name, signature);
    if (!target) return false;
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_hooks.find(target) != g_hooks.end();
}

bool IsHookedReflected(JNIEnv* env, jobject reflected) {
    if (!g_initialized || !env || !reflected) return false;
    ArtMethodPtr target = ArtMethodFromReflected(env, reflected);
    if (!target) return false;
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_hooks.find(target) != g_hooks.end();
}

Diagnostics GetDiagnostics() {
    Diagnostics d{};
    d.initialized = g_initialized.load();
    std::lock_guard<std::mutex> lk(g_mutex);
    const ArtMethodLayout& l = Layout();
    d.has_jni_bridge = l.jni_bridge_quick_entry != nullptr;
    d.art_method_size = l.art_method_size;
    d.offset_access_flags = l.offset_access_flags;
    d.offset_entry_point_jni = l.offset_entry_point_jni;
    d.offset_entry_point_quick_code = l.offset_entry_point_quick_code;
    d.active_hooks = g_hooks.size();
    return d;
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
        case Status::kInvalidArgument:
            return "kInvalidArgument";
        case Status::kOutOfMemory:
            return "kOutOfMemory";
        case Status::kNoJniBridge:
            return "kNoJniBridge";
        case Status::kInternalError:
            return "kInternalError";
    }
    return "<unknown>";
}

}  // namespace arthook
