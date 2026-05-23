// SPDX-License-Identifier: Apache-2.0

#include "arthook/Hooked.h"

#include <utility>

#include "art/AccessFlags.h"
#include "art/ArtMethod.h"
#include "util/Log.h"

namespace arthook {

Hooked::Hooked(Hooked&& o) noexcept
    : backup_fn_(o.backup_fn_),
      backup_jm_(o.backup_jm_),
      decl_(o.decl_),
      is_static_(o.is_static_),
      name_(std::move(o.name_)),
      sig_(std::move(o.sig_)) {
    o.backup_fn_ = nullptr;
    o.backup_jm_ = nullptr;
    o.decl_ = nullptr;
    o.is_static_ = false;
}

Hooked& Hooked::operator=(Hooked&& o) noexcept {
    if (this == &o) return *this;
    if (installed()) {
        LOGW(
            "Hooked: move-assigning onto an installed handle; the previous "
            "hook + GlobalRef will leak (no JNIEnv available here)");
    }
    backup_fn_ = o.backup_fn_;
    backup_jm_ = o.backup_jm_;
    decl_ = o.decl_;
    is_static_ = o.is_static_;
    name_ = std::move(o.name_);
    sig_ = std::move(o.sig_);
    o.backup_fn_ = nullptr;
    o.backup_jm_ = nullptr;
    o.decl_ = nullptr;
    o.is_static_ = false;
    return *this;
}

Hooked::~Hooked() {
    if (installed()) {
        LOGW(
            "Hooked: destroyed while still installed (%s%s); the hook and "
            "its GlobalRef will leak. Call release(env) before destruction.",
            name_.c_str(),
            sig_.c_str());
    }
}

Status Hooked::install(
    JNIEnv* env, jclass clazz, const char* name, const char* signature, void* replacement) {
    if (installed()) return Status::kAlreadyHooked;
    if (!env || !clazz || !name || !signature || !replacement) return Status::kInternalError;

    ArtMethodPtr target = ArtMethodFromJniBinding(env, clazz, name, signature);
    if (!target) return Status::kMethodNotFound;

    uint32_t flags = GetAccessFlags(target);
    bool is_native = (flags & kAccNative) != 0;
    bool is_static = (flags & kAccStatic) != 0;

    jclass decl_global = static_cast<jclass>(env->NewGlobalRef(clazz));
    if (!decl_global) return Status::kInternalError;

    void* backup = nullptr;
    Status s = arthook::Hook(env, clazz, name, signature, replacement, &backup);
    if (s != Status::kOk) {
        env->DeleteGlobalRef(decl_global);
        return s;
    }

    decl_ = decl_global;
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

Status Hooked::release(JNIEnv* env) {
    if (!installed()) return Status::kOk;
    if (!env) return Status::kInternalError;

    Status s = arthook::Unhook(env, decl_, name_.c_str(), sig_.c_str());
    env->DeleteGlobalRef(decl_);
    decl_ = nullptr;
    backup_fn_ = nullptr;
    backup_jm_ = nullptr;
    is_static_ = false;
    name_.clear();
    sig_.clear();
    return s;
}

}  // namespace arthook
