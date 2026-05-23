# Test catalog

What each test category checks, and what SKIP reasons mean.

## Methods

| Test | What it verifies |
|---|---|
| `static_int_add` | Static method, two `int` args, `int` return. Hook fires once, backup not exercised, restoration after unhook. |
| `static_void_no_args` | Static method with no args and `void` return. Hook suppresses the body (observable via a static counter on Targets). |
| `instance_int_arg` | Instance method, one `int` arg. Verifies arg pass-through into the hook. |
| `instance_string_concat` | Instance method, `String` arg and return. Verifies object-typed args and references stay valid in the hook. |
| `final_class_method` | Method on a `final` class. Final classes can't be subclassed but ART still stores the method in their `methods_` array; arthook should hook them like any other. |
| `long_double_int_args` | Mixed 64-bit argument passing — `long` (8B), `double` (8B in FP reg), `int` (4B). Catches arm32 64-bit register-pair bugs and arm64 mixed GP/FP regs. |
| `double_return_many_doubles` | Four `double` args and a `double` return. Catches FP-return-register usage bugs. |
| `eight_int_args_stack_passed` | Eight `int` args — on arm64, exceeds x0..x7 + thiz, forcing stack-passed args through ART's bridge. |
| `returns_null` | Object-returning method that returns null. Hook also returns null. |
| `throws_exception_propagates` | Method that throws `RuntimeException`. Hook throws its own; verifies exception machinery still works. |
| `package_private_class` | Hooks a method on a non-public class via reflection. |
| `private_method` | Hooks a `private` method through a public wrapper that calls it. |
| `protected_method` | Hooks a `protected` method through a same-class accessor. |
| `constructor_init` | Hooks `<init>`. SKIPs gracefully if ART rejects. When it works, the body is suppressed (fields remain at defaults). |
| `static_initializer_interaction` | Hooks a static method that is called from another class's `<clinit>`. Uses reflective Class.forName(name, false) + `arthook::HookReflected` to install before the class is initialized. **SKIPs on rerun** because once a class's `<clinit>` has run it can't run again in the same process. |

## Modifiers

| Test | What it verifies |
|---|---|
| `synchronized_method` | Hook intercepts; the monitor enter/exit around the call still happens (the original retains synchronization on restore). |
| `final_method` | Final on the method itself (not the class). |
| `abstract_method_graceful` | Abstract method has no real implementation. Either arthook refuses the install (any non-zero status) or the install is a no-op (abstract slot isn't directly invoked through any concrete subclass's vtable). **PASS = no crash, regardless of which path.** |
| `native_registered_via_register_natives` | Native method explicitly bound by `RegisterNatives`. Hook replaces the JNI binding; backup is the original C function pointer. |
| `interface_default_method` | Java 8 interface `default` method. **SKIPs on Android 13+**: ART dispatches past the per-class *copied* ArtMethod we patch and goes straight to the interface's original. Hooking the interface's ArtMethod itself would catch it — different code path, not currently exposed. |
| `parent_child_polymorphism` | Hook the child's vtable slot, call on parent — parent should be unaffected. Verifies we hook the right ArtMethod, not the shared one. |

## Concurrency

| Test | What it verifies |
|---|---|
| `double_hook_returns_already_hooked` | Two `installHook` on the same {class,name,sig} → second returns `kAlreadyHooked` (status 5). |
| `unhook_not_hooked` | Unhook on a method that isn't hooked → `kNotHooked` (status 6). |
| `hook_unhook_hook_again` | Hook–unhook–hook cycle three times. Verifies state machine doesn't leak. |
| `concurrent_invoke_8x10000` | 8 threads × 10 000 invocations of a hooked method = 80 000 fires expected. No missed hooks, no crashes. |
| `concurrent_install_8_threads` | 8 threads each install + uninstall their own (distinct) hook 50 times. Installation path is mutex-serialized; checking the mutex doesn't deadlock. |

## Backup

| Test | What it verifies |
|---|---|
| `backup_returns_original_value` | Backup `jmethodID` returned by `Hook()` invokes the original behavior. |
| `backup_call_from_hook_wrap` | Hook function calls backup inline (wrap pattern). |
| `backup_call_from_different_thread` | Backup invoked from a thread other than the one that installed the hook. |
| `backup_loop_1000x` | 1 000 sequential backup invocations — no drift, no leak. |

