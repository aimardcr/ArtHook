package com.ak4ne.arthooktest.testkit;

public final class TestResult {
    public enum Status { PASS, FAIL, SKIP }

    public final String category;
    public final String name;
    public final Status status;
    public final String reason;
    public final long durationMs;

    public TestResult(String category, String name, Status status, String reason, long durationMs) {
        this.category   = category;
        this.name       = name;
        this.status     = status;
        this.reason     = reason;
        this.durationMs = durationMs;
    }

    @Override public String toString() {
        return String.format("[%s] %s/%s (%dms)%s",
                status, category, name, durationMs,
                reason == null ? "" : " — " + reason);
    }
}
