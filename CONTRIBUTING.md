# Contributing to arthook

Thanks for your interest. arthook is a small, focused library — patches are
welcome, but the bar for additions is that they keep the dependency surface
zero and don't increase per-device fragility.

## Building

Requires Android NDK r25+ and CMake 3.18+. Each ABI is its own build:

```sh
cmake -B build/arm64-v8a \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-26 \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build/arm64-v8a
```

Repeat with `armeabi-v7a`, `x86_64`, `x86` for the other ABIs.

## Running the test suite

The test app under `tests/` is a standard Android Studio project — open it,
deploy to a device or emulator running API 26+, and tap **Run all**.
Categories are also runnable individually. See `tests/TESTING.md` for what
each test category verifies and the `SKIP` semantics.

To rerun after editing arthook itself, just rebuild the test app — Gradle
picks up the CMake subproject automatically.

## Code style

C++17, formatted with `clang-format` using the project's `.clang-format`
(Google-based, 100 col, 4-space indent). Run:

```sh
find src include examples tests -name "*.cpp" -o -name "*.h" \
    | xargs clang-format -i
```

Other conventions worth knowing:

- **`reinterpret_cast` is fine and expected**, given the nature of the
  work. Don't try to eliminate it.
- **No exceptions, no RTTI** in the library itself (release builds set
  `-fno-exceptions -fno-rtti`). The test app and examples can use either.
- **Comments explain *why*, not *what*.** If a comment restates what the
  code obviously does, delete it. Verbose comments are encouraged for ART
  quirks, ABI subtleties, and trampoline assembly.

## What kinds of PRs are welcome

- New Android version support — if you have a device where layout discovery
  or bridge sampling fails, please attach the logcat from
  `arthook::Initialize` and we'll figure it out.
- Bug fixes with a reproducer test in `tests/`.
- Documentation improvements.
- Per-arch trampoline improvements (e.g. preserve more registers, smaller
  template).

## What's likely to be declined

- New external dependencies. The zero-dependency property is a feature.
- Hooking strategies that require root or a privileged daemon (out of scope —
  arthook is the *in-process* hooking primitive only).
- Generic argument-logging / libffi-style dispatch. The expectation is the
  caller knows the JNI signature of the target.
- Deoptimization for AOT-inlined call sites. We intentionally don't go there.

## Commit messages

Subject line ≤ 72 chars, imperative mood. Reference issues/PRs by `#NNN`.
Multi-paragraph body is welcome when the change isn't self-explanatory.

## Reporting bugs

Please include:

- Device model + Android version + build fingerprint
- The full logcat from `arthook` and the test app (or your consumer) from
  process start through the failure
- Whether the test suite at `tests/` passes on the same device

## License

By contributing you agree your contribution is licensed under the
[Apache License 2.0](LICENSE), matching the rest of the project.
