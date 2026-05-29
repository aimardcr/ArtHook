# arthook test suite

Android app that exercises arthook against ~50 scenarios across 10
categories. Pass/fail signal feeds back into arthook regressions.

## Running

1. Open `D:/Project/ArtHook/tests` in Android Studio, or build the APK from
   the command line:

       ./gradlew.bat :app:assembleDebug

   The APK is at `app/build/outputs/apk/debug/app-debug.apk`.

2. Install on a device or emulator (API 26+, arm64-v8a recommended):

       adb install -r app/build/outputs/apk/debug/app-debug.apk
       adb shell am start -n com.ak4ne.arthooktest/.MainActivity

3. In the app:
   - **Run all** runs the full suite (~30s on a modern device).
   - Per-category buttons run a single category.
   - **Copy results** copies the on-screen log to the clipboard.

4. To scrape results from `adb logcat`:

       adb logcat -s arthook-test:I arthook:I

   The runner emits a parseable summary line:

       arthook-test: SUMMARY pass=48 fail=0 skip=2 total=50
       arthook-test: SKIP test=hook_before_initialize reason="arthook stays initialized for process lifetime"
       arthook-test: SKIP test=static_initializer_interaction reason="Clinitable already initialized; rerun in fresh process"

## Categories

| Category    | Focus |
|-------------|-------|
| Methods     | Each ArtMethod kind: static/instance, primitive/object, final class, package-private, private, protected, constructor, long/double args, double return, 8-arg stack pass, void/null return, throws, static-initializer interaction |
| Modifiers   | synchronized, final, abstract, native + RegisterNatives, interface default, parent/child polymorphism |
| Concurrency | already-hooked / not-hooked status, hook-unhook-hook cycle, 8-thread install, 8×10 000 concurrent invocations |
| Backup      | Backup returns original, wrap pattern, cross-thread, 1000-call loop |
| Args        | All 8 primitive types, null object, 1 MB string, mixed-null object array |
| Lifecycle   | Hook uncalled method, hook after JIT warm-up, JIT-warm a caller after hook |
| Failure     | Bogus class/method/signature, null replacement, Initialize twice, hook abstract |
| Resources   | fd leak check, trampoline page accounting, RSS growth bound |
| Diagnostics | Discovered layout + device fingerprint (prints to logcat) |

## Interpreting results

- **PASS**: arthook handled the scenario correctly.
- **FAIL**: a regression, open with the logcat line attached.
- **SKIP**: known limitation. See `TESTING.md` for what each skip
  reason means.

Common expected SKIPs on a clean build (Android 8–15):

- `hook_before_initialize`, arthook is process-lifetime singleton.
- `static_initializer_interaction`, only runs on the first launch of
  the process; rerunning the suite re-skips.
- `hook_after_jit_warmup` / `warmup_caller_after_hook`, may skip if
  the JIT inlined the original before the hook installed.

## Multi-ABI

The Gradle build compiles for `arm64-v8a`, `armeabi-v7a`, `x86_64`,
and `x86` automatically. The installed APK selects the ABI matching
the device. To smoke-test a specific ABI on the emulator, install the
emulator image for that ABI and rerun.
