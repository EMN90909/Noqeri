package org.noqeri.mobile;

import android.app.Activity;
import android.os.Bundle;
import android.graphics.Typeface;
import android.view.View;
import android.view.inputmethod.InputMethodManager;
import android.content.Context;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    static {
        System.loadLibrary("noqeri_android");
    }

    private native String evaluate(String source, boolean execute);

    private final ExecutorService evaluator = Executors.newSingleThreadExecutor();
    private EditText editor;
    private TextView result;
    private Button checkButton;
    private Button runButton;

    private int dip(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setPadding(dip(16), dip(24), dip(16), dip(12));

        TextView heading = new TextView(this);
        heading.setText("Noqeri");
        heading.setTextSize(25);
        heading.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        column.addView(heading);

        TextView description = new TextView(this);
        description.setText("Local Noqeri syntax checking and reference execution. Experimental Stage-0 compiler.");
        description.setPadding(0, dip(8), 0, dip(12));
        column.addView(description);

        editor = new EditText(this);
        editor.setText("function add(a: int, b: int): int {\n    return a + b\n}\nprint(add(10, 20))\n");
        editor.setTypeface(Typeface.MONOSPACE);
        editor.setTextSize(14);
        editor.setGravity(android.view.Gravity.TOP);
        editor.setSingleLine(false);
        editor.setMinLines(10);
        editor.setContentDescription("Noqeri source editor");
        column.addView(editor, new LinearLayout.LayoutParams(-1, 0, 2f));

        LinearLayout actions = new LinearLayout(this);
        checkButton = new Button(this);
        checkButton.setText("Check");
        runButton = new Button(this);
        runButton.setText("Run");
        actions.addView(checkButton, new LinearLayout.LayoutParams(0, -2, 1));
        actions.addView(runButton, new LinearLayout.LayoutParams(0, -2, 1));
        column.addView(actions);

        result = new TextView(this);
        result.setText("Ready. Enter Noqeri code and tap Check or Run.");
        result.setTextIsSelectable(true);
        result.setTypeface(Typeface.MONOSPACE);
        result.setPadding(0, dip(12), 0, dip(12));
        ScrollView output = new ScrollView(this);
        output.addView(result);
        column.addView(output, new LinearLayout.LayoutParams(-1, 0, 1f));

        checkButton.setOnClickListener(view -> submit(false));
        runButton.setOnClickListener(view -> submit(true));
        setContentView(column);
    }

    private void submit(boolean execute) {
        final String code = editor.getText().toString();
        checkButton.setEnabled(false);
        runButton.setEnabled(false);
        result.setText(execute ? "Executing locally..." : "Checking...");
        evaluator.execute(() -> {
            String output;
            try {
                output = evaluate(code, execute);
            } catch (Exception exception) {
                output = "Noqeri error: " + exception.getMessage();
            }
            final String message = output;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                result.setText(message);
                checkButton.setEnabled(true);
                runButton.setEnabled(true);
            });
        });
    }

    @Override
    protected void onDestroy() {
        evaluator.shutdownNow();
        super.onDestroy();
    }
}
