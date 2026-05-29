# arthook

A self-contained C++17 static library for hooking Java methods at the ART
(Android Runtime) level. Manipulates `ArtMethod` structures directly via
runtime-derived offsets, **no Frida, no Xposed, no LSPlant, no YAHFA, no
Pine, no SandHook**. Standard system libraries only (`libc`, `libdl`,
`liblog`, and ART symbols resolved at runtime).

Compatible with **Android 8.0 (API 26) through the latest Android version**
without per-version hardcoded offset tables.

## What it does

- Replace any Java method's implementation with a C function of matching
  JNI signature.
- Hand back a callable "backup" `jmethodID` that invokes the original
  unmodified method.
- Install and remove hooks at runtime, thread-safely.
- Survive ART layout changes across Android releases by **discovering**
  `ArtMethod` field offsets at initialization time rather than hardcoding
  them.

## What it does NOT do

- Inject the `.so` into another process. You're expected to have already
  done that (zygisk, ptrace, or whatever else); arthook just provides the
  hooking primitive once you're inside.
- Restore methods that have been **inlined** into hot AOT-compiled callers.
  Those callers are not deoptimized, they keep running the inlined copy.
- Provide a scripting layer, argument-logger, or libffi-based generic
  dispatch. Your replacement function must match the original's ABI.

## Build

Requires NDK r25+ and CMake 3.18+.

```sh
cmake -B build/arm64 \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-26 \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build/arm64
```

This produces `build/arm64/libarthook.a`.

Supported ABIs: `arm64-v8a`, `armeabi-v7a`, `x86_64`, `x86`.

## Consuming from another NDK project

```cmake
add_subdirectory(third_party/arthook)

add_library(my_payload SHARED my_payload.cpp)
target_link_libraries(my_payload PRIVATE arthook::arthook)
```

```cpp
// my_payload.cpp, hooks Object.toString() (a non-native Java method).
#include <arthook/Hooked.h>
#include <android/log.h>

static arthook::Hooked g_toString;

extern "C" JNIEXPORT jstring JNICALL
HookedToString(JNIEnv* env, jobject self) {
    __android_log_print(ANDROID_LOG_INFO, "demo", "toString called");
    return g_toString.invoke<jstring>(env, self);
}

extern "C" JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (arthook::Initialize(env) != arthook::Status::kOk) return JNI_ERR;

    jclass obj = env->FindClass("java/lang/Object");
    g_toString.install(env, obj, "toString", "()Ljava/lang/String;",
                       reinterpret_cast<void*>(&HookedToString));
    env->DeleteLocalRef(obj);
    return JNI_VERSION_1_6;
}
```

`Hooked::invoke<Ret>(env, thiz, args...)` works the same for native and
non-native targets, instance and static. For static methods pass `nullptr`
as `thiz`, it's ignored. Call `g_toString.release(env)` before the handle
goes out of scope (e.g. from `JNI_OnUnload`); the destructor cannot do it
because no `JNIEnv` is available there.

The lower-level `arthook::Hook` / `Unhook` API in `ArtHook.h` remains
available if you'd rather manage the backup `jmethodID` and declaring-class
`GlobalRef` yourself.

### Injected payloads (no `JNI_OnLoad`)

If your `.so` is loaded by an injector (zygisk, ptrace, another `.so`
calling `dlopen`) instead of `System.loadLibrary`, there's no `JNI_OnLoad`
callback to hand you a `JavaVM*`. Use `arthook::AttachToJavaVM`, it
locates the running `JavaVM` via `JNI_GetCreatedJavaVMs`, attaches the
calling thread if needed, and detaches automatically on scope exit:

```cpp
#include <arthook/ArtHook.h>
#include <arthook/Hooked.h>

static arthook::Hooked g_toString;

// Called by your injector at some entry point of its choosing.
extern "C" void arthook_payload_start() {
    arthook::AttachToJavaVM([](JNIEnv* env) {
        if (arthook::Initialize(env) != arthook::Status::kOk) return;

        jclass obj = env->FindClass("java/lang/Object");
        g_toString.install(env, obj, "toString", "()Ljava/lang/String;",
                           reinterpret_cast<void*>(&HookedToString));
        env->DeleteLocalRef(obj);
    });
}
```

