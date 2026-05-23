// SPDX-License-Identifier: Apache-2.0
//
// Per-test hook installers, replacement functions, and state.
// Each test is keyed by a string. The registry below maps each key to a
// target {class, name, signature} and a replacement function pointer.
// State (fire counts, recorded args) lives per-key in a global map.

#include <android/log.h>
#include <jni.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <arthook/ArtHook.h>

#define TAG "arthook-test"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

constexpr jint SENTINEL_INT = 0x5EED1234;
constexpr jlong SENTINEL_LONG = 0x5EED5EED5EED5EEDLL;
constexpr jdouble SENTINEL_DOUBLE = 12345.6789;
constexpr const char* SENTINEL_STRING = "<hooked>";

// ---- Per-test state ------------------------------------------------------

struct TestState {
    std::atomic<int> fire_count{0};
    std::atomic<int64_t> last_long[8] = {};
    std::atomic<double> last_double[8] = {};
    std::mutex str_mu;
    std::string last_strings[4];
    std::atomic<int> last_array_len{0};
    std::atomic<bool> last_array_had_null{false};
    std::atomic<int> last_string_length{0};

    void* backup = nullptr;
    jmethodID backup_jm = nullptr;
    jclass declaring_class = nullptr;  // GlobalRef, valid while installed
    bool installed = false;

    void reset() {
        fire_count = 0;
        for (auto& v : last_long) v.store(0);
        for (auto& v : last_double) v.store(0.0);
        {
            std::lock_guard<std::mutex> lk(str_mu);
            for (auto& s : last_strings) s.clear();
        }
        last_array_len = 0;
        last_array_had_null = false;
        last_string_length = 0;
    }
};

std::unordered_map<std::string, TestState> g_state;
std::mutex g_state_mu;

TestState& State(const char* key) {
    std::lock_guard<std::mutex> lk(g_state_mu);
    return g_state[key];
}

// ---- Trampoline-page counter (incremented in install, decremented in unhook)

std::atomic<int> g_trampoline_pages_in_use{0};

// ---- Replacement functions ----------------------------------------------

extern "C" {

// (II)I — Targets.staticAdd
jint Hook_static_int_add(JNIEnv*, jclass, jint a, jint b) {
    auto& s = State("static_int_add");
    s.fire_count++;
    s.last_long[0] = a;
    s.last_long[1] = b;
    return SENTINEL_INT;
}

// ()V — Targets.staticDoNothing
void Hook_static_void(JNIEnv*, jclass) {
    State("static_void_no_args").fire_count++;
}

// (I)I — instance Targets.argInt
jint Hook_instance_int_arg(JNIEnv*, jobject, jint v) {
    auto& s = State("instance_int_arg");
    s.fire_count++;
    s.last_long[0] = v;
    return SENTINEL_INT;
}

jint Hook_arg_int_alt(JNIEnv*, jobject, jint v) {
    auto& s = State("arg_int_alt");
    s.fire_count++;
    s.last_long[0] = v;
    return SENTINEL_INT;
}

// (Ljava/lang/String;)Ljava/lang/String; — Targets.concat
jstring Hook_instance_string_concat(JNIEnv* env, jobject, jstring arg) {
    auto& s = State("instance_string_concat");
    s.fire_count++;
    if (arg) {
        const char* utf = env->GetStringUTFChars(arg, nullptr);
        if (utf) {
            std::lock_guard<std::mutex> lk(s.str_mu);
            s.last_strings[0] = utf;
            env->ReleaseStringUTFChars(arg, utf);
        }
    }
    return env->NewStringUTF(SENTINEL_STRING);
}

// Wrap-pattern hook for concat — calls the backup via CallNonvirtual so the
// vtable lookup doesn't re-resolve to the patched (= recursive) target.
jstring Hook_wrap_concat(JNIEnv* env, jobject thiz, jstring arg) {
    auto& s = State("wrap_concat_with_backup");
    s.fire_count++;
    if (!s.backup_jm || !s.declaring_class) return env->NewStringUTF("NO_BACKUP");
    jstring orig =
        (jstring)env->CallNonvirtualObjectMethod(thiz, s.declaring_class, s.backup_jm, arg);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return env->NewStringUTF("EXC");
    }
    std::string wrapped = "WRAP(";
    if (orig) {
        const char* u = env->GetStringUTFChars(orig, nullptr);
        if (u) {
            wrapped += u;
            env->ReleaseStringUTFChars(orig, u);
        }
        env->DeleteLocalRef(orig);
    }
    wrapped += ")";
    return env->NewStringUTF(wrapped.c_str());
}

