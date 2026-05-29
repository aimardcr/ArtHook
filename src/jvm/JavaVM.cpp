// SPDX-License-Identifier: Apache-2.0

#include "arthook/ArtHook.h"

#include <dlfcn.h>
#include <jni.h>

#include <atomic>

#include "elf/ElfResolver.h"
#include "util/Log.h"

namespace arthook {

namespace {

using JniGetVmsFn = jint (*)(JavaVM**, jsize, jsize*);

JniGetVmsFn ResolveJniGetCreatedVMs() {
    // libart exports JNI_GetCreatedJavaVMs; fall back to libnativehelper
    // (forwards on Q+) in case libart is in a stricter linker namespace.
    if (auto p = reinterpret_cast<JniGetVmsFn>(ResolveLibartSymbol("JNI_GetCreatedJavaVMs"))) {
        return p;
    }
    // Not dlclose'd: the resolved fn pointer must stay valid process-wide.
    if (void* h = dlopen("libnativehelper.so", RTLD_NOW)) {
        if (auto p = reinterpret_cast<JniGetVmsFn>(dlsym(h, "JNI_GetCreatedJavaVMs"))) {
            return p;
        }
    }
    LOGE("AttachToJavaVM: JNI_GetCreatedJavaVMs not resolvable");
    return nullptr;
}

}  // namespace

namespace detail {

Status AcquireJniEnv(JNIEnv** env_out, JavaVM** vm_out, bool* attached_out) {
    if (!env_out || !vm_out || !attached_out) return Status::kInvalidArgument;
    *env_out = nullptr;
    *vm_out = nullptr;
    *attached_out = false;

    // Retry until it resolves, don't cache null forever.
    static std::atomic<JniGetVmsFn> cached{nullptr};
    JniGetVmsFn get_vms = cached.load();
    if (!get_vms) {
        get_vms = ResolveJniGetCreatedVMs();
        if (get_vms) cached.store(get_vms);
    }
    if (!get_vms) return Status::kInternalError;

    JavaVM* vms[1] = {nullptr};
    jsize n = 0;
    if (get_vms(vms, 1, &n) != JNI_OK || n == 0 || !vms[0]) {
        LOGE("AttachToJavaVM: no JavaVM in process");
        return Status::kInternalError;
    }
    JavaVM* vm = vms[0];

    JNIEnv* env = nullptr;
    jint r = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (r == JNI_OK) {
        *env_out = env;
        *vm_out = vm;
        *attached_out = false;
        return Status::kOk;
    }
    if (r == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            LOGE("AttachToJavaVM: AttachCurrentThread failed");
            return Status::kInternalError;
        }
        *env_out = env;
        *vm_out = vm;
        *attached_out = true;
        return Status::kOk;
    }
    LOGE("AttachToJavaVM: GetEnv returned %d", r);
    return Status::kInternalError;
}

}  // namespace detail

}  // namespace arthook
