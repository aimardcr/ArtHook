// SPDX-License-Identifier: Apache-2.0
//
// ArtHook: ART method hooking. Install/remove takes a mutex; hooked-method
// invocation is lock-free.

#ifndef ARTHOOK_ARTHOOK_H_
#define ARTHOOK_ARTHOOK_H_

#include <jni.h>

#include <cstddef>

namespace arthook {

enum class Status {
    kOk,
    kNotInitialized,
    kLayoutDiscoveryFailed,
    kMethodNotFound,
    kTrampolineAllocFailed,
    kAlreadyHooked,
    kNotHooked,
    kInvalidArgument,  // null/empty argument or other caller misuse
    kOutOfMemory,      // an allocation failed
    kNoJniBridge,      // non-native target but no JNI bridge captured;
                       // recover via SetBridgeProbe()/ForceBridgeProbe()
    kDeoptUnavailable,  // Deoptimize() can't run: needed libart symbol stripped
    kInternalError,
};

// Discovers ArtMethod layout. Call once per process before any Hook().
// Idempotent.
Status Initialize(JNIEnv* env);

// Preflight: hook an internal sentinel, invoke it, confirm the replacement
// and backup both fire, then remove it. Returns kOk only if hooking is fully
// functional on this device. Run after Initialize() to fail fast when layout
// discovery produced wrong offsets, instead of crashing on a later real hook.
// Exercises the native-method path + layout/trampoline; non-native readiness
// is reported separately by HasJniBridge().
Status SelfTest(JNIEnv* env);

namespace detail {
Status AcquireJniEnv(JNIEnv** env, JavaVM** vm, bool* attached);

// Re-sync the backup ArtMethod's declaring-class ref from the live target
// before invoking it, a moving GC can otherwise leave the backup's copy
// stale. No-op on null. Called by Hooked::invoke().
void RefreshBackupClass(void* backup, void* target);
}

// Run `body(env)` with a JNIEnv for the calling thread, attaching to the
// JavaVM if needed and detaching on scope exit. Use from libraries not
// loaded via System.loadLibrary (zygisk, ptrace, dlopen).
template <class Fn>
Status AttachToJavaVM(Fn&& body) {
    JNIEnv* env = nullptr;
    JavaVM* vm = nullptr;
    bool attached = false;
    Status s = detail::AcquireJniEnv(&env, &vm, &attached);
    if (s != Status::kOk) return s;
    struct Detacher {
        JNIEnv* env;
        JavaVM* vm;
        bool attached;
        ~Detacher() {
            if (attached && vm) {
                // Detaching with a pending exception can abort under CheckJNI.
                if (env && env->ExceptionCheck()) {
                    env->ExceptionDescribe();
                    env->ExceptionClear();
                }
                vm->DetachCurrentThread();
            }
        }
    } guard{env, vm, attached};
    body(env);
    return Status::kOk;
}

// True when Initialize() captured a usable JNI bridge. Non-native hooks
// need one; supply a fallback via SetBridgeProbe() if false.
bool HasJniBridge();

// Designate a native method whose quick entry is the generic JNI bridge
// inside libart. No-op if Initialize() already captured one (the auto-
// captured bridge is preferred). Returns kInvalidArgument if the
// candidate's quick entry isn't a usable bridge.
Status SetBridgeProbe(JNIEnv* env, jclass clazz, const char* name, const char* signature);

// Like SetBridgeProbe(), but overrides an already-captured bridge instead of
// no-opping, use it to recover from a wrong auto-captured bridge (e.g. on
// stripped/non-arm64 builds). The candidate is still validated, so a bad
// probe is rejected rather than installed.
Status ForceBridgeProbe(JNIEnv* env, jclass clazz, const char* name, const char* signature);

// Replace `name`/`signature` on `clazz` with `replacement` (JNI calling
// convention). `backup_out` receives:
//   - NATIVE target → callable C fn pointer (original JNI impl).
//   - NON-NATIVE target → jmethodID (as void*). Invoke via
//     env->CallNonvirtual*Method(thiz, declaringClass, backup, args...)
//     or CallStatic*Method for static targets. Plain virtual Call*Method
//     re-resolves to the patched target and infinitely recurses.
// Backup stays callable after Unhook().
//
// Non-native backups can go stale under a moving GC, before invoking, call
// detail::RefreshBackupClass(backup, target), or use the Hooked wrapper
// (its invoke() does this for you).
Status Hook(JNIEnv* env,
            jclass clazz,
            const char* name,
            const char* signature,
            void* replacement,
            void** backup_out);

// Convenience overload: resolve `class_name` via the app classloader
// (with FindClass fallback) and forward. Accepts either JNI form
// ("com/foo/Bar") or dotted form ("com.foo.Bar"). Returns
// kMethodNotFound if the class itself cannot be resolved.
Status Hook(JNIEnv* env,
            const char* class_name,
            const char* name,
            const char* signature,
            void* replacement,
            void** backup_out);

// As Hook(), but identifies the target via a java.lang.reflect.Method.
Status HookReflected(JNIEnv* env, jobject reflected_method, void* replacement, void** backup_out);

// Force a NON-NATIVE method to run in the interpreter so already-compiled or
// inlined callers re-dispatch through it. Opt-in mitigation for the AOT/JIT
// inline gap: to make a hooked method fire from a caller that inlined it,
// Deoptimize the CALLER (not the hooked method itself, which is refused with
// kAlreadyHooked). Best-effort and NOT sticky: returns kDeoptUnavailable when
// the needed libart symbol is stripped, and ART may re-optimize later (poll
// IsHookLive on the hook). Does not invalidate AOT boot-image inlines.
Status Deoptimize(JNIEnv* env, jclass clazz, const char* name, const char* signature);
Status DeoptimizeReflected(JNIEnv* env, jobject reflected_method);

// Hook every declared method named `name` on the class (all overloads) with
// the same `replacement` — handy when a target's signature varies across
// Android versions (e.g. SSL-pinning checks). `replacement` must be
// ABI-compatible with every matched overload. Returns the number hooked; no
// backups are returned (hook by signature if you need to call the original).
// Methods only, not constructors.
int HookAllOverloads(JNIEnv* env, jclass clazz, const char* name, void* replacement);
int HookAllOverloads(JNIEnv* env, const char* class_name, const char* name, void* replacement);

// Restore the original. Backup remains callable.
Status Unhook(JNIEnv* env, jclass clazz, const char* name, const char* signature);
Status Unhook(JNIEnv* env, const char* class_name, const char* name, const char* signature);

// True if the named/reflected method is currently hooked (a record exists).
bool IsHooked(JNIEnv* env, jclass clazz, const char* name, const char* signature);
bool IsHookedReflected(JNIEnv* env, jobject reflected_method);

// True if the hook is not just recorded but still LIVE — its entry points
// haven't been overwritten. Returns false if a hook was silently clobbered:
// e.g. RegisterNatives re-registering a hooked native method, or ART resetting
// a static method's entry on class (re)initialization. Re-Hook() to recover.
bool IsHookLive(JNIEnv* env, jclass clazz, const char* name, const char* signature);
bool IsHookLiveReflected(JNIEnv* env, jobject reflected_method);

// Snapshot of the discovered ArtMethod layout and engine state, for
// diagnostics/telemetry (e.g. to attach when reporting a device where
// discovery failed). All offsets are SIZE_MAX until Initialize() succeeds.
struct Diagnostics {
    bool initialized;
    bool has_jni_bridge;
    size_t art_method_size;
    size_t offset_access_flags;
    size_t offset_entry_point_jni;
    size_t offset_entry_point_quick_code;
    size_t active_hooks;
};
Diagnostics GetDiagnostics();

bool IsInitialized();
const char* StatusToString(Status s);

}  // namespace arthook

#endif  // ARTHOOK_ARTHOOK_H_