// okhttp3.CertificatePinner.check$okhttp(String, Function0)V — calls the
// backup, then swallows any SSLPeerUnverifiedException so the caller sees
// "no pin violation".
void Hook_okhttp_pinner_bypass(JNIEnv* env, jobject self, jstring host, jobject fn) {
    auto& s = State("okhttp_certificate_pinner_bypass");
    s.fire_count++;
    if (!s.backup_jm || !s.declaring_class) return;
    env->CallNonvirtualVoidMethod(self, s.declaring_class, s.backup_jm, host, fn);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// ()Ljava/lang/String; — Targets.FinalGreeter.greet
jstring Hook_final_class_method(JNIEnv* env, jobject) {
    State("final_class_method").fire_count++;
    return env->NewStringUTF(SENTINEL_STRING);
}

// (JDI)J — Targets.longDoubleInt
jlong Hook_long_double_int(JNIEnv*, jobject, jlong a, jdouble b, jint c) {
    auto& s = State("long_double_int_args");
    s.fire_count++;
    s.last_long[0] = a;
    s.last_double[0] = b;
    s.last_long[1] = c;
    return SENTINEL_LONG;
}

// (DDDD)D — Targets.manyDoubles
jdouble Hook_many_doubles(JNIEnv*, jobject, jdouble a, jdouble b, jdouble c, jdouble d) {
    auto& s = State("double_return_many_doubles");
    s.fire_count++;
    s.last_double[0] = a;
    s.last_double[1] = b;
    s.last_double[2] = c;
    s.last_double[3] = d;
    return SENTINEL_DOUBLE;
}

// (IIIIIIII)I — Targets.eightInts
jint Hook_eight_ints(
    JNIEnv*, jobject, jint a, jint b, jint c, jint d, jint e, jint f, jint g, jint h) {
    auto& s = State("eight_int_args");
    s.fire_count++;
    s.last_long[0] = a;
    s.last_long[1] = b;
    s.last_long[2] = c;
    s.last_long[3] = d;
    s.last_long[4] = e;
    s.last_long[5] = f;
    s.last_long[6] = g;
    s.last_long[7] = h;
    return SENTINEL_INT;
}

// ()Ljava/lang/Object; — Targets.returnNull
jobject Hook_returns_null(JNIEnv*, jobject) {
    State("returns_null").fire_count++;
    return nullptr;
}

// ()I — Targets.throwsRuntime
jint Hook_throws_exception(JNIEnv* env, jobject) {
    State("throws_exception").fire_count++;
    jclass cls = env->FindClass("java/lang/RuntimeException");
    env->ThrowNew(cls, "hooked-throw");
    env->DeleteLocalRef(cls);
    return 0;
}

// ()Ljava/lang/String; — PackagePrivateClass.hello
jstring Hook_package_private(JNIEnv* env, jobject) {
    State("package_private_class").fire_count++;
    return env->NewStringUTF(SENTINEL_STRING);
}

// ()I — Targets.callPrivateM
jint Hook_private_method(JNIEnv*, jobject) {
    State("private_method").fire_count++;
    return SENTINEL_INT;
}

jint Hook_call_private_m(JNIEnv*, jobject) {
    State("call_private_m").fire_count++;
    return SENTINEL_INT;
}

// ()I — Targets.protectedM
jint Hook_protected_method(JNIEnv*, jobject) {
    State("protected_method").fire_count++;
    return SENTINEL_INT;
}

// (I)V — Targets.CtorTarget.<init>
void Hook_constructor_init(JNIEnv*, jobject, jint v) {
    auto& s = State("constructor_init");
    s.fire_count++;
    s.last_long[0] = v;
    // Do NOT call backup — leave the new object's fields at defaults so
    // the test can verify the body was skipped.
}

// ()I — Targets.Clinitable.clinitTarget
jint Hook_clinit_target(JNIEnv*, jclass) {
    State("clinit_target").fire_count++;
    return SENTINEL_INT;
}

// ()I — Targets.syncIncrement
jint Hook_synchronized(JNIEnv*, jobject) {
    State("synchronized_method").fire_count++;
    return SENTINEL_INT;
}

// ()I — Targets.finalReturn42
jint Hook_final_method(JNIEnv*, jobject) {
    State("final_method").fire_count++;
    return SENTINEL_INT;
}

// ()I — Targets.WithAbstract.abstractM
jint Hook_abstract(JNIEnv*, jobject) {
    State("abstract_method").fire_count++;
    return SENTINEL_INT;
}

// (II)I — Targets.nativeAddJni (registered via RegisterNatives)
jint Hook_native_registered(JNIEnv*, jclass, jint a, jint b) {
    auto& s = State("native_registered");
    s.fire_count++;
    s.last_long[0] = a;
    s.last_long[1] = b;
    return SENTINEL_INT;
}

// ()Ljava/lang/String; — Targets.UsesDefault.defaultGreet
jstring Hook_interface_default(JNIEnv* env, jobject) {
    State("interface_default").fire_count++;
    return env->NewStringUTF(SENTINEL_STRING);
}

// ()Ljava/lang/String; — Targets.Child.describe
jstring Hook_child_describe(JNIEnv* env, jobject) {
    State("child_describe").fire_count++;
    return env->NewStringUTF(SENTINEL_STRING);
}

// ---- Primitive arg-recording replacements -------------------------------

jboolean Hook_arg_boolean(JNIEnv*, jobject, jboolean v) {
    auto& s = State("arg_boolean");
    s.fire_count++;
    s.last_long[0] = v ? 1 : 0;
    return JNI_FALSE;
}
jbyte Hook_arg_byte(JNIEnv*, jobject, jbyte v) {
    auto& s = State("arg_byte");
    s.fire_count++;
    s.last_long[0] = v;  // sign-extended via int64_t assignment
    return 0;
}
jchar Hook_arg_char(JNIEnv*, jobject, jchar v) {
    auto& s = State("arg_char");
    s.fire_count++;
    s.last_long[0] = v;
    return 0;
}
jshort Hook_arg_short(JNIEnv*, jobject, jshort v) {
    auto& s = State("arg_short");
    s.fire_count++;
    s.last_long[0] = v;
    return 0;
}
jlong Hook_arg_long(JNIEnv*, jobject, jlong v) {
    auto& s = State("arg_long");
    s.fire_count++;
    s.last_long[0] = v;
    return 0;
}
jfloat Hook_arg_float(JNIEnv*, jobject, jfloat v) {
    auto& s = State("arg_float");
    s.fire_count++;
    s.last_double[0] = (double)v;
    return 0.0f;
}
jdouble Hook_arg_double(JNIEnv*, jobject, jdouble v) {
    auto& s = State("arg_double");
    s.fire_count++;
    s.last_double[0] = v;
    return 0.0;
}

// (Ljava/lang/Object;)Z — Targets.isObjectNull
jboolean Hook_is_object_null(JNIEnv*, jobject, jobject o) {
    auto& s = State("is_object_null");
    s.fire_count++;
    s.last_long[0] = (o == nullptr) ? 1 : 0;
    return JNI_FALSE;
}

// (Ljava/lang/String;)I — Targets.measureString
jint Hook_measure_string(JNIEnv* env, jobject, jstring s) {
    auto& st = State("measure_string");
    st.fire_count++;
    if (s) {
        st.last_string_length = env->GetStringLength(s);
    } else {
        st.last_string_length = -1;
    }
    return 0;
}

// ([Ljava/lang/Object;)I — Targets.countNullsInArray
jint Hook_count_nulls(JNIEnv* env, jobject, jobjectArray arr) {
    auto& s = State("count_nulls");
    s.fire_count++;
    if (!arr) {
        s.last_array_len = -1;
        return 0;
    }
    jsize len = env->GetArrayLength(arr);
    s.last_array_len = len;
    bool had_null = false;
    for (jsize i = 0; i < len; ++i) {
        jobject e = env->GetObjectArrayElement(arr, i);
        if (!e) {
            had_null = true;
        } else
            env->DeleteLocalRef(e);
    }
    s.last_array_had_null = had_null;
    return 0;
}

// thread_keys_*: each binds to a distinct primitive-arg target so the
// concurrent install/uninstall test doesn't contend on a single ArtMethod.
jboolean Hook_thread_keys_a(JNIEnv*, jobject, jboolean) {
    State("thread_keys_a").fire_count++;
    return 0;
}
jbyte Hook_thread_keys_b(JNIEnv*, jobject, jbyte) {
    State("thread_keys_b").fire_count++;
    return 0;
}
jchar Hook_thread_keys_c(JNIEnv*, jobject, jchar) {
    State("thread_keys_c").fire_count++;
    return 0;
}
jshort Hook_thread_keys_d(JNIEnv*, jobject, jshort) {
    State("thread_keys_d").fire_count++;
    return 0;
}
jint Hook_thread_keys_e(JNIEnv*, jobject, jint) {
    State("thread_keys_e").fire_count++;
    return 0;
}
jlong Hook_thread_keys_f(JNIEnv*, jobject, jlong) {
    State("thread_keys_f").fire_count++;
    return 0;
}
jfloat Hook_thread_keys_g(JNIEnv*, jobject, jfloat) {
    State("thread_keys_g").fire_count++;
    return 0.0f;
}
jdouble Hook_thread_keys_h(JNIEnv*, jobject, jdouble) {
    State("thread_keys_h").fire_count++;
    return 0.0;
}

}  // extern "C"

