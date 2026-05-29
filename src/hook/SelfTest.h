// SPDX-License-Identifier: Apache-2.0
//
// Internal device preflight, run by Initialize(env, verify=true).

#ifndef ARTHOOK_HOOK_SELFTEST_H_
#define ARTHOOK_HOOK_SELFTEST_H_

#include <jni.h>

namespace arthook {

// Install/invoke/unhook an internal sentinel to confirm hooking works on this
// device. Returns true on success. Not part of the public API.
bool RunSelfTest(JNIEnv* env);

}  // namespace arthook

#endif  // ARTHOOK_HOOK_SELFTEST_H_
