// SPDX-License-Identifier: Apache-2.0
//
// ArtMethod access-flag bits. Low 16 = JVM/dex flags. Upper bits are
// ART-private and have shifted across Android versions; we treat the
// fast/critical/pre-compiled cluster as a unit rather than depending on
// any individual bit position.

#ifndef ARTHOOK_ART_ACCESSFLAGS_H_
#define ARTHOOK_ART_ACCESSFLAGS_H_

#include <cstdint>

namespace arthook {

// Standard dex access flags.
constexpr uint32_t kAccPublic = 0x0001;
constexpr uint32_t kAccPrivate = 0x0002;
constexpr uint32_t kAccProtected = 0x0004;
constexpr uint32_t kAccStatic = 0x0008;
constexpr uint32_t kAccFinal = 0x0010;
constexpr uint32_t kAccSynchronized = 0x0020;
constexpr uint32_t kAccBridge = 0x0040;
constexpr uint32_t kAccVarargs = 0x0080;
constexpr uint32_t kAccNative = 0x0100;
constexpr uint32_t kAccAbstract = 0x0400;
constexpr uint32_t kAccStrict = 0x0800;
constexpr uint32_t kAccSynthetic = 0x1000;

// ART-private flags.
constexpr uint32_t kAccCompileDontBother = 0x02000000;
constexpr uint32_t kAccFastNative = 0x00080000;
constexpr uint32_t kAccCriticalNative = 0x00200000;
constexpr uint32_t kAccPreCompiled = 0x00800000;
constexpr uint32_t kAccDefault = 0x00400000;
constexpr uint32_t kAccDeclaredSynchronized = 0x00020000;
constexpr uint32_t kAccConstructor = 0x00010000;
constexpr uint32_t kAccInterface = 0x0200;  // same bit as for classes

// Bits at 0x00100000 / 0x00040000 / 0x00800000 carry overloaded ART-private
// meanings ("intrinsified", "obsolete", "previously-warm") depending on
// Android version. Left set, they steer ART's JNI bridge into specialized
// code paths that mishandle object-typed args (bridge passes IRT slot
// pointers in the wrong registers — `env` ends up looking like an IRT slot
// address). Cleared, the method dispatches through the plain generic JNI
// bridge for any signature.
constexpr uint32_t kAccIntrinsified = 0x00100000;
constexpr uint32_t kAccObsoleteMethod = 0x00040000;
constexpr uint32_t kAccSingleImpl = 0x08000000;

// Cleared on hook install. We force-clear:
//   - access modifiers other than what we re-add (kAccPrivate / kAccNative)
//   - kAccAbstract / kAccInterface / kAccDefault — non-concrete method
//     attributes
//   - kAccSynchronized / kAccDeclaredSynchronized — bridge would set up
//     monitor enter/exit we don't want
//   - kAccFastNative / kAccCriticalNative / kAccPreCompiled — fast-path
//     dispatch variants that bypass entry_point_from_jni_
//   - kAccIntrinsified / kAccObsoleteMethod / kAccSingleImpl — runtime
//     hints that make the bridge dispatch object args wrong on Android 13+
constexpr uint32_t kAccHookClearMask = kAccPublic | kAccProtected | kAccAbstract | kAccInterface |
                                       kAccDefault | kAccSynchronized | kAccDeclaredSynchronized |
                                       kAccFastNative | kAccCriticalNative | kAccPreCompiled |
                                       kAccIntrinsified | kAccObsoleteMethod | kAccSingleImpl;

}  // namespace arthook

#endif  // ARTHOOK_ART_ACCESSFLAGS_H_
