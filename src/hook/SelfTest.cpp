// SPDX-License-Identifier: Apache-2.0
//
// Device preflight: hook a fresh sentinel native, invoke it, confirm the
// replacement and backup both fire, then unhook and confirm restoration.
// Catches a wrong layout discovery on this device before the consumer hooks
// anything real, turning a would-be crash into a clean kOk/failure result.

#include "arthook/ArtHook.h"

#include <atomic>

#include "probe/Probe.h"
#include "util/Log.h"

namespace arthook {

namespace {

constexpr jint kOrigMagic = 0x0A10A1;
constexpr jint kHookMagic = 0x0B20B2;

std::atomic<jint> g_orig_ran{0};
std::atomic<jint> g_hook_ran{0};
void* g_backup = nullptr;

void SelfTestOrig(JNIEnv*, jclass) { g_orig_ran.store(kOrigMagic); }

void SelfTestHook(JNIEnv* env, jclass cls) {
    g_hook_ran.store(kHookMagic);
    // Prove the backup is callable from inside the replacement.
    if (g_backup) reinterpret_cast<void (*)(JNIEnv*, jclass)>(g_backup)(env, cls);
}

}  // namespace

Status SelfTest(JNIEnv* env) {
    if (!IsInitialized()) return Status::kNotInitialized;
    if (!env) return Status::kInvalidArgument;

    jclass cls = Probe::LoadClass(env);
    const char* name = Probe::MethodName();
    if (!cls || !name) {
        LOGE("SelfTest: could not load probe class");
        return Status::kInternalError;
    }

    Status result = Status::kInternalError;
    JNINativeMethod nm{const_cast<char*>(name), const_cast<char*>("()V"),
                       reinterpret_cast<void*>(&SelfTestOrig)};
    jmethodID mid = nullptr;
    if (env->RegisterNatives(cls, &nm, 1) != JNI_OK) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        LOGE("SelfTest: RegisterNatives failed");
    } else if ((mid = env->GetStaticMethodID(cls, name, "()V")) == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        LOGE("SelfTest: GetStaticMethodID failed");
    } else {
        g_orig_ran = 0;
        g_hook_ran = 0;
        g_backup = nullptr;

        Status s = Hook(env, cls, name, "()V", reinterpret_cast<void*>(&SelfTestHook), &g_backup);
        if (s != Status::kOk) {
            LOGE("SelfTest: Hook failed (%s)", StatusToString(s));
        } else {
            env->CallStaticVoidMethod(cls, mid);  // -> SelfTestHook -> backup
            if (env->ExceptionCheck()) env->ExceptionClear();
            bool hook_fired = g_hook_ran.load() == kHookMagic;
            bool backup_ok = g_orig_ran.load() == kOrigMagic;

            Unhook(env, cls, name, "()V");
            g_orig_ran = 0;
            g_hook_ran = 0;
            env->CallStaticVoidMethod(cls, mid);  // -> original again
            if (env->ExceptionCheck()) env->ExceptionClear();
            bool restored = g_orig_ran.load() == kOrigMagic && g_hook_ran.load() != kHookMagic;

            if (hook_fired && backup_ok && restored) {
                result = Status::kOk;
                LOGI("SelfTest: hooking verified on this device");
            } else {
                LOGE("SelfTest failed: replacement=%d backup=%d restore=%d", hook_fired, backup_ok,
                     restored);
            }
        }
    }

    env->DeleteLocalRef(cls);
    return result;
}

}  // namespace arthook
