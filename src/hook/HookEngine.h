// SPDX-License-Identifier: Apache-2.0
//
// Hook installation engine. The public API in include/arthook/ArtHook.h
// forwards here.

#ifndef ARTHOOK_HOOK_HOOKENGINE_H_
#define ARTHOOK_HOOK_HOOKENGINE_H_

#include <jni.h>

#include "arthook/ArtHook.h"

namespace arthook {

class HookEngine {
public:
    static Status Initialize(JNIEnv* env, bool verify);

    static Status Hook(JNIEnv* env,
                       jclass clazz,
                       const char* name,
                       const char* signature,
                       void* replacement,
                       void** backup_out);

    static Status HookReflected(JNIEnv* env,
                                jobject reflected,
                                void* replacement,
                                void** backup_out);

    static Status Unhook(JNIEnv* env, jclass clazz, const char* name, const char* signature);

    static bool IsInitialized();
};

// Internal: true if `target` (an ArtMethodPtr) currently has a hook record.
bool IsTargetHooked(void* target);

}  // namespace arthook

#endif  // ARTHOOK_HOOK_HOOKENGINE_H_
