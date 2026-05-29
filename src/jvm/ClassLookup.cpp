// SPDX-License-Identifier: Apache-2.0

#include "jvm/ClassLookup.h"

#include <jni.h>

#include <mutex>
#include <string>

#include "util/Log.h"

namespace arthook {

namespace {

bool HasDot(const char* s) {
    for (const char* p = s; *p; ++p)
        if (*p == '.') return true;
    return false;
}

bool HasSlash(const char* s) {
    for (const char* p = s; *p; ++p)
        if (*p == '/') return true;
    return false;
}

std::string ToDotted(const char* name) {
    std::string out;
    for (const char* p = name; *p; ++p) out += (*p == '/') ? '.' : *p;
    return out;
}

std::string ToSlashed(const char* name) {
    std::string out;
    for (const char* p = name; *p; ++p) out += (*p == '.') ? '/' : *p;
    return out;
}

jclass TryFindClass(JNIEnv* env, const char* slashed) {
    jclass c = env->FindClass(slashed);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return c;
}

// Cached app classloader (GlobalRef) and ClassLoader.loadClass id. Resolved
// once via ActivityThread.currentApplication().getClassLoader(); set-once
// under the lock, then read freely.
std::mutex g_cl_mu;
jobject g_app_loader = nullptr;
jmethodID g_load_class = nullptr;

// Returns false until an Application exists (e.g. early zygote), so the
// caller falls back to FindClass.
bool EnsureAppLoader(JNIEnv* env) {
    std::lock_guard<std::mutex> lk(g_cl_mu);
    if (g_app_loader && g_load_class) return true;

    jclass atCls = env->FindClass("android/app/ActivityThread");
    if (!atCls || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    jmethodID curApp =
        env->GetStaticMethodID(atCls, "currentApplication", "()Landroid/app/Application;");
    if (!curApp) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(atCls);
        return false;
    }
    jobject app = env->CallStaticObjectMethod(atCls, curApp);
    env->DeleteLocalRef(atCls);
    if (env->ExceptionCheck() || !app) {
        env->ExceptionClear();
        return false;
    }

    jclass appCls = env->GetObjectClass(app);
    jmethodID getCL = env->GetMethodID(appCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    env->DeleteLocalRef(appCls);
    if (!getCL) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(app);
        return false;
    }
    jobject loader = env->CallObjectMethod(app, getCL);
    env->DeleteLocalRef(app);
    if (env->ExceptionCheck() || !loader) {
        env->ExceptionClear();
        return false;
    }

    jclass clCls = env->GetObjectClass(loader);
    g_load_class = env->GetMethodID(clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    env->DeleteLocalRef(clCls);
    if (!g_load_class) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(loader);
        return false;
    }
    g_app_loader = env->NewGlobalRef(loader);
    env->DeleteLocalRef(loader);
    return g_app_loader != nullptr;
}

jclass LoadViaAppClassLoader(JNIEnv* env, const char* dotted) {
    if (!EnsureAppLoader(env)) return nullptr;
    jstring jname = env->NewStringUTF(dotted);
    if (!jname) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }
    jobject loaded = env->CallObjectMethod(g_app_loader, g_load_class, jname);
    env->DeleteLocalRef(jname);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return reinterpret_cast<jclass>(loaded);
}

}  // namespace

jclass FindClassByName(JNIEnv* env, const char* name) {
    if (!env || !name || !*name) return nullptr;
    // JNI with a pending exception is UB; bail without clearing it.
    if (env->ExceptionCheck()) {
        LOGE("FindClassByName: called with a pending exception");
        return nullptr;
    }

    // App classloader first (superset of FindClass), then FindClass.
    std::string dotted = HasSlash(name) ? ToDotted(name) : name;
    std::string slashed = HasDot(name) ? ToSlashed(name) : name;

    if (jclass c = LoadViaAppClassLoader(env, dotted.c_str())) return c;
    if (jclass c = TryFindClass(env, slashed.c_str())) return c;
    LOGE("FindClassByName: could not resolve '%s'", name);
    return nullptr;
}

}  // namespace arthook
