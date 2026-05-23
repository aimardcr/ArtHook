package com.ak4ne.arthooktest.tests;

import com.ak4ne.arthooktest.Targets;
import com.ak4ne.arthooktest.testkit.Assert;
import com.ak4ne.arthooktest.testkit.NativeBridge;

/** Accessor that crosses the package boundary into Targets' package. */
final class PackagePrivateAccessor {
    static void run() throws Throwable {
        Class<?> ppCls = Class.forName("com.ak4ne.arthooktest.PackagePrivateClass");
        java.lang.reflect.Constructor<?> ctor = ppCls.getDeclaredConstructor();
        ctor.setAccessible(true);
        Object o = ctor.newInstance();
        java.lang.reflect.Method m = ppCls.getDeclaredMethod("hello");
        m.setAccessible(true);
        Assert.expectEq("pp-hello", m.invoke(o), "pre");

        NativeBridge.installOrSkip("package_private_class");
        try {
            Assert.expectEq(Targets.SENTINEL_STRING, m.invoke(o), "hook");
        } finally {
            NativeBridge.uninstallHook("package_private_class");
        }
        Assert.expectEq("pp-hello", m.invoke(o), "post");
    }
}
