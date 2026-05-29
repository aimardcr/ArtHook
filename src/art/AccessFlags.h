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

// Overloaded ART-private bits (meanings/positions vary by version). Left set,
// they route the JNI bridge through specialized paths that misplace object
// args; cleared, dispatch uses the plain generic bridge.
constexpr uint32_t kAccIntrinsified = 0x00100000;
constexpr uint32_t kAccObsoleteMethod = 0x00040000;
constexpr uint32_t kAccSingleImpl = 0x08000000;

// Cleared on hook install: stale modifiers, non-concrete attributes,
// synchronized (avoid bridge monitor setup), and the fast/critical/
// pre-compiled/intrinsified dispatch shortcuts that bypass the generic JNI
// bridge or misroute object args on Android 13+.
constexpr uint32_t kAccHookClearMask = kAccPublic | kAccProtected | kAccAbstract | kAccInterface |
                                       kAccDefault | kAccSynchronized | kAccDeclaredSynchronized |
                                       kAccFastNative | kAccCriticalNative | kAccPreCompiled |
                                       kAccIntrinsified | kAccObsoleteMethod | kAccSingleImpl;

}  // namespace arthook

#endif  // ARTHOOK_ART_ACCESSFLAGS_H_
