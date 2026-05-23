package com.ak4ne.arthooktest.tests;

import com.ak4ne.arthooktest.testkit.Assert;
import com.ak4ne.arthooktest.testkit.NativeBridge;
import com.ak4ne.arthooktest.testkit.TestRunner;

public final class FailureTests {
    public static final String CAT = "Failure";

    public static void register(TestRunner r) {
        r.add(CAT, "no_such_class", () -> {
            int rc = NativeBridge.installHook("nonexistent_class");
            Assert.expectTrue(rc != 0, "should fail, got " + rc);
        });

        r.add(CAT, "no_such_method", () -> {
            int rc = NativeBridge.installHook("nonexistent_method");
            Assert.expectTrue(rc != 0);
        });

        r.add(CAT, "wrong_signature", () -> {
            int rc = NativeBridge.installHook("wrong_signature");
            Assert.expectTrue(rc != 0);
        });

        r.add(CAT, "null_replacement", () -> {
            int rc = NativeBridge.installHook("null_replacement");
            Assert.expectTrue(rc != 0, "passing nullptr replacement must error");
        });

        r.add(CAT, "initialize_twice_is_ok", () -> {
            // arthook is already initialized; a second Initialize should
            // return kOk per the API contract.
            int rc = NativeBridge.initializeAgain();
            Assert.expectEq(0, rc, "second Initialize");
        });

        r.add(CAT, "hook_before_initialize", () -> {
            // We cannot un-initialize arthook for one test. Skip — this case
            // is covered indirectly by the very first launch of the app.
            Assert.skip("arthook stays initialized for process lifetime");
        });

        r.add(CAT, "hook_abstract_method", () -> {
            // Same expectation as ModifierTests.abstract_method_graceful —
            // no crash. Reuse the same key.
            int rc = NativeBridge.installHook("abstract_method");
            // Either zero (best-effort install) or non-zero — both fine.
            if (rc == 0) NativeBridge.uninstallHook("abstract_method");
        });
    }
}
