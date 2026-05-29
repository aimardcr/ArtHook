// SPDX-License-Identifier: Apache-2.0
//
// Offset-based accessors for ART's ArtMethod. The fields we touch
// (access_flags_, entry_point_from_jni_, entry_point_from_quick_compiled_code_)
// have always been present, just at version-dependent offsets, supplied
// at runtime by the Layout singleton.

#ifndef ARTHOOK_ART_ARTMETHOD_H_
#define ARTHOOK_ART_ARTMETHOD_H_

#include <jni.h>

#include <cstddef>
#include <cstdint>

namespace arthook {

using ArtMethodPtr = void*;

// Plain cast, safe only for jmethodIDs we minted ourselves. For IDs from
// env->GetMethodID() on Android 11+, use the resolvers below: they handle
// the kSwapablePointer-mode (index << 1) | 1 encoding.
ArtMethodPtr ArtMethodFromMethodId(jmethodID id);
jmethodID ArtMethodToMethodId(ArtMethodPtr m);

// Resolve via env->ToReflectedMethod + Executable.artMethod. Tries instance
// then static.
ArtMethodPtr ArtMethodFromJniBinding(JNIEnv* env, jclass clazz, const char* name, const char* sig);

ArtMethodPtr ArtMethodFromReflected(JNIEnv* env, jobject reflected_method);

uint32_t GetAccessFlags(ArtMethodPtr m);
void SetAccessFlags(ArtMethodPtr m, uint32_t flags);

void* GetEntryPointFromJni(ArtMethodPtr m);
void SetEntryPointFromJni(ArtMethodPtr m, void* p);

void* GetEntryPointFromQuickCompiledCode(ArtMethodPtr m);
void SetEntryPointFromQuickCompiledCode(ArtMethodPtr m, void* p);

void CopyArtMethod(ArtMethodPtr dst, ArtMethodPtr src);

}  // namespace arthook

#endif  // ARTHOOK_ART_ARTMETHOD_H_
