package com.umbrly.sandbox;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.Map;

public class MainActivity extends Activity {
    private static final String PREFS = "scripts";
    private static final int REQUEST_OPEN_SCRIPT = 42;
    private static final int BLACK = 0xff111111;
    private static final int WHITE = 0xffffffff;
    private static final int SOFT = 0xfff4f4f4;

    private final LinkedHashMap<String, Script> scripts = new LinkedHashMap<>();
    private SharedPreferences prefs;
    private LinearLayout root;

    private static final class Script {
        final String name;
        String source;
        final boolean asset;

        Script(String name, String source, boolean asset) {
            this.name = name;
            this.source = source;
            this.asset = asset;
        }
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        loadScripts();
        showHome();
    }

    private void showHome() {
        root = baseRoot();
        root.addView(header(null));

        ScrollView scroll = new ScrollView(this);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(18), dp(24), dp(18), dp(18));
        scroll.addView(content);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.TOP);
        content.addView(row, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        for (Script script : scripts.values()) {
            row.addView(scriptTile(script));
        }
        row.addView(newTile());

        root.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        setContentView(root);
    }

    private void showEditor(Script script) {
        root = baseRoot();
        root.addView(header(script.name));

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(14), dp(14), dp(14), dp(14));
        root.addView(content, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        actions.setGravity(Gravity.CENTER_VERTICAL);
        content.addView(actions);

        TextView back = button("Back", false);
        back.setOnClickListener(v -> showHome());
        actions.addView(back, new LinearLayout.LayoutParams(dp(82), dp(44)));

        TextView run = button("Run", true);
        actions.addView(run, new LinearLayout.LayoutParams(dp(82), dp(44)));

        TextView save = button("Save", false);
        actions.addView(save, new LinearLayout.LayoutParams(dp(82), dp(44)));

        EditText code = editor(script.source, true);
        content.addView(label("Code"));
        content.addView(code, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1.35f));

        EditText input = editor("", false);
        input.setHint("Input lines for INPUT:");
        content.addView(label("Input"));
        content.addView(input, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(92)));

        TextView output = outputBox();
        content.addView(label("Output"));
        content.addView(output, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 0.9f));

        save.setOnClickListener(v -> {
            script.source = code.getText().toString();
            prefs.edit().putString(script.name, script.source).apply();
            output.setText("Saved: " + script.name);
        });

        run.setOnClickListener(v -> {
            script.source = code.getText().toString();
            prefs.edit().putString(script.name, script.source).apply();
            output.setText(UmbrlyNative.runScript(script.source, input.getText().toString()));
        });

        setContentView(root);
    }

    private LinearLayout baseRoot() {
        LinearLayout view = new LinearLayout(this);
        view.setOrientation(LinearLayout.VERTICAL);
        view.setBackgroundColor(WHITE);
        return view;
    }

    private View header(String subtitle) {
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(dp(18), 0, dp(12), 0);
        bar.setBackgroundColor(WHITE);

        TextView menu = new TextView(this);
        menu.setText("☰");
        menu.setTextColor(BLACK);
        menu.setTextSize(38);
        menu.setTypeface(Typeface.DEFAULT_BOLD);
        menu.setGravity(Gravity.CENTER);
        bar.addView(menu, new LinearLayout.LayoutParams(dp(50), dp(76)));

        LinearLayout titles = new LinearLayout(this);
        titles.setOrientation(LinearLayout.VERTICAL);
        titles.setGravity(Gravity.CENTER_VERTICAL);

        TextView title = new TextView(this);
        title.setText("Umbrly Android Sandbox");
        title.setTextColor(BLACK);
        title.setTextSize(24);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        titles.addView(title);

        if (subtitle != null) {
            TextView sub = new TextView(this);
            sub.setText(subtitle);
            sub.setTextColor(0xff555555);
            sub.setTextSize(12);
            titles.addView(sub);
        }

        bar.addView(titles, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.MATCH_PARENT, 1f));

        LinearLayout outer = new LinearLayout(this);
        outer.setOrientation(LinearLayout.VERTICAL);
        outer.addView(bar, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(84)));

        View line = new View(this);
        line.setBackgroundColor(BLACK);
        outer.addView(line, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(4)));
        return outer;
    }

    private View scriptTile(Script script) {
        LinearLayout wrap = new LinearLayout(this);
        wrap.setOrientation(LinearLayout.VERTICAL);
        wrap.setGravity(Gravity.CENTER_HORIZONTAL);
        wrap.setPadding(0, 0, dp(20), 0);

        TextView logo = new TextView(this);
        logo.setBackgroundResource(com.umbrly.sandbox.R.drawable.script_tile);
        logo.setGravity(Gravity.CENTER);
        logo.setText("TUT LOGO\nKOTORE V\nCE PLAGIN");
        logo.setTextColor(BLACK);
        logo.setTextSize(9);
        logo.setTypeface(Typeface.DEFAULT_BOLD);
        wrap.addView(logo, new LinearLayout.LayoutParams(dp(64), dp(64)));

        TextView name = new TextView(this);
        name.setText(script.name);
        name.setTextColor(BLACK);
        name.setTextSize(12);
        name.setTypeface(Typeface.DEFAULT_BOLD);
        name.setGravity(Gravity.CENTER);
        wrap.addView(name, new LinearLayout.LayoutParams(dp(78), dp(36)));

        wrap.setOnClickListener(v -> showEditor(script));
        return wrap;
    }

    private View newTile() {
        LinearLayout wrap = new LinearLayout(this);
        wrap.setOrientation(LinearLayout.VERTICAL);
        wrap.setGravity(Gravity.CENTER_HORIZONTAL);

        TextView plus = new TextView(this);
        plus.setText("+");
        plus.setTextColor(BLACK);
        plus.setTextSize(76);
        plus.setGravity(Gravity.CENTER);
        wrap.addView(plus, new LinearLayout.LayoutParams(dp(78), dp(64)));

        TextView label = new TextView(this);
        label.setText("Create or\nload script");
        label.setTextColor(BLACK);
        label.setTextSize(12);
        label.setTypeface(Typeface.DEFAULT_BOLD);
        label.setGravity(Gravity.CENTER);
        wrap.addView(label, new LinearLayout.LayoutParams(dp(100), dp(44)));

        wrap.setOnClickListener(v -> showCreateDialog());
        return wrap;
    }

    private void showCreateDialog() {
        new AlertDialog.Builder(this)
                .setItems(new String[]{"New script", "Load .umb"}, (dialog, which) -> {
                    if (which == 0) {
                        createNewScript();
                    } else {
                        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                        intent.addCategory(Intent.CATEGORY_OPENABLE);
                        intent.setType("*/*");
                        startActivityForResult(intent, REQUEST_OPEN_SCRIPT);
                    }
                })
                .show();
    }

    private void createNewScript() {
        String name = nextScriptName();
        Script script = new Script(name, "PRINT: \"New Umbrly script\"\n", false);
        scripts.put(name, script);
        prefs.edit().putString(name, script.source).apply();
        showEditor(script);
    }

    private TextView button(String text, boolean primary) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextSize(14);
        view.setTypeface(Typeface.DEFAULT_BOLD);
        view.setGravity(Gravity.CENTER);
        view.setTextColor(primary ? WHITE : BLACK);
        view.setBackgroundResource(primary
                ? com.umbrly.sandbox.R.drawable.button_primary
                : com.umbrly.sandbox.R.drawable.button_secondary);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(dp(82), dp(44));
        params.setMargins(0, 0, dp(8), dp(10));
        view.setLayoutParams(params);
        return view;
    }

    private TextView label(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextColor(BLACK);
        view.setTextSize(12);
        view.setTypeface(Typeface.DEFAULT_BOLD);
        view.setPadding(0, dp(8), 0, dp(4));
        return view;
    }

    private EditText editor(String text, boolean code) {
        EditText edit = new EditText(this);
        edit.setText(text);
        edit.setTextColor(BLACK);
        edit.setTextSize(14);
        edit.setGravity(Gravity.TOP | Gravity.START);
        edit.setTypeface(Typeface.MONOSPACE);
        edit.setPadding(dp(10), dp(8), dp(10), dp(8));
        edit.setBackgroundResource(com.umbrly.sandbox.R.drawable.editor_panel);
        edit.setInputType(InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_FLAG_MULTI_LINE
                | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        edit.setHorizontallyScrolling(code);
        edit.setSingleLine(false);
        return edit;
    }

    private TextView outputBox() {
        TextView view = new TextView(this);
        view.setTextColor(BLACK);
        view.setTextSize(13);
        view.setTypeface(Typeface.MONOSPACE);
        view.setGravity(Gravity.TOP | Gravity.START);
        view.setPadding(dp(10), dp(8), dp(10), dp(8));
        view.setBackgroundResource(com.umbrly.sandbox.R.drawable.editor_panel);
        return view;
    }

    private void loadScripts() {
        scripts.clear();
        addAsset("test", "scripts/test.umb");
        addAsset("calculator", "scripts/calculator.umb");

        Map<String, ?> saved = prefs.getAll();
        ArrayList<String> names = new ArrayList<>(saved.keySet());
        names.sort(String::compareToIgnoreCase);
        for (String name : names) {
            Object value = saved.get(name);
            if (value instanceof String) {
                scripts.put(name, new Script(name, (String) value, false));
            }
        }
    }

    private void addAsset(String name, String path) {
        scripts.put(name, new Script(name, readAsset(path), true));
    }

    private String readAsset(String path) {
        StringBuilder out = new StringBuilder();
        try (InputStream stream = getAssets().open(path);
             BufferedReader reader = new BufferedReader(
                     new InputStreamReader(stream, StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                out.append(line).append('\n');
            }
        } catch (IOException error) {
            out.append("PRINT: \"Cannot load ").append(path).append("\"\n");
        }
        return out.toString();
    }

    private String readUri(Uri uri) throws IOException {
        StringBuilder out = new StringBuilder();
        try (InputStream stream = getContentResolver().openInputStream(uri)) {
            if (stream == null) {
                throw new IOException("empty stream");
            }
            BufferedReader reader = new BufferedReader(
                    new InputStreamReader(stream, StandardCharsets.UTF_8));
            String line;
            while ((line = reader.readLine()) != null) {
                out.append(line).append('\n');
            }
        }
        return out.toString();
    }

    private String nextScriptName() {
        int index = 1;
        while (scripts.containsKey("script" + index)) {
            index++;
        }
        return "script" + index;
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_OPEN_SCRIPT || resultCode != RESULT_OK || data == null) {
            return;
        }

        Uri uri = data.getData();
        if (uri == null) {
            return;
        }

        try {
            String name = nextImportedName(uri);
            Script script = new Script(name, readUri(uri), false);
            scripts.put(name, script);
            prefs.edit().putString(name, script.source).apply();
            showEditor(script);
        } catch (IOException error) {
            Script script = new Script(nextScriptName(),
                    "PRINT: \"Cannot load selected script\"\n", false);
            scripts.put(script.name, script);
            showEditor(script);
        }
    }

    private String nextImportedName(Uri uri) {
        String raw = uri.getLastPathSegment();
        if (raw == null || raw.trim().isEmpty()) {
            raw = nextScriptName();
        }
        int slash = Math.max(raw.lastIndexOf('/'), raw.lastIndexOf(':'));
        if (slash >= 0 && slash + 1 < raw.length()) {
            raw = raw.substring(slash + 1);
        }
        if (raw.endsWith(".umb")) {
            raw = raw.substring(0, raw.length() - 4);
        }
        raw = raw.replaceAll("[^A-Za-z0-9_ -]", "").trim();
        if (raw.isEmpty()) {
            raw = nextScriptName();
        }

        String candidate = raw;
        int index = 2;
        while (scripts.containsKey(candidate)) {
            candidate = raw + " " + index;
            index++;
        }
        return candidate;
    }
}
