// Test app entry point: arthook initialization, registered-native targets,
// diagnostic helpers (layout info, RSS, fd count).

#include <android/log.h>
#include <jni.h>

#include <dirent.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

#include <arthook/ArtHook.h>

#define TAG "arthook-test"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// Targets.nativeAddJni is bound via RegisterNatives (not name-based
// lookup) so the "native_registered" test can verify hooking that path.

static jint Native_addJni(JNIEnv*, jclass, jint a, jint b) {
    return a + b;
}

static bool RegisterTargets(JNIEnv* env) {
    jclass cls = env->FindClass("com/ak4ne/arthooktest/Targets");
    if (!cls) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        LOGE("Targets class not found");
        return false;
    }
    JNINativeMethod m[] = {
        {"nativeAddJni", "(II)I", reinterpret_cast<void*>(&Native_addJni)},
    };
    int rc = env->RegisterNatives(cls, m, sizeof(m) / sizeof(m[0]));
    env->DeleteLocalRef(cls);
    if (rc != JNI_OK) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        LOGE("RegisterNatives failed");
        return false;
    }
    return true;
}

// ---- Diagnostics ---------------------------------------------------------

extern "C" {

JNIEXPORT jstring JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_layoutInfo(JNIEnv* env,
                                                                                     jclass) {
    char buf[512];
    bool inited = arthook::IsInitialized();
    std::snprintf(buf,
                  sizeof(buf),
                  "arthook initialized: %s\n"
                  "(layout fields are inside the static library; see logcat "
                  "for the discovery log printed at Initialize() time)\n",
                  inited ? "yes" : "no");
    return env->NewStringUTF(buf);
}

JNIEXPORT jlong JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_processRssKb(JNIEnv*,
                                                                                     jclass) {
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long rss = -1;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "VmRSS:", 6) == 0) {
            std::sscanf(line + 6, "%ld", &rss);
            break;
        }
    }
    std::fclose(f);
    return static_cast<jlong>(rss);
}

JNIEXPORT jint JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_openFdCount(JNIEnv*,
                                                                                   jclass) {
    DIR* d = ::opendir("/proc/self/fd");
    if (!d) return -1;
    int n = 0;
    while (struct dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        ++n;
    }
    ::closedir(d);
    return n;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;

    arthook::Status s = arthook::Initialize(env);
    if (s != arthook::Status::kOk) {
        LOGE("arthook::Initialize failed at JNI_OnLoad: %s", arthook::StatusToString(s));
    } else {
        LOGI("arthook initialized");
    }

    if (!RegisterTargets(env)) {
        LOGE("Failed to register native targets, tests of native-bound methods will fail");
    }

    if (arthook::HasJniBridge()) {
        LOGI("JNI bridge captured by Initialize()");
    } else {
        LOGW("no JNI bridge, non-native hooks will fail with kNoJniBridge");
    }
    return JNI_VERSION_1_6;
}

}  // extern "C"
