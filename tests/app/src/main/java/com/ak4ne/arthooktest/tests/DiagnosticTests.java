package com.ak4ne.arthooktest.tests;

import android.os.Build;
import android.util.Log;

import com.ak4ne.arthooktest.testkit.NativeBridge;
import com.ak4ne.arthooktest.testkit.TestRunner;

public final class DiagnosticTests {
    public static final String CAT = "Diagnostics";

    public static void register(TestRunner r) {
        r.add(CAT, "print_layout_and_device", () -> {
            String info = NativeBridge.layoutInfo();
            String header =
                  "device: " + Build.MANUFACTURER + " " + Build.MODEL + "\n"
                + "fingerprint: " + Build.FINGERPRINT + "\n"
                + "api: " + Build.VERSION.SDK_INT + " (" + Build.VERSION.RELEASE + ")\n"
                + "supportedAbis: " + java.util.Arrays.toString(Build.SUPPORTED_ABIS) + "\n"
                + "device.abi: " + (Build.SUPPORTED_ABIS.length > 0 ? Build.SUPPORTED_ABIS[0] : "?") + "\n";
            Log.i(TestRunner.TAG, "DIAGNOSTICS\n" + header + info);
        });
    }
}
