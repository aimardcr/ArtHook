// SPDX-License-Identifier: Apache-2.0
//
// RAII handle for a single arthook installation, with a typed `invoke()`
// that calls the original method uniformly for native and non-native
// targets, instance and static.

#ifndef ARTHOOK_HOOKED_H_
#define ARTHOOK_HOOKED_H_

#include <jni.h>

#include <string>
#include <type_traits>

#include "arthook/ArtHook.h"

namespace arthook {

// Internal: re-sync the backup ArtMethod's declaring-class ref from the live
// target before invoking it (a moving GC can otherwise leave the off-heap
// backup's copy stale). No-op on null. Used by Hooked::invoke(). Not public.
namespace detail {
void RefreshBackupClass(void* backup, void* target);
}

class Hooked {
public:
    Hooked() = default;
    Hooked(const Hooked&) = delete;
    Hooked& operator=(const Hooked&) = delete;
    Hooked(Hooked&& other) noexcept;
    Hooked& operator=(Hooked&& other) noexcept;
    ~Hooked();

    // Install a hook. `clazz` may be a local ref, we promote our own
    // GlobalRef internally. `replacement` must follow the JNI calling
    // convention. Idempotent failure: returns the status without altering
    // *this if install fails.
    Status install(
        JNIEnv* env, jclass clazz, const char* name, const char* signature, void* replacement);

    // Convenience overload: resolve `class_name` via the app classloader
    // (with FindClass fallback) and forward. Accepts JNI form
    // ("com/foo/Bar") or dotted form ("com.foo.Bar").
    Status install(JNIEnv* env,
                   const char* class_name,
                   const char* name,
                   const char* signature,
                   void* replacement);

    // Unhook and release the GlobalRef. Required before destruction (we
    // have no JNIEnv in the destructor). Calling on an uninstalled
    // handle is a no-op returning kOk.
    Status release(JNIEnv* env);

    bool installed() const { return decl_ != nullptr; }
    bool target_is_native() const { return backup_jm_ == nullptr && installed(); }
    bool target_is_static() const { return is_static_; }

    // Invoke the original. `thiz` is ignored for static targets.
    //
    // Does not check or clear exceptions: if the original throws, it stays
    // pending and the return value is unspecified. Returning it straight to
    // the VM is fine (ART rethrows), but call ExceptionCheck() before any
    // further JNI calls or before substituting your own result.
    template <class Ret, class... Args>
    Ret invoke(JNIEnv* env, jobject thiz, Args... args) const;

private:
    void* backup_fn_ = nullptr;      // valid when target was native
    jmethodID backup_jm_ = nullptr;  // valid when target was non-native
    void* target_ = nullptr;         // live target ArtMethod (for backup refresh)
    jclass decl_ = nullptr;          // GlobalRef of declaring class
    bool is_static_ = false;
    std::string name_;
    std::string sig_;
};

template <class Ret, class... Args>
Ret Hooked::invoke(JNIEnv* env, jobject thiz, Args... args) const {
    if (backup_fn_) {
        // Native target: call the original JNI impl directly. Second arg is
        // the declaring jclass (static) or the receiver (instance); pick the
        // matching fn-ptr type so the call is well-formed.
        if (is_static_) {
            using FnStatic = Ret (*)(JNIEnv*, jclass, Args...);
            return reinterpret_cast<FnStatic>(backup_fn_)(env, decl_, args...);
        }
        using FnInstance = Ret (*)(JNIEnv*, jobject, Args...);
        return reinterpret_cast<FnInstance>(backup_fn_)(env, thiz, args...);
    }

    // Non-native: keep the off-heap backup's class ref fresh against moving GC.
    detail::RefreshBackupClass(reinterpret_cast<void*>(backup_jm_), target_);

#define ARTHOOK_DISPATCH(Suffix)                                              \
    (is_static_ ? env->CallStatic##Suffix##Method(decl_, backup_jm_, args...) \
                : env->CallNonvirtual##Suffix##Method(thiz, decl_, backup_jm_, args...))

    if constexpr (std::is_same_v<Ret, void>)
        return ARTHOOK_DISPATCH(Void);
    else if constexpr (std::is_same_v<Ret, jboolean>)
        return ARTHOOK_DISPATCH(Boolean);
    else if constexpr (std::is_same_v<Ret, jbyte>)
        return ARTHOOK_DISPATCH(Byte);
    else if constexpr (std::is_same_v<Ret, jchar>)
        return ARTHOOK_DISPATCH(Char);
    else if constexpr (std::is_same_v<Ret, jshort>)
        return ARTHOOK_DISPATCH(Short);
    else if constexpr (std::is_same_v<Ret, jint>)
        return ARTHOOK_DISPATCH(Int);
    else if constexpr (std::is_same_v<Ret, jlong>)
        return ARTHOOK_DISPATCH(Long);
    else if constexpr (std::is_same_v<Ret, jfloat>)
        return ARTHOOK_DISPATCH(Float);
    else if constexpr (std::is_same_v<Ret, jdouble>)
        return ARTHOOK_DISPATCH(Double);
    else
        return reinterpret_cast<Ret>(ARTHOOK_DISPATCH(Object));

#undef ARTHOOK_DISPATCH
}

}  // namespace arthook

#endif  // ARTHOOK_HOOKED_H_
