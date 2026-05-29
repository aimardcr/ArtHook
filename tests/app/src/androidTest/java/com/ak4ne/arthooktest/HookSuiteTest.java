package com.ak4ne.arthooktest;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.ak4ne.arthooktest.testkit.TestResult;
import com.ak4ne.arthooktest.testkit.TestRunner;
import com.ak4ne.arthooktest.tests.ArgTests;
import com.ak4ne.arthooktest.tests.BackupTests;
import com.ak4ne.arthooktest.tests.ConcurrencyTests;
import com.ak4ne.arthooktest.tests.DiagnosticTests;
import com.ak4ne.arthooktest.tests.FailureTests;
import com.ak4ne.arthooktest.tests.LifecycleTests;
import com.ak4ne.arthooktest.tests.MethodKindTests;
import com.ak4ne.arthooktest.tests.ModifierTests;
import com.ak4ne.arthooktest.tests.ResourceTests;
import com.ak4ne.arthooktest.tests.SslBypassTests;

import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertEquals;

/** Headless driver for the full suite; known limitations report SKIP, so a healthy run has zero FAILs. */
@RunWith(AndroidJUnit4.class)
public class HookSuiteTest {
    @Test
    public void runArtHookSuite() {
        TestRunner r = new TestRunner();
        MethodKindTests.register(r);
        ModifierTests.register(r);
        ConcurrencyTests.register(r);
        BackupTests.register(r);
        ArgTests.register(r);
        LifecycleTests.register(r);
        FailureTests.register(r);
        ResourceTests.register(r);
        SslBypassTests.register(r);
        DiagnosticTests.register(r);

        TestRunner.Summary s = r.run(r.entries(), null);

        // Embed each FAIL/SKIP reason in the assertion message so it lands in the JUnit HTML report.
        StringBuilder sb = new StringBuilder(
                "arthook suite (pass=" + s.pass + " fail=" + s.fail + " skip=" + s.skip + ")");
        for (TestResult t : s.results) {
            if (t.status == TestResult.Status.FAIL)
                sb.append("\n  FAIL ").append(t.category).append('/').append(t.name)
                  .append(": ").append(t.reason);
        }
        for (TestResult t : s.results) {
            if (t.status == TestResult.Status.SKIP)
                sb.append("\n  SKIP ").append(t.category).append('/').append(t.name)
                  .append(": ").append(t.reason);
        }
        assertEquals(sb.toString(), 0, s.fail);
    }
}
