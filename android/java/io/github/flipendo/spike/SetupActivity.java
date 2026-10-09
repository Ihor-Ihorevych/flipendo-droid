package io.github.flipendo.spike;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * Launcher activity. Gets "All files access", and when this APK carries the game ("self pack", assets/gamedata/),
 * unpacks it into the game folder with a progress bar before starting the game. A plain APK has no bundled data and
 * goes straight on. Files that already exist are never overwritten (saves, ini files, the user's own copy).
 */
public class SetupActivity extends Activity {
    private static final String BUNDLE = "gamedata";
    private static final String MARKER = ".selfpack-installed";

    private final Handler ui = new Handler(Looper.getMainLooper());
    private TextView status;
    private ProgressBar bar;
    private boolean askedPermission;
    private boolean started;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(0xFF000000);
        root.setPadding(64, 64, 64, 64);

        status = new TextView(this);
        status.setTextColor(0xFFFFFFFF);
        status.setTextSize(18);
        status.setGravity(Gravity.CENTER);
        status.setText("Flipendo");
        root.addView(status);

        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setIndeterminate(false);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.topMargin = 48;
        root.addView(bar, lp);
        setContentView(root);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (started) {
            return;
        }
        if (!Environment.isExternalStorageManager()) {
            status.setText("Allow \"All files access\" for Flipendo, then come back.");
            if (!askedPermission) {
                askedPermission = true;
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            }
            return;
        }
        started = true;
        String problem = previousProblem();
        if (problem != null) {
            showProblem(problem);
        } else {
            begin();
        }
    }

    /** Unpacks the game data if this APK carries it, then starts the game. */
    private void begin() {
        new Thread(new Runnable() {
            public void run() {
                installGameData();
                ui.post(new Runnable() {
                    public void run() {
                        startActivity(new Intent(SetupActivity.this, FlipendoActivity.class));
                        finish();
                    }
                });
            }
        }).start();
    }

    /** Set when the game stopped on its own (FlipendoActivity.onGameStopped) or the last run of the app crashed. */
    private String previousProblem() {
        String stopped = getIntent().getStringExtra("stopped");
        if (stopped != null) {
            String fatal = LogReport.lastFatal();
            return "The game stopped (" + stopped + ")." + (fatal != null ? "\n\n" + fatal : "");
        }
        if (Build.VERSION.SDK_INT >= 30) {
            long acknowledged = getSharedPreferences("flipendo", MODE_PRIVATE).getLong("ack", 0);
            ActivityManager am = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
            for (ApplicationExitInfo info : am.getHistoricalProcessExitReasons(getPackageName(), 0, 3)) {
                int reason = info.getReason();
                if (info.getTimestamp() > acknowledged
                        && (reason == ApplicationExitInfo.REASON_CRASH_NATIVE || reason == ApplicationExitInfo.REASON_CRASH
                        || reason == ApplicationExitInfo.REASON_ANR || reason == ApplicationExitInfo.REASON_INITIALIZATION_FAILURE)) {
                    return "The game crashed last time (" + LogReport.exitReason(reason) + ").";
                }
            }
        }
        return null;
    }

    private void showProblem(String text) {
        status.setText(text + "\n\nPlease send the report to the developer.");
        bar.setVisibility(android.view.View.GONE);
        LinearLayout root = (LinearLayout) status.getParent();
        android.widget.Button send = new android.widget.Button(this);
        send.setText("Send logs");
        send.setOnClickListener(new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                LogReport.share(SetupActivity.this);
            }
        });
        android.widget.Button play = new android.widget.Button(this);
        play.setText("Play anyway");
        play.setOnClickListener(new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                getSharedPreferences("flipendo", MODE_PRIVATE).edit().putLong("ack", System.currentTimeMillis()).apply();
                bar.setVisibility(android.view.View.VISIBLE);
                begin();
            }
        });
        root.addView(send);
        root.addView(play);
    }

    private void say(final String text, final int done, final int total) {
        ui.post(new Runnable() {
            public void run() {
                status.setText(text);
                bar.setMax(Math.max(total, 1));
                bar.setProgress(done);
            }
        });
    }

    private void collect(String dir, List<String> out) throws IOException {
        String[] names = getAssets().list(dir);
        if (names == null) {
            return;
        }
        for (String name : names) {
            String path = dir + "/" + name;
            String[] children = getAssets().list(path);
            if (children != null && children.length > 0) {
                collect(path, out);
            } else {
                out.add(path);
            }
        }
    }

    private String readStamp() {
        try (InputStream in = getAssets().open(BUNDLE + "/.stamp")) {
            byte[] data = new byte[64];
            int n = in.read(data);
            return new String(data, 0, Math.max(n, 0)).trim();
        } catch (IOException e) {
            return null;
        }
    }

    private void installGameData() {
        String stamp = readStamp();
        if (stamp == null) {
            return; // not a self pack
        }
        File gameDir = new File(FlipendoActivity.GAME_DIR);
        File marker = new File(gameDir, MARKER);
        try {
            if (marker.exists()) {
                try (InputStream in = new java.io.FileInputStream(marker)) {
                    byte[] data = new byte[64];
                    int n = in.read(data);
                    if (stamp.equals(new String(data, 0, Math.max(n, 0)).trim())) {
                        return; // this exact game data is already unpacked
                    }
                }
            }

            List<String> files = new ArrayList<>();
            collect(BUNDLE, files);
            byte[] buffer = new byte[1 << 20];
            int done = 0;
            for (String path : files) {
                String relative = path.substring(BUNDLE.length() + 1);
                if (relative.equals(".stamp")) {
                    done++;
                    continue;
                }
                File out = new File(gameDir, relative);
                say("Installing game data " + (done + 1) + " / " + files.size() + "\n" + relative, done, files.size());
                if (!out.exists()) {
                    out.getParentFile().mkdirs();
                    File temp = new File(out.getPath() + ".part");
                    try (InputStream in = getAssets().open(path); OutputStream os = new FileOutputStream(temp)) {
                        int n;
                        while ((n = in.read(buffer)) > 0) {
                            os.write(buffer, 0, n);
                        }
                    }
                    temp.renameTo(out);
                }
                done++;
            }
            try (OutputStream os = new FileOutputStream(marker)) {
                os.write(stamp.getBytes());
            }
        } catch (IOException e) {
            android.util.Log.e("flipendo", "installing game data failed: " + e);
            say("Installing the game data failed: " + e.getMessage(), 0, 1);
            try {
                Thread.sleep(8000);
            } catch (InterruptedException ignored) {
            }
        }
    }
}
