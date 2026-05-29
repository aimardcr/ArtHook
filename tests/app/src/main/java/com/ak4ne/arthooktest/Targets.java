package com.ak4ne.arthooktest;

import java.util.concurrent.atomic.AtomicInteger;

// Methods hooked by the test suite. Hooks return the SENTINEL_* values so
// tests can distinguish "original ran" from "hook fired".
public final class Targets {
    public static final int     SENTINEL_INT    = 0x5EED1234;
    public static final long    SENTINEL_LONG   = 0x5EED5EED5EED5EEDL;
    public static final double  SENTINEL_DOUBLE = 12345.6789;
    public static final String  SENTINEL_STRING = "<hooked>";

    // Static methods.
    public static int  staticAdd(int a, int b) { return a + b; }
    public static void staticDoNothing() { staticCallCount.incrementAndGet(); }
    public static final AtomicInteger staticCallCount = new AtomicInteger();

    // Primitive arg/return coverage.
    public boolean argBoolean(boolean v) { return !v; }
    public byte    argByte(byte v)       { return (byte)(v + 1); }
    public char    argChar(char v)       { return (char)(v + 1); }
    public short   argShort(short v)     { return (short)(v + 1); }
    public int     argInt(int v)         { return v + 1; }
    public long    argLong(long v)       { return v + 1; }
    public float   argFloat(float v)     { return v + 1.0f; }
    public double  argDouble(double v)   { return v + 1.0; }

    // Mixed long/double, exercises 64-bit arg passing.
    public long    longDoubleInt(long a, double b, int c) { return a + (long)b + c; }
    public double  manyDoubles(double a, double b, double c, double d) { return a + b + c + d; }

    // Eight ints, forces stack-passed args on arm64.
    public int     eightInts(int a, int b, int c, int d, int e, int f, int g, int h) {
        return a + b + c + d + e + f + g + h;
    }

    // Object-typed.
    public String  concat(String s)              { return "[" + s + "]"; }
    public Object  returnNull()                  { return null; }
    public int     measureString(String s)       { return s == null ? -1 : s.length(); }
    public boolean isObjectNull(Object o)        { return o == null; }
    public int     countNullsInArray(Object[] xs) {
        int n = 0; for (Object x : xs) if (x == null) n++; return n;
    }

    // Modifiers.
    public final  int  finalReturn42()           { return 42; }
    public synchronized int syncIncrement()      { return ++syncCounter; }
    private int syncCounter = 0;

    public  int  publicM()    { return 1; }
    private int  privateM()   { return 2; }
    protected int protectedM(){ return 3; }
    int           packagePrivateM() { return 4; }
    // Accessors so tests in other packages can reach the wrapped methods.
    public int  callPrivateM()        { return privateM(); }
    public int  callPackagePrivateM() { return packagePrivateM(); }
    public int  callProtectedM()      { return protectedM(); }

    // Throwing.
    public int  throwsRuntime()  { throw new RuntimeException("orig"); }

    // Flag lives on the outer class so the test can check it without
    // touching Clinitable and triggering its <clinit>.
    public static volatile boolean clinitableLoaded = false;
    public static final class Clinitable {
        public static int firstSeen;
        static {
            firstSeen = clinitTarget();
            clinitableLoaded = true;
        }
        public static int clinitTarget()   { return 100; }
        public static int clinitDelegate() { return clinitTarget(); }
    }

    // Final class.
    public static final class FinalGreeter {
        public String greet() { return "hello-final"; }
    }

    // Inheritance / polymorphism.
    public static class Parent {
        public String describe() { return "Parent"; }
    }
    public static class Child extends Parent {
        @Override public String describe() { return "Child"; }
    }

    // Interface with default.
    public interface DefaultGreeter {
        default String defaultGreet() { return "default-greet"; }
    }
    public static class UsesDefault implements DefaultGreeter {}

    // Abstract.
    public static abstract class WithAbstract {
        public abstract int abstractM();
    }
    public static class ConcreteAbstract extends WithAbstract {
        @Override public int abstractM() { return 7; }
    }

    // Constructor target.
    public static final class CtorTarget {
        public int initVal;
        public CtorTarget(int v) { this.initVal = v + 1; }
    }

    // Native-target tests.
    public static native int nativeAddJni(int a, int b);
}

class PackagePrivateClass {
    String hello() { return "pp-hello"; }
}