The hooks installed inside the lambda persist after detach, the
installation is recorded in ART's method tables, not in the env.

## Public API

The full surface is in [`include/arthook/ArtHook.h`](include/arthook/ArtHook.h):

```cpp
namespace arthook {

enum class Status { kOk, kNotInitialized, kLayoutDiscoveryFailed,
                    kMethodNotFound, kTrampolineAllocFailed,
                    kAlreadyHooked, kNotHooked, kInvalidArgument,
                    kOutOfMemory, kNoJniBridge, kDeoptUnavailable,
                    kInternalError };

Status Initialize(JNIEnv* env, bool verify = false);  // verify self-tests hooking
Status Hook(JNIEnv* env, jclass clazz, const char* name, const char* sig,
            void* replacement, void** backup_out);
Status HookReflected(JNIEnv* env, jobject reflected_method,
                     void* replacement, void** backup_out);
Status Unhook(JNIEnv* env, jclass clazz, const char* name, const char* sig);

// Force a non-native method to the interpreter (deopt a CALLER to defeat
// inlining of a hooked method). Opt-in, best-effort, not sticky.
Status Deoptimize(JNIEnv* env, jclass clazz, const char* name, const char* sig);

bool        IsInitialized();
const char* StatusToString(Status s);

// Also: class-name overloads of Hook/Unhook, the AttachToJavaVM(body) helper,
// and the Hooked RAII wrapper (see the header).
}
```

The replacement function follows the standard JNI calling convention: first
arg `JNIEnv*`, second arg `jobject` (instance) or `jclass` (static), then
the Java parameters mapped to JNI types.

## How layout discovery works

We never hardcode `ArtMethod` offsets per Android version. Instead, at
`Initialize()` time we derive four numbers from the live runtime:

| What                                        | How                                                                                                                                                                                  |
| ------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `sizeof(ArtMethod)`                         | Collect 6 `jmethodID`s from `java.lang.Object`, sort, take pairwise diffs, methods of the same class are stored contiguously, so the smallest sane diff is exactly `sizeof(ArtMethod)`. |
| `access_flags_` offset                      | The low 16 bits hold standard JVM dex flags. For 4 `public` instance methods of `Object`, those bits can only contain `{kAccPublic, kAccFinal, kAccNative}`. Scan for the unique 32-bit word where every probe satisfies that invariant. |
| `entry_point_from_jni_` & `_quick_compiled_code_` offsets | Structural: the trailing `PtrSizedFields` is `(data_, entry_point_from_quick_compiled_code_)` on every Android with ART since 6.0, so both offsets fall out of `sizeof(ArtMethod)`. |
| `jni_bridge_quick_entry` (= `art_quick_generic_jni_trampoline`) | Required only for hooking *non-native* methods. Three tiers, first match wins: (1) resolve directly from libart's `.dynsym` (rarely exported); (2) sample the quick entry of an `Object` native that isn't AOT'd into boot.oat; (3) build a probe dex at runtime, load it via `InMemoryDexClassLoader`, read the (unresolved) native's quick entry, `art_quick_resolution_trampoline` on Android 11+, and apply the known `+0x140` offset to reach the generic bridge. |

If any of these checks fails or returns an ambiguous result, `Initialize()`
returns `kLayoutDiscoveryFailed` and the library refuses to install hooks.

The implementation lives in [`src/art/Layout.cpp`](src/art/Layout.cpp) and
is the most safety-critical part of the codebase.

## Architecture

```
include/arthook/ArtHook.h    -- public API (the only header consumers see)
src/
  art/                       -- ArtMethod offset accessors + runtime layout discovery
  elf/                       -- libart.so dynamic + on-disk symbol resolver
  trampoline/                -- per-arch RWX trampoline pages + .S templates
  probe/                     -- runtime-built probe dex + InMemoryDexClassLoader injection
  hook/                      -- install/remove hooks; jmethodID ↔ ArtMethod mapping
  util/                      -- Log macros + safe memcpy-based offset reads
```