// ---- Spec registry -------------------------------------------------------

struct Spec {
    const char* key;
    const char* clazz;
    const char* name;
    const char* sig;
    void* replacement;
};

const Spec kSpecs[] = {
    {"static_int_add",
     "com/ak4ne/arthooktest/Targets",
     "staticAdd",
     "(II)I",
     reinterpret_cast<void*>(&Hook_static_int_add)},
    {"static_void_no_args",
     "com/ak4ne/arthooktest/Targets",
     "staticDoNothing",
     "()V",
     reinterpret_cast<void*>(&Hook_static_void)},
    {"instance_int_arg",
     "com/ak4ne/arthooktest/Targets",
     "argInt",
     "(I)I",
     reinterpret_cast<void*>(&Hook_instance_int_arg)},
    {"arg_int_alt",
     "com/ak4ne/arthooktest/Targets",
     "argInt",
     "(I)I",
     reinterpret_cast<void*>(&Hook_arg_int_alt)},
    {"instance_string_concat",
     "com/ak4ne/arthooktest/Targets",
     "concat",
     "(Ljava/lang/String;)Ljava/lang/String;",
     reinterpret_cast<void*>(&Hook_instance_string_concat)},
    {"wrap_concat_with_backup",
     "com/ak4ne/arthooktest/Targets",
     "concat",
     "(Ljava/lang/String;)Ljava/lang/String;",
     reinterpret_cast<void*>(&Hook_wrap_concat)},
    {"final_class_method",
     "com/ak4ne/arthooktest/Targets$FinalGreeter",
     "greet",
     "()Ljava/lang/String;",
     reinterpret_cast<void*>(&Hook_final_class_method)},
    {"long_double_int_args",
     "com/ak4ne/arthooktest/Targets",
     "longDoubleInt",
     "(JDI)J",
     reinterpret_cast<void*>(&Hook_long_double_int)},
    {"double_return_many_doubles",
     "com/ak4ne/arthooktest/Targets",
     "manyDoubles",
     "(DDDD)D",
     reinterpret_cast<void*>(&Hook_many_doubles)},
    {"eight_int_args",
     "com/ak4ne/arthooktest/Targets",
     "eightInts",
     "(IIIIIIII)I",
     reinterpret_cast<void*>(&Hook_eight_ints)},
    {"returns_null",
     "com/ak4ne/arthooktest/Targets",
     "returnNull",
     "()Ljava/lang/Object;",
     reinterpret_cast<void*>(&Hook_returns_null)},
    {"throws_exception",
     "com/ak4ne/arthooktest/Targets",
     "throwsRuntime",
     "()I",
     reinterpret_cast<void*>(&Hook_throws_exception)},
    {"package_private_class",
     "com/ak4ne/arthooktest/PackagePrivateClass",
     "hello",
     "()Ljava/lang/String;",
     reinterpret_cast<void*>(&Hook_package_private)},
    {"private_method",
     "com/ak4ne/arthooktest/Targets",
     "callPrivateM",
     "()I",
     reinterpret_cast<void*>(&Hook_private_method)},
    {"call_private_m",
     "com/ak4ne/arthooktest/Targets",
     "callPrivateM",
     "()I",
     reinterpret_cast<void*>(&Hook_call_private_m)},
    {"protected_method",
     "com/ak4ne/arthooktest/Targets",
     "protectedM",
     "()I",
     reinterpret_cast<void*>(&Hook_protected_method)},
    {"constructor_init",
     "com/ak4ne/arthooktest/Targets$CtorTarget",
     "<init>",
     "(I)V",
     reinterpret_cast<void*>(&Hook_constructor_init)},
    {"clinit_target",
     "com/ak4ne/arthooktest/Targets$Clinitable",
     "clinitTarget",
     "()I",
     reinterpret_cast<void*>(&Hook_clinit_target)},

    {"synchronized_method",
     "com/ak4ne/arthooktest/Targets",
     "syncIncrement",
     "()I",
     reinterpret_cast<void*>(&Hook_synchronized)},
    {"final_method",
     "com/ak4ne/arthooktest/Targets",
     "finalReturn42",
     "()I",
     reinterpret_cast<void*>(&Hook_final_method)},
    {"abstract_method",
     "com/ak4ne/arthooktest/Targets$WithAbstract",
     "abstractM",
     "()I",
     reinterpret_cast<void*>(&Hook_abstract)},
    {"native_registered",
     "com/ak4ne/arthooktest/Targets",
     "nativeAddJni",
     "(II)I",
     reinterpret_cast<void*>(&Hook_native_registered)},
    {"interface_default",
     "com/ak4ne/arthooktest/Targets$UsesDefault",
     "defaultGreet",
     "()Ljava/lang/String;",
     reinterpret_cast<void*>(&Hook_interface_default)},
    {"child_describe",
     "com/ak4ne/arthooktest/Targets$Child",
     "describe",
     "()Ljava/lang/String;",
     reinterpret_cast<void*>(&Hook_child_describe)},

    {"arg_boolean",
     "com/ak4ne/arthooktest/Targets",
     "argBoolean",
     "(Z)Z",
     reinterpret_cast<void*>(&Hook_arg_boolean)},
    {"arg_byte",
     "com/ak4ne/arthooktest/Targets",
     "argByte",
     "(B)B",
     reinterpret_cast<void*>(&Hook_arg_byte)},
    {"arg_char",
     "com/ak4ne/arthooktest/Targets",
     "argChar",
     "(C)C",
     reinterpret_cast<void*>(&Hook_arg_char)},
    {"arg_short",
     "com/ak4ne/arthooktest/Targets",
     "argShort",
     "(S)S",
     reinterpret_cast<void*>(&Hook_arg_short)},
    {"arg_long",
     "com/ak4ne/arthooktest/Targets",
     "argLong",
     "(J)J",
     reinterpret_cast<void*>(&Hook_arg_long)},
    {"arg_float",
     "com/ak4ne/arthooktest/Targets",
     "argFloat",
     "(F)F",
     reinterpret_cast<void*>(&Hook_arg_float)},
    {"arg_double",
     "com/ak4ne/arthooktest/Targets",
     "argDouble",
     "(D)D",
     reinterpret_cast<void*>(&Hook_arg_double)},
    {"is_object_null",
     "com/ak4ne/arthooktest/Targets",
     "isObjectNull",
     "(Ljava/lang/Object;)Z",
     reinterpret_cast<void*>(&Hook_is_object_null)},
    {"measure_string",
     "com/ak4ne/arthooktest/Targets",
     "measureString",
     "(Ljava/lang/String;)I",
     reinterpret_cast<void*>(&Hook_measure_string)},
    {"count_nulls",
     "com/ak4ne/arthooktest/Targets",
     "countNullsInArray",
     "([Ljava/lang/Object;)I",
     reinterpret_cast<void*>(&Hook_count_nulls)},

    {"thread_keys_a",
     "com/ak4ne/arthooktest/Targets",
     "argBoolean",
     "(Z)Z",
     reinterpret_cast<void*>(&Hook_thread_keys_a)},
    {"thread_keys_b",
     "com/ak4ne/arthooktest/Targets",
     "argByte",
     "(B)B",
     reinterpret_cast<void*>(&Hook_thread_keys_b)},
    {"thread_keys_c",
     "com/ak4ne/arthooktest/Targets",
     "argChar",
     "(C)C",
     reinterpret_cast<void*>(&Hook_thread_keys_c)},
    {"thread_keys_d",
     "com/ak4ne/arthooktest/Targets",
     "argShort",
     "(S)S",
     reinterpret_cast<void*>(&Hook_thread_keys_d)},
    {"thread_keys_e",
     "com/ak4ne/arthooktest/Targets",
     "argInt",
     "(I)I",
     reinterpret_cast<void*>(&Hook_thread_keys_e)},
    {"thread_keys_f",
     "com/ak4ne/arthooktest/Targets",
     "argLong",
     "(J)J",
     reinterpret_cast<void*>(&Hook_thread_keys_f)},
    {"thread_keys_g",
     "com/ak4ne/arthooktest/Targets",
     "argFloat",
     "(F)F",
     reinterpret_cast<void*>(&Hook_thread_keys_g)},
    {"thread_keys_h",
     "com/ak4ne/arthooktest/Targets",
     "argDouble",
     "(D)D",
     reinterpret_cast<void*>(&Hook_thread_keys_h)},

    // OkHttp SSL pinning bypass — call-original-then-swallow pattern.
    {"okhttp_certificate_pinner_bypass",
     "okhttp3/CertificatePinner",
     "check$okhttp",
     "(Ljava/lang/String;Lkotlin/jvm/functions/Function0;)V",
     reinterpret_cast<void*>(&Hook_okhttp_pinner_bypass)},

    // Failure-mode keys — these intentionally don't resolve.
    {"nonexistent_class",
     "com/ak4ne/arthooktest/NoSuchClass",
     "foo",
     "()V",
     reinterpret_cast<void*>(&Hook_static_void)},
    {"nonexistent_method",
     "com/ak4ne/arthooktest/Targets",
     "noSuchMethodPlease",
     "()V",
     reinterpret_cast<void*>(&Hook_static_void)},
    {"wrong_signature",
     "com/ak4ne/arthooktest/Targets",
     "argInt",
     "(Ljava/lang/String;)I",
     reinterpret_cast<void*>(&Hook_static_void)},
    {"null_replacement", "com/ak4ne/arthooktest/Targets", "argInt", "(I)I", nullptr},
};

