// SPDX-License-Identifier: Apache-2.0
//
// Batch helper: hook every declared method of a given name on a class, across
// all overloads. Useful when a target's signature varies across Android
// versions (e.g. SSL-pinning checks) — the example ssl_bypass hand-rolled
// this pattern.

#include "arthook/ArtHook.h"

#include <cstring>

#include "jvm/ClassLookup.h"
#include "util/Log.h"

namespace arthook {

int HookAllOverloads(JNIEnv* env, jclass clazz, const char* name, void* replacement) {
    if (!IsInitialized() || !env || !clazz || !name || !replacement) return 0;

    jclass class_cls = env->FindClass("java/lang/Class");
    jclass method_cls = env->FindClass("java/lang/reflect/Method");
    jmethodID get_methods =
        class_cls ? env->GetMethodID(class_cls, "getDeclaredMethods",
                                     "()[Ljava/lang/reflect/Method;")
                  : nullptr;
    jmethodID get_name =
        method_cls ? env->GetMethodID(method_cls, "getName", "()Ljava/lang/String;") : nullptr;
    if (!get_methods || !get_name) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (class_cls) env->DeleteLocalRef(class_cls);
        if (method_cls) env->DeleteLocalRef(method_cls);
        return 0;
    }

    auto methods = static_cast<jobjectArray>(env->CallObjectMethod(clazz, get_methods));
    if (env->ExceptionCheck() || !methods) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(class_cls);
        env->DeleteLocalRef(method_cls);
        return 0;
    }

    int count = 0;
    jsize n = env->GetArrayLength(methods);
    for (jsize i = 0; i < n; ++i) {
        jobject m = env->GetObjectArrayElement(methods, i);
        if (!m) continue;
        auto jn = static_cast<jstring>(env->CallObjectMethod(m, get_name));
        if (!env->ExceptionCheck() && jn) {
            const char* mn = env->GetStringUTFChars(jn, nullptr);
            if (mn && std::strcmp(mn, name) == 0 &&
                HookReflected(env, m, replacement, nullptr) == Status::kOk) {
                ++count;
            }
            if (mn) env->ReleaseStringUTFChars(jn, mn);
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        if (jn) env->DeleteLocalRef(jn);
        env->DeleteLocalRef(m);
    }

    env->DeleteLocalRef(methods);
    env->DeleteLocalRef(class_cls);
    env->DeleteLocalRef(method_cls);
    LOGI("HookAllOverloads(%s): %d hooked", name, count);
    return count;
}

int HookAllOverloads(JNIEnv* env, const char* class_name, const char* name, void* replacement) {
    if (!env || !class_name) return 0;
    jclass clazz = FindClassByName(env, class_name);
    if (!clazz) return 0;
    int count = HookAllOverloads(env, clazz, name, replacement);
    env->DeleteLocalRef(clazz);
    return count;
}

}  // namespace arthook
