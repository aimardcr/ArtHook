// SPDX-License-Identifier: Apache-2.0

#include "arthook/Hooked.h"

#include <utility>

#include "art/AccessFlags.h"
#include "art/ArtMethod.h"
#include "jvm/ClassLookup.h"
#include "util/Log.h"

namespace arthook {

namespace {

// Reclaim the declaring-class GlobalRef when a handle is dropped without
// release(). We do NOT auto-Unhook: the hook stays installed (a warned leak),
// since Unhook at teardown might run when classes are no longer loadable.
void BestEffortReleaseRef(jclass decl) {
    if (!decl) return;
    JNIEnv* env = nullptr;
    JavaVM* vm = nullptr;
    bool attached = false;
    if (detail::AcquireJniEnv(&env, &vm, &attached) == Status::kOk && env) {
        env->DeleteGlobalRef(decl);
    }
    if (attached && vm) vm->DetachCurrentThread();
}

}  // namespace

Hooked::Hooked(Hooked&& o) noexcept
    : backup_fn_(o.backup_fn_),
      backup_jm_(o.backup_jm_),
      target_(o.target_),
      decl_(o.decl_),
      is_static_(o.is_static_),
      name_(std::move(o.name_)),
      sig_(std::move(o.sig_)) {
    o.backup_fn_ = nullptr;
    o.backup_jm_ = nullptr;
    o.target_ = nullptr;
    o.decl_ = nullptr;
    o.is_static_ = false;
}

Hooked& Hooked::operator=(Hooked&& o) noexcept {
    if (this == &o) return *this;
    if (installed()) {
        LOGW(
            "Hooked: move-assigning onto an installed handle (%s%s); the "
            "previous hook leaks (call release(env) first). Reclaiming its "
            "declaring-class GlobalRef.",
            name_.c_str(),
            sig_.c_str());
        BestEffortReleaseRef(decl_);
    }
    backup_fn_ = o.backup_fn_;
    backup_jm_ = o.backup_jm_;
    target_ = o.target_;
    decl_ = o.decl_;
    is_static_ = o.is_static_;
    name_ = std::move(o.name_);
    sig_ = std::move(o.sig_);
    o.backup_fn_ = nullptr;
    o.backup_jm_ = nullptr;
    o.target_ = nullptr;
    o.decl_ = nullptr;
    o.is_static_ = false;
    return *this;
}

Hooked::~Hooked() {
    if (installed()) {
        LOGW(
            "Hooked: destroyed while still installed (%s%s); the hook leaks. "
            "Call release(env) before destruction. Reclaiming the "
            "declaring-class GlobalRef.",
            name_.c_str(),
            sig_.c_str());
        BestEffortReleaseRef(decl_);
    }
}

Status Hooked::install(
    JNIEnv* env, jclass clazz, const char* name, const char* signature, void* replacement) {
    if (installed()) return Status::kAlreadyHooked;
    if (!env || !clazz || !name || !signature || !replacement) return Status::kInvalidArgument;

    ArtMethodPtr target = ArtMethodFromJniBinding(env, clazz, name, signature);
    if (!target) return Status::kMethodNotFound;

    uint32_t flags = GetAccessFlags(target);
    bool is_native = (flags & kAccNative) != 0;
    bool is_static = (flags & kAccStatic) != 0;

    jclass decl_global = static_cast<jclass>(env->NewGlobalRef(clazz));
    if (!decl_global) return Status::kOutOfMemory;

    void* backup = nullptr;
    Status s = arthook::Hook(env, clazz, name, signature, replacement, &backup);
    if (s != Status::kOk) {
        env->DeleteGlobalRef(decl_global);
        return s;
    }

    decl_ = decl_global;
    target_ = target;
    is_static_ = is_static;
    name_ = name;
    sig_ = signature;
    if (is_native) {
        backup_fn_ = backup;
        backup_jm_ = nullptr;
    } else {
        backup_fn_ = nullptr;
        backup_jm_ = reinterpret_cast<jmethodID>(backup);
    }
    return Status::kOk;
}

Status Hooked::install(JNIEnv* env,
                       const char* class_name,
                       const char* name,
                       const char* signature,
                       void* replacement) {
    if (!env || !class_name) return Status::kInvalidArgument;
    jclass clazz = FindClassByName(env, class_name);
    if (!clazz) return Status::kMethodNotFound;
    Status s = install(env, clazz, name, signature, replacement);
    env->DeleteLocalRef(clazz);
    return s;
}

Status Hooked::release(JNIEnv* env) {
    if (!installed()) return Status::kOk;
    if (!env) return Status::kInvalidArgument;

    Status s = arthook::Unhook(env, decl_, name_.c_str(), sig_.c_str());
    // Tear down only once the hook is gone (kOk / kNotHooked); on other
    // failures keep state so installed() stays true and the caller can retry.
    if (s != Status::kOk && s != Status::kNotHooked) {
        return s;
    }

    env->DeleteGlobalRef(decl_);
    decl_ = nullptr;
    backup_fn_ = nullptr;
    backup_jm_ = nullptr;
    target_ = nullptr;
    is_static_ = false;
    name_.clear();
    sig_.clear();
    return s;
}

}  // namespace arthook
