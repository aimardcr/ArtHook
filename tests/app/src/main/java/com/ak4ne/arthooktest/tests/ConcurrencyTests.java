package com.ak4ne.arthooktest.tests;

import com.ak4ne.arthooktest.Targets;
import com.ak4ne.arthooktest.testkit.Assert;
import com.ak4ne.arthooktest.testkit.NativeBridge;
import com.ak4ne.arthooktest.testkit.TestRunner;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public final class ConcurrencyTests {
    public static final String CAT = "Concurrency";

    public static void register(TestRunner r) {
        r.add(CAT, "double_hook_returns_already_hooked", () -> {
            NativeBridge.installOrSkip("instance_int_arg");
            try {
                int rc2 = NativeBridge.installHook("instance_int_arg");
                Assert.expectTrue(rc2 != 0, "second install should not return kOk");
            } finally {
                NativeBridge.uninstallHook("instance_int_arg");
            }
        });

        r.add(CAT, "unhook_not_hooked", () -> {
            int rc = NativeBridge.uninstallHook("instance_int_arg");
            Assert.expectTrue(rc != 0, "unhooking a method that's not hooked should error");
        });

        r.add(CAT, "hook_unhook_hook_again", () -> {
            Targets t = new Targets();
            for (int i = 0; i < 3; i++) {
                NativeBridge.installOrSkip("instance_int_arg");
                NativeBridge.resetState("instance_int_arg");
                Assert.expectEq(Targets.SENTINEL_INT, t.argInt(10), "iter " + i);
                Assert.expectEq(0, NativeBridge.uninstallHook("instance_int_arg"));
                Assert.expectEq(11, t.argInt(10), "post iter " + i);
            }
        });

        r.add(CAT, "concurrent_invoke_8x10000", 30_000, () -> {
            Targets t = new Targets();
            NativeBridge.installOrSkip("instance_int_arg");
            NativeBridge.resetState("instance_int_arg");

            final int threads = 8;
            final int iters   = 10_000;
            CountDownLatch start = new CountDownLatch(1);
            CountDownLatch done  = new CountDownLatch(threads);
            AtomicInteger errors = new AtomicInteger();

            List<Thread> ts = new ArrayList<>();
            for (int i = 0; i < threads; i++) {
                Thread th = new Thread(() -> {
                    try { start.await(); } catch (InterruptedException ignored) {}
                    for (int j = 0; j < iters; j++) {
                        int r2 = t.argInt(j);
                        if (r2 != Targets.SENTINEL_INT) errors.incrementAndGet();
                    }
                    done.countDown();
                }, "invoke-thread-" + i);
                ts.add(th);
                th.start();
            }
            start.countDown();
            try {
                if (!done.await(25, TimeUnit.SECONDS))
                    throw new AssertionError("threads did not finish");
                Assert.expectEq(0, errors.get(), "missed-hook count");
                Assert.expectEq(threads * iters, NativeBridge.fireCount("instance_int_arg"),
                                "total hook fires");
            } finally {
                NativeBridge.uninstallHook("instance_int_arg");
            }
        });

        r.add(CAT, "concurrent_install_8_threads", 30_000, () -> {
            if (!NativeBridge.hasJniBridge())
                Assert.skip("JNI bridge unavailable; thread_keys_* target non-native methods");
            final String[] keys = {
                "thread_keys_a", "thread_keys_b", "thread_keys_c", "thread_keys_d",
                "thread_keys_e", "thread_keys_f", "thread_keys_g", "thread_keys_h",
            };
            final int rounds = 50;
            CountDownLatch start = new CountDownLatch(1);
            CountDownLatch done  = new CountDownLatch(keys.length);
            AtomicInteger errors = new AtomicInteger();

            for (String k : keys) {
                Thread th = new Thread(() -> {
                    try { start.await(); } catch (InterruptedException ignored) {}
                    for (int j = 0; j < rounds; j++) {
                        if (NativeBridge.installHook(k) != 0) {
                            errors.incrementAndGet(); break;
                        }
                        if (NativeBridge.uninstallHook(k) != 0) {
                            errors.incrementAndGet(); break;
                        }
                    }
                    done.countDown();
                }, "install-" + k);
                th.start();
            }
            start.countDown();
            if (!done.await(25, TimeUnit.SECONDS))
                throw new AssertionError("install threads stuck");
            Assert.expectEq(0, errors.get(), "install/unhook errors");
        });
    }
}
