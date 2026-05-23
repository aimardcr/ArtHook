package com.ak4ne.arthooktest;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.SpannableStringBuilder;
import android.text.style.ForegroundColorSpan;
import android.view.View;
import android.widget.Button;
import android.widget.GridLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

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

import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class MainActivity extends AppCompatActivity {

    private TestRunner    runner;
    private TextView      summary;
    private TextView      log;
    private ScrollView    logScroll;
    private final SpannableStringBuilder logBuffer = new SpannableStringBuilder();
    private final Handler ui = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        summary   = findViewById(R.id.summary);
        log       = findViewById(R.id.log);
        logScroll = findViewById(R.id.log_scroll);

        runner = new TestRunner();
        MethodKindTests.register(runner);
        ModifierTests.register(runner);
        ConcurrencyTests.register(runner);
        BackupTests.register(runner);
        ArgTests.register(runner);
        LifecycleTests.register(runner);
        FailureTests.register(runner);
        ResourceTests.register(runner);
        SslBypassTests.register(runner);
        DiagnosticTests.register(runner);

        findViewById(R.id.btn_run_all).setOnClickListener(v -> runAsync(runner.entries()));
        findViewById(R.id.btn_copy).setOnClickListener(this::copyToClipboard);

        GridLayout catButtons = findViewById(R.id.cat_buttons);
        for (String cat : categoriesInOrder()) {
            Button b = new Button(this);
            b.setText(cat);
            b.setAllCaps(false);
            b.setOnClickListener(v -> runAsync(runner.byCategory(cat)));
            GridLayout.LayoutParams lp = new GridLayout.LayoutParams();
            lp.width = 0;
            lp.height = LinearLayout.LayoutParams.WRAP_CONTENT;
            lp.columnSpec = GridLayout.spec(GridLayout.UNDEFINED, 1, 1f);
            lp.rowSpec    = GridLayout.spec(GridLayout.UNDEFINED, 1, 1f);
            b.setLayoutParams(lp);
            catButtons.addView(b);
        }

        appendLine("device: " + Build.MANUFACTURER + " " + Build.MODEL
                   + ", API " + Build.VERSION.SDK_INT, Color.DKGRAY);
        appendLine("registered " + runner.entries().size() + " tests across "
                   + categoriesInOrder().size() + " categories.", Color.DKGRAY);
    }

    private Set<String> categoriesInOrder() {
        Set<String> s = new LinkedHashSet<>();
        for (TestRunner.Entry e : runner.entries()) s.add(e.category);
        return s;
    }

    private void runAsync(List<TestRunner.Entry> entries) {
        clearLog();
        appendLine("running " + entries.size() + " tests…", Color.BLUE);
        summary.setText("running…");

        Thread t = new Thread(() -> {
            TestRunner.Summary s = runner.run(entries,
                    (r, idx, total) -> ui.post(() -> {
                        int color = colorFor(r.status);
                        appendLine(String.format("[%s] %s/%s (%dms)%s",
                                r.status, r.category, r.name, r.durationMs,
                                r.reason == null ? "" : "  — " + r.reason), color);
                    }));
            ui.post(() -> {
                summary.setText(String.format("%d/%d passed  •  %d failed  •  %d skipped",
                        s.pass, s.total(), s.fail, s.skip));
            });
        }, "arthook-test-driver");
        t.setDaemon(true);
        t.start();
    }

    private int colorFor(TestResult.Status st) {
        switch (st) {
            case PASS: return 0xFF0F7B0F;
            case FAIL: return 0xFFB00020;
            case SKIP: return 0xFF8A6D00;
            default:   return Color.BLACK;
        }
    }

    private void clearLog() {
        logBuffer.clear();
        log.setText("");
    }

    private void appendLine(String s, int color) {
        int start = logBuffer.length();
        logBuffer.append(s).append('\n');
        logBuffer.setSpan(new ForegroundColorSpan(color), start, logBuffer.length(),
                          android.text.Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
        log.setText(logBuffer);
        logScroll.post(() -> logScroll.fullScroll(View.FOCUS_DOWN));
    }

    private void copyToClipboard(View v) {
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (cm != null) {
            cm.setPrimaryClip(ClipData.newPlainText("arthook-test results", log.getText()));
            Toast.makeText(this, "results copied", Toast.LENGTH_SHORT).show();
        }
    }
}
