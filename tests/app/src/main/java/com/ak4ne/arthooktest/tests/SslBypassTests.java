package com.ak4ne.arthooktest.tests;

import com.ak4ne.arthooktest.testkit.Assert;
import com.ak4ne.arthooktest.testkit.NativeBridge;
import com.ak4ne.arthooktest.testkit.TestRunner;

import java.lang.reflect.Method;

import kotlin.jvm.functions.Function0;
import okhttp3.CertificatePinner;

public final class SslBypassTests {
    public static final String CAT = "SSL";

    public static void register(TestRunner r) {
        // Pin a host to a deliberately-wrong fingerprint so the real check
        // would throw SSLPeerUnverifiedException; the hook should swallow it.
        r.add(CAT, "okhttp_certificate_pinner_bypass", () -> {
            CertificatePinner pinner = new CertificatePinner.Builder()
                    .add("example.com",
                         "sha256/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")
                    .build();

            // check$okhttp(String host, Function0<List<Certificate>> cleanedCerts)
            Method check = CertificatePinner.class.getDeclaredMethod(
                    "check$okhttp", String.class, Function0.class);
            check.setAccessible(true);

            // Build a Function0 that returns an EMPTY list of certs, the real
            // checker compares this against the pin set and rejects mismatch.
            Function0<java.util.List<java.security.cert.Certificate>> emptyCerts =
                    () -> java.util.Collections.emptyList();

            // Sanity: without hook, this should throw (wrapped in InvocationTargetException).
            boolean threwWithoutHook = false;
            try {
                check.invoke(pinner, "example.com", emptyCerts);
            } catch (java.lang.reflect.InvocationTargetException ite) {
                threwWithoutHook = ite.getCause() instanceof javax.net.ssl.SSLPeerUnverifiedException;
            }
            Assert.expectTrue(threwWithoutHook,
                    "pre-condition: real check$okhttp must reject empty cert chain");

            NativeBridge.installOrSkip("okhttp_certificate_pinner_bypass");
            try {
                // With hook installed, the exception should be swallowed.
                check.invoke(pinner, "example.com", emptyCerts);
                int fires = NativeBridge.fireCount("okhttp_certificate_pinner_bypass");
                Assert.expectTrue(fires >= 1,
                        "hook should have fired (count=" + fires + ")");
            } catch (java.lang.reflect.InvocationTargetException ite) {
                throw new AssertionError(
                        "hook failed to swallow: " + ite.getCause(), ite.getCause());
            } finally {
                NativeBridge.uninstallHook("okhttp_certificate_pinner_bypass");
            }

            // Post-unhook, the real check should reject again.
            boolean threwAgain = false;
            try {
                check.invoke(pinner, "example.com", emptyCerts);
            } catch (java.lang.reflect.InvocationTargetException ite) {
                threwAgain = ite.getCause() instanceof javax.net.ssl.SSLPeerUnverifiedException;
            }
            Assert.expectTrue(threwAgain,
                    "post-unhook: real check$okhttp must reject again");
        });
    }
}