const Spec* FindSpec(const std::string& key) {
    for (const auto& s : kSpecs)
        if (key == s.key) return &s;
    return nullptr;
}

bool TargetIsNative(const Spec& s) {
    return std::strcmp(s.key, "native_registered") == 0;
}

}  // anonymous namespace

// =================== JNI bridge ==========================================

extern "C" {

JNIEXPORT jint JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_installHook(JNIEnv* env,
                                                                                   jclass,
                                                                                   jstring jkey) {
    const char* c_key = env->GetStringUTFChars(jkey, nullptr);
    if (!c_key) return 99;
    std::string key(c_key);
    env->ReleaseStringUTFChars(jkey, c_key);

    const Spec* sp = FindSpec(key);
    if (!sp) {
        LOGE("installHook: unknown key %s", key.c_str());
        return 100;
    }

    jclass clazz = env->FindClass(sp->clazz);
    if (!clazz) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        LOGE("installHook: FindClass(%s) failed", sp->clazz);
        return static_cast<jint>(arthook::Status::kMethodNotFound);
    }

    auto& st = State(key.c_str());
    st.reset();
    st.backup = nullptr;
    st.backup_jm = nullptr;

    arthook::Status s = arthook::Hook(env, clazz, sp->name, sp->sig, sp->replacement, &st.backup);
    if (s == arthook::Status::kOk) {
        st.installed = true;
        ++g_trampoline_pages_in_use;
        if (!TargetIsNative(*sp)) {
            st.backup_jm = reinterpret_cast<jmethodID>(st.backup);
        }
        // Promote the class ref to a global so it survives until unhook;
        // we need it for CallNonvirtual*Method on the backup jmethodID.
        if (st.declaring_class) env->DeleteGlobalRef(st.declaring_class);
        st.declaring_class = (jclass)env->NewGlobalRef(clazz);
    } else {
        LOGE("installHook(%s): rc=%d", key.c_str(), static_cast<int>(s));
    }
    env->DeleteLocalRef(clazz);
    return static_cast<jint>(s);
}

