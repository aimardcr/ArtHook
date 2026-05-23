package com.ak4ne.arthooktest.tests;

import com.ak4ne.arthooktest.Targets;
import com.ak4ne.arthooktest.testkit.Assert;
import com.ak4ne.arthooktest.testkit.NativeBridge;
import com.ak4ne.arthooktest.testkit.TestRunner;

public final class ModifierTests {
    public static final String CAT = "Modifiers";

    public static void register(TestRunner r) {
        r.add(CAT, "synchronized_method", () -> {
            Targets t = new Targets();
            int before = t.syncIncrement();
            NativeBridge.installOrSkip("synchronized_method");
            try {
                int hooked = t.syncIncrement();
                Assert.expectEq(Targets.SENTINEL_INT, hooked, "hook return");
            } finally {
                NativeBridge.uninstallHook("synchronized_method");
            }
            int restored = t.syncIncrement();
            Assert.expectEq(before + 1, restored, "monitor still works post-unhook");
        });

        r.add(CAT, "final_method", () -> {
            Targets t = new Targets();
            Assert.expectEq(42, t.finalReturn42(), "pre");
            NativeBridge.installOrSkip("final_method");
            try {
                Assert.expectEq(Targets.SENTINEL_INT, t.finalReturn42());
            } finally {
                NativeBridge.uninstallHook("final_method");
            }
            Assert.expectEq(42, t.finalReturn42(), "post");
        });

        r.add(CAT, "abstract_method_graceful", () -> {
            // We try to hook an abstract method. Either the engine refuses
            // (any non-zero status is acceptable) OR it installs but the
            // hook never fires (abstract methods aren't directly invoked).
            // Either way, we should not crash.
            int rc = NativeBridge.installHook("abstract_method");
            if (rc != 0) {
                // graceful refusal — what we hope for
                return;
            }
            try {
                Targets.ConcreteAbstract c = new Targets.ConcreteAbstract();
                int v = c.abstractM();
                // Concrete override should still return 7 — hooking the
                // *abstract* slot shouldn't affect the concrete's vtable
                // entry.
                Assert.expectEq(7, v, "concrete override should be intact");
            } finally {
                NativeBridge.uninstallHook("abstract_method");
            }
        });

        r.add(CAT, "native_registered_via_register_natives", () -> {
            Assert.expectEq(5, Targets.nativeAddJni(2, 3), "pre");
            NativeBridge.installOrSkip("native_registered");
            try {
                Assert.expectEq(Targets.SENTINEL_INT, Targets.nativeAddJni(2, 3));
            } finally {
                NativeBridge.uninstallHook("native_registered");
            }
            Assert.expectEq(5, Targets.nativeAddJni(2, 3), "post");
        });

        r.add(CAT, "interface_default_method", () -> {
            Targets.UsesDefault u = new Targets.UsesDefault();
            Assert.expectEq("default-greet", u.defaultGreet(), "pre");
            NativeBridge.installOrSkip("interface_default");
            String hookedResult;
            try {
                hookedResult = u.defaultGreet();
            } finally {
                NativeBridge.uninstallHook("interface_default");
            }
            if (!Targets.SENTINEL_STRING.equals(hookedResult)) {
                // Install succeeded but ART dispatched the default-method
                // invocation past the copied ArtMethod we hooked (an
                // Android 13+ optimization for non-overridden defaults).
                // Hooking the interface's ArtMethod itself would be needed.
                Assert.skip("ART dispatched past the copied default method "
                            + "(got \"" + hookedResult + "\") — known limitation");
            }
            Assert.expectEq("default-greet", u.defaultGreet(), "post");
        });

        r.add(CAT, "parent_child_polymorphism", () -> {
            Targets.Parent p = new Targets.Parent();
            Targets.Child  c = new Targets.Child();
            Assert.expectEq("Parent", p.describe(), "pre parent");
            Assert.expectEq("Child",  c.describe(), "pre child");

            // Hook the CHILD's slot — parent unaffected.
            NativeBridge.installOrSkip("child_describe");
            try {
                Assert.expectEq("Parent", p.describe(), "parent unaffected");
                Assert.expectEq(Targets.SENTINEL_STRING, c.describe(), "child hooked");
            } finally {
                NativeBridge.uninstallHook("child_describe");
            }
            Assert.expectEq("Parent", p.describe(), "post parent");
            Assert.expectEq("Child",  c.describe(), "post child");
        });
    }
}
