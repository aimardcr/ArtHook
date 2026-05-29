// SPDX-License-Identifier: Apache-2.0
//
// ArtHook: ART method hooking. Install/remove takes a mutex; hooked-method
// invocation is lock-free.

#ifndef ARTHOOK_ARTHOOK_H_
#define ARTHOOK_ARTHOOK_H_

#include <jni.h>

namespace arthook {

enum class Status {
    kOk,
    kNotInitialized,
    kLayoutDiscoveryFailed,
    kMethodNotFound,
    kTrampolineAllocFailed,
    kAlreadyHooked,
    kNotHooked,
    kInvalidArgument,   // null/empty argument or other caller misuse
    kOutOfMemory,       // an allocation failed
    kNoJniBridge,       // non-native target but no JNI bridge was captured
    kDeoptUnavailable,  // Deoptimize() can't run: needed libart symbol stripped
    kInternalError,
};

// Discovers the ArtMethod layout. Call once per process before any Hook().
// Idempotent. With `verify`, also confirms hooking works on this device by
// installing and invoking an internal sentinel, returning kInternalError if
// that fails (so a wrong layout discovery is caught at init, not on a real
// hook).
Status Initialize(JNIEnv* env, bool verify = false);

// Internal: required by the AttachToJavaVM template below. Not public API.
namespace detail {
Status AcquireJniEnv(JNIEnv** env, JavaVM** vm, bool* attached);
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

// Replace `name`/`signature` on `clazz` with `replacement` (JNI calling
// convention). `backup_out` receives:
//   - NATIVE target -> callable C fn pointer (original JNI impl).
//   - NON-NATIVE target -> jmethodID (as void*). Invoke via
//     env->CallNonvirtual*Method(thiz, declaringClass, backup, args...)
//     or CallStatic*Method for static targets. Plain virtual Call*Method
//     re-resolves to the patched target and infinitely recurses.
// Backup stays callable after Unhook(). For non-native backups under a moving
// GC, prefer the Hooked wrapper, whose invoke() refreshes the backup.
Status Hook(JNIEnv* env,
            jclass clazz,
            const char* name,
            const char* signature,
            void* replacement,
            void** backup_out);

// Convenience overload: resolve `class_name` via the app classloader (with
// FindClass fallback) and forward. Accepts JNI ("com/foo/Bar") or dotted
// ("com.foo.Bar") form. Returns kMethodNotFound if the class can't resolve.
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
// Deoptimize the CALLER (the hooked method itself is refused with
// kAlreadyHooked). Best-effort and not sticky; returns kDeoptUnavailable when
// the needed libart symbol is stripped.
Status Deoptimize(JNIEnv* env, jclass clazz, const char* name, const char* signature);

// Restore the original. Backup remains callable.
Status Unhook(JNIEnv* env, jclass clazz, const char* name, const char* signature);
Status Unhook(JNIEnv* env, const char* class_name, const char* name, const char* signature);

bool IsInitialized();
const char* StatusToString(Status s);

}  // namespace arthook

#endif  // ARTHOOK_ARTHOOK_H_