JNIEXPORT jint JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_installHookOnReflected(
    JNIEnv* env, jclass, jstring jkey, jobject method) {
    const char* c_key = env->GetStringUTFChars(jkey, nullptr);
    if (!c_key) return 99;
    std::string key(c_key);
    env->ReleaseStringUTFChars(jkey, c_key);

    const Spec* sp = FindSpec(key);
    if (!sp) return 100;

    auto& st = State(key.c_str());
    st.reset();
    st.backup = nullptr;
    st.backup_jm = nullptr;

    arthook::Status s = arthook::HookReflected(env, method, sp->replacement, &st.backup);
    if (s == arthook::Status::kOk) {
        st.installed = true;
        ++g_trampoline_pages_in_use;
        if (!TargetIsNative(*sp)) st.backup_jm = reinterpret_cast<jmethodID>(st.backup);
    }
    return static_cast<jint>(s);
}

JNIEXPORT jint JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_uninstallHook(JNIEnv* env,
                                                                                     jclass,
                                                                                     jstring jkey) {
    const char* c_key = env->GetStringUTFChars(jkey, nullptr);
    if (!c_key) return 99;
    std::string key(c_key);
    env->ReleaseStringUTFChars(jkey, c_key);

    const Spec* sp = FindSpec(key);
    if (!sp) return 100;
    jclass clazz = env->FindClass(sp->clazz);
    if (!clazz) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return static_cast<jint>(arthook::Status::kMethodNotFound);
    }
    arthook::Status s = arthook::Unhook(env, clazz, sp->name, sp->sig);
    env->DeleteLocalRef(clazz);
    auto& st = State(key.c_str());
    if (s == arthook::Status::kOk && st.installed) {
        st.installed = false;
        --g_trampoline_pages_in_use;
        if (st.declaring_class) {
            env->DeleteGlobalRef(st.declaring_class);
            st.declaring_class = nullptr;
        }
    }
    return static_cast<jint>(s);
}

