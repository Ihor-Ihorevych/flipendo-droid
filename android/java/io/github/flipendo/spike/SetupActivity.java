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
    private boolean gameReady;  // the game data is there: start the game once the splash has been up long enough
    private long splashUntil;   // uptime until which the splash stays (held while the channel link is open)
    static final String CHANNEL_URL = "https://t.me/flipendodroid";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(0xFF000000);
        root.setPadding(64, 64, 64, 64);

        // the splash: icon, name and the Telegram channel (a tap opens it)
        splashUntil = android.os.SystemClock.uptimeMillis() + 2500;
        try {
            android.widget.ImageView icon = new android.widget.ImageView(this);
            icon.setImageDrawable(getDrawable(getApplicationInfo().icon));
            LinearLayout.LayoutParams ip = new LinearLayout.LayoutParams(256, 256);
            ip.bottomMargin = 32;
            root.addView(icon, ip);
        } catch (RuntimeException ignored) {
        }
        TextView title = new TextView(this);
        title.setTextColor(0xFFFFFFFF);
        title.setTextSize(34);
        title.setGravity(Gravity.CENTER);
        title.setText("Flipendo");
        root.addView(title);
        TextView tagline = new TextView(this);
        tagline.setTextColor(0xFFAAAAAA);
        tagline.setTextSize(15);
        tagline.setGravity(Gravity.CENTER);
        tagline.setText("Harry Potter and the Sorcerer's Stone, on Android");
        root.addView(tagline);
        TextView link = new TextView(this);
        link.setTextColor(0xFF5EB8FF);
        link.setTextSize(18);
        link.setGravity(Gravity.CENTER);
        link.setPadding(32, 40, 32, 40);
        link.setPaintFlags(link.getPaintFlags() | android.graphics.Paint.UNDERLINE_TEXT_FLAG);
        link.setText("Telegram: t.me/flipendodroid");
        link.setOnClickListener(new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                splashUntil = Long.MAX_VALUE; // hold the splash while the channel is open; onResume lets it go
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(CHANNEL_URL)));
            }
        });
        root.addView(link);

        status = new TextView(this);
        status.setTextColor(0xFFFFFFFF);
        status.setTextSize(18);
        status.setGravity(Gravity.CENTER);
        status.setText("");
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
        if (splashUntil == Long.MAX_VALUE) {
            splashUntil = android.os.SystemClock.uptimeMillis() + 800; // back from the channel
            tryStart();
        }
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
                final boolean haveGame = findGame(new File(FlipendoActivity.GAME_DIR), 2) != null;
                ui.post(new Runnable() {
                    public void run() {
                        if (!haveGame) {
                            showMissingGame();
                            return;
                        }
                        gameReady = true;
                        tryStart();
                    }
                });
            }
        }).start();
    }

    private final Runnable launch = new Runnable() {
        public void run() {
            tryStart();
        }
    };

    /** Starts the game once its data is ready and the splash has been up long enough (not while the channel link is open). */
    private void tryStart() {
        ui.removeCallbacks(launch);
        if (!gameReady || isFinishing() || splashUntil == Long.MAX_VALUE) {
            return;
        }
        long left = splashUntil - android.os.SystemClock.uptimeMillis();
        if (left > 0) {
            ui.postDelayed(launch, left);
            return;
        }
        gameReady = false;
        startActivity(new Intent(this, FlipendoActivity.class));
        finish();
    }

    /** The folder with System/HP.exe: the game folder itself, or one or two levels inside it (a copy made one folder too deep). */
    static File findGame(File dir, int depth) {
        if (new File(dir, "System/HP.exe").isFile() || new File(dir, "system/HP.exe").isFile()) {
            return dir;
        }
        File[] children = depth > 0 ? dir.listFiles() : null;
        if (children != null) {
            for (File child : children) {
                if (child.isDirectory() && !child.getName().startsWith(".")) {
                    File found = findGame(child, depth - 1);
                    if (found != null) {
                        return found;
                    }
                }
            }
        }
        return null;
    }

    private LinearLayout buttons;

    private LinearLayout buttonRow() {
        if (buttons == null) {
            buttons = new LinearLayout(this);
            buttons.setOrientation(LinearLayout.VERTICAL);
            ((LinearLayout) status.getParent()).addView(buttons);
        }
        buttons.removeAllViews();
        return buttons;
    }

    private android.widget.Button button(String label, android.view.View.OnClickListener onClick) {
        android.widget.Button b = new android.widget.Button(this);
        b.setText(label);
        b.setOnClickListener(onClick);
        return b;
    }

    /** There is no game on the phone yet: say where to put it (this is the most common first-run problem). */
    private void showMissingGame() {
        status.setText("Game data not found.\n\nCopy the game folder from your PC (the one that contains System, Maps, Textures, "
                + "Sounds and Music) to the phone, into Internal storage / FlipendoHP.\n\n"
                + "The app looks for " + FlipendoActivity.GAME_DIR + "/System/HP.exe.");
        bar.setVisibility(android.view.View.GONE);
        LinearLayout row = buttonRow();
        row.addView(button("Check again", new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                bar.setVisibility(android.view.View.VISIBLE);
                begin();
            }
        }));
        row.addView(button("Send logs", new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                LogReport.share(SetupActivity.this);
            }
        }));
    }

    /** Set when the game stopped on its own (FlipendoActivity.onGameStopped) or the last run of the app crashed. */
    private String previousProblem() {
        if (getIntent().getBooleanExtra("restart", false)) {
            return null; // the game was closed on purpose (the language switch)
        }
        String stopped = getIntent().getStringExtra("stopped");
        if (stopped != null) {
            String fatal = LogReport.lastFatal();
            return "The game stopped (" + stopped + ")." + (fatal != null ? "\n\n" + fatal : "");
        }
        if (Build.VERSION.SDK_INT >= 30) {
            long acknowledged = getSharedPreferences("setup", MODE_PRIVATE).getLong("ack", 0);
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
        LinearLayout row = buttonRow();
        row.addView(button("Send logs", new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                LogReport.share(SetupActivity.this);
            }
        }));
        row.addView(button("Play anyway", new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                getSharedPreferences("setup", MODE_PRIVATE).edit().putLong("ack", System.currentTimeMillis()).apply();
                bar.setVisibility(android.view.View.VISIBLE);
                begin();
            }
        }));
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

    static final String LANG_DIR = "langs";
    static final String LANG_MARKER = ".selfpack-lang"; // the language whose files are in the game folder
    static final String LANG_WANT = ".selfpack-want";   // written by the in-game switch: the language to install next

    /** The languages this APK carries as {code, label} (the first is the base install); empty for a single-language build. */
    static List<String[]> readLanguages(android.content.Context context) {
        List<String[]> out = new ArrayList<>();
        try (InputStream in = context.getAssets().open(LANG_DIR + "/langs.txt")) {
            java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
            byte[] buffer = new byte[1024];
            int n;
            while ((n = in.read(buffer)) > 0) {
                bytes.write(buffer, 0, n);
            }
            for (String line : bytes.toString("UTF-8").split("\n")) {
                String[] parts = line.trim().split("[|]", 2);
                if (parts.length == 2 && !parts[0].isEmpty()) {
                    out.add(parts);
                }
            }
        } catch (IOException e) {
            // no langs.txt: a single-language build
        }
        return out;
    }

    static String readSmallFile(File file) {
        try (InputStream in = new java.io.FileInputStream(file)) {
            byte[] data = new byte[64];
            int n = in.read(data);
            return new String(data, 0, Math.max(n, 0)).trim();
        } catch (IOException e) {
            return null;
        }
    }

    static void writeSmallFile(File file, String text) throws IOException {
        try (OutputStream os = new FileOutputStream(file)) {
            os.write(text.getBytes());
        }
    }

    /** Copies one asset into the game folder, replacing what is there (through a .part file, so a stop leaves no half file). */
    private void copyAsset(String path, File out, byte[] buffer) throws IOException {
        out.getParentFile().mkdirs();
        File temp = new File(out.getPath() + ".part");
        try (InputStream in = getAssets().open(path); OutputStream os = new FileOutputStream(temp)) {
            int n;
            while ((n = in.read(buffer)) > 0) {
                os.write(buffer, 0, n);
            }
        }
        if (out.exists()) {
            out.delete();
        }
        temp.renameTo(out);
    }

    /** Puts one language's files (voices, fonts, texts) into the game folder. */
    private void installLanguage(File gameDir, String code, String label, byte[] buffer) throws IOException {
        List<String> files = new ArrayList<>();
        collect(LANG_DIR + "/" + code, files);
        int done = 0;
        for (String path : files) {
            String relative = path.substring(LANG_DIR.length() + 1 + code.length() + 1);
            say("Switching to " + label + " " + (done + 1) + " / " + files.size() + "\n" + relative, done, files.size());
            copyAsset(path, new File(gameDir, relative), buffer);
            done++;
        }
        writeSmallFile(new File(gameDir, LANG_MARKER), code);
    }

    /**
     * Self pack: puts the game data in place. The files that all languages share are unpacked when the APK's data changes
     * (they replace what is there, but never the saves); the language files are copied when the wanted language is not the
     * installed one (the in-game language switch writes the wish to LANG_WANT).
     */
    private void installGameData() {
        String stamp = readStamp();
        if (stamp == null) {
            return; // not a self pack
        }
        File gameDir = new File(FlipendoActivity.GAME_DIR);
        File marker = new File(gameDir, MARKER);
        try {
            List<String[]> languages = readLanguages(this);
            String installed = readSmallFile(new File(gameDir, LANG_MARKER));
            String wanted = readSmallFile(new File(gameDir, LANG_WANT));
            if (wanted == null || wanted.isEmpty()) {
                wanted = installed;
            }
            String label = wanted;
            boolean known = false;
            for (String[] l : languages) {
                if (l[0].equals(wanted)) {
                    known = true;
                    label = l[1];
                }
            }
            if (!known && !languages.isEmpty()) {
                wanted = languages.get(0)[0]; // never installed, or a language this APK doesn't have: the base one
                label = languages.get(0)[1];
            }
            boolean dataCurrent = marker.exists() && stamp.equals(readSmallFile(marker));
            byte[] buffer = new byte[1 << 20];

            if (!dataCurrent) {
                List<String> files = new ArrayList<>();
                collect(BUNDLE, files);
                int done = 0;
                for (String path : files) {
                    String relative = path.substring(BUNDLE.length() + 1);
                    if (relative.equals(".stamp")) {
                        done++;
                        continue;
                    }
                    File out = new File(gameDir, relative);
                    say("Installing game data " + (done + 1) + " / " + files.size() + "\n" + relative, done, files.size());
                    // saves of the player are never replaced; everything else is the APK's version
                    boolean keep = out.exists() && relative.toLowerCase().startsWith("save/");
                    if (!keep) {
                        copyAsset(path, out, buffer);
                    }
                    done++;
                }
                if (!languages.isEmpty()) {
                    installLanguage(gameDir, wanted, label, buffer);
                }
                writeSmallFile(marker, stamp);
            } else if (!languages.isEmpty() && !wanted.equals(installed)) {
                installLanguage(gameDir, wanted, label, buffer);
            }
            File want = new File(gameDir, LANG_WANT);
            if (want.exists()) {
                want.delete();
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
