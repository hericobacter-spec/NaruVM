package dev.henge.research;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Typeface;
import android.text.Html;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;

/** Offline access to the exact copyright and license files packaged in this APK. */
final class LicenseViewer {
    private static final int LIMIT = 4 * 1024 * 1024;
    static void show(Activity activity) { folder(activity, "licenses", activity.getString(R.string.ui_open_source_licenses)); }
    private static void folder(Activity activity, String path, String title) {
        try {
            String[] children = activity.getAssets().list(path);
            if (children == null) throw new java.io.IOException(activity.getString(R.string.ui_cannot_read_the_list));
            Arrays.sort(children);
            ArrayList<String> names = new ArrayList<>(), targets = new ArrayList<>();
            if (path.equals("licenses")) {
                names.add(activity.getString(R.string.ui_distribution_source_and_modifications)); targets.add("licenses/project/source-info.txt");
                names.add(activity.getString(R.string.ui_original_henge_gplv2_or_later)); targets.add("Henge-LICENSE.txt");
                names.add(activity.getString(R.string.ui_original_mbedtls_notice)); targets.add("MbedTLS-LICENSE.txt");
            }
            for (String child : children) { names.add(child); targets.add(path + "/" + child); }
            new AlertDialog.Builder(activity).setTitle(title)
                .setItems(names.toArray(new String[0]), (dialog, which) -> open(activity, targets.get(which)))
                .setNegativeButton(activity.getString(R.string.ui_close), null).show();
        } catch (java.io.IOException error) { failure(activity, error); }
    }
    private static void open(Activity activity, String path) {
        try {
            String[] children = activity.getAssets().list(path);
            if (children != null && children.length != 0) {
                folder(activity, path, path.substring(path.lastIndexOf('/') + 1)); return;
            }
        } catch (java.io.IOException error) { failure(activity, error); return; }
        TextView text = new TextView(activity); text.setText(activity.getString(R.string.ui_loading));
        text.setTextSize(14); text.setTextIsSelectable(true);
        int pad = Math.round(20 * activity.getResources().getDisplayMetrics().density);
        text.setPadding(pad, pad, pad, pad);
        ScrollView scroll = new ScrollView(activity); scroll.addView(text);
        AlertDialog dialog = new AlertDialog.Builder(activity).setTitle(path.substring(path.lastIndexOf('/') + 1))
            .setView(scroll).setPositiveButton(activity.getString(R.string.ui_list), (d, w) -> show(activity)).setNegativeButton(activity.getString(R.string.ui_close), null).show();
        new Thread(() -> {
            try (InputStream input = activity.getAssets().open(path); ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[16384]; int count;
                while ((count = input.read(buffer)) != -1) {
                    if (bytes.size() + count > LIMIT) throw new java.io.IOException(activity.getString(R.string.ui_notice_file_exceeds_the_size_limit));
                    bytes.write(buffer, 0, count);
                }
                String raw = new String(bytes.toByteArray(), StandardCharsets.UTF_8);
                boolean html = path.endsWith(".html");
                CharSequence content = html ? Html.fromHtml(raw, Html.FROM_HTML_MODE_LEGACY) : raw;
                activity.runOnUiThread(() -> {
                    if (!activity.isFinishing() && dialog.isShowing()) {
                        if (!html) text.setTypeface(Typeface.MONOSPACE);
                        text.setText(content);
                    }
                });
            } catch (java.io.IOException error) {
                activity.runOnUiThread(() -> { if (dialog.isShowing()) text.setText(activity.getString(R.string.ui_read_error) + error.getMessage()); });
            }
        }, "license-reader").start();
    }
    private static void failure(Activity activity, Exception error) {
        new AlertDialog.Builder(activity).setTitle(activity.getString(R.string.ui_license_reading_error)).setMessage(error.getMessage()).setPositiveButton(activity.getString(R.string.ui_close), null).show();
    }
}
