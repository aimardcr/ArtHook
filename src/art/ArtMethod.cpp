// SPDX-License-Identifier: Apache-2.0
//
// jmethodID encoding: on Android 11+ a jmethodID may be an index
// `(index << 1) | 1` rather than an ArtMethod*. ART's DecodeMethodId isn't
// reachable (stripped), so we round-trip via ToReflectedMethod and read the
// raw pointer from the private Executable.artMethod long field.

#include "art/ArtMethod.h"

#include <atomic>
#include <cstring>
#include <mutex>

#include "art/Layout.h"
#include "util/Log.h"
#include "util/Memory.h"

namespace arthook {

namespace {

// Cached after first success. Not call_once: a transient first-call failure
// must not poison resolution forever, so we retry under the lock.
std::mutex g_exec_mu;
std::atomic<jfieldID> g_exec_art_method_fid{nullptr};

void EnsureExecutableField(JNIEnv* env) {
    if (g_exec_art_method_fid.load()) return;
    std::lock_guard<std::mutex> lk(g_exec_mu);
    if (g_exec_art_method_fid.load()) return;

    jfieldID fid = nullptr;
    jclass exec = env->FindClass("java/lang/reflect/Executable");
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (exec) {
        fid = env->GetFieldID(exec, "artMethod", "J");
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(exec);
    }
    // Pre-Android-6 fallback: the field lived directly on Method.
    if (!fid) {
        jclass m = env->FindClass("java/lang/reflect/Method");
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (m) {
            fid = env->GetFieldID(m, "artMethod", "J");
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(m);
        }
    }
    if (!fid) {
        LOGE("Executable.artMethod not found, will retry on next call");
    } else {
        g_exec_art_method_fid.store(fid);
        LOGI("Executable.artMethod field bound: fid=%p", fid);
    }
}

ArtMethodPtr ReadArtMethodField(JNIEnv* env, jobject reflected) {
    EnsureExecutableField(env);
    jfieldID fid = g_exec_art_method_fid.load();
    if (!fid) return nullptr;
    jlong v = env->GetLongField(reflected, fid);
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

    // Fast path: real pointer (indexed IDs have bit 0 set; pointers > 0x10000).
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

namespace detail {
void RefreshBackupClass(void* backup, void* target) {
    if (!backup || !target) return;
    // declaring_class_ is a 4-byte GcRoot at offset 0; copy it from the live
    // (GC-tracked) target so a moving GC can't leave the backup's copy stale.
    std::memcpy(backup, target, sizeof(uint32_t));
}
}  // namespace detail

}  // namespace arthook