JNIEXPORT void JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_resetState(JNIEnv* env,
                                                                                  jclass,
                                                                                  jstring jkey) {
    const char* c_key = env->GetStringUTFChars(jkey, nullptr);
    if (!c_key) return;
    State(c_key).reset();
    env->ReleaseStringUTFChars(jkey, c_key);
}

JNIEXPORT jint JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_fireCount(JNIEnv* env,
                                                                                 jclass,
                                                                                 jstring jkey) {
    const char* c_key = env->GetStringUTFChars(jkey, nullptr);
    int v = c_key ? State(c_key).fire_count.load() : 0;
    if (c_key) env->ReleaseStringUTFChars(jkey, c_key);
    return v;
}

JNIEXPORT jlong JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_lastLongArg(JNIEnv* env,
                                                                                    jclass,
                                                                                    jstring jkey,
                                                                                    jint idx) {
    const char* c_key = env->GetStringUTFChars(jkey, nullptr);
    jlong v = 0;
    if (c_key && idx >= 0 && idx < 8) v = State(c_key).last_long[idx].load();
    if (c_key) env->ReleaseStringUTFChars(jkey, c_key);
    return v;
}

JNIEXPORT jdouble JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_lastDoubleArg(
    JNIEnv* env, jclass, jstring jkey, jint idx) {
    const char* c_key = env->GetStringUTFChars(jkey, nullptr);
    jdouble v = 0;
    if (c_key && idx >= 0 && idx < 8) v = State(c_key).last_double[idx].load();
    if (c_key) env->ReleaseStringUTFChars(jkey, c_key);
    return v;
}

