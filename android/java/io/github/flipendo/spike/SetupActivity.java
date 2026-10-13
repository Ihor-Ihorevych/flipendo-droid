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
        splashUntil = android.os.SystemClock.uptimeMillis() + 2500;

        // The start screen: the name on one side, the launch panel (renderer, "Remember my choice", progress, Play) on the
        // other; stacked when the screen is too narrow for two columns. Everything in dp/sp, and it scrolls when a short
        // screen has no room (Play stays reachable).
        android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
        float widthDp = Math.max(dm.widthPixels, dm.heightPixels) / dm.density; // the activity is landscape
        boolean wide = widthDp >= 560;
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(wide ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setPadding(dp(28), dp(16), dp(28), dp(16));
        if (wide) {
            root.addView(brand(true), new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
            LinearLayout.LayoutParams pl = new LinearLayout.LayoutParams(dp(Math.min(380f, widthDp * 0.46f)), LinearLayout.LayoutParams.WRAP_CONTENT);
            pl.leftMargin = dp(24);
            root.addView(launchPanel(), pl);
        } else {
            root.addView(brand(false), new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
            LinearLayout.LayoutParams pl = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            pl.topMargin = dp(16);
            root.addView(launchPanel(), pl);
        }

        android.widget.ScrollView scroll = new android.widget.ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackground(new android.graphics.drawable.GradientDrawable(
                android.graphics.drawable.GradientDrawable.Orientation.TL_BR, new int[] { 0xFF1B1730, 0xFF0B0A12, 0xFF000000 }));
        scroll.addView(root, new android.widget.FrameLayout.LayoutParams(
                android.widget.FrameLayout.LayoutParams.MATCH_PARENT, android.widget.FrameLayout.LayoutParams.WRAP_CONTENT));
        startScreen = scroll;
        setContentView(startScreen);
    }

    /** The icon, the name, the tagline and the Telegram channel (a tap opens it). */
    private LinearLayout brand(boolean wide) {
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setGravity(Gravity.CENTER);
        try {
            android.widget.ImageView icon = new android.widget.ImageView(this);
            icon.setImageDrawable(getDrawable(getApplicationInfo().icon));
            int size = dp(wide ? 104 : 72);
            LinearLayout.LayoutParams ip = new LinearLayout.LayoutParams(size, size);
            ip.bottomMargin = dp(10);
            col.addView(icon, ip);
        } catch (RuntimeException ignored) {
        }
        TextView title = new TextView(this);
        title.setTextColor(0xFFFFFFFF);
        title.setTextSize(wide ? 40 : 32);
        title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        title.setGravity(Gravity.CENTER);
        title.setText("Flipendo");
        col.addView(title);
        TextView tagline = new TextView(this);
        tagline.setTextColor(MUTED);
        tagline.setTextSize(14);
        tagline.setGravity(Gravity.CENTER);
        tagline.setText("Harry Potter and the Sorcerer's Stone, on Android");
        col.addView(tagline);
        TextView link = new TextView(this);
        link.setTextColor(0xFF7CC4FF);
        link.setTextSize(14);
        link.setGravity(Gravity.CENTER);
        link.setPadding(dp(16), dp(8), dp(16), dp(8));
        link.setBackground(rounded(0x1A5EB8FF, 20, 0x405EB8FF));
        link.setText("Telegram channel");
        link.setOnClickListener(new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                splashUntil = Long.MAX_VALUE; // hold the splash while the channel is open; onResume lets it go
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(CHANNEL_URL)));
            }
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(14);
        col.addView(link, lp);
        return col;
    }

    private TextView vulkanPill, glPill;
    private TextView play;      // starts the game (a normal launch waits for it)
    private boolean playPressed;

    /** The launch panel, a card in the settings panel's style. The renderer is for this and later runs: the same file the
     *  in-game switch and the automatic fallback write (FlipendoActivity.RENDERER_FILE, read by android_main.cpp). */
    private LinearLayout launchPanel() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(20), dp(18), dp(20), dp(20));
        card.setBackground(rounded(CARD, 20, 0x22FFFFFF));

        TextView label = new TextView(this);
        label.setTextColor(GOLD);
        label.setTextSize(12);
        label.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        label.setLetterSpacing(0.12f);
        label.setText("RENDERER");
        card.addView(label);

        // a segmented control: OpenGL ES | Vulkan
        LinearLayout segments = new LinearLayout(this);
        segments.setOrientation(LinearLayout.HORIZONTAL);
        segments.setPadding(dp(4), dp(4), dp(4), dp(4));
        segments.setBackground(rounded(0x14FFFFFF, 14, 0x22FFFFFF));
        glPill = segment("OpenGL ES", "opengl");
        vulkanPill = segment("Vulkan", "vulkan");
        segments.addView(glPill, new LinearLayout.LayoutParams(0, dp(44), 1f));
        LinearLayout.LayoutParams vp = new LinearLayout.LayoutParams(0, dp(44), 1f);
        vp.leftMargin = dp(4);
        segments.addView(vulkanPill, vp);
        LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        sp.topMargin = dp(8);
        card.addView(segments, sp);

        TextView hint = new TextView(this);
        hint.setTextColor(MUTED);
        hint.setTextSize(12);
        hint.setText("Which one runs better depends on the phone. If the game is slow, looks wrong or does not start, try the other.");
        LinearLayout.LayoutParams hp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        hp.topMargin = dp(8);
        card.addView(hint, hp);

        // "Remember my choice": ticked, the renderer picked here stays for later launches; unticked (the default) it is for
        // this launch only, and the next launch goes back to the one from before (restoreRenderer)
        remember = new android.widget.CheckBox(this);
        remember.setText("Remember my choice");
        remember.setTextColor(0xDDFFFFFF);
        remember.setTextSize(14);
        remember.setButtonTintList(android.content.res.ColorStateList.valueOf(GOLD));
        remember.setChecked(getSharedPreferences("setup", MODE_PRIVATE).getBoolean(PREF_KEEP, false));
        remember.setOnCheckedChangeListener(new android.widget.CompoundButton.OnCheckedChangeListener() {
            public void onCheckedChanged(android.widget.CompoundButton b, boolean on) {
                android.content.SharedPreferences.Editor e = getSharedPreferences("setup", MODE_PRIVATE).edit().putBoolean(PREF_KEEP, on);
                if (on) {
                    e.remove(PREF_RESTORE); // the renderer now selected is the one to keep
                }
                e.apply();
            }
        });
        LinearLayout.LayoutParams rp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        rp.topMargin = dp(6);
        rp.leftMargin = -dp(6); // the box's own inset: line it up with the text above
        card.addView(remember, rp);

        // what the launcher is doing (unpacking, permissions), then Play
        status = new TextView(this);
        status.setTextColor(0xDDFFFFFF);
        status.setTextSize(13);
        status.setText("");
        LinearLayout.LayoutParams tp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        tp.topMargin = dp(10);
        card.addView(status, tp);

        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setIndeterminate(false);
        bar.setProgressTintList(android.content.res.ColorStateList.valueOf(GOLD));
        bar.setProgressBackgroundTintList(android.content.res.ColorStateList.valueOf(0x44FFFFFF));
        LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(8));
        bp.topMargin = dp(6);
        card.addView(bar, bp);

        play = new TextView(this);
        play.setText("\u25B6  Play");
        play.setTextSize(18);
        play.setGravity(Gravity.CENTER);
        play.setTextColor(0xFF1A1A1A);
        play.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        play.setFocusable(true);
        play.setOnClickListener(new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                playPressed = true;
                splashUntil = 0;
                tryStart();
            }
        });
        play.setOnFocusChangeListener(new android.view.View.OnFocusChangeListener() {
            public void onFocusChange(android.view.View v, boolean focused) { stylePlay(); }
        });
        stylePlay();
        play.setVisibility(android.view.View.GONE);
        LinearLayout.LayoutParams pp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(52));
        pp.topMargin = dp(12);
        card.addView(play, pp);
        return card;
    }

    /** One half of the renderer control (a gamepad moves between the halves and A picks one). */
    private TextView segment(String text, final String value) {
        TextView b = new TextView(this);
        b.setText(text);
        b.setTextSize(15);
        b.setGravity(Gravity.CENTER);
        b.setFocusable(true);
        b.setOnClickListener(new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) { chooseRenderer(value); }
        });
        b.setOnFocusChangeListener(new android.view.View.OnFocusChangeListener() {
            public void onFocusChange(android.view.View v, boolean focused) { showRenderer(); }
        });
        return b;
    }

    private void stylePlay() {
        play.setBackground(rounded(GOLD, 14, play.isFocused() ? 0xFFFFFFFF : 0));
    }

    static final String PREF_KEEP = "keepRenderer";       // "Remember my choice"
    static final String PREF_RESTORE = "restoreRenderer"; // a one-launch choice is in the file: what to put back ("" = none)
    private android.widget.CheckBox remember;
    private boolean restored;

    /** A normal launch after a one-launch choice puts the renderer from before it back. A restart from inside the game
     *  (language, mod, renderer switch, the OpenGL ES fallback) is the same session and keeps it. */
    private void restoreRenderer() {
        android.content.SharedPreferences prefs = getSharedPreferences("setup", MODE_PRIVATE);
        if (restored || getIntent().getBooleanExtra("restart", false) || !prefs.contains(PREF_RESTORE)) {
            return;
        }
        restored = true;
        String previous = prefs.getString(PREF_RESTORE, "");
        File file = new File(FlipendoActivity.GAME_DIR, FlipendoActivity.RENDERER_FILE);
        try {
            if (previous.isEmpty()) {
                file.delete();
            } else {
                writeSmallFile(file, previous);
            }
            prefs.edit().remove(PREF_RESTORE).apply();
        } catch (IOException e) {
            android.util.Log.e("flipendo", "could not put the renderer back: " + e);
        }
    }

    private void showRenderer() {
        String r = readSmallFile(new File(FlipendoActivity.GAME_DIR, FlipendoActivity.RENDERER_FILE));
        boolean gl = !"vulkan".equals(r); // OpenGL ES unless Vulkan was picked (android_main.cpp)
        glPill.setText("opengl-auto".equals(r) ? "OpenGL ES (auto)" : "OpenGL ES");
        stylePill(vulkanPill, !gl);
        stylePill(glPill, gl);
    }

    private void stylePill(TextView b, boolean selected) {
        b.setTextColor(selected ? 0xFF1A1A1A : 0xCCFFFFFF);
        b.setTypeface(android.graphics.Typeface.DEFAULT, selected ? android.graphics.Typeface.BOLD : android.graphics.Typeface.NORMAL);
        b.setBackground(rounded(selected ? GOLD : 0x00000000, 11, b.isFocused() ? 0xFFFFFFFF : 0));
    }

    private void chooseRenderer(String value) {
        File file = new File(FlipendoActivity.GAME_DIR, FlipendoActivity.RENDERER_FILE);
        android.content.SharedPreferences prefs = getSharedPreferences("setup", MODE_PRIVATE);
        if (!remember.isChecked() && !prefs.contains(PREF_RESTORE)) {
            // for this launch only: keep what to put back on the next one
            String previous = readSmallFile(file);
            prefs.edit().putString(PREF_RESTORE, previous != null ? previous : "").apply();
        }
        try {
            new File(FlipendoActivity.GAME_DIR).mkdirs();
            writeSmallFile(file, value);
        } catch (IOException e) {
            android.util.Log.e("flipendo", "could not keep the renderer choice: " + e);
        }
        showRenderer();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (hasStorageAccess(this)) {
            restoreRenderer();
        }
        showRenderer(); // readable only once storage access is granted
        if (splashUntil == Long.MAX_VALUE) {
            splashUntil = android.os.SystemClock.uptimeMillis() + 800; // back from the channel
            tryStart();
        }
        if (started) {
            return;
        }
        if (!hasStorageAccess(this)) {
            if (Build.VERSION.SDK_INT >= 30) {
                status.setText("Allow \"All files access\" for Flipendo, then come back.");
                if (!askedPermission) {
                    askedPermission = true;
                    startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                            Uri.parse("package:" + getPackageName())));
                }
            } else {
                // Android 10: no "All files access" (isExternalStorageManager is Android 11; calling it crashed the app at
                // once, POCO X3): the storage permission, with the legacy whole-storage view from the manifest
                status.setText("Allow access to the phone's storage for Flipendo: it reads the game from there.");
                if (!askedPermission) {
                    askedPermission = true;
                    requestPermissions(new String[] { android.Manifest.permission.READ_EXTERNAL_STORAGE,
                            android.Manifest.permission.WRITE_EXTERNAL_STORAGE }, REQUEST_STORAGE);
                }
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

    private static final int REQUEST_STORAGE = 8;

    /** Whether the app may read and write the game folder: "All files access" (Android 11+), the storage permission on 10. */
    static boolean hasStorageAccess(android.content.Context context) {
        if (Build.VERSION.SDK_INT >= 30) {
            return Environment.isExternalStorageManager();
        }
        return context.checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE) == android.content.pm.PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_STORAGE) {
            if (hasStorageAccess(this)) {
                onResume(); // go on as if the player had come back from the settings
            } else {
                askedPermission = false; // ask again the next time the app comes to the front
                status.setText("Flipendo needs access to the phone's storage to read the game. Open the app again to allow it.");
            }
        }
    }

    /** Unpacks the game data if this APK carries it, then starts the game. */
    private void begin() {
        new Thread(new Runnable() {
            public void run() {
                // the in-game language switch's wish becomes the choice
                File wish = new File(FlipendoActivity.GAME_DIR, LANG_WANT);
                String wished = readSmallFile(wish);
                if (wished != null && !wished.isEmpty()) {
                    try {
                        writeSmallFile(new File(FlipendoActivity.GAME_DIR, CHOSEN_LANG), wished);
                    } catch (IOException e) {
                        android.util.Log.e("flipendo", "could not keep the language choice: " + e);
                    }
                }
                wish.delete();
                boolean text = languageHasText(SetupActivity.this, chosenLanguage(SetupActivity.this));
                if (!text) {
                    // before a self pack puts its editions' files in: the originals the translation replaced go back first
                    try {
                        removeTextTranslation(new File(FlipendoActivity.GAME_DIR));
                    } catch (IOException e) {
                        android.util.Log.e("flipendo", "removing the text translation failed: " + e);
                    }
                }
                installGameData();
                // the default place first (a self pack unpacks there), else the folder the player picked
                File found = findGame(new File(FlipendoActivity.GAME_DIR), 2);
                final String picked = getSharedPreferences("setup", MODE_PRIVATE).getString(PREF_GAME_DIR, null);
                if (found == null && picked != null) {
                    found = findGame(new File(picked), 2);
                }
                if (found != null) {
                    try {
                        if (text) {
                            applyTextTranslation(found);
                        } else {
                            removeTextTranslation(found);
                        }
                    } catch (IOException e) {
                        android.util.Log.e("flipendo", "the text translation failed: " + e);
                    }
                    try {
                        applyVoice(found, VOICE_LANG.equals(chosenLanguage(SetupActivity.this)));
                    } catch (IOException e) {
                        android.util.Log.e("flipendo", "the dubbing failed: " + e);
                    }
                    // the mod to have: the in-game wish, else what is installed
                    File modWish = new File(FlipendoActivity.GAME_DIR, MOD_WANT);
                    String modWanted = readSmallFile(modWish);
                    if (modWanted == null || modWanted.isEmpty()) {
                        modWanted = readSmallFile(new File(found, MOD_MARKER));
                    }
                    try {
                        applyMod(found, modWanted, new byte[1 << 20]);
                    } catch (IOException e) {
                        android.util.Log.e("flipendo", "switching the mod failed: " + e);
                        say("Switching the mod failed: " + e.getMessage(), 0, 1);
                        try {
                            Thread.sleep(6000);
                        } catch (InterruptedException ignored) {
                        }
                    }
                    modWish.delete();
                }
                final File game = found;
                ui.post(new Runnable() {
                    public void run() {
                        if (game == null) {
                            if (picked != null) {
                                showWrongFolder(picked);
                            } else {
                                showMissingGame();
                            }
                            return;
                        }
                        gameDir = game.getPath();
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

    /** Starts the game once its data is ready: on Play, or by itself after a restart from inside the game (a language, renderer
     *  or mod switch, the fallback to OpenGL ES), once the splash has been up long enough (not while the channel link is open). */
    private void tryStart() {
        ui.removeCallbacks(launch);
        if (!gameReady || isFinishing() || splashUntil == Long.MAX_VALUE) {
            return;
        }
        if (!getIntent().getBooleanExtra("restart", false) && !playPressed) {
            bar.setVisibility(android.view.View.GONE);
            status.setText("");
            status.setVisibility(android.view.View.GONE);
            play.setVisibility(android.view.View.VISIBLE);
            play.requestFocus(); // a gamepad's A presses it
            return;
        }
        long left = splashUntil - android.os.SystemClock.uptimeMillis();
        if (left > 0) {
            ui.postDelayed(launch, left);
            return;
        }
        gameReady = false;
        Intent intent = new Intent(this, FlipendoActivity.class);
        if (gameDir != null) {
            intent.putExtra(EXTRA_GAME_DIR, gameDir);
        }
        startActivity(intent);
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
        showCard(title, message, primaryLabel, primary, secondaryLabel, secondary, null, null);
    }

    /** The same with a third action (left of the other two). */
    private void showCard(String title, String message, String primaryLabel, android.view.View.OnClickListener primary,
                          String secondaryLabel, android.view.View.OnClickListener secondary,
                          String thirdLabel, android.view.View.OnClickListener third) {
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
        app.setText("Flipendo for Android " + LogReport.version(this));
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
        if (thirdLabel != null) {
            LinearLayout.LayoutParams tp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            tp.rightMargin = dp(12);
            footer.addView(pill(thirdLabel, false, third), tp);
        }
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
        status.setVisibility(android.view.View.VISIBLE);
        play.setVisibility(android.view.View.GONE);
        status.setText("");
        begin();
    }

    private final android.view.View.OnClickListener sendLogs = new android.view.View.OnClickListener() {
        public void onClick(android.view.View v) {
            LogReport.share(SetupActivity.this);
        }
    };

    // The game folder: found in the default place or picked by the player (a folder anywhere on the phone or an SD card)
    static final String EXTRA_GAME_DIR = "gameDir";
    static final String PREF_GAME_DIR = "gameDir";
    private static final int REQUEST_PICK_FOLDER = 7;
    private String gameDir;

    private final android.view.View.OnClickListener checkAgain = new android.view.View.OnClickListener() {
        public void onClick(android.view.View v) {
            backToStart();
        }
    };

    private final android.view.View.OnClickListener pickFolder = new android.view.View.OnClickListener() {
        public void onClick(android.view.View v) {
            try {
                startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_PICK_FOLDER);
            } catch (RuntimeException e) {
                android.widget.Toast.makeText(SetupActivity.this, "No folder picker on this phone: copy the game to "
                        + FlipendoActivity.GAME_DIR + " instead", android.widget.Toast.LENGTH_LONG).show();
            }
        }
    };

    /** A folder from the system picker as a file path (the app has all files access): internal storage or an SD card. */
    static String treeUriToPath(Uri uri) {
        if (uri == null || !"com.android.externalstorage.documents".equals(uri.getAuthority())) {
            return null;
        }
        String id;
        try {
            id = android.provider.DocumentsContract.getTreeDocumentId(uri); // "primary:Games/HP", "1A2B-3C4D:HP"
        } catch (IllegalArgumentException e) {
            return null;
        }
        int colon = id.indexOf(':');
        if (colon < 0) {
            return null;
        }
        String volume = id.substring(0, colon), relative = id.substring(colon + 1);
        String base = "primary".equalsIgnoreCase(volume) ? Environment.getExternalStorageDirectory().getPath() : "/storage/" + volume;
        return relative.isEmpty() ? base : base + "/" + relative;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_PICK_FOLDER || resultCode != RESULT_OK || data == null) {
            return;
        }
        String path = treeUriToPath(data.getData());
        if (path == null) {
            showWrongFolder("(a place the app can't read as a folder)");
            return;
        }
        // remembered even when it's the wrong one: the next start says so instead of asking from scratch
        getSharedPreferences("setup", MODE_PRIVATE).edit().putString(PREF_GAME_DIR, path).apply();
        if (findGame(new File(path), 2) == null) {
            showWrongFolder(path);
        } else {
            backToStart();
        }
    }

    /** There is no game on the phone yet: say where to put it, or let the player pick the folder it is in. */
    private void showMissingGame() {
        showCard("Game data not found",
                "Copy the game folder from your PC (the one that contains System, Maps, Textures, Sounds and Music) to the phone, "
                        + "into Internal storage / FlipendoHP, then tap Check again.\n\n"
                        + "Already copied it somewhere else? Tap Choose folder and pick it (or the folder it is in).",
                "Choose folder", pickFolder, "Check again", checkAgain, "Send logs", sendLogs);
    }

    /** The folder the player picked has no game in it. */
    private void showWrongFolder(String path) {
        showCard("This isn't the game folder",
                "There is no game in " + path + " (looked for System/HP.exe there and one or two folders deeper).\n\n"
                        + "Pick the folder you copied from the PC: it contains System, Maps, Textures, Sounds and Music. "
                        + "Or copy the game into Internal storage / FlipendoHP and tap Check again.",
                "Choose another folder", pickFolder, "Check again", checkAgain, "Send logs", sendLogs);
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

    /** The editions a self pack carries as {code, label} (the first is the base install); empty for other builds. */
    static List<String[]> readPackLanguages(android.content.Context context) {
        // "code|label" or "code|label|rutext": an edition the Russian text translation goes with (a dubbing such as SoftClub's)
        List<String[]> out = new ArrayList<>();
        for (String[] pair : readPairs(context, LANG_DIR + "/langs.txt")) {
            String[] parts = pair[1].split("[|]", 2);
            out.add(new String[] { pair[0], parts[0], parts.length > 1 ? parts[1] : "" });
        }
        return out;
    }

    /** Whether the text translation is put in for this language: the text one itself, or an edition marked with it. */
    static boolean languageHasText(android.content.Context context, String code) {
        if (TEXT_LANG.equals(code) || VOICE_LANG.equals(code)) {
            return true;
        }
        for (String[] l : readPackLanguages(context)) {
            if (l[0].equals(code)) {
                return TEXT_LANG.equals(l[2]) && hasTextTranslation(context);
            }
        }
        return false;
    }

    // The Russian text translation, in every APK (assets/rutext: the game's .int files in Windows-1251)
    static final String TEXT_LANG = "rutext";
    static final String TEXT_DIR = "rutext";
    static final String TEXT_MARKER = ".flipendo-text";     // in the game folder: the translation is in its System folder
    static final String TEXT_BACKUP = ".text-backup";       // in the game folder: the .int files the translation replaced
    static final String CHOSEN_LANG = ".flipendo-language"; // in FlipendoHP: the language the player chose

    static boolean hasTextTranslation(android.content.Context context) {
        try {
            String[] files = context.getAssets().list(TEXT_DIR);
            return files != null && files.length > 0;
        } catch (IOException e) {
            return false;
        }
    }

    /** Every language the player can choose: the self pack's editions (or the installed game's own), then the text translation. */
    static List<String[]> readLanguages(android.content.Context context) {
        List<String[]> out = new ArrayList<>(readPackLanguages(context));
        if (out.isEmpty()) {
            out.add(new String[] { "en", "English" });
        }
        if (hasTextTranslation(context)) {
            out.add(new String[] { TEXT_LANG, "Русский (текст)" }); // Русский (текст)
        }
        if (hasImportedVoice(context)) {
            out.add(new String[] { VOICE_LANG, "Русский (своя озвучка)", TEXT_LANG }); // its voices and the Russian text
        }
        return out;
    }

    // A dubbing imported from a folder the player picked (Settings > Language > "Add a dubbing from a folder"): the sound
    // files that differ from the game's are kept in the app's private files (<files>/voices/custom/, paths relative to
    // Sounds), and put over the game folder's Sounds with the originals kept in .voice-backup, like the movement mod.
    static final String VOICE_LANG = "voice";
    static final String VOICE_DIR = "voices/custom";
    static final String VOICE_MARKER = ".flipendo-voice"; // in the game folder: the imported dubbing is in its Sounds
    static final String VOICE_BACKUP = ".voice-backup";   // in the game folder: the sound files it replaced

    static boolean hasImportedVoice(android.content.Context context) {
        String[] files = new File(context.getFilesDir(), VOICE_DIR).list();
        return files != null && files.length > 0;
    }

    /** The Sounds folder of a game folder (its case differs between installs). */
    private static File soundsDir(File gameDir) {
        return findIgnoringCase(gameDir, "Sounds");
    }

    private static void listFiles(File dir, String prefix, List<String> out) {
        File[] children = dir.listFiles();
        if (children == null) {
            return;
        }
        for (File child : children) {
            if (child.isDirectory()) {
                listFiles(child, prefix + child.getName() + "/", out);
            } else {
                out.add(prefix + child.getName());
            }
        }
    }

    /** The game's file for a path relative to Sounds, with each part's case as the game folder has it. */
    private static File findSound(File sounds, String relative) {
        File f = sounds;
        for (String part : relative.split("/")) {
            f = findIgnoringCase(f, part);
        }
        return f;
    }

    /**
     * Imports a dubbing from a folder: its Sounds folder (or the folder itself when it holds the sound files), keeping the
     * files the game has under the same name and that differ from the game's. Returns null when it worked, else what is wrong.
     */
    static String importVoice(android.content.Context context, String folder, File gameDir) {
        File source = new File(folder);
        File inner = findIgnoringCase(source, "Sounds");
        if (inner.isDirectory()) {
            source = inner;
        }
        File sounds = gameDir != null ? soundsDir(gameDir) : null;
        if (sounds == null || !sounds.isDirectory()) {
            return "The game folder has no Sounds folder";
        }
        List<String> files = new ArrayList<>();
        listFiles(source, "", files);
        File voices = new File(context.getFilesDir(), VOICE_DIR);
        File temp = new File(voices.getPath() + ".part");
        deleteTree(temp);
        temp.mkdirs();
        int kept = 0, known = 0;
        byte[] buffer = new byte[1 << 16];
        try {
            for (String relative : files) {
                String lower = relative.toLowerCase();
                if (!lower.endsWith(".uax") && !lower.endsWith(".wav")) {
                    continue;
                }
                File original = findSound(sounds, relative);
                if (!original.isFile()) {
                    continue; // a sound the game doesn't have would never be loaded
                }
                known++;
                File file = new File(source, relative);
                if (file.length() == original.length() && sameContent(file, original, buffer)) {
                    continue;
                }
                copyFile(file, new File(temp, relative), buffer);
                kept++;
            }
            if (known == 0) {
                return "No sound files of the game in " + folder + ": choose the dubbing's Sounds folder (AllDialog.uax and the others)";
            }
            if (kept == 0) {
                return "These sound files are the same as the game's: nothing to change";
            }
            deleteTree(voices);
            voices.getParentFile().mkdirs();
            if (!temp.renameTo(voices)) {
                return "Could not keep the dubbing in the app's files";
            }
            return null;
        } catch (IOException e) {
            return "Could not read the folder: " + e.getMessage();
        } finally {
            deleteTree(temp);
        }
    }

    private static boolean sameContent(File a, File b, byte[] buffer) throws IOException {
        byte[] other = new byte[buffer.length];
        try (InputStream ia = new java.io.FileInputStream(a); InputStream ib = new java.io.FileInputStream(b)) {
            while (true) {
                int na = ia.read(buffer), nb = ib.read(other, 0, Math.max(na, 0));
                if (na <= 0) {
                    return nb <= 0;
                }
                if (na != nb) {
                    return false; // short reads differ: treat as different (the copy is then just made)
                }
                for (int i = 0; i < na; i++) {
                    if (buffer[i] != other[i]) {
                        return false;
                    }
                }
            }
        }
    }

    /**
     * Puts the imported dubbing into a game folder's Sounds (on) or takes it out (off): the originals it replaces go to
     * .voice-backup first and come back when it is switched off. A game folder copied over again gets it once more.
     */
    private void applyVoice(File gameDir, boolean on) throws IOException {
        File marker = new File(gameDir, VOICE_MARKER);
        File backups = new File(gameDir, VOICE_BACKUP);
        File sounds = soundsDir(gameDir);
        boolean installed = marker.exists();
        List<String> files = new ArrayList<>();
        File voices = new File(getFilesDir(), VOICE_DIR);
        listFiles(voices, "", files);
        byte[] buffer = new byte[1 << 20];
        if (on && !files.isEmpty()) {
            int done = 0;
            for (String relative : files) {
                File source = new File(voices, relative);
                File target = findSound(sounds, relative);
                if (installed && target.length() == source.length()) {
                    done++;
                    continue; // already there
                }
                say("Installing the dubbing " + (done + 1) + " / " + files.size() + "\n" + relative, done, files.size());
                File backup = new File(backups, relative);
                if (target.exists() && !backup.exists()) {
                    copyFile(target, backup, buffer);
                }
                File temp = new File(target.getPath() + ".part");
                copyFile(source, temp, buffer);
                target.delete();
                temp.renameTo(target);
                done++;
            }
            writeSmallFile(marker, VOICE_LANG);
        } else if (installed) {
            List<String> originals = new ArrayList<>();
            listFiles(backups, "", originals);
            for (String relative : originals) {
                say("Restoring " + relative, 0, 1);
                copyFile(new File(backups, relative), findSound(sounds, relative), buffer);
            }
            deleteTree(backups);
            marker.delete();
        }
    }

    /** The language the player chose; before any choice the self pack's installed edition, else the base one. */
    static String chosenLanguage(android.content.Context context) {
        String chosen = readSmallFile(new File(FlipendoActivity.GAME_DIR, CHOSEN_LANG));
        if (chosen == null || chosen.isEmpty()) {
            chosen = readSmallFile(new File(FlipendoActivity.GAME_DIR, LANG_MARKER));
        }
        List<String[]> languages = readLanguages(context);
        for (String[] l : languages) {
            if (l[0].equals(chosen)) {
                return chosen;
            }
        }
        return languages.get(0)[0];
    }

    private static File findIgnoringCase(File dir, String name) {
        File[] children = dir.listFiles();
        if (children != null) {
            for (File child : children) {
                if (child.getName().equalsIgnoreCase(name)) {
                    return child;
                }
            }
        }
        return new File(dir, name);
    }

    private static byte[] readAll(InputStream in) throws IOException {
        java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
        byte[] buffer = new byte[1 << 16];
        int n;
        while ((n = in.read(buffer)) > 0) {
            bytes.write(buffer, 0, n);
        }
        return bytes.toByteArray();
    }

    /**
     * The translated .int file with the original's lines it lacks added at its end (under their sections), so a key the
     * translation doesn't have shows the English text instead of an error. Both are single-byte text.
     */
    static byte[] mergeIni(byte[] translated, byte[] original) throws IOException {
        if (original == null) {
            return translated;
        }
        java.nio.charset.Charset latin = java.nio.charset.StandardCharsets.ISO_8859_1;
        java.util.Set<String> have = new java.util.HashSet<>();
        String section = "";
        for (String line : new String(translated, latin).split("\r?\n")) {
            String t = line.trim();
            if (t.startsWith("[") && t.endsWith("]")) {
                section = t.toLowerCase();
            } else if (t.indexOf('=') > 0) {
                have.add(section + "|" + t.substring(0, t.indexOf('=')).trim().toLowerCase());
            }
        }
        java.util.LinkedHashMap<String, StringBuilder> missing = new java.util.LinkedHashMap<>();
        section = "";
        String header = "";
        for (String line : new String(original, latin).split("\r?\n")) {
            String t = line.trim();
            if (t.startsWith("[") && t.endsWith("]")) {
                section = t.toLowerCase();
                header = t;
            } else if (t.indexOf('=') > 0 && !have.contains(section + "|" + t.substring(0, t.indexOf('=')).trim().toLowerCase())) {
                StringBuilder lines = missing.get(header);
                if (lines == null) {
                    lines = new StringBuilder();
                    missing.put(header, lines);
                }
                lines.append(line).append("\r\n");
            }
        }
        if (missing.isEmpty()) {
            return translated;
        }
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        out.write(translated);
        for (java.util.Map.Entry<String, StringBuilder> e : missing.entrySet()) {
            out.write(("\r\n" + e.getKey() + "\r\n" + e.getValue()).getBytes(latin));
        }
        return out.toByteArray();
    }

    /** Puts the text translation into the game folder's System folder, keeping the originals it replaces. */
    private void applyTextTranslation(File gameDir) throws IOException {
        File system = systemDir(gameDir);
        File backups = new File(gameDir, TEXT_BACKUP);
        List<String> files = new ArrayList<>();
        collect(TEXT_DIR, files);
        byte[] buffer = new byte[1 << 16];
        int done = 0;
        for (String path : files) {
            String name = path.substring(path.lastIndexOf('/') + 1);
            say("Installing the Russian translation " + (done + 1) + " / " + files.size() + "\n" + name, done, files.size());
            File target = findIgnoringCase(system, name);
            File backup = new File(backups, target.getName());
            if (target.exists() && !backup.exists()) {
                copyFile(target, backup, buffer);
            }
            byte[] translated;
            try (InputStream in = getAssets().open(path)) {
                translated = readAll(in);
            }
            byte[] original = null;
            if (backup.exists()) {
                try (InputStream in = new java.io.FileInputStream(backup)) {
                    original = readAll(in);
                }
            }
            File temp = new File(target.getPath() + ".part");
            try (OutputStream os = new FileOutputStream(temp)) {
                os.write(mergeIni(translated, original));
            }
            if (target.exists()) {
                target.delete();
            }
            temp.renameTo(target);
            done++;
        }
        writeSmallFile(new File(gameDir, TEXT_MARKER), TEXT_LANG);
    }

    /** Takes the text translation out of a game folder: its originals back, the marker gone. */
    private void removeTextTranslation(File gameDir) throws IOException {
        File backups = new File(gameDir, TEXT_BACKUP);
        File marker = new File(gameDir, TEXT_MARKER);
        File[] originals = backups.listFiles();
        if (originals != null && originals.length > 0) {
            File system = systemDir(gameDir);
            byte[] buffer = new byte[1 << 16];
            for (File original : originals) {
                say("Restoring " + original.getName(), 0, 1);
                copyFile(original, findIgnoringCase(system, original.getName()), buffer);
                original.delete();
            }
        }
        backups.delete();
        if (marker.exists()) {
            marker.delete();
        }
    }

    /** The mods the player can switch on as {id, label}: the ones this APK carries, then the ones imported from a file. */
    static List<String[]> readMods(android.content.Context context) {
        List<String[]> out = readPairs(context, MOD_DIR + "/mods.txt");
        File[] imported = new File(context.getFilesDir(), MOD_DIR).listFiles();
        if (imported != null) {
            for (File dir : imported) {
                String[] packages = dir.list();
                if (!dir.isDirectory() || dir.getName().endsWith(".part") || packages == null || packages.length < 2) {
                    continue; // label.txt and at least one package
                }
                boolean carried = false;
                for (String[] m : out) {
                    carried |= m[0].equals(dir.getName());
                }
                if (!carried) {
                    String label = readSmallFile(new File(dir, MOD_LABEL));
                    out.add(new String[] { dir.getName(), label != null && !label.isEmpty() ? label : dir.getName() });
                }
            }
        }
        return out;
    }

    // A mod imported from a file the player picked (the light APK can't carry one: a mod is the game's own scripts, changed):
    // its packages are kept in the app's private files, <files>/mods/<id>/*.u, with label.txt
    static final String MOD_LABEL = "label.txt";
    static final String IMPORTED_MOD = "movement";

    /** The packages of a mod: the APK's assets, else the imported copy. Each entry is {file name, asset path or file path}. */
    private List<String[]> modPackages(String id) throws IOException {
        List<String[]> out = new ArrayList<>();
        List<String> assets = new ArrayList<>();
        collect(MOD_DIR + "/" + id, assets);
        for (String path : assets) {
            out.add(new String[] { path.substring(path.lastIndexOf('/') + 1), path, "asset" });
        }
        if (out.isEmpty()) {
            File[] files = new File(new File(getFilesDir(), MOD_DIR), id).listFiles();
            if (files != null) {
                for (File f : files) {
                    if (f.getName().toLowerCase().endsWith(".u")) {
                        out.add(new String[] { f.getName(), f.getPath(), "file" });
                    }
                }
            }
        }
        return out;
    }

    /**
     * Imports a mod from what the player picked: zip archives (their .u files, at any depth) or .u files themselves. The
     * packages must replace ones the game has (HarryPotter.u, HPBase.u, ...), so a wrong file is turned down. Returns
     * null when it worked, else what is wrong, for the player.
     */
    static String importMod(android.content.Context context, List<Uri> picked, File gameDir) {
        File mods = new File(context.getFilesDir(), MOD_DIR);
        File temp = new File(mods, IMPORTED_MOD + ".part");
        deleteTree(temp);
        temp.mkdirs();
        File system = gameDir != null ? systemDir(gameDir) : null;
        int packages = 0;
        List<String> unknown = new ArrayList<>();
        byte[] buffer = new byte[1 << 16];
        try {
            for (Uri uri : picked) {
                String name = displayName(context, uri);
                try (InputStream in = context.getContentResolver().openInputStream(uri)) {
                    if (in == null) {
                        return "Could not open " + name;
                    }
                    if (name.toLowerCase().endsWith(".u")) {
                        packages += keepPackage(name, in, temp, system, unknown, buffer);
                        continue;
                    }
                    java.util.zip.ZipInputStream zip = new java.util.zip.ZipInputStream(in);
                    java.util.zip.ZipEntry entry;
                    boolean anyEntry = false;
                    while ((entry = zip.getNextEntry()) != null) {
                        anyEntry = true;
                        String entryName = entry.getName().replace('\\', '/');
                        entryName = entryName.substring(entryName.lastIndexOf('/') + 1);
                        if (!entry.isDirectory() && entryName.toLowerCase().endsWith(".u")) {
                            packages += keepPackage(entryName, zip, temp, system, unknown, buffer);
                        }
                    }
                    if (!anyEntry) {
                        return name + " is not a zip archive or a .u package";
                    }
                }
            }
            if (packages == 0) {
                return unknown.isEmpty() ? "No .u packages in what you picked: choose the mod's zip (or its .u files)"
                        : "These packages are not the game's own: " + android.text.TextUtils.join(", ", unknown)
                        + ". Choose a mod that replaces the game's scripts (HarryPotter.u, HPBase.u, ...).";
            }
            writeSmallFile(new File(temp, MOD_LABEL), "Movement mod (your file)");
            File target = new File(mods, IMPORTED_MOD);
            deleteTree(target);
            if (!temp.renameTo(target)) {
                return "Could not keep the mod in the app's files";
            }
            return null;
        } catch (IOException e) {
            return "Could not read the file: " + e.getMessage();
        } finally {
            deleteTree(temp);
        }
    }

    /** Copies one package into the import folder when the game has a package by that name (1), else notes it (0). */
    private static int keepPackage(String name, InputStream in, File dir, File system, List<String> unknown, byte[] buffer) throws IOException {
        if (system != null && !findIgnoringCase(system, name).exists()) {
            unknown.add(name);
            return 0;
        }
        File out = new File(dir, system != null ? findIgnoringCase(system, name).getName() : name);
        try (OutputStream os = new FileOutputStream(out)) {
            int n;
            while ((n = in.read(buffer)) > 0) {
                os.write(buffer, 0, n);
            }
        }
        return 1;
    }

    private static String displayName(android.content.Context context, Uri uri) {
        try (android.database.Cursor c = context.getContentResolver().query(uri,
                new String[] { android.provider.OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst() && c.getString(0) != null) {
                return c.getString(0);
            }
        } catch (RuntimeException ignored) {
        }
        String last = uri.getLastPathSegment();
        return last != null ? last : "the file";
    }

    private static void deleteTree(File f) {
        File[] children = f.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        f.delete();
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
        File system = systemDir(gameDir);
        File backups = new File(gameDir, MOD_BACKUP);
        if (current.equals(wanted) && !wanted.equals("none")) {
            // still in place? A game copied over again brings the originals back: then the mod goes in again
            for (String[] p : modPackages(wanted)) {
                File target = findIgnoringCase(system, p[0]);
                long size = p[2].equals("file") ? new File(p[1]).length() : -1;
                if (!target.exists() || (size >= 0 && target.length() != size)) {
                    android.util.Log.i("flipendo", "mod " + wanted + ": " + p[0] + " was replaced, installing the mod again");
                    deleteTree(new File(backups, wanted));
                    current = "none";
                    break;
                }
            }
        }
        if (current.equals(wanted)) {
            return;
        }
        if (!current.equals("none")) {
            File[] originals = new File(backups, current).listFiles();
            if (originals != null) {
                for (File original : originals) {
                    say("Restoring " + original.getName(), 0, 1);
                    copyFile(original, findIgnoringCase(system, original.getName()), buffer);
                }
            }
        }
        if (!wanted.equals("none")) {
            List<String[]> files = modPackages(wanted);
            int done = 0;
            for (String[] p : files) {
                String name = p[0];
                say("Installing the mod " + (done + 1) + " / " + files.size() + "\n" + name, done, files.size());
                File target = findIgnoringCase(system, name);
                File backup = new File(new File(backups, wanted), target.getName());
                if (target.exists() && !backup.exists()) {
                    copyFile(target, backup, buffer);
                }
                if (p[2].equals("asset")) {
                    copyAsset(p[1], target, buffer);
                } else {
                    File temp = new File(target.getPath() + ".part");
                    copyFile(new File(p[1]), temp, buffer);
                    target.delete();
                    temp.renameTo(target);
                }
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
            List<String[]> languages = readPackLanguages(this);
            String installed = readSmallFile(new File(gameDir, LANG_MARKER));
            // the edition to have: the chosen one; the text translation goes over the base edition
            String wanted = chosenLanguage(this);
            if ((TEXT_LANG.equals(wanted) || VOICE_LANG.equals(wanted)) && !languages.isEmpty()) {
                wanted = languages.get(0)[0];
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
                // the .int files were just replaced by the originals: the text translation is put in again after this
                File[] stale = new File(gameDir, TEXT_BACKUP).listFiles();
                if (stale != null) {
                    for (File f : stale) {
                        f.delete();
                    }
                }
                new File(gameDir, TEXT_MARKER).delete();
                // the mod's packages were just replaced by the originals: begin() puts the mod in again
                new File(gameDir, MOD_MARKER).delete();
                deleteTree(new File(gameDir, MOD_BACKUP));
                writeSmallFile(marker, stamp);
            } else {
                if (!languages.isEmpty() && !wanted.equals(installed)) {
                    installLanguage(gameDir, wanted, label, buffer);
                }
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
