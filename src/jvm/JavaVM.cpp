// SPDX-License-Identifier: Apache-2.0

#include "arthook/ArtHook.h"

#include <dlfcn.h>
#include <jni.h>

#include "elf/ElfResolver.h"
#include "util/Log.h"

namespace arthook {

namespace {

using JniGetVmsFn = jint (*)(JavaVM**, jsize, jsize*);

JniGetVmsFn ResolveJniGetCreatedVMs() {
    // libart always exports JNI_GetCreatedJavaVMs (it implements the
    // Invocation API). libnativehelper forwards to it on Android Q+; try
    // it as a fallback in case the host process has libart in a stricter
    // linker namespace.
    if (auto p = reinterpret_cast<JniGetVmsFn>(ResolveLibartSymbol("JNI_GetCreatedJavaVMs"))) {
        return p;
    }
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
    if (!env_out || !vm_out || !attached_out) return Status::kInternalError;
    *env_out = nullptr;
    *vm_out = nullptr;
    *attached_out = false;

    static JniGetVmsFn get_vms = ResolveJniGetCreatedVMs();
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