## Args

Verifies argument pass-through for every JNI primitive type, plus
object/null/large-string/array shapes.

| Test | What it verifies |
|---|---|
| `arg_boolean`, `arg_byte`, `arg_char`, `arg_short`, `arg_int`, `arg_long`, `arg_float`, `arg_double` | The hook sees the same value the caller passed. `arg_byte` checks signed sign-extension; `arg_long` checks the full 64 bits round-trip. |
| `arg_string_readable_in_hook` | `jstring` arg is a valid local ref inside the hook (`GetStringUTFChars` succeeds). |
| `arg_null_object` | `null` object arg reaches the hook as `nullptr`. |
| `arg_long_string_1mb` | 1 MB string — `GetStringLength` returns 1 048 576. |
| `arg_object_array_mixed_null` | `Object[]` with mixed null entries — array length and per-element nullness preserved. |

## Lifecycle

| Test | What it verifies |
|---|---|
| `hook_uncalled_method` | Target hasn't been invoked yet this session; hook fires on the first call. |
| `hook_after_jit_warmup` | Caller is warmed (50 000 iterations) to encourage JIT compilation, then hook is installed. **SKIPs if** the JIT inlined the original — that's a known arthook limitation (we don't deoptimize). |
| `warmup_caller_after_hook` | Hook is installed first, then 50 000 warm-up iterations run. Verifies the hook stays effective through tier transitions. May SKIP for the same reason. |

## Failure

| Test | What it verifies |
|---|---|
| `no_such_class` | `installHook` with a key that points at a bogus class name. Returns non-zero, no crash. |
| `no_such_method` | Method name doesn't exist on a real class. |
| `wrong_signature` | Class+name exist but signature doesn't match — same error code. |
| `null_replacement` | Passing `nullptr` as the replacement → `kInternalError`. |
| `initialize_twice_is_ok` | Second `arthook::Initialize(env)` returns `kOk` (idempotent). |
| `hook_before_initialize` | Always SKIPs: we can't un-initialize arthook within the same process. The contract is exercised at startup (first `Hook()` call would return `kNotInitialized` if `Initialize` hadn't run). |
| `hook_abstract_method` | Same as the Modifier-category variant. Mustn't crash. |

## Resources

| Test | What it verifies |
|---|---|
| `fd_count_stable_after_install_unhook_loop` | 25 install/unhook cycles don't leak file descriptors (delta ≤ 4 for class-loader transients). |
| `trampoline_pages_free_after_unhook` | Counter tracking `BuildTrampoline`/`FreeTrampoline` returns to baseline after unhook. |
| `rss_growth_bounded_after_loop` | 10 outer × 1 000 inner invocations. RSS growth < 8 MB (allowing JIT cache, class loading). A real leak shows tens of MB. |

## Diagnostics

| Test | What it verifies |
|---|---|
| `print_layout_and_device` | Always PASSes. Prints `Build.MANUFACTURER`, `MODEL`, `FINGERPRINT`, API level, supported ABIs, and the layout info string (which echoes whether arthook initialized) to logcat. Attach this output to any bug report. |

## SKIP semantics

A SKIP is **not** a failure. It means the test was deliberately not
executed because:

1. The scenario can't be re-run in the same process (e.g.
   `static_initializer_interaction` after the first run).
2. The scenario hit a known arthook limitation that has been
   documented but not yet implemented (JIT-inlined original, full
   un-initialize).
3. The device/OS combination doesn't expose the necessary surface
   (rare; not currently used).

SKIPs print their reason on the same line as the result and are
counted separately from FAILs in the summary.

## Adding a new test

1. Pick the category file under
   `app/src/main/java/com/ak4ne/arthooktest/tests/`.
2. Add a `r.add(CAT, "your_test_name", () -> { ... });` block.
3. If the test needs a new target method, add it to
   `Targets.java` and a `Spec` entry to
   `app/src/main/cpp/test_hooks.cpp`.
4. If the replacement needs a new signature, add a `Hook_yourname`
   function to `test_hooks.cpp` and wire it into the Spec entry.

The framework runs each test on its own thread with a 10 s timeout
(override via `r.add(CAT, name, timeoutMs, fn)`).