JNIEXPORT jstring JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_lastStringArg(
    JNIEnv* env, jclass, jstring jkey, jint idx) {
    const char* c_key = env->GetStringUTFChars(jkey, nullptr);
    if (!c_key) return env->NewStringUTF("");
    std::string copy;
    if (idx >= 0 && idx < 4) {
        auto& s = State(c_key);
        std::lock_guard<std::mutex> lk(s.str_mu);
        copy = s.last_strings[idx];
    }
    env->ReleaseStringUTFChars(jkey, c_key);
    return env->NewStringUTF(copy.c_str());
}

JNIEXPORT jint JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_lastObjectArrayLen(
    JNIEnv* env, jclass, jstring jkey) {
    const char* c_key = env->GetStringUTFChars(jkey, nullptr);
    int v = c_key ? State(c_key).last_array_len.load() : 0;
    if (c_key) env->ReleaseStringUTFChars(jkey, c_key);
    return v;
}

JNIEXPORT jboolean JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_lastObjectArrayHadNull(
    JNIEnv* env, jclass, jstring jkey) {
    const char* c_key = env->GetStringUTFChars(jkey, nullptr);
    bool v = c_key ? State(c_key).last_array_had_null.load() : false;
    if (c_key) env->ReleaseStringUTFChars(jkey, c_key);
    return v ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_lastStringLength(
    JNIEnv* env, jclass, jstring jkey) {
    const char* c_key = env->GetStringUTFChars(jkey, nullptr);
    int v = c_key ? State(c_key).last_string_length.load() : 0;
    if (c_key) env->ReleaseStringUTFChars(jkey, c_key);
    return v;
}

JNIEXPORT jstring JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_invokeBackupStringInst(
    JNIEnv* env, jclass, jstring jkey, jobject thiz, jstring arg) {
    const char* c_key = env->GetStringUTFChars(jkey, nullptr);
    if (!c_key) return nullptr;
    auto& s = State(c_key);
    env->ReleaseStringUTFChars(jkey, c_key);
    if (!s.backup_jm || !s.declaring_class) return env->NewStringUTF("NO_BACKUP");
    // Non-virtual dispatch — required, because the backup ArtMethod shares
    // method_index_ with the target. Virtual dispatch through `thiz`'s
    // vtable would resolve to the target (hooked) ArtMethod instead.
    jstring r = (jstring)env->CallNonvirtualObjectMethod(thiz, s.declaring_class, s.backup_jm, arg);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return env->NewStringUTF("EXC");
    }
    return r;
}

