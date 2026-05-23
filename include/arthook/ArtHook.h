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
    kInternalError,
};

// Discovers ArtMethod layout. Call once per process before any Hook().
// Idempotent.
Status Initialize(JNIEnv* env);

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
        JavaVM* vm;
        bool attached;
        ~Detacher() {
            if (attached && vm) vm->DetachCurrentThread();
        }
    } guard{vm, attached};
    body(env);
    return Status::kOk;
}

// True when Initialize() captured a usable JNI bridge. Non-native hooks
// need one; supply a fallback via SetBridgeProbe() if false.
bool HasJniBridge();

// Designate a native method whose quick entry is the generic JNI bridge
// inside libart. No-op if Initialize() already captured one. Returns
// kInternalError if the candidate's quick entry isn't in libart.
Status SetBridgeProbe(JNIEnv* env, jclass clazz, const char* name, const char* signature);

// Replace `name`/`signature` on `clazz` with `replacement` (JNI calling
// convention). `backup_out` receives:
//   - NATIVE target → callable C fn pointer (original JNI impl).
//   - NON-NATIVE target → jmethodID (as void*). Invoke via
//     env->CallNonvirtual*Method(thiz, declaringClass, backup, args...)
//     or CallStatic*Method for static targets. Plain virtual Call*Method
//     re-resolves to the patched target and infinitely recurses.
// Backup stays callable after Unhook().
Status Hook(JNIEnv* env,
            jclass clazz,
            const char* name,
            const char* signature,
            void* replacement,
            void** backup_out);

// As Hook(), but identifies the target via a java.lang.reflect.Method.
Status HookReflected(JNIEnv* env, jobject reflected_method, void* replacement, void** backup_out);

// Restore the original. Backup remains callable.
Status Unhook(JNIEnv* env, jclass clazz, const char* name, const char* signature);

bool IsInitialized();
const char* StatusToString(Status s);

}  // namespace arthook

#endif  // ARTHOOK_ARTHOOK_H_