A hook installation does the following:

1. Resolve target `ArtMethod*` via `GetMethodID` / `FromReflectedMethod`.
2. Snapshot the current access flags + both entry points (used to undo).
3. Allocate a backup ArtMethod and `memcpy` the original into it. Return
   that to the caller as a callable `jmethodID`.
4. Rewrite the access flags: set `kAccPrivate | kAccNative |
   kAccCompileDontBother`, clear the cluster of dispatch-shortcut bits
   listed in `src/art/AccessFlags.h` (`kAccFastNative`, `kAccCriticalNative`,
   `kAccPreCompiled`, `kAccIntrinsified`, etc.). This forces ART onto the
   generic JNI dispatch path that consults the entry-point fields.
5. Build an RX trampoline pointing at the user's replacement and install
   it as `entry_point_from_jni_`. For *non-native* targets also install
   the captured generic-JNI bridge as `entry_point_from_quick_compiled_code_`
   so quick callers go through it before reaching JNI.

`Unhook()` restores the saved access flags and entry points and releases the
declaring-class GlobalRef. The RX trampoline page and the backup ArtMethod
slot are intentionally leaked, invocation is lock-free, so a thread may still
be executing the trampoline. One page + one slot per unhook, so avoid tight
hook/unhook loops at runtime.

## Threading

`Initialize`, `Hook`, `HookReflected`, and `Unhook` take a single global
mutex. They are safe to call from any thread.

Hooked method **invocation** is **lock-free**. The trampoline is a few
bytes of `ldr+br` (ARM64), `ldr+bx` (ARM), `jmp [rip+0]` (x86_64), or
`push+ret` (x86), no synchronization, no extra branches. ART thinks it
called the original method and dispatches normally.

## Caveats

- On Android 11+ default ART builds `jmethodID` is `(index << 1) | 1` (the
  `kSwapablePointer` JNI-IDs mode), not a direct `ArtMethod*`. We
  transparently round-trip through `env->ToReflectedMethod` and read the
  raw pointer out of the `Executable.artMethod` long field, see
  `src/art/ArtMethod.cpp`.
- AOT-inlined and already-compiled call sites bypass the hook: there is no
  deoptimization. `kAccCompileDontBother` only prevents *future* compilation
 , it does not evict code ART already compiled or inlined, and boot-image
  AOT inlines never fall out. Hook a method before it goes hot. Redirecting
  already-compiled callers would need a deopt path; this library deliberately
  stays out of that complexity.
- Interface `default` methods are not currently hookable on Android 13+:
  ART dispatches past the per-class *copied* `ArtMethod` we patch, going
  directly to the interface's original. Would require hooking the
  interface's `ArtMethod` itself (different code path; not exposed).

## Test app

A comprehensive test suite lives under [`tests/`](tests), 56 tests across
10 categories (method kinds, modifiers, concurrency, backup, args,
lifecycle, failure, resources, diagnostics, SSL). Open it as an Android
Studio project, run on a device/emulator with API 26+, tap **Run all**.
See [`tests/TESTING.md`](tests/TESTING.md) for what each category covers.

## Examples

[`examples/ssl_bypass/`](examples/ssl_bypass) is a complete payload that
neutralizes the certificate-pinning checks of OkHttp, Trustkit, Conscrypt
`TrustManagerImpl`, WebView, Apache HttpClient, and a handful of legacy
libraries, a port of Maurizio Siddu's `frida_multiple_unpinning` script.
It demonstrates the recommended consumption pattern (deferred init via a
worker thread to avoid the early-startup GC race) and uses the shared
prebuilt `arthook` static lib from [`examples/arthook/`](examples/arthook).

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for build, style, and PR guidelines.

## License

[Apache License 2.0](LICENSE).
