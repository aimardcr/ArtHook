// SPDX-License-Identifier: Apache-2.0
//
// jmethodID encoding on modern ART
// --------------------------------
// On Android 11+ the default JNI-IDs mode is kSwapablePointer, so
// jmethodIDs returned by GetMethodID can be indices encoded as
// `(index << 1) | 1` instead of direct ArtMethod* pointers. We can't reach
// art::jni::JniIdManager::DecodeMethodId from outside libart (release
// builds are stripped and the symbol isn't exported), so we use a
// reflection roundtrip instead: env->ToReflectedMethod() lets ART decode
// the index internally, and Executable.artMethod is a private long field
// holding the raw ArtMethod* pointer.

#include "art/ArtMethod.h"

#include <cstring>
#include <mutex>

#include "art/Layout.h"
#include "util/Log.h"
#include "util/Memory.h"

namespace arthook {

namespace {

std::once_flag g_exec_once;
jfieldID g_exec_art_method_fid = nullptr;

void EnsureExecutableField(JNIEnv* env) {
    std::call_once(g_exec_once, [env] {
        jclass exec = env->FindClass("java/lang/reflect/Executable");
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (exec) {
            g_exec_art_method_fid = env->GetFieldID(exec, "artMethod", "J");
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(exec);
        }
        // Pre-Android-6 fallback: the field lived directly on Method.
        if (!g_exec_art_method_fid) {
            jclass m = env->FindClass("java/lang/reflect/Method");
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (m) {
                g_exec_art_method_fid = env->GetFieldID(m, "artMethod", "J");
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(m);
            }
        }
        if (!g_exec_art_method_fid) {
            LOGE("Executable.artMethod not found — cannot resolve ArtMethod*");
        } else {
            LOGI("Executable.artMethod field bound: fid=%p", g_exec_art_method_fid);
        }
    });
}

ArtMethodPtr ReadArtMethodField(JNIEnv* env, jobject reflected) {
    EnsureExecutableField(env);
    if (!g_exec_art_method_fid) return nullptr;
    jlong v = env->GetLongField(reflected, g_exec_art_method_fid);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return reinterpret_cast<ArtMethodPtr>(static_cast<uintptr_t>(v));
}

}  // namespace

ArtMethodPtr ArtMethodFromMethodId(jmethodID id) {
    return reinterpret_cast<ArtMethodPtr>(id);
}

jmethodID ArtMethodToMethodId(ArtMethodPtr m) {
    return reinterpret_cast<jmethodID>(m);
}

ArtMethodPtr ArtMethodFromJniBinding(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    if (!env || !clazz || !name || !sig) return nullptr;

    jmethodID id = env->GetMethodID(clazz, name, sig);
    bool is_static = false;
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (!id) {
        id = env->GetStaticMethodID(clazz, name, sig);
        if (env->ExceptionCheck()) env->ExceptionClear();
        is_static = (id != nullptr);
    }
    if (!id) return nullptr;

    // Fast path: real heap pointer. Indexed IDs always have bit 0 set; heap
    // pointers on 64-bit Android are well above 0x10000.
    uintptr_t v = reinterpret_cast<uintptr_t>(id);
    if ((v & 1u) == 0 && v > 0x10000) return reinterpret_cast<ArtMethodPtr>(id);

    // Slow path: reflect to let ART decode, then read the field.
    jobject reflected = env->ToReflectedMethod(clazz, id, is_static ? JNI_TRUE : JNI_FALSE);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (!reflected) {
        LOGE("ToReflectedMethod failed for %s%s", name, sig);
        return nullptr;
    }
    ArtMethodPtr out = ReadArtMethodField(env, reflected);
    env->DeleteLocalRef(reflected);
    return out;
}

ArtMethodPtr ArtMethodFromReflected(JNIEnv* env, jobject reflected) {
    if (!env || !reflected) return nullptr;
    return ReadArtMethodField(env, reflected);
}

uint32_t GetAccessFlags(ArtMethodPtr m) {
    return ReadU32(m, Layout().offset_access_flags);
}

void SetAccessFlags(ArtMethodPtr m, uint32_t flags) {
    WriteU32(m, Layout().offset_access_flags, flags);
}

void* GetEntryPointFromJni(ArtMethodPtr m) {
    return reinterpret_cast<void*>(ReadUintPtr(m, Layout().offset_entry_point_jni));
}

void SetEntryPointFromJni(ArtMethodPtr m, void* p) {
    WriteUintPtr(m, Layout().offset_entry_point_jni, reinterpret_cast<uintptr_t>(p));
}

void* GetEntryPointFromQuickCompiledCode(ArtMethodPtr m) {
    return reinterpret_cast<void*>(ReadUintPtr(m, Layout().offset_entry_point_quick_code));
}

void SetEntryPointFromQuickCompiledCode(ArtMethodPtr m, void* p) {
    WriteUintPtr(m, Layout().offset_entry_point_quick_code, reinterpret_cast<uintptr_t>(p));
}

void CopyArtMethod(ArtMethodPtr dst, ArtMethodPtr src) {
    std::memcpy(dst, src, Layout().art_method_size);
}

}  // namespace arthook
