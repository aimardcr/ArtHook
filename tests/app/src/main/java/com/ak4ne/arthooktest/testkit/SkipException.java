package com.ak4ne.arthooktest.testkit;

public class SkipException extends RuntimeException {
    public SkipException(String reason) { super(reason); }
}
