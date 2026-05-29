// SPDX-License-Identifier: Apache-2.0
//
// Opt-in deoptimization: force a non-native method to run in the interpreter
// so already-compiled or inlined callers re-dispatch through it. Mirrors
// LSPlant's lightweight approach (ClassLinker::SetEntryPointsToInterpreter)
// without hooking ART internals, so it is best-effort and not sticky.

#include "arthook/ArtHook.h"

#include <cstdint>
#include <mutex>

#include "art/AccessFlags.h"
#include "art/ArtMethod.h"
#include "elf/ElfResolver.h"
#include "util/Log.h"

namespace arthook {

namespace {

// `void art::ClassLinker::SetEntryPointsToInterpreter(art::ArtMethod*) const`.
// Resolved by mangled name; it never dereferences `this`, so we pass nullptr.
using SetInterpFn = void (*)(const void*, void*);

std::once_flag g_once;
SetInterpFn g_set_interp = nullptr;
void* g_to_interp_bridge = nullptr;

void EnsureSyms() {
    std::call_once(g_once, [] {
        g_set_interp = reinterpret_cast<SetInterpFn>(ResolveLibartSymbol(
            "_ZNK3art11ClassLinker27SetEntryPointsToInterpreterEPNS_9ArtMethodE"));
        g_to_interp_bridge = ResolveLibartSymbol("art_quick_to_interpreter_bridge");
        LOGI("deopt syms: SetEntryPointsToInterpreter=%p to_interpreter_bridge=%p",
             reinterpret_cast<void*>(g_set_interp),
             g_to_interp_bridge);
    });
}

Status DeoptimizeTarget(ArtMethodPtr target) {
    if (!target) return Status::kMethodNotFound;
    // Native methods have no interpreter code; deopt is a no-op/invalid there.
    if (GetAccessFlags(target) & kAccNative) return Status::kInvalidArgument;

    EnsureSyms();
    if (g_set_interp) {
        g_set_interp(nullptr, target);
        return Status::kOk;
    }
    // A13+ fallback: the symbol is gone but the raw bridge may resolve.
    if (g_to_interp_bridge) {
        SetEntryPointFromQuickCompiledCode(target, g_to_interp_bridge);
        return Status::kOk;
    }
    return Status::kDeoptUnavailable;
}

}  // namespace

Status Deoptimize(JNIEnv* env, jclass clazz, const char* name, const char* signature) {
    if (!IsInitialized()) return Status::kNotInitialized;
    if (!env || !clazz || !name || !signature) return Status::kInvalidArgument;
    if (IsHooked(env, clazz, name, signature)) {
        LOGW("Deoptimize: target is hooked; deoptimize callers, not the hooked method");
        return Status::kAlreadyHooked;
    }
    return DeoptimizeTarget(ArtMethodFromJniBinding(env, clazz, name, signature));
}

Status DeoptimizeReflected(JNIEnv* env, jobject reflected_method) {
    if (!IsInitialized()) return Status::kNotInitialized;
    if (!env || !reflected_method) return Status::kInvalidArgument;
    if (IsHookedReflected(env, reflected_method)) {
        LOGW("Deoptimize: target is hooked; deoptimize callers, not the hooked method");
        return Status::kAlreadyHooked;
    }
    return DeoptimizeTarget(ArtMethodFromReflected(env, reflected_method));
}

}  // namespace arthook
