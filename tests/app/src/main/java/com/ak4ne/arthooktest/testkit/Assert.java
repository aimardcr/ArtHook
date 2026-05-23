package com.ak4ne.arthooktest.testkit;

import java.util.Arrays;

public final class Assert {
    private Assert() {}

    public static void skip(String reason) { throw new SkipException(reason); }

    public static void expectTrue(boolean cond) {
        if (!cond) throw new AssertionError("expected true");
    }

    public static void expectTrue(boolean cond, String msg) {
        if (!cond) throw new AssertionError(msg);
    }

    public static void expectFalse(boolean cond, String msg) {
        if (cond) throw new AssertionError(msg);
    }

    public static void expectEq(int expected, int actual) {
        if (expected != actual) throw new AssertionError("expected " + expected + " got " + actual);
    }

    public static void expectEq(int expected, int actual, String msg) {
        if (expected != actual) throw new AssertionError(msg + ": expected " + expected + " got " + actual);
    }

    public static void expectEq(long expected, long actual, String msg) {
        if (expected != actual) throw new AssertionError(msg + ": expected " + expected + " got " + actual);
    }

    public static void expectEq(long expected, long actual) {
        if (expected != actual) throw new AssertionError("expected " + expected + " got " + actual);
    }

    public static void expectEq(double expected, double actual, double eps, String msg) {
        if (Math.abs(expected - actual) > eps)
            throw new AssertionError(msg + ": expected " + expected + " got " + actual);
    }

    public static void expectEq(double expected, double actual, double eps) {
        if (Math.abs(expected - actual) > eps)
            throw new AssertionError("expected " + expected + " got " + actual);
    }

    public static void expectEq(Object expected, Object actual, String msg) {
        if (expected == null ? actual != null : !expected.equals(actual))
            throw new AssertionError(msg + ": expected " + expected + " got " + actual);
    }

    public static void expectEq(Object expected, Object actual) {
        if (expected == null ? actual != null : !expected.equals(actual))
            throw new AssertionError("expected " + expected + " got " + actual);
    }

    public static void expectNull(Object o, String msg) {
        if (o != null) throw new AssertionError(msg + ": expected null got " + o);
    }

    public static void expectNonNull(Object o, String msg) {
        if (o == null) throw new AssertionError(msg + ": expected non-null");
    }

    public static void expectArrayEq(byte[] expected, byte[] actual, String msg) {
        if (!Arrays.equals(expected, actual)) throw new AssertionError(msg + ": arrays differ");
    }

    public interface ThrowingRunnable { void run() throws Throwable; }

    public static <T extends Throwable> T expectThrows(Class<T> type, ThrowingRunnable r) {
        try {
            r.run();
        } catch (Throwable t) {
            if (type.isInstance(t)) return type.cast(t);
            throw new AssertionError("expected " + type.getName() + ", got " + t);
        }
        throw new AssertionError("expected " + type.getName() + ", nothing thrown");
    }
}
