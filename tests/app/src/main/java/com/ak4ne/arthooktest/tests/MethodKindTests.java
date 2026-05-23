package com.ak4ne.arthooktest.tests;

import com.ak4ne.arthooktest.Targets;
import com.ak4ne.arthooktest.testkit.Assert;
import com.ak4ne.arthooktest.testkit.NativeBridge;
import com.ak4ne.arthooktest.testkit.TestRunner;

public final class MethodKindTests {
    public static final String CAT = "Methods";

    public static void register(TestRunner r) {
        r.add(CAT, "static_int_add", () -> {
            Assert.expectEq(5, Targets.staticAdd(2, 3), "pre-hook");
            assertInstalled("static_int_add");
            try {
                Assert.expectEq(Targets.SENTINEL_INT, Targets.staticAdd(2, 3), "hook should fire");
                Assert.expectEq(1, NativeBridge.fireCount("static_int_add"), "fire count");
            } finally {
                NativeBridge.uninstallHook("static_int_add");
            }
            Assert.expectEq(5, Targets.staticAdd(2, 3), "post-unhook");
        });

        r.add(CAT, "static_void_no_args", () -> {
            int before = Targets.staticCallCount.get();
            Targets.staticDoNothing();
            Assert.expectEq(before + 1, Targets.staticCallCount.get(), "orig should bump counter");

            assertInstalled("static_void_no_args");
            try {
                int b2 = Targets.staticCallCount.get();
                Targets.staticDoNothing();
                Assert.expectEq(b2, Targets.staticCallCount.get(), "hook should skip body");
                Assert.expectEq(1, NativeBridge.fireCount("static_void_no_args"));
            } finally {
                NativeBridge.uninstallHook("static_void_no_args");
            }
            int b3 = Targets.staticCallCount.get();
            Targets.staticDoNothing();
            Assert.expectEq(b3 + 1, Targets.staticCallCount.get(), "orig restored");
        });

        r.add(CAT, "instance_int_arg", () -> {
            Targets t = new Targets();
            Assert.expectEq(11, t.argInt(10), "pre-hook");
            assertInstalled("instance_int_arg");
            try {
                Assert.expectEq(Targets.SENTINEL_INT, t.argInt(10));
                Assert.expectEq(10L, NativeBridge.lastLongArg("instance_int_arg", 0), "arg pass-through");
            } finally {
                NativeBridge.uninstallHook("instance_int_arg");
            }
            Assert.expectEq(11, t.argInt(10), "post-unhook");
        });

        r.add(CAT, "instance_string_concat", () -> {
            Targets t = new Targets();
            Assert.expectEq("[foo]", t.concat("foo"), "pre-hook");
            assertInstalled("instance_string_concat");
            try {
                Assert.expectEq(Targets.SENTINEL_STRING, t.concat("foo"), "hook return");
                Assert.expectEq("foo", NativeBridge.lastStringArg("instance_string_concat", 0), "arg");
            } finally {
                NativeBridge.uninstallHook("instance_string_concat");
            }
            Assert.expectEq("[foo]", t.concat("foo"));
        });

        r.add(CAT, "final_class_method", () -> {
            Targets.FinalGreeter g = new Targets.FinalGreeter();
            Assert.expectEq("hello-final", g.greet(), "pre");
            assertInstalled("final_class_method");
            try {
                Assert.expectEq(Targets.SENTINEL_STRING, g.greet());
            } finally {
                NativeBridge.uninstallHook("final_class_method");
            }
            Assert.expectEq("hello-final", g.greet());
        });

        r.add(CAT, "long_double_int_args", () -> {
            Targets t = new Targets();
            Assert.expectEq(125L, t.longDoubleInt(100L, 20.0, 5), "pre");
            assertInstalled("long_double_int_args");
            try {
                long ret = t.longDoubleInt(100L, 20.0, 5);
                Assert.expectEq(Targets.SENTINEL_LONG, ret, "hook return");
                Assert.expectEq(100L, NativeBridge.lastLongArg("long_double_int_args", 0), "long arg");
                Assert.expectEq(20.0, NativeBridge.lastDoubleArg("long_double_int_args", 0), 1e-9, "double arg");
                Assert.expectEq(5L, NativeBridge.lastLongArg("long_double_int_args", 1), "int arg");
            } finally {
                NativeBridge.uninstallHook("long_double_int_args");
            }
            Assert.expectEq(125L, t.longDoubleInt(100L, 20.0, 5), "post");
        });

        r.add(CAT, "double_return_many_doubles", () -> {
            Targets t = new Targets();
            Assert.expectEq(10.0, t.manyDoubles(1.0, 2.0, 3.0, 4.0), 1e-9, "pre");
            assertInstalled("double_return_many_doubles");
            try {
                double r2 = t.manyDoubles(1.0, 2.0, 3.0, 4.0);
                Assert.expectEq(Targets.SENTINEL_DOUBLE, r2, 1e-9, "hook return");
            } finally {
                NativeBridge.uninstallHook("double_return_many_doubles");
            }
            Assert.expectEq(10.0, t.manyDoubles(1.0, 2.0, 3.0, 4.0), 1e-9, "post");
        });

        r.add(CAT, "eight_int_args_stack_passed", () -> {
            Targets t = new Targets();
            Assert.expectEq(36, t.eightInts(1, 2, 3, 4, 5, 6, 7, 8), "pre");
            assertInstalled("eight_int_args");
            try {
                int r2 = t.eightInts(1, 2, 3, 4, 5, 6, 7, 8);
                Assert.expectEq(Targets.SENTINEL_INT, r2);
                for (int i = 0; i < 8; i++) {
                    Assert.expectEq(i + 1L, NativeBridge.lastLongArg("eight_int_args", i),
                                    "arg " + i);
                }
            } finally {
                NativeBridge.uninstallHook("eight_int_args");
            }
            Assert.expectEq(36, t.eightInts(1, 2, 3, 4, 5, 6, 7, 8), "post");
        });

        r.add(CAT, "returns_null", () -> {
            Targets t = new Targets();
            Assert.expectNull(t.returnNull(), "pre");
            assertInstalled("returns_null");
            try {
                Assert.expectNull(t.returnNull(), "hook should also return null");
                Assert.expectEq(1, NativeBridge.fireCount("returns_null"));
            } finally {
                NativeBridge.uninstallHook("returns_null");
            }
            Assert.expectNull(t.returnNull(), "post");
        });

        r.add(CAT, "throws_exception_propagates", () -> {
            Targets t = new Targets();
            Assert.expectThrows(RuntimeException.class, t::throwsRuntime);
            assertInstalled("throws_exception");
            try {
                RuntimeException e = Assert.expectThrows(RuntimeException.class, t::throwsRuntime);
                Assert.expectEq("hooked-throw", e.getMessage(), "hook's exception");
            } finally {
                NativeBridge.uninstallHook("throws_exception");
            }
            RuntimeException e2 = Assert.expectThrows(RuntimeException.class, t::throwsRuntime);
            Assert.expectEq("orig", e2.getMessage(), "post-unhook");
        });

        r.add(CAT, "package_private_class", () -> {
            PackagePrivateAccessor.run();
        });

        r.add(CAT, "private_method", () -> {
            Targets t = new Targets();
            Assert.expectEq(2, t.callPrivateM(), "pre");
            assertInstalled("private_method");
            try {
                Assert.expectEq(Targets.SENTINEL_INT, t.callPrivateM(), "hook");
            } finally {
                NativeBridge.uninstallHook("private_method");
            }
            Assert.expectEq(2, t.callPrivateM(), "post");
        });

        r.add(CAT, "protected_method", () -> {
            Targets t = new Targets();
            Assert.expectEq(3, t.callProtectedM(), "pre");
            assertInstalled("protected_method");
            try {
                Assert.expectEq(Targets.SENTINEL_INT, t.callProtectedM());
            } finally {
                NativeBridge.uninstallHook("protected_method");
            }
            Assert.expectEq(3, t.callProtectedM(), "post");
        });

        r.add(CAT, "constructor_init", () -> {
            int rc = NativeBridge.installHook("constructor_init");
            if (rc != 0) Assert.skip("constructor hook returned status " + rc);
            try {
                Targets.CtorTarget c = new Targets.CtorTarget(5);
                Assert.expectEq(1, NativeBridge.fireCount("constructor_init"),
                                "ctor hook should fire on new");
                Assert.expectEq(5L, NativeBridge.lastLongArg("constructor_init", 0),
                                "ctor arg");
                // initVal should NOT be 6 (the original would have set it to v+1).
                // The hook is a no-op, so initVal stays at default 0.
                Assert.expectEq(0, c.initVal, "ctor body should not have run");
            } finally {
                NativeBridge.uninstallHook("constructor_init");
            }
            Targets.CtorTarget c2 = new Targets.CtorTarget(5);
            Assert.expectEq(6, c2.initVal, "post-unhook original body runs");
        });

        r.add(CAT, "static_initializer_interaction", () -> {
            if (Targets.clinitableLoaded)
                Assert.skip("Clinitable already initialized; rerun in fresh process");
            if (!NativeBridge.hasJniBridge())
                Assert.skip("JNI bridge unavailable; clinitTarget is non-native");

            ClassLoader cl = MethodKindTests.class.getClassLoader();
            Class<?> cls = Class.forName(
                    "com.ak4ne.arthooktest.Targets$Clinitable", false, cl);
            java.lang.reflect.Method m = cls.getDeclaredMethod("clinitTarget");
            m.setAccessible(true);

            int rc = NativeBridge.installHookOnReflected("clinit_target", m);
            Assert.expectEq(0, rc, "install on reflected");
            try {
                Assert.expectFalse(Targets.clinitableLoaded,
                                   "clinit must not have run yet");
                // Reading the field triggers <clinit>, which calls
                // clinitTarget — which should now hit our hook.
                int seen = ((Integer) cls.getDeclaredField("firstSeen").get(null)).intValue();
                Assert.expectTrue(Targets.clinitableLoaded, "clinit should have run");
                Assert.expectEq(Targets.SENTINEL_INT, seen,
                                "hook should have fired during clinit");
            } finally {
                NativeBridge.uninstallHook("clinit_target");
            }
        });
    }

    private static void assertInstalled(String key) {
        // Skip non-native hooks gracefully when the JNI bridge isn't
        // captured on this device (notify in boot.oat + no symbol/.dynsym
        // entry for art_quick_generic_jni_trampoline).
        if (!NativeBridge.targetIsNative(key) && !NativeBridge.hasJniBridge()) {
            Assert.skip("JNI bridge not available on this device; "
                        + "non-native hooks disabled");
        }
        int rc = NativeBridge.installHook(key);
        Assert.expectEq(0, rc, "installHook(" + key + ") rc");
    }
}
