package com.ak4ne.arthooktest.tests;

import com.ak4ne.arthooktest.Targets;
import com.ak4ne.arthooktest.testkit.Assert;
import com.ak4ne.arthooktest.testkit.NativeBridge;
import com.ak4ne.arthooktest.testkit.TestRunner;

public final class ArgTests {
    public static final String CAT = "Args";

    public static void register(TestRunner r) {
        r.add(CAT, "arg_boolean", () -> {
            Targets t = new Targets();
            NativeBridge.installOrSkip("arg_boolean");
            try {
                t.argBoolean(true);
                Assert.expectEq(1L, NativeBridge.lastLongArg("arg_boolean", 0), "true");
                t.argBoolean(false);
                Assert.expectEq(0L, NativeBridge.lastLongArg("arg_boolean", 0), "false");
            } finally { NativeBridge.uninstallHook("arg_boolean"); }
        });

        r.add(CAT, "arg_byte", () -> {
            Targets t = new Targets();
            NativeBridge.installOrSkip("arg_byte");
            try {
                t.argByte((byte)0x7F);
                Assert.expectEq(0x7FL, NativeBridge.lastLongArg("arg_byte", 0), "pos");
                t.argByte((byte)-1);
                Assert.expectEq(-1L, NativeBridge.lastLongArg("arg_byte", 0), "neg (signed)");
            } finally { NativeBridge.uninstallHook("arg_byte"); }
        });

        r.add(CAT, "arg_char", () -> {
            Targets t = new Targets();
            NativeBridge.installOrSkip("arg_char");
            try {
                t.argChar('Z');
                Assert.expectEq((long)'Z', NativeBridge.lastLongArg("arg_char", 0));
            } finally { NativeBridge.uninstallHook("arg_char"); }
        });

        r.add(CAT, "arg_short", () -> {
            Targets t = new Targets();
            NativeBridge.installOrSkip("arg_short");
            try {
                t.argShort((short)-12345);
                Assert.expectEq(-12345L, NativeBridge.lastLongArg("arg_short", 0));
            } finally { NativeBridge.uninstallHook("arg_short"); }
        });

        r.add(CAT, "arg_int", () -> {
            Targets t = new Targets();
            NativeBridge.installOrSkip("arg_int_alt");
            try {
                t.argInt(0x12345678);
                Assert.expectEq(0x12345678L, NativeBridge.lastLongArg("arg_int_alt", 0));
            } finally { NativeBridge.uninstallHook("arg_int_alt"); }
        });

        r.add(CAT, "arg_long", () -> {
            Targets t = new Targets();
            NativeBridge.installOrSkip("arg_long");
            try {
                t.argLong(0x0123456789ABCDEFL);
                Assert.expectEq(0x0123456789ABCDEFL,
                                NativeBridge.lastLongArg("arg_long", 0), "preserves all 64 bits");
            } finally { NativeBridge.uninstallHook("arg_long"); }
        });

        r.add(CAT, "arg_float", () -> {
            Targets t = new Targets();
            NativeBridge.installOrSkip("arg_float");
            try {
                t.argFloat(2.5f);
                Assert.expectEq(2.5, NativeBridge.lastDoubleArg("arg_float", 0), 1e-6, "value");
            } finally { NativeBridge.uninstallHook("arg_float"); }
        });

        r.add(CAT, "arg_double", () -> {
            Targets t = new Targets();
            NativeBridge.installOrSkip("arg_double");
            try {
                t.argDouble(3.141592653589793);
                Assert.expectEq(3.141592653589793,
                                NativeBridge.lastDoubleArg("arg_double", 0), 1e-12);
            } finally { NativeBridge.uninstallHook("arg_double"); }
        });

        r.add(CAT, "arg_string_readable_in_hook", () -> {
            Targets t = new Targets();
            NativeBridge.installOrSkip("instance_string_concat");
            try {
                t.concat("hello-jni");
                Assert.expectEq("hello-jni",
                                NativeBridge.lastStringArg("instance_string_concat", 0),
                                "string readable");
            } finally { NativeBridge.uninstallHook("instance_string_concat"); }
        });

        r.add(CAT, "arg_null_object", () -> {
            Targets t = new Targets();
            NativeBridge.installOrSkip("is_object_null");
            try {
                t.isObjectNull(null);
                Assert.expectEq(1L, NativeBridge.lastLongArg("is_object_null", 0), "saw null");
            } finally { NativeBridge.uninstallHook("is_object_null"); }
        });

        r.add(CAT, "arg_long_string_1mb", 15_000, () -> {
            Targets t = new Targets();
            StringBuilder sb = new StringBuilder(1024 * 1024);
            for (int i = 0; i < 1024 * 1024; i++) sb.append('x');
            String big = sb.toString();
            NativeBridge.installOrSkip("measure_string");
            try {
                t.measureString(big);
                Assert.expectEq(1024 * 1024, NativeBridge.lastStringLength("measure_string"),
                                "1MB string length");
            } finally { NativeBridge.uninstallHook("measure_string"); }
        });

        r.add(CAT, "arg_object_array_mixed_null", () -> {
            Targets t = new Targets();
            Object[] mixed = { "a", null, "b", null, "c" };
            NativeBridge.installOrSkip("count_nulls");
            try {
                t.countNullsInArray(mixed);
                Assert.expectEq(5, NativeBridge.lastObjectArrayLen("count_nulls"), "array len");
                Assert.expectTrue(NativeBridge.lastObjectArrayHadNull("count_nulls"),
                                  "should detect nulls");
            } finally { NativeBridge.uninstallHook("count_nulls"); }
        });
    }
}
