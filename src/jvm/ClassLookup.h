// SPDX-License-Identifier: Apache-2.0
//
// Resolve a class by name without relying on JNI FindClass alone.
// FindClass uses the caller's classloader, which is the bootstrap
// loader for threads attached via the Invocation API or for code
// running before Application init, so app-loaded classes are
// invisible there. We instead route through
// ActivityThread.currentApplication().getClassLoader().loadClass(),
// which delegates to the bootstrap loader for system classes and so
// is a superset of FindClass when an Application exists.

#ifndef ARTHOOK_JVM_CLASSLOOKUP_H_
#define ARTHOOK_JVM_CLASSLOOKUP_H_

#include <jni.h>

namespace arthook {

// Resolve a class by name. `name` may be in JNI form ("java/lang/Object")
// or dotted form ("java.lang.Object"); both are accepted. Returns a
// local ref on success, nullptr on failure with any pending exception
// cleared. Falls back to env->FindClass if no Application is available
// (e.g. early init, zygisk pre-specialize).
jclass FindClassByName(JNIEnv* env, const char* name);

}  // namespace arthook

#endif  // ARTHOOK_JVM_CLASSLOOKUP_H_
