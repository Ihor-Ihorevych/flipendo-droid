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
    private android.view.View startScreen; // the splash with the progress: what the problem cards go back to
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
        startScreen = root;
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

    // The problem screens: a card in the settings panel's style (gold accent, dark card, gold main button). The text scrolls, so a
    // long error never pushes the buttons off a small or landscape screen.
    static final int GOLD = 0xFFE8C15A, CARD = 0xF2181722, MUTED = 0x99FFFFFF;

    private int dp(float v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private android.graphics.drawable.GradientDrawable rounded(int color, float radiusDp, int strokeColor) {
        android.graphics.drawable.GradientDrawable d = new android.graphics.drawable.GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(dp(radiusDp));
        if (strokeColor != 0) {
            d.setStroke(dp(1), strokeColor);
        }
        return d;
    }

    private TextView pill(String label, boolean primary, android.view.View.OnClickListener onClick) {
        TextView b = new TextView(this);
        b.setText(label);
        b.setTextSize(16);
        b.setGravity(Gravity.CENTER);
        b.setTextColor(primary ? 0xFF1A1A1A : 0xFFFFFFFF);
        b.setTypeface(android.graphics.Typeface.DEFAULT, primary ? android.graphics.Typeface.BOLD : android.graphics.Typeface.NORMAL);
        b.setPadding(dp(22), dp(11), dp(22), dp(11));
        b.setBackground(rounded(primary ? GOLD : 0x1FFFFFFF, 24, primary ? 0 : 0x33FFFFFF));
        b.setOnClickListener(onClick);
        return b;
    }

    /** Puts a card on the screen: title, message (scrolls), the main action in gold and a second one. */
    private void showCard(String title, String message, String primaryLabel, android.view.View.OnClickListener primary,
                          String secondaryLabel, android.view.View.OnClickListener secondary) {
        android.widget.FrameLayout screen = new android.widget.FrameLayout(this);
        screen.setBackgroundColor(0xFF000000);
        screen.setPadding(dp(16), dp(16), dp(16), dp(16));

        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(rounded(CARD, 20, 0x33FFFFFF));
        card.setPadding(dp(24), dp(20), dp(24), dp(18));

        // header: a gold accent and the title, the app's name on the right
        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        android.view.View accent = new android.view.View(this);
        accent.setBackground(rounded(GOLD, 2, 0));
        header.addView(accent, new LinearLayout.LayoutParams(dp(4), dp(24)));
        TextView titleView = new TextView(this);
        titleView.setText(title);
        titleView.setTextColor(0xFFFFFFFF);
        titleView.setTextSize(22);
        titleView.setTypeface(android.graphics.Typeface.DEFAULT, android.graphics.Typeface.BOLD);
        titleView.setPadding(dp(12), 0, dp(12), 0);
        header.addView(titleView, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        TextView app = new TextView(this);
        app.setText("Flipendo for Android");
        app.setTextColor(MUTED);
        app.setTextSize(13);
        header.addView(app);
        card.addView(header);

        android.view.View divider = new android.view.View(this);
        divider.setBackgroundColor(0x22FFFFFF);
        LinearLayout.LayoutParams dl = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(1));
        dl.topMargin = dp(14);
        dl.bottomMargin = dp(12);
        card.addView(divider, dl);

        // the message: takes what is left of the screen and scrolls
        android.widget.ScrollView scroll = new android.widget.ScrollView(this);
        TextView body = new TextView(this);
        body.setText(message);
        body.setTextColor(0xDDFFFFFF);
        body.setTextSize(15);
        body.setLineSpacing(0, 1.15f);
        body.setTextIsSelectable(true);
        scroll.addView(body);
        card.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        // footer: the channel on the left, the actions on the right
        LinearLayout footer = new LinearLayout(this);
        footer.setOrientation(LinearLayout.HORIZONTAL);
        footer.setGravity(Gravity.CENTER_VERTICAL);
        TextView channel = new TextView(this);
        channel.setText("t.me/flipendodroid");
        channel.setTextColor(0xFF8CC8FF);
        channel.setTextSize(14);
        channel.setPadding(0, dp(8), dp(8), dp(8));
        channel.setOnClickListener(new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                try {
                    startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(CHANNEL_URL)));
                } catch (RuntimeException ignored) {
                }
            }
        });
        footer.addView(channel, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        if (secondaryLabel != null) {
            LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            sp.rightMargin = dp(12);
            footer.addView(pill(secondaryLabel, false, secondary), sp);
        }
        footer.addView(pill(primaryLabel, true, primary));
        LinearLayout.LayoutParams fl = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        fl.topMargin = dp(16);
        card.addView(footer, fl);

        // the card is as tall as its text needs, at most the screen; at most 760 dp wide
        int width = Math.min(getResources().getDisplayMetrics().widthPixels - dp(32), dp(760));
        android.widget.FrameLayout.LayoutParams cl = new android.widget.FrameLayout.LayoutParams(width,
                android.widget.FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER);
        screen.addView(card, cl);
        setContentView(screen);
    }

    /** Back from a problem card to the start screen (progress, then the game). */
    private void backToStart() {
        setContentView(startScreen);
        bar.setVisibility(android.view.View.VISIBLE);
        status.setText("");
        begin();
    }

    private final android.view.View.OnClickListener sendLogs = new android.view.View.OnClickListener() {
        public void onClick(android.view.View v) {
            LogReport.share(SetupActivity.this);
        }
    };

    /** There is no game on the phone yet: say where to put it (this is the most common first-run problem). */
    private void showMissingGame() {
        showCard("Game data not found",
                "Copy the game folder from your PC (the one that contains System, Maps, Textures, Sounds and Music) to the phone, "
                        + "into Internal storage / FlipendoHP.\n\nThe app looks for " + FlipendoActivity.GAME_DIR + "/System/HP.exe "
                        + "(or one folder deeper).\n\nStill not found after copying? Send the logs: they list what the app sees.",
                "Check again", new android.view.View.OnClickListener() {
                    public void onClick(android.view.View v) {
                        backToStart();
                    }
                },
                "Send logs", sendLogs);
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

    /** The game crashed or stopped: say so, and offer to send the logs right away (the share sheet opens by itself, once). */
    private void showProblem(String text) {
        showCard("Something went wrong",
                text + "\n\nPlease send the logs: they help fix it. Pick an app to send them with (Telegram, mail, ...), "
                        + "or share them in t.me/flipendodroid. A copy is also saved in Download.",
                "Send logs", sendLogs,
                "Play anyway", new android.view.View.OnClickListener() {
                    public void onClick(android.view.View v) {
                        getSharedPreferences("setup", MODE_PRIVATE).edit().putLong("ack", System.currentTimeMillis()).apply();
                        backToStart();
                    }
                });
        ui.postDelayed(new Runnable() {
            public void run() {
                if (!isFinishing()) {
                    LogReport.share(SetupActivity.this);
                }
            }
        }, 700);
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
    static final String MOD_DIR = "mods";
    static final String MOD_MARKER = ".selfpack-mod";       // the mod whose packages are in the System folder ("none": the originals)
    static final String MOD_WANT = ".selfpack-mod-want";    // written by the in-game switch: the mod to have next
    static final String MOD_BACKUP = ".mod-backup";         // the original packages a mod replaced, per mod

    /** The languages this APK carries as {code, label} (the first is the base install); empty for a single-language build. */
    static List<String[]> readLanguages(android.content.Context context) {
        return readPairs(context, LANG_DIR + "/langs.txt");
    }

    /** The mods this APK carries as {id, label}; empty when it has none. */
    static List<String[]> readMods(android.content.Context context) {
        return readPairs(context, MOD_DIR + "/mods.txt");
    }

    /** Reads an asset made of "id|label" lines. */
    static List<String[]> readPairs(android.content.Context context, String asset) {
        List<String[]> out = new ArrayList<>();
        try (InputStream in = context.getAssets().open(asset)) {
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
            // the asset isn't there: nothing to offer
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

    private static String normalizeMod(String id) {
        return id == null || id.isEmpty() ? "none" : id;
    }

    /** The System folder inside the game folder (its case differs between installs). */
    private static File systemDir(File gameDir) {
        File[] children = gameDir.listFiles();
        if (children != null) {
            for (File child : children) {
                if (child.isDirectory() && child.getName().equalsIgnoreCase("system")) {
                    return child;
                }
            }
        }
        return new File(gameDir, "System");
    }

    private static void copyFile(File from, File to, byte[] buffer) throws IOException {
        to.getParentFile().mkdirs();
        try (InputStream in = new java.io.FileInputStream(from); OutputStream os = new FileOutputStream(to)) {
            int n;
            while ((n = in.read(buffer)) > 0) {
                os.write(buffer, 0, n);
            }
        }
    }

    /**
     * Puts a mod's packages into the System folder, or takes the mod out: the originals it replaced are kept in .mod-backup
     * and copied back. MOD_MARKER records what is installed. Called with the game data in place and the mod to have.
     */
    private void applyMod(File gameDir, String wanted, byte[] buffer) throws IOException {
        List<String[]> mods = readMods(this);
        if (mods.isEmpty()) {
            return;
        }
        File marker = new File(gameDir, MOD_MARKER);
        String current = normalizeMod(readSmallFile(marker));
        wanted = normalizeMod(wanted);
        boolean known = false;
        for (String[] m : mods) {
            known |= m[0].equals(wanted);
        }
        if (!known) {
            wanted = "none"; // a mod this APK doesn't carry
        }
        if (current.equals(wanted)) {
            return;
        }
        File system = systemDir(gameDir);
        File backups = new File(gameDir, MOD_BACKUP);
        if (!current.equals("none")) {
            File[] originals = new File(backups, current).listFiles();
            if (originals != null) {
                for (File original : originals) {
                    say("Restoring " + original.getName(), 0, 1);
                    copyFile(original, new File(system, original.getName()), buffer);
                }
            }
        }
        if (!wanted.equals("none")) {
            List<String> files = new ArrayList<>();
            collect(MOD_DIR + "/" + wanted, files);
            int done = 0;
            for (String path : files) {
                String name = path.substring(path.lastIndexOf('/') + 1);
                say("Installing the mod " + (done + 1) + " / " + files.size() + "\n" + name, done, files.size());
                File target = new File(system, name);
                if (target.exists()) {
                    copyFile(target, new File(new File(backups, wanted), name), buffer);
                }
                copyAsset(path, target, buffer);
                done++;
            }
        }
        writeSmallFile(marker, wanted);
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
            // the mod to have: the in-game wish, else what is installed (new game data brings the originals' packages back)
            String modWanted = readSmallFile(new File(gameDir, MOD_WANT));
            File modMarker = new File(gameDir, MOD_MARKER);
            if (modWanted == null || modWanted.isEmpty()) {
                modWanted = readSmallFile(modMarker);
            }
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
                modMarker.delete(); // the mod's packages were just replaced by the originals
                applyMod(gameDir, modWanted, buffer);
                writeSmallFile(marker, stamp);
            } else {
                if (!languages.isEmpty() && !wanted.equals(installed)) {
                    installLanguage(gameDir, wanted, label, buffer);
                }
                applyMod(gameDir, modWanted, buffer);
            }
            File modWish = new File(gameDir, MOD_WANT);
            if (modWish.exists()) {
                modWish.delete();
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
