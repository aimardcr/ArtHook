package com.ak4ne.arthooktest.tests;

import com.ak4ne.arthooktest.Targets;
import com.ak4ne.arthooktest.testkit.Assert;
import com.ak4ne.arthooktest.testkit.NativeBridge;
import com.ak4ne.arthooktest.testkit.TestRunner;

public final class ResourceTests {
    public static final String CAT = "Resources";

    public static void register(TestRunner r) {
        r.add(CAT, "fd_count_stable_after_install_unhook_loop", () -> {
            int fdBefore = NativeBridge.openFdCount();
            for (int i = 0; i < 25; i++) {
                NativeBridge.installOrSkip("instance_int_arg");
                Assert.expectEq(0, NativeBridge.uninstallHook("instance_int_arg"));
            }
            int fdAfter = NativeBridge.openFdCount();
            // Allow tiny slack — Android may open transient fds for class loading.
            int delta = fdAfter - fdBefore;
            Assert.expectTrue(delta <= 4,
                              "fd leak: before=" + fdBefore + " after=" + fdAfter);
        });

        r.add(CAT, "trampoline_pages_free_after_unhook", () -> {
            int p0 = NativeBridge.trampolinePagesInUse();
            NativeBridge.installOrSkip("instance_int_arg");
            int p1 = NativeBridge.trampolinePagesInUse();
            Assert.expectTrue(p1 > p0, "trampoline page should be tracked while hooked");
            Assert.expectEq(0, NativeBridge.uninstallHook("instance_int_arg"));
            int p2 = NativeBridge.trampolinePagesInUse();
            Assert.expectEq(p0, p2, "trampoline page count should return to baseline");
        });

        r.add(CAT, "rss_growth_bounded_after_loop", 30_000, () -> {
            Targets t = new Targets();
            long rssBefore = NativeBridge.processRssKb();
            for (int i = 0; i < 10; i++) {
                NativeBridge.installOrSkip("instance_int_arg");
                for (int j = 0; j < 1000; j++) t.argInt(j);
                Assert.expectEq(0, NativeBridge.uninstallHook("instance_int_arg"));
            }
            long rssAfter = NativeBridge.processRssKb();
            long deltaKb = rssAfter - rssBefore;
            // Allow up to 8 MB of growth for JIT artefacts, class loading.
            // A real leak shows up as tens of MB.
            Assert.expectTrue(deltaKb < 8 * 1024,
                              "RSS grew by " + deltaKb + " KB (before " + rssBefore
                              + " after " + rssAfter + ")");
        });
    }
}
