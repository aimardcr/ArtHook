// SPDX-License-Identifier: Apache-2.0
//
// Comprehensive SSL pinning bypass for arthook. Port of Maurizio Siddu's
// frida_multiple_unpinning.js — best-effort hooks for every well-known
// pinning library; absent targets are silently skipped.
//
// Loaded via dlopen by an injector (JsHook / NativeLibrary / zygisk).
// The constructor spawns a worker that does the actual install so dlopen
// returns quickly.
//
// Prerequisite: the proxy's CA must already be in the device's system
// trust store. These hooks only neutralize app-level cert pinning that
// runs AFTER the TLS handshake successfully validates the server cert.

#include <arthook/ArtHook.h>
#include <arthook/Hooked.h>

#include <android/log.h>
#include <pthread.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

#define TAG "ssl-bypass"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

namespace {

std::vector<arthook::Hooked> g_hooks;

// ---- Classloader navigation ------------------------------------------------

jclass FindClassInApp(JNIEnv* env, const char* slash_name) {
    if (jclass c = env->FindClass(slash_name)) return c;
    if (env->ExceptionCheck()) env->ExceptionClear();

    jclass atCls = env->FindClass("android/app/ActivityThread");
    if (!atCls) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }
    jmethodID curApp =
        env->GetStaticMethodID(atCls, "currentApplication", "()Landroid/app/Application;");
    jobject app = env->CallStaticObjectMethod(atCls, curApp);
    env->DeleteLocalRef(atCls);
    if (env->ExceptionCheck() || !app) {
        env->ExceptionClear();
        return nullptr;
    }

