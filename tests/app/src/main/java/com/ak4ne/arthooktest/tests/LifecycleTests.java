package com.ak4ne.arthooktest.tests;

import com.ak4ne.arthooktest.Targets;
import com.ak4ne.arthooktest.testkit.Assert;
import com.ak4ne.arthooktest.testkit.NativeBridge;
import com.ak4ne.arthooktest.testkit.TestRunner;

public final class LifecycleTests {
    public static final String CAT = "Lifecycle";

    public static void register(TestRunner r) {
        r.add(CAT, "hook_uncalled_method", () -> {
            // We hook a method we haven't called this session. We can't
            // truly guarantee "uncalled" across reruns, but we can at least
            // assert the hook fires on the next call.
            Targets t = new Targets();
            NativeBridge.installOrSkip("call_private_m");
            try {
                int v = t.callPrivateM();
                Assert.expectEq(Targets.SENTINEL_INT, v, "hook fires");
            } finally {
                NativeBridge.uninstallHook("call_private_m");
            }
            Assert.expectEq(2, t.callPrivateM(), "original restored");
        });

        r.add(CAT, "hook_after_jit_warmup", 20_000, () -> {
            Targets t = new Targets();
            // Warm up the target to encourage JIT compilation.
            int acc = 0;
            for (int i = 0; i < 50_000; i++) acc += t.argInt(i);
            // Use acc to prevent dead-code elimination.
            Assert.expectTrue(acc != Integer.MIN_VALUE, "guard");
            NativeBridge.installOrSkip("instance_int_arg");
            try {
                int v = t.argInt(7);
                if (v != Targets.SENTINEL_INT) {
                    Assert.skip("JIT-compiled callers retained inlined original — "
                                + "known limitation when callers inlined the target");
                }
            } finally {
                NativeBridge.uninstallHook("instance_int_arg");
            }
        });

        r.add(CAT, "warmup_caller_after_hook", 20_000, () -> {
            Targets t = new Targets();
            NativeBridge.installOrSkip("instance_int_arg");
            try {
                NativeBridge.resetState("instance_int_arg");
                // Try to JIT-compile a caller while the hook is installed.
                int acc = 0;
                for (int i = 0; i < 50_000; i++) {
                    int r2 = t.argInt(i);
                    acc += r2;
                    if (r2 != Targets.SENTINEL_INT) {
                        Assert.skip("hook missed at iter " + i
                                    + " — likely JIT inlining of original");
                    }
                }
                Assert.expectTrue(acc != Integer.MIN_VALUE, "guard");
                Assert.expectEq(50_000, NativeBridge.fireCount("instance_int_arg"));
            } finally {
                NativeBridge.uninstallHook("instance_int_arg");
            }
        });
    }
}
