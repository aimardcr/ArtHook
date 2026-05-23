// SPDX-License-Identifier: Apache-2.0
//
// Runtime-discovered ArtMethod layout. Filled by DiscoverLayout() at init.

#ifndef ARTHOOK_ART_LAYOUT_H_
#define ARTHOOK_ART_LAYOUT_H_

#include <jni.h>

#include <cstddef>

namespace arthook {

struct ArtMethodLayout {
    size_t art_method_size = 0;
    size_t offset_access_flags = static_cast<size_t>(-1);
    size_t offset_entry_point_jni = static_cast<size_t>(-1);
    size_t offset_entry_point_quick_code = static_cast<size_t>(-1);

    // Sampled from Object.notify()'s quick entry — the standard JNI bridge
    // (art_quick_generic_jni_trampoline on AOSP). Installed as the new
    // quick entry when hooking a non-native method, so ART's existing
    // bridge code does the quick→JNI ABI fix-up for our replacement.
    void* jni_bridge_quick_entry = nullptr;

    bool valid = false;
};

const ArtMethodLayout& Layout();

// Idempotent on success.
bool DiscoverLayout(JNIEnv* env);

// Records `m`'s quick entry as the JNI bridge, after verifying it lives
// inside libart.so. Used by arthook::SetBridgeProbe().
bool SetJniBridgeFromMethod(void* m);

}  // namespace arthook

#endif  // ARTHOOK_ART_LAYOUT_H_
