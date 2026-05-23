package com.ak4ne.arthooktest.tests;

import com.ak4ne.arthooktest.Targets;
import com.ak4ne.arthooktest.testkit.Assert;
import com.ak4ne.arthooktest.testkit.NativeBridge;
import com.ak4ne.arthooktest.testkit.TestRunner;

public final class BackupTests {
    public static final String CAT = "Backup";

    public static void register(TestRunner r) {
        r.add(CAT, "backup_returns_original_value", () -> {
            // Hook that records but does NOT call backup; we invoke backup
            // separately via the native bridge.
            Targets t = new Targets();
            NativeBridge.installOrSkip("instance_string_concat");
            try {
                String invoked = NativeBridge.invokeBackupStringInst(
                        "instance_string_concat", t, "bar");
                Assert.expectEq("[bar]", invoked, "backup returns original");
            } finally {
                NativeBridge.uninstallHook("instance_string_concat");
            }
        });

        r.add(CAT, "backup_call_from_hook_wrap", () -> {
            // The hook calls backup internally and wraps the result.
            Targets t = new Targets();
            NativeBridge.installOrSkip("wrap_concat_with_backup");
            try {
                String r2 = t.concat("baz");
                Assert.expectEq("WRAP([baz])", r2, "wrap-pattern");
            } finally {
                NativeBridge.uninstallHook("wrap_concat_with_backup");
            }
            Assert.expectEq("[baz]", t.concat("baz"), "post");
        });

        r.add(CAT, "backup_call_from_different_thread", () -> {
            Targets t = new Targets();
            NativeBridge.installOrSkip("instance_string_concat");
            try {
                final String[] out = new String[1];
                Thread th = new Thread(() -> {
                    out[0] = NativeBridge.invokeBackupStringInst(
                            "instance_string_concat", t, "thr");
                });
                th.start();
                th.join(5000);
                Assert.expectEq("[thr]", out[0], "backup callable from another thread");
            } finally {
                NativeBridge.uninstallHook("instance_string_concat");
            }
        });

        r.add(CAT, "backup_loop_1000x", () -> {
            Targets t = new Targets();
            NativeBridge.installOrSkip("instance_string_concat");
            try {
                for (int i = 0; i < 1000; i++) {
                    String r2 = NativeBridge.invokeBackupStringInst(
                            "instance_string_concat", t, "x" + i);
                    if (!("[x" + i + "]").equals(r2))
                        throw new AssertionError("drift at iter " + i + ": got " + r2);
                }
            } finally {
                NativeBridge.uninstallHook("instance_string_concat");
            }
        });
    }
}