JNIEXPORT jint JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_invokeBackupIntInst(
    JNIEnv* env, jclass, jstring jkey, jobject thiz, jint arg) {
    const char* c_key = env->GetStringUTFChars(jkey, nullptr);
    if (!c_key) return -1;
    auto& s = State(c_key);
    env->ReleaseStringUTFChars(jkey, c_key);
    if (!s.backup_jm || !s.declaring_class) return -1;
    jint r = env->CallNonvirtualIntMethod(thiz, s.declaring_class, s.backup_jm, arg);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return -1;
    }
    return r;
}

JNIEXPORT jint JNICALL
Java_com_ak4ne_arthooktest_testkit_NativeBridge_trampolinePagesInUse(JNIEnv*, jclass) {
    return g_trampoline_pages_in_use.load();
}

JNIEXPORT jboolean JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_hasJniBridge(JNIEnv*,
                                                                                        jclass) {
    return arthook::HasJniBridge() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_ak4ne_arthooktest_testkit_NativeBridge_targetIsNative(JNIEnv* env, jclass, jstring jkey) {
    const char* c_key = env->GetStringUTFChars(jkey, nullptr);
    if (!c_key) return JNI_FALSE;
    std::string key(c_key);
    env->ReleaseStringUTFChars(jkey, c_key);
    const Spec* sp = FindSpec(key);
    return (sp && TargetIsNative(*sp)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_com_ak4ne_arthooktest_testkit_NativeBridge_initializeAgain(JNIEnv* env,
                                                                                       jclass) {
    return static_cast<jint>(arthook::Initialize(env));
}

}  // extern "C"
