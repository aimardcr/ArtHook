package com.ak4ne.arthooktest.testkit;

// Native test bridge. Tests are identified by short string keys mapped on
// the native side to a target {class, name, signature} and replacement.
// installHook / uninstallHook return arthook::Status as int (0 = kOk).
public final class NativeBridge {
    static { System.loadLibrary("arthooktest"); }
    private NativeBridge() {}

    public static native int    installHook(String testKey);
    public static native int    installHookOnReflected(String testKey, java.lang.reflect.Method m);
    public static native int    uninstallHook(String testKey);
    public static native void   resetState(String testKey);
    public static native boolean hasJniBridge();
    public static native boolean targetIsNative(String testKey);

    public static native int    fireCount(String testKey);
    public static native long   lastLongArg(String testKey, int idx);
    public static native double lastDoubleArg(String testKey, int idx);
    public static native String lastStringArg(String testKey, int idx);
    public static native int    lastObjectArrayLen(String testKey);
    public static native boolean lastObjectArrayHadNull(String testKey);
    public static native int    lastStringLength(String testKey);

    // Backup invocation (works for both native and non-native targets).
    public static native String invokeBackupStringInst(String testKey, Object thiz, String arg);
    public static native int    invokeBackupIntInst(String testKey, Object thiz, int arg);

    // Diagnostics.
    public static native String layoutInfo();
    public static native long   processRssKb();
    public static native int    openFdCount();
    public static native int    trampolinePagesInUse();

    // Init path tests.
    public static native int    initializeAgain();

    /**
     * Install a hook, but skip the test if the JNI bridge wasn't captured
     * AND the target is non-native (so it would need the bridge). Native
     * targets go through unconditionally.
     */
    public static void installOrSkip(String key) {
        if (!targetIsNative(key) && !hasJniBridge())
            throw new SkipException(
                    "JNI bridge unavailable on this device; non-native hooks disabled");
        int rc = installHook(key);
        if (rc != 0)
            throw new AssertionError("installHook(" + key + ") rc: expected 0 got " + rc);
    }
}
