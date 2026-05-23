// SPDX-License-Identifier: Apache-2.0
//
// Runtime-built probe dex. Loaded via InMemoryDexClassLoader so the probe
// class can't live in boot.oat — its single native method's quick entry is
// therefore the generic JNI bridge (or, on Android 11+, the resolution
// trampoline at a known offset from it). Used by Layout.cpp as the
// third-tier fallback when neither the libart .dynsym nor any Object
// native exposes the bridge directly.

#ifndef ARTHOOK_PROBE_PROBE_H_
#define ARTHOOK_PROBE_PROBE_H_

#include <jni.h>

namespace arthook {

class Probe {
public:
    // Inject the probe dex, load its class, and return the probe method's
    // ArtMethod* (resolved via reflection through Executable.artMethod).
    // Returns nullptr on any failure.
    static void* SampleProbeArtMethod(JNIEnv* env);
};

}  // namespace arthook

#endif  // ARTHOOK_PROBE_PROBE_H_