    jclass appCls = env->GetObjectClass(app);
    jmethodID getCL = env->GetMethodID(appCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject cl = env->CallObjectMethod(app, getCL);
    env->DeleteLocalRef(appCls);
    env->DeleteLocalRef(app);
    if (!cl) return nullptr;

    std::string dotted;
    dotted.reserve(64);
    for (const char* p = slash_name; *p; ++p) dotted += (*p == '/') ? '.' : *p;

    jclass clCls = env->GetObjectClass(cl);
    jmethodID loadClass =
        env->GetMethodID(clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring jname = env->NewStringUTF(dotted.c_str());
    jobject loaded = env->CallObjectMethod(cl, loadClass, jname);
    env->DeleteLocalRef(jname);
    env->DeleteLocalRef(clCls);
    env->DeleteLocalRef(cl);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return reinterpret_cast<jclass>(loaded);
}

// ---- Reusable hook functions -----------------------------------------------
//
// Each function targets a *return type*, not a specific signature. JNI on
// AArch64 passes args in x2..x7 then on the stack; ignoring extra arg
// slots is ABI-safe, so one fn declaration with the max arg count we
// expect works for every overload with the same return type.

extern "C" void Hook_Void(
    JNIEnv*, jobject, void*, void*, void*, void*, void*, void*, void*, void*) {}

extern "C" jboolean Hook_BoolTrue(
    JNIEnv*, jobject, void*, void*, void*, void*, void*, void*, void*) {
    return JNI_TRUE;
}

extern "C" jobject Hook_EmptyList(
    JNIEnv* env, jobject, void*, void*, void*, void*, void*, void*, void*, void*) {
    jclass al = env->FindClass("java/util/ArrayList");
    jmethodID ctor = env->GetMethodID(al, "<init>", "()V");
    jobject list = env->NewObject(al, ctor);
    env->DeleteLocalRef(al);
    return list;
}

extern "C" jobject Hook_PassthroughFirstArg(
    JNIEnv*, jobject, jobject first_arg, void*, void*, void*, void*, void*, void*) {
    return first_arg;
}

// ---- Reflection-based by-name install -------------------------------------
//
// For methods whose signature varies across Android versions, hook every
// overload by name (matching Frida's `Foo.bar.implementation = ...` with
// no explicit .overload). The caller picks the hook fn whose return-type
// shape suits every overload — typically that means picking a fn whose
// return matches.

int InstallAllByName(JNIEnv* env, jclass c, const char* method_name, void* hook_fn) {
    jclass classCls = env->GetObjectClass(c);
    jmethodID gdm =
        env->GetMethodID(classCls, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
    auto methods = reinterpret_cast<jobjectArray>(env->CallObjectMethod(c, gdm));
    if (env->ExceptionCheck() || !methods) {
        env->ExceptionClear();
        env->DeleteLocalRef(classCls);
        return 0;
    }

    jclass methodCls = env->FindClass("java/lang/reflect/Method");
    jmethodID getName = env->GetMethodID(methodCls, "getName", "()Ljava/lang/String;");
    jclass accCls = env->FindClass("java/lang/reflect/AccessibleObject");
    jmethodID setAcc = env->GetMethodID(accCls, "setAccessible", "(Z)V");

    int hooked = 0;
    jsize n = env->GetArrayLength(methods);
    for (jsize i = 0; i < n; i++) {
        jobject m = env->GetObjectArrayElement(methods, i);
        auto jname = reinterpret_cast<jstring>(env->CallObjectMethod(m, getName));
        const char* nm = env->GetStringUTFChars(jname, nullptr);
        bool match = nm && std::strcmp(nm, method_name) == 0;
        if (nm) env->ReleaseStringUTFChars(jname, nm);
        env->DeleteLocalRef(jname);

        if (match) {
            env->CallVoidMethod(m, setAcc, JNI_TRUE);
            if (env->ExceptionCheck()) env->ExceptionClear();
            void* backup = nullptr;
            arthook::Status s = arthook::HookReflected(env, m, hook_fn, &backup);
            if (s == arthook::Status::kOk) ++hooked;
        }
        env->DeleteLocalRef(m);
    }

    env->DeleteLocalRef(methods);
    env->DeleteLocalRef(classCls);
    env->DeleteLocalRef(methodCls);
    env->DeleteLocalRef(accCls);
    return hooked;
}

// ---- Spec table ------------------------------------------------------------

struct HookSpec {
    const char* clazz;
    const char* method;
    const char* signature;  // nullptr → hook all overloads by name via reflection
    void* hook_fn;
};

const HookSpec kSpecs[] = {
    // --- OkHTTPv3 (quadruple bypass) ---
    {"okhttp3/CertificatePinner",
     "check",
     "(Ljava/lang/String;Ljava/util/List;)V",
     (void*)&Hook_Void},
    {"okhttp3/CertificatePinner",
     "check",
     "(Ljava/lang/String;Ljava/security/cert/Certificate;)V",
     (void*)&Hook_Void},
    {"okhttp3/CertificatePinner",
     "check",
     "(Ljava/lang/String;[Ljava/security/cert/Certificate;)V",
     (void*)&Hook_Void},
    {"okhttp3/CertificatePinner",
     "check$okhttp",
     "(Ljava/lang/String;Lkotlin/jvm/functions/Function0;)V",
     (void*)&Hook_Void},

    // --- Trustkit ---
    {"com/datatheorem/android/trustkit/pinning/OkHostnameVerifier",
     "verify",
     "(Ljava/lang/String;Ljavax/net/ssl/SSLSession;)Z",
     (void*)&Hook_BoolTrue},
    {"com/datatheorem/android/trustkit/pinning/OkHostnameVerifier",
     "verify",
     "(Ljava/lang/String;Ljava/security/cert/X509Certificate;)Z",
     (void*)&Hook_BoolTrue},
    {"com/datatheorem/android/trustkit/pinning/PinningTrustManager",
     "checkServerTrusted",
     "([Ljava/security/cert/X509Certificate;Ljava/lang/String;)V",
     (void*)&Hook_Void},

    // --- TrustManagerImpl (apex conscrypt, Android > 7) ---
    // Signature varies across Android versions — enumerate by name.
    {"com/android/org/conscrypt/TrustManagerImpl",
     "checkTrustedRecursive",
     nullptr,
     (void*)&Hook_EmptyList},
    {"com/android/org/conscrypt/TrustManagerImpl",
     "verifyChain",
     nullptr,
     (void*)&Hook_PassthroughFirstArg},

    // --- GMS TrustManagerImpl (Google Play Services conscrypt) ---
    {"com/google/android/gms/org/conscrypt/TrustManagerImpl",
     "checkTrustedRecursive",
     nullptr,
     (void*)&Hook_EmptyList},
    {"com/google/android/gms/org/conscrypt/TrustManagerImpl",
     "verifyChain",
     nullptr,
     (void*)&Hook_PassthroughFirstArg},

    // --- Appcelerator Titanium ---
    {"appcelerator/https/PinningTrustManager",
     "checkServerTrusted",
     "([Ljava/security/cert/X509Certificate;Ljava/lang/String;)V",
     (void*)&Hook_Void},

    // --- Fabric (legacy Crashlytics SDK) ---
    {"io/fabric/sdk/android/services/network/PinningTrustManager",
     "checkServerTrusted",
     "([Ljava/security/cert/X509Certificate;Ljava/lang/String;)V",
     (void*)&Hook_Void},

    // --- Conscrypt OpenSSLSocketImpl (removed in Android 14+, kept for older) ---
    {"com/android/org/conscrypt/OpenSSLSocketImpl",
     "verifyCertificateChain",
     nullptr,
     (void*)&Hook_Void},
    {"com/android/org/conscrypt/OpenSSLEngineSocketImpl",
     "verifyCertificateChain",
     nullptr,
     (void*)&Hook_Void},

    // --- Apache Harmony OpenSSLSocketImpl (very old Android) ---
    {"org/apache/harmony/xnet/provider/jsse/OpenSSLSocketImpl",
     "verifyCertificateChain",
     nullptr,
     (void*)&Hook_Void},

    // --- PhoneGap ---
    {"nl/xservices/plugins/sslCertificateChecker",
     "execute",
     "(Ljava/lang/String;Lorg/json/JSONArray;Lorg/apache/cordova/CallbackContext;)Z",
     (void*)&Hook_BoolTrue},

    // --- IBM MobileFirst (instance methods on WLClient singleton — Frida
    //     dispatches through getInstance(); we hook the method on the
    //     class directly, ART finds the instance ArtMethod) ---
    {"com/worklight/wlclient/api/WLClient",
     "pinTrustedCertificatePublicKey",
     "(Ljava/lang/String;)V",
     (void*)&Hook_Void},
    {"com/worklight/wlclient/api/WLClient",
     "pinTrustedCertificatePublicKey",
     "([Ljava/lang/String;)V",
     (void*)&Hook_Void},

    // --- IBM WorkLight HostNameVerifierWithCertificatePinning ---
    {"com/worklight/wlclient/certificatepinning/HostNameVerifierWithCertificatePinning",
     "verify",
     nullptr,
     (void*)&Hook_BoolTrue},

    // --- WebViewClient (catches subclasses that don't override) ---
    {"android/webkit/WebViewClient",
     "onReceivedSslError",
     "(Landroid/webkit/WebView;Landroid/webkit/SslErrorHandler;Landroid/net/http/SslError;)V",
     (void*)&Hook_Void},
    {"android/webkit/WebViewClient", "onReceivedError", nullptr, (void*)&Hook_Void},

    // --- Apache Cordova WebViewClient ---
    {"org/apache/cordova/CordovaWebViewClient",
     "onReceivedSslError",
     "(Landroid/webkit/WebView;Landroid/webkit/SslErrorHandler;Landroid/net/http/SslError;)V",
     (void*)&Hook_Void},

    // --- Boye AbstractVerifier ---
    {"ch/boye/httpclientandroidlib/conn/ssl/AbstractVerifier",
     "verify",
     nullptr,
     (void*)&Hook_Void},

    // --- Apache AbstractVerifier (4 overloads) ---
    {"org/apache/http/conn/ssl/AbstractVerifier", "verify", nullptr, (void*)&Hook_Void},

    // --- Conscrypt CertPinManager ---
    {"com/android/org/conscrypt/CertPinManager",
     "checkChainPinning",
     "(Ljava/lang/String;Ljava/util/List;)V",
     (void*)&Hook_Void},
    {"com/android/org/conscrypt/CertPinManager",
     "isChainValid",
     "(Ljava/lang/String;Ljava/util/List;)Z",
     (void*)&Hook_BoolTrue},

    // --- CWAC-Netsecurity ---
    {"com/commonsware/cwac/netsecurity/conscrypt/CertPinManager",
     "isChainValid",
     "(Ljava/lang/String;Ljava/util/List;)Z",
     (void*)&Hook_BoolTrue},

    // --- IBM Worklight Androidgap plugin ---
    {"com/worklight/androidgap/plugin/WLCertificatePinningPlugin",
     "execute",
     "(Ljava/lang/String;Lorg/json/JSONArray;Lorg/apache/cordova/CallbackContext;)Z",
     (void*)&Hook_BoolTrue},

    // --- Netty FingerprintTrustManagerFactory ---
    {"io/netty/handler/ssl/util/FingerprintTrustManagerFactory",
     "checkTrusted",
     nullptr,
     (void*)&Hook_Void},

    // --- Squareup CertificatePinner (OkHttp < v3) ---
    {"com/squareup/okhttp/CertificatePinner",
     "check",
     "(Ljava/lang/String;Ljava/security/cert/Certificate;)V",
     (void*)&Hook_Void},
    {"com/squareup/okhttp/CertificatePinner",
     "check",
     "(Ljava/lang/String;Ljava/util/List;)V",
     (void*)&Hook_Void},

    // --- Squareup OkHostnameVerifier ---
    {"com/squareup/okhttp/internal/tls/OkHostnameVerifier",
     "verify",
     "(Ljava/lang/String;Ljava/security/cert/X509Certificate;)Z",
     (void*)&Hook_BoolTrue},
    {"com/squareup/okhttp/internal/tls/OkHostnameVerifier",
     "verify",
     "(Ljava/lang/String;Ljavax/net/ssl/SSLSession;)Z",
     (void*)&Hook_BoolTrue},

    // --- Flutter pinning plugins ---
    {"diefferson/http_certificate_pinning/HttpCertificatePinning",
     "checkConnexion",
     nullptr,
     (void*)&Hook_BoolTrue},
    {"com/macif/plugin/sslpinningplugin/SslPinningPlugin",
     "checkConnexion",
     nullptr,
     (void*)&Hook_BoolTrue},
};

// ---- Install driver --------------------------------------------------------

int TryInstall(JNIEnv* env, const HookSpec& spec) {
    jclass c = FindClassInApp(env, spec.clazz);
    if (!c) {
        LOGI("[-] %s not in process; skipping", spec.clazz);
        return 0;
    }

    int installed = 0;
    if (spec.signature) {
        g_hooks.emplace_back();
        arthook::Status s =
            g_hooks.back().install(env, c, spec.method, spec.signature, spec.hook_fn);
        if (s == arthook::Status::kOk) {
            LOGI("[+] hooked %s.%s%s", spec.clazz, spec.method, spec.signature);
            installed = 1;
        } else {
            g_hooks.pop_back();
            if (s == arthook::Status::kMethodNotFound) {
                LOGI("[-] %s.%s%s overload not present", spec.clazz, spec.method, spec.signature);
            } else {
                LOGW("[-] %s.%s%s install rc=%d",
                     spec.clazz,
                     spec.method,
                     spec.signature,
                     static_cast<int>(s));
            }
        }
    } else {
        installed = InstallAllByName(env, c, spec.method, spec.hook_fn);
        if (installed > 0) {
            LOGI("[+] hooked %s.%s (%d overload%s by name)",
                 spec.clazz,
                 spec.method,
                 installed,
                 installed == 1 ? "" : "s");
        } else {
            LOGI("[-] %s.%s no matching overload", spec.clazz, spec.method);
        }
    }

    env->DeleteLocalRef(c);
    return installed;
}

void* MainThread(void* /*arg*/) {
    LOGI("[+] Attaching to JavaVM...");

    arthook::AttachToJavaVM([](JNIEnv* env) {
        if (arthook::Initialize(env) != arthook::Status::kOk) {
            LOGW("[-] arthook::Initialize failed");
            return;
        }

        g_hooks.reserve(sizeof(kSpecs) / sizeof(kSpecs[0]));
        int total = 0;
        for (const HookSpec& spec : kSpecs) {
            total += TryInstall(env, spec);
        }
        LOGI("[+] bypass active — %d hook(s) installed", total);
    });
    return nullptr;
}

}  // namespace

__attribute__((constructor)) void init() {
    LOGI("[+] SSL Bypass startup");
    pthread_t t;
    if (pthread_create(&t, nullptr, &MainThread, nullptr) == 0) {
        pthread_detach(t);
    } else {
        LOGW("[-] pthread_create failed; payload inactive");
    }
}
