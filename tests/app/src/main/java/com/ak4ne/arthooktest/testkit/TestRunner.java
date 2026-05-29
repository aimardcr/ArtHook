package com.ak4ne.arthooktest.testkit;

import android.util.Log;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

public final class TestRunner {
    public static final String TAG = "arthook-test";

    public interface TestFn { void run() throws Throwable; }

    public static final class Entry {
        public final String  category;
        public final String  name;
        public final TestFn  fn;
        public final long    timeoutMs;
        public Entry(String c, String n, TestFn f, long t) {
            category = c; name = n; fn = f; timeoutMs = t;
        }
    }

    public static final class Summary {
        public final List<TestResult> results = new ArrayList<>();
        public int pass, fail, skip;

        public int total() { return pass + fail + skip; }

        public void add(TestResult r) {
            results.add(r);
            switch (r.status) {
                case PASS: pass++; break;
                case FAIL: fail++; break;
                case SKIP: skip++; break;
            }
        }
    }

    public interface ProgressListener { void onProgress(TestResult r, int idx, int total); }

    private final List<Entry> tests = new ArrayList<>();

    public void add(String category, String name, TestFn fn) {
        tests.add(new Entry(category, name, fn, 10_000));
    }

    public void add(String category, String name, long timeoutMs, TestFn fn) {
        tests.add(new Entry(category, name, fn, timeoutMs));
    }

    public List<Entry> entries() { return tests; }

    public List<Entry> byCategory(String c) {
        List<Entry> out = new ArrayList<>();
        for (Entry e : tests) if (e.category.equals(c)) out.add(e);
        return out;
    }

    public Summary run(List<Entry> subset, ProgressListener listener) {
        Summary s = new Summary();
        ExecutorService exec = Executors.newSingleThreadExecutor(r -> {
            Thread t = new Thread(r, "arthook-test");
            t.setDaemon(true);
            return t;
        });
        try {
            for (int i = 0; i < subset.size(); i++) {
                Entry e = subset.get(i);
                // Logged before the test runs so a native crash (which kills
                // the process) leaves the culprit as the last RUN line.
                Log.i(TAG, "RUN " + e.category + "/" + e.name);
                TestResult r = runOne(exec, e);
                s.add(r);
                if (listener != null) listener.onProgress(r, i, subset.size());
            }
        } finally {
            exec.shutdownNow();
        }
        logSummary(s);
        return s;
    }

    private TestResult runOne(ExecutorService exec, Entry e) {
        long t0 = System.nanoTime();
        Future<?> f = exec.submit(() -> {
            try { e.fn.run(); }
            catch (Throwable t) { throw new RuntimeException(t); }
        });
        try {
            f.get(e.timeoutMs, TimeUnit.MILLISECONDS);
            long ms = (System.nanoTime() - t0) / 1_000_000L;
            return new TestResult(e.category, e.name, TestResult.Status.PASS, null, ms);
        } catch (TimeoutException te) {
            f.cancel(true);
            long ms = (System.nanoTime() - t0) / 1_000_000L;
            return new TestResult(e.category, e.name, TestResult.Status.FAIL,
                                  "timeout after " + e.timeoutMs + "ms", ms);
        } catch (ExecutionException ee) {
            long ms = (System.nanoTime() - t0) / 1_000_000L;
            Throwable cause = ee.getCause();
            if (cause instanceof RuntimeException && cause.getCause() != null)
                cause = cause.getCause();
            if (cause instanceof SkipException)
                return new TestResult(e.category, e.name, TestResult.Status.SKIP, cause.getMessage(), ms);
            return new TestResult(e.category, e.name, TestResult.Status.FAIL,
                                  cause == null ? "ExecutionException" : describe(cause), ms);
        } catch (InterruptedException ie) {
            Thread.currentThread().interrupt();
            long ms = (System.nanoTime() - t0) / 1_000_000L;
            return new TestResult(e.category, e.name, TestResult.Status.FAIL, "interrupted", ms);
        }
    }

    private static String describe(Throwable t) {
        StringBuilder sb = new StringBuilder(t.getClass().getSimpleName());
        if (t.getMessage() != null) sb.append(": ").append(t.getMessage());
        StackTraceElement[] st = t.getStackTrace();
        if (st.length > 0) sb.append(" @ ").append(st[0]);
        return sb.toString();
    }

    private void logSummary(Summary s) {
        Log.i(TAG, String.format("SUMMARY pass=%d fail=%d skip=%d total=%d",
                                 s.pass, s.fail, s.skip, s.total()));
        for (TestResult r : s.results) {
            if (r.status == TestResult.Status.FAIL)
                Log.i(TAG, String.format("FAIL test=%s reason=\"%s\"",
                                         r.name, r.reason == null ? "" : r.reason));
            else if (r.status == TestResult.Status.SKIP)
                Log.i(TAG, String.format("SKIP test=%s reason=\"%s\"",
                                         r.name, r.reason == null ? "" : r.reason));
        }
    }
}
