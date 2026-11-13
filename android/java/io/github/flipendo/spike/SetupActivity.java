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
    private static final String HP2_BUNDLE = "hp2data"; // Harry Potter 2 in a self pack (build-apk.sh --hp2)
    private static final String HP3_BUNDLE = "hp3data"; // Harry Potter 3 in a self pack (build-apk.sh --hp3)
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

    /** The launcher's size: every size here is in dp / sp, so the screen's density scaled by this makes the whole launcher larger
     *  (text, buttons, boxes) at once. Steps of the "Interface size" control; the default is 100%. */
    static final String PREF_UI = "launcherScale";
    static final float[] UI_STEPS = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f };
    static final float UI_DEFAULT = 1.0f;

    /** A launcher text in the chosen language (Tr: English is the key; the in-game overlay uses the same table). */
    static String tr(String en) {
        return Tr.tr(en);
    }

    private static float savedUiScale(android.content.Context c) {
        return c.getSharedPreferences("setup", MODE_PRIVATE).getFloat(PREF_UI, UI_DEFAULT);
    }

    @Override
    protected void attachBaseContext(android.content.Context base) {
        android.content.res.Configuration config = new android.content.res.Configuration(base.getResources().getConfiguration());
        config.densityDpi = Math.round(config.densityDpi * savedUiScale(base));
        Tr.init(base);
        super.attachBaseContext(base.createConfigurationContext(config));
    }

    /** "Interface size": a step smaller, the size, a step larger. The launcher starts again at the new size. */
    private LinearLayout uiSizeRow() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        final float current = savedUiScale(this);
        int at = 0;
        for (int i = 0; i < UI_STEPS.length; i++) if (Math.abs(UI_STEPS[i] - current) < Math.abs(UI_STEPS[at] - current)) at = i;
        final int index = at;
        TextView smaller = uiStepButton("A\u2212", index > 0 ? UI_STEPS[index - 1] : -1f);
        TextView larger = uiStepButton("A+", index < UI_STEPS.length - 1 ? UI_STEPS[index + 1] : -1f);
        TextView value = new TextView(this);
        value.setTextColor(INK_MUTED);
        value.setTextSize(13);
        value.setGravity(Gravity.CENTER);
        value.setText(Math.round(UI_STEPS[index] * 100) + "%");
        value.setContentDescription(tr("Interface size ") + Math.round(UI_STEPS[index] * 100) + "%");
        row.addView(smaller, new LinearLayout.LayoutParams(dp(40), dp(34)));
        LinearLayout.LayoutParams vp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        vp.leftMargin = dp(8);
        vp.rightMargin = dp(8);
        row.addView(value, vp);
        row.addView(larger, new LinearLayout.LayoutParams(dp(40), dp(34)));
        return row;
    }

    private TextView uiStepButton(String label, final float scale) {
        TextView b = new TextView(this);
        b.setText(label);
        b.setTextSize(14);
        b.setGravity(Gravity.CENTER);
        b.setTextColor(scale > 0 ? GOLD : 0x55FFFFFF);
        b.setBackground(roundedFocus(0x14FFFFFF, 17, 0x26FFFFFF));
        if (scale > 0) {
            b.setFocusable(true);
            b.setOnClickListener(new android.view.View.OnClickListener() {
                public void onClick(android.view.View v) {
                    getSharedPreferences("setup", MODE_PRIVATE).edit().putFloat(PREF_UI, scale).commit();
                    recreate();
                }
            });
        }
        return b;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // the in-game "Play the other game" button names the game here (this process keeps its own copy of the preferences)
        String requested = getIntent().getStringExtra("game");
        if ("hp1".equals(requested) || "hp2".equals(requested) || "hp3".equals(requested)) {
            getSharedPreferences("setup", MODE_PRIVATE).edit().putString(PREF_GAME, requested).commit();
        }
        splashUntil = android.os.SystemClock.uptimeMillis() + 2500;

        // The start screen: the name and the links along the top, the two games as covers on the left (the one to play lit),
        // the saved games and the renderer on the right, and Play pinned under them, so it never scrolls away. On a screen
        // too narrow for two columns the covers go above the rest. Everything in dp/sp (Interface size scales it all).
        android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
        float widthDp = Math.max(dm.widthPixels, dm.heightPixels) / dm.density; // the activity is landscape
        boolean wide = widthDp >= 560;
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(12), dp(24), dp(14));
        root.setBackground(new android.graphics.drawable.GradientDrawable(
                android.graphics.drawable.GradientDrawable.Orientation.TL_BR, new int[] { NIGHT_TOP, NIGHT_BOTTOM }));
        root.addView(topBar(), new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        LinearLayout covers = shelfColumn(wide);
        LinearLayout details = detailsHolder();
        LinearLayout footer = footer();
        LinearLayout.LayoutParams bodyLp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        bodyLp.topMargin = dp(12);
        if (wide) {
            LinearLayout body = new LinearLayout(this);
            body.setOrientation(LinearLayout.HORIZONTAL);
            body.addView(covers, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.MATCH_PARENT));
            LinearLayout right = new LinearLayout(this);
            right.setOrientation(LinearLayout.VERTICAL);
            android.widget.ScrollView scroll = new android.widget.ScrollView(this);
            scroll.addView(details);
            right.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
            right.addView(footer, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
            LinearLayout.LayoutParams rl = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
            rl.leftMargin = dp(24);
            body.addView(right, rl);
            root.addView(body, bodyLp);
        } else {
            LinearLayout col = new LinearLayout(this);
            col.setOrientation(LinearLayout.VERTICAL);
            col.addView(covers, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
            LinearLayout.LayoutParams dl = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            dl.topMargin = dp(8);
            col.addView(details, dl);
            android.widget.ScrollView scroll = new android.widget.ScrollView(this);
            scroll.setVerticalFadingEdgeEnabled(true);
            scroll.addView(col);
            root.addView(scroll, bodyLp);
            root.addView(footer, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        }
        applyPlatform();
        startScreen = root;
        setContentView(startScreen);
        checkUpdatesQuietly();
    }

    // The start screen's palette: a night sky over the castle, parchment for the text, the candle gold of the in-game settings,
    // and each game's cover in its own colour (the Philosopher's Stone red, the Chamber's serpent green)
    static final int NIGHT_TOP = 0xFF1A1F45, NIGHT_BOTTOM = 0xFF0B0E26, PARCHMENT = 0xFFF1E6CC, INK_MUTED = 0xFFA9A7C6,
            CRIMSON = 0xFF7E1B2A, CRIMSON_DEEP = 0xFF2C0A12, EMERALD = 0xFF145A42, EMERALD_DEEP = 0xFF07231B,
            AZKABAN = 0xFF2B2766, AZKABAN_DEEP = 0xFF0C0B22;
    private static final android.graphics.Typeface SERIF_BOLD = android.graphics.Typeface.create(android.graphics.Typeface.SERIF, android.graphics.Typeface.BOLD);

    /** The name and its line on the left; the channel, the game folders and the interface size on the right. */
    private LinearLayout topBar() {
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = new TextView(this);
        title.setTextColor(PARCHMENT);
        title.setTextSize(24);
        title.setIncludeFontPadding(false);
        title.setTypeface(SERIF_BOLD);
        title.setText("Flipendo");
        LinearLayout names = new LinearLayout(this);
        names.setOrientation(LinearLayout.VERTICAL);
        // the name, and the version beside it in gold: one tap on it is an easter egg (the link of the game picked below)
        LinearLayout titleRow = new LinearLayout(this);
        titleRow.setOrientation(LinearLayout.HORIZONTAL);
        titleRow.setGravity(Gravity.CENTER_VERTICAL);
        titleRow.addView(title);
        TextView versionChip = new TextView(this);
        versionChip.setText("v" + LogReport.version(this));
        versionChip.setTextColor(GOLD);
        versionChip.setTextSize(12);
        versionChip.setSingleLine(true);
        versionChip.setPadding(dp(10), dp(3), dp(10), dp(3));
        versionChip.setBackground(roundedFocus(0x22E8C15A, 12, 0x66E8C15A));
        versionChip.setFocusable(true);
        versionChip.setOnClickListener(new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                versionTap();
            }
        });
        LinearLayout.LayoutParams vcl = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        vcl.leftMargin = dp(10);
        titleRow.addView(versionChip, vcl);
        names.addView(titleRow);
        TextView tagline = new TextView(this);
        tagline.setTextColor(INK_MUTED);
        tagline.setTextSize(13);
        tagline.setSingleLine(true);
        tagline.setEllipsize(android.text.TextUtils.TruncateAt.END);
        tagline.setText(tr("Harry Potter 1 and 2 on Android"));
        tagline.setTextSize(12);
        names.addView(tagline);
        bar.addView(names, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        bar.addView(chip("Telegram", 0xFF8CC8FF, new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                splashUntil = Long.MAX_VALUE; // hold the splash while the channel is open; onResume lets it go
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(CHANNEL_URL)));
            }
        }));
        LinearLayout.LayoutParams fl = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        fl.leftMargin = dp(8);
        bar.addView(chip(tr("Game folders"), GOLD, new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                showFolderSettings();
            }
        }), fl);
        LinearLayout.LayoutParams ll = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        ll.leftMargin = dp(8);
        bar.addView(chip(tr("Language") + ": " + Tr.NAMES[Tr.index()], PARCHMENT, new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                new android.app.AlertDialog.Builder(SetupActivity.this)
                        .setTitle(tr("Language"))
                        .setItems(Tr.orderedNames(), new android.content.DialogInterface.OnClickListener() {
                            public void onClick(android.content.DialogInterface d, int which) {
                                Tr.save(SetupActivity.this, Tr.ORDER[which]); // the game's overlay reads the same file
                                Tr.init(SetupActivity.this);
                                recreate();
                            }
                        }).show();
            }
        }), ll);
        LinearLayout.LayoutParams ul = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        LinearLayout.LayoutParams upl = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        upl.leftMargin = dp(8);
        updateChip = chip(tr("Updates"), PARCHMENT, new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                onUpdateChip();
            }
        });
        bar.addView(updateChip, upl);
        ul.leftMargin = dp(14);
        bar.addView(uiSizeRow(), ul);
        return bar;
    }

    // Updates from GitHub Releases (Updater, UpdateRules): the chip in the top bar says when a newer release is out and opens
    // the update; with nothing new it opens the check and the switch for the automatic check.
    static final String PREF_UPDATE_AUTO = "updateAuto"; // on by default
    static final String PREF_UPDATE_LAST = "updateLastCheck";
    static final String PREF_UPDATE_SKIP = "updateSkipped";
    private static final long UPDATE_INTERVAL_MS = 12L * 3600 * 1000;
    private TextView updateChip;
    private UpdateRules.Candidate availableUpdate;
    private volatile boolean updateCancelled;

    private void refreshUpdateChip() {
        if (updateChip == null) return;
        boolean any = availableUpdate != null;
        updateChip.setText(any ? tr("Update") + " " + availableUpdate.version() : tr("Updates"));
        updateChip.setTextColor(any ? GOLD : PARCHMENT);
    }

    /** Once the start screen is up: looks for a newer release without a word, when the player allows it and the last look was a while ago. */
    private void checkUpdatesQuietly() {
        android.content.SharedPreferences p = getSharedPreferences("setup", MODE_PRIVATE);
        if (!p.getBoolean(PREF_UPDATE_AUTO, true) || !UpdateRules.due(System.currentTimeMillis(), p.getLong(PREF_UPDATE_LAST, 0), UPDATE_INTERVAL_MS)) {
            return;
        }
        checkUpdates(false);
    }

    private void checkUpdates(final boolean manual) {
        if (manual) {
            android.widget.Toast.makeText(this, tr("Looking for updates..."), android.widget.Toast.LENGTH_SHORT).show();
        }
        new Thread(new Runnable() {
            public void run() {
                UpdateRules.Candidate found = null;
                String problem = null;
                try {
                    found = Updater.check(SetupActivity.this);
                } catch (java.io.IOException e) {
                    problem = e.getMessage() != null ? e.getMessage() : e.toString();
                }
                final UpdateRules.Candidate result = found;
                final String error = problem;
                ui.post(new Runnable() {
                    public void run() {
                        finishUpdateCheck(result, error, manual);
                    }
                });
            }
        }, "update-check").start();
    }

    private void finishUpdateCheck(UpdateRules.Candidate found, String error, boolean manual) {
        android.util.Log.i("flipendo", "update check (" + (manual ? "asked" : "automatic") + "): installed " + LogReport.version(this)
                + ", " + (error != null ? "failed: " + error : found != null ? "release " + found.tag + " is out" : "nothing newer"));
        if (isFinishing()) return;
        android.content.SharedPreferences p = getSharedPreferences("setup", MODE_PRIVATE);
        if (error == null) {
            p.edit().putLong(PREF_UPDATE_LAST, System.currentTimeMillis()).apply();
        }
        availableUpdate = found != null && (manual || !found.version().equals(p.getString(PREF_UPDATE_SKIP, ""))) ? found : null;
        refreshUpdateChip();
        if (error != null) {
            if (manual) {
                android.widget.Toast.makeText(this, tr("Could not look for updates: ") + error, android.widget.Toast.LENGTH_LONG).show();
            }
        } else if (found == null) {
            if (manual) {
                android.widget.Toast.makeText(this, tr("You have the latest version."), android.widget.Toast.LENGTH_SHORT).show();
            }
        } else if (manual) {
            showUpdateDialog(found);
        } else if (availableUpdate != null) {
            android.widget.Toast.makeText(this, tr("Update") + " " + found.version() + " " + tr("is available."), android.widget.Toast.LENGTH_LONG).show();
        }
    }

    private void onUpdateChip() {
        if (availableUpdate != null) {
            showUpdateDialog(availableUpdate);
            return;
        }
        final android.content.SharedPreferences p = getSharedPreferences("setup", MODE_PRIVATE);
        boolean auto = p.getBoolean(PREF_UPDATE_AUTO, true);
        String[] items = { tr("Check now"), tr("Check automatically") + ": " + (auto ? tr("on") : tr("off")) };
        new android.app.AlertDialog.Builder(this)
                .setTitle(tr("Updates") + " (v" + LogReport.version(this) + ")")
                .setItems(items, new android.content.DialogInterface.OnClickListener() {
                    public void onClick(android.content.DialogInterface d, int which) {
                        if (which == 0) {
                            checkUpdates(true);
                        } else {
                            p.edit().putBoolean(PREF_UPDATE_AUTO, !p.getBoolean(PREF_UPDATE_AUTO, true)).apply();
                        }
                    }
                }).show();
    }

    private void showUpdateDialog(final UpdateRules.Candidate c) {
        String size = c.size > 0 ? " (" + Math.max(1, (c.size + 524288) / 1048576) + " MB)" : "";
        String message = tr("Installed: ") + LogReport.version(this) + "\n" + tr("Available: ") + c.version() + size + "\n"
                + tr("Your games and saves stay.") + "\n\n" + UpdateRules.excerpt(c.notes, 900);
        new android.app.AlertDialog.Builder(this)
                .setTitle(tr("Update") + " " + c.version())
                .setMessage(message)
                .setPositiveButton(tr("Update"), new android.content.DialogInterface.OnClickListener() {
                    public void onClick(android.content.DialogInterface d, int which) {
                        startUpdate(c);
                    }
                })
                .setNeutralButton(tr("Skip this version"), new android.content.DialogInterface.OnClickListener() {
                    public void onClick(android.content.DialogInterface d, int which) {
                        getSharedPreferences("setup", MODE_PRIVATE).edit().putString(PREF_UPDATE_SKIP, c.version()).apply();
                        availableUpdate = null;
                        refreshUpdateChip();
                    }
                })
                .setNegativeButton(tr("Later"), null)
                .show();
    }

    /** Downloads the release's APK (a dialog shows how far it is) and hands it to the system installer. */
    private void startUpdate(final UpdateRules.Candidate c) {
        if (Build.VERSION.SDK_INT >= 26 && !getPackageManager().canRequestPackageInstalls()) {
            // the system asks the app's permission to install apps once: it is given on its own page, then Update is pressed again
            android.widget.Toast.makeText(this, tr("Allow Flipendo to install updates, then press Update again."), android.widget.Toast.LENGTH_LONG).show();
            splashUntil = Long.MAX_VALUE;
            startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + getPackageName())));
            return;
        }
        updateCancelled = false;
        final TextView text = new TextView(this);
        text.setPadding(dp(24), dp(16), dp(24), dp(8));
        text.setText(tr("Downloading") + "...");
        final android.app.AlertDialog dialog = new android.app.AlertDialog.Builder(this)
                .setTitle(tr("Update") + " " + c.version())
                .setView(text)
                .setCancelable(false)
                .setNegativeButton(tr("Cancel"), new android.content.DialogInterface.OnClickListener() {
                    public void onClick(android.content.DialogInterface d, int which) {
                        updateCancelled = true;
                    }
                }).show();
        new Thread(new Runnable() {
            public void run() {
                String problem = null;
                java.io.File apk = null;
                try {
                    apk = Updater.download(SetupActivity.this, c, new Updater.Progress() {
                        private long shown;
                        public boolean update(final long done, final long total) {
                            long now = android.os.SystemClock.uptimeMillis();
                            if (now - shown > 250) {
                                shown = now;
                                ui.post(new Runnable() {
                                    public void run() {
                                        text.setText(tr("Downloading") + " " + (done / 1048576) + (total > 0 ? " / " + (total / 1048576) : "") + " MB");
                                    }
                                });
                            }
                            return updateCancelled;
                        }
                    });
                    if (apk != null) {
                        ui.post(new Runnable() {
                            public void run() {
                                text.setText(tr("Installing") + "...");
                            }
                        });
                        Updater.install(SetupActivity.this, apk);
                    }
                } catch (java.io.IOException | RuntimeException e) {
                    problem = e.getMessage() != null ? e.getMessage() : e.toString();
                }
                final String error = problem;
                ui.post(new Runnable() {
                    public void run() {
                        dialog.dismiss();
                        if (error != null) {
                            android.widget.Toast.makeText(SetupActivity.this, tr("The update failed: ") + error, android.widget.Toast.LENGTH_LONG).show();
                        }
                    }
                });
            }
        }, "update-download").start();
    }

    private boolean eggPending;

    /** One tap on the version: a toast with a line of the game picked on the start screen, and two seconds later its easter egg link. */
    private void versionTap() {
        if (eggPending) return;
        eggPending = true;
        final boolean hp2 = isHp2();
        VersionSound.play(this);
        android.widget.Toast.makeText(this, hp2 ? "КАКОГО ДЬЯВОЛА ТЫ ЗДЕСЬ ШУМИШЬ" : "Я должен был наложить здесь кирпичей, профессор Макгоннагал", android.widget.Toast.LENGTH_SHORT).show();
        ui.postDelayed(new Runnable() {
            public void run() {
                eggPending = false;
                splashUntil = Long.MAX_VALUE; // hold the splash while the link is open; onResume lets it go
                try {
                    startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(hp2 ? FlipendoActivity.Overlay.EGG_HP2_URL : FlipendoActivity.Overlay.EGG_HP1_URL)));
                } catch (RuntimeException ignored) {
                }
            }
        }, 2000);
    }

    private TextView chip(String text, int color, android.view.View.OnClickListener click) {
        TextView c = new TextView(this);
        c.setText(text);
        c.setTextColor(color);
        c.setTextSize(13);
        c.setGravity(Gravity.CENTER);
        c.setPadding(dp(14), dp(7), dp(14), dp(7));
        c.setBackground(roundedFocus(0x14FFFFFF, 18, 0x26FFFFFF));
        c.setFocusable(true);
        c.setOnClickListener(click);
        return c;
    }

    /** The games side by side, as covers: a tap picks the one to play. */
    private LinearLayout coverShelf(boolean wide) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        hp1Pill = cover("hp1");
        hp2Pill = cover("hp2");
        hp3Pill = cover("hp3");
        LinearLayout.LayoutParams a = wide ? new LinearLayout.LayoutParams(dp(142), LinearLayout.LayoutParams.MATCH_PARENT)
                : new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
        row.addView(hp1Pill, a);
        LinearLayout.LayoutParams b = wide ? new LinearLayout.LayoutParams(dp(142), LinearLayout.LayoutParams.MATCH_PARENT)
                : new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
        b.leftMargin = dp(12);
        row.addView(hp2Pill, b);
        LinearLayout.LayoutParams c3 = wide ? new LinearLayout.LayoutParams(dp(142), LinearLayout.LayoutParams.MATCH_PARENT)
                : new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
        c3.leftMargin = dp(12);
        if (hp3Present()) row.addView(hp3Pill, c3);
        return row;
    }

    /** The text for a game: HP1's, HP2's or HP3's. */
    private static String pick(String game, String hp1, String hp2, String hp3) {
        return "hp3".equals(game) ? hp3 : "hp2".equals(game) ? hp2 : hp1;
    }

    private android.view.View cover(final String game) {
        android.widget.FrameLayout f = new android.widget.FrameLayout(this);
        f.setBackground(new CoverDrawable(game, dp(1)));
        TextView year = new TextView(this);
        year.setTextColor(0xB3F1E6CC);
        year.setTextSize(11);
        year.setLetterSpacing(0.08f);
        year.setText(pick(game, "2001", "2002", "2004"));
        android.widget.FrameLayout.LayoutParams yl = new android.widget.FrameLayout.LayoutParams(
                android.widget.FrameLayout.LayoutParams.WRAP_CONTENT, android.widget.FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.START);
        yl.setMargins(dp(12), dp(10), 0, 0);
        f.addView(year, yl);
        LinearLayout text = new LinearLayout(this);
        text.setOrientation(LinearLayout.VERTICAL);
        text.setGravity(Gravity.BOTTOM);
        text.setPadding(dp(12), dp(12), dp(12), dp(14));
        TextView series = new TextView(this);
        series.setTextColor(0xCCF1E6CC);
        series.setTextSize(10);
        series.setSingleLine(true);
        series.setTypeface(android.graphics.Typeface.SERIF);
        series.setText(tr("Harry Potter and the"));
        text.addView(series);
        TextView name = new TextView(this);
        name.setTextColor(PARCHMENT);
        name.setTextSize(15);
        name.setHyphenationFrequency(android.text.Layout.HYPHENATION_FREQUENCY_NONE);
        name.setTypeface(SERIF_BOLD);
        name.setLineSpacing(0f, 0.95f);
        name.setText(pick(game, tr("Philosopher's Stone"), tr("Chamber of Secrets"), tr("Prisoner of Azkaban")));
        text.addView(name);
        f.addView(text, new android.widget.FrameLayout.LayoutParams(android.widget.FrameLayout.LayoutParams.MATCH_PARENT, android.widget.FrameLayout.LayoutParams.MATCH_PARENT));
        f.setFocusable(true);
        f.setContentDescription(pick(game, tr("Harry Potter and the Philosopher's Stone"), tr("Harry Potter and the Chamber of Secrets"),
                tr("Harry Potter and the Prisoner of Azkaban")));
        f.setOnClickListener(new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) { chooseGame(game); }
        });
        f.setOnFocusChangeListener(new android.view.View.OnFocusChangeListener() {
            public void onFocusChange(android.view.View v, boolean focused) { showGame(); }
        });
        return f;
    }

    /** The chosen game's cover is lit with a gold edge, the other one dims; a gamepad's focus is a white edge. */
    private void styleCover(android.view.View v, boolean selected) {
        CoverDrawable d = (CoverDrawable) v.getBackground();
        d.selected = selected;
        d.focused = v.isFocused();
        d.invalidateSelf();
        v.setAlpha(selected ? 1f : 0.55f);
    }

    /** A game's cover: its colour from top to bottom, a line drawing of what the game is named after (the Stone, a cut gem;
     *  the Chamber's serpent; a crescent moon over Azkaban), darker at the foot for the title, and the edge. */
    private static final class CoverDrawable extends android.graphics.drawable.Drawable {
        final String game;
        final float px;
        boolean selected, focused;
        final android.graphics.Paint paint = new android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG);
        final android.graphics.RectF box = new android.graphics.RectF();
        final android.graphics.Path path = new android.graphics.Path();

        CoverDrawable(String game, float px) {
            this.game = game;
            this.px = px;
        }

        @Override
        public void draw(android.graphics.Canvas c) {
            box.set(getBounds());
            float r = 14f * px, w = box.width(), h = box.height();
            paint.setStyle(android.graphics.Paint.Style.FILL);
            paint.setShader(new android.graphics.LinearGradient(0, box.top, 0, box.bottom,
                    "hp3".equals(game) ? AZKABAN : "hp2".equals(game) ? EMERALD : CRIMSON,
                    "hp3".equals(game) ? AZKABAN_DEEP : "hp2".equals(game) ? EMERALD_DEEP : CRIMSON_DEEP, android.graphics.Shader.TileMode.CLAMP));
            c.drawRoundRect(box, r, r, paint);
            paint.setShader(null);
            c.save();
            path.reset();
            path.addRoundRect(box, r, r, android.graphics.Path.Direction.CW);
            c.clipPath(path);
            paint.setStyle(android.graphics.Paint.Style.STROKE);
            paint.setStrokeJoin(android.graphics.Paint.Join.ROUND);
            paint.setStrokeCap(android.graphics.Paint.Cap.ROUND);
            // in the upper part, clear of the title at the foot
            float s = Math.min(w * 0.32f, h * 0.2f), cx = box.left + w * 0.62f, cy = box.top + h * 0.24f;
            if ("hp1".equals(game)) {
                paint.setColor(0x66E8C15A);
                paint.setStrokeWidth(1.6f * px);
                float tx = s * 0.5f, ty = cy - s * 0.35f, gx = s * 0.85f, mx = s * 0.25f, by = cy + s * 0.95f;
                path.reset();
                path.moveTo(cx - tx, ty);
                path.lineTo(cx + tx, ty);
                path.lineTo(cx + gx, cy);
                path.lineTo(cx, by);
                path.lineTo(cx - gx, cy);
                path.close();
                c.drawPath(path, paint);
                c.drawLine(cx - gx, cy, cx + gx, cy, paint);
                c.drawLine(cx - tx, ty, cx - mx, cy, paint);
                c.drawLine(cx - mx, cy, cx, by, paint);
                c.drawLine(cx + tx, ty, cx + mx, cy, paint);
                c.drawLine(cx + mx, cy, cx, by, paint);
                c.drawLine(cx, ty, cx - mx, cy, paint);
                c.drawLine(cx, ty, cx + mx, cy, paint);
            } else if ("hp2".equals(game)) {
                paint.setColor(0x55DCE6E0);
                paint.setStrokeWidth(3f * px);
                float x0 = cx, y0 = cy - s * 0.75f;
                path.reset();
                path.moveTo(x0, y0);
                path.cubicTo(x0 + s, y0 + s * 0.2f, x0 - s, y0 + s * 0.6f, x0, y0 + s * 0.9f);
                path.cubicTo(x0 + s * 0.9f, y0 + s * 1.2f, x0 - s * 0.6f, y0 + s * 1.55f, x0 + s * 0.15f, y0 + s * 1.9f);
                c.drawPath(path, paint);
                paint.setStyle(android.graphics.Paint.Style.FILL);
                c.drawCircle(x0, y0, 4.2f * px, paint);
            } else {
                // a crescent: the outer circle's arc where the inner circle, set off to the right, does not cover it
                paint.setColor(0x55D8D4F0);
                paint.setStrokeWidth(3f * px);
                float outer = s * 0.85f, inner = outer * 0.8f, off = outer * 0.5f, my = cy + s * 0.15f;
                float ix = (outer * outer - inner * inner + off * off) / (2f * off);
                float iy = (float) Math.sqrt(Math.max(0f, outer * outer - ix * ix));
                float a1 = (float) Math.toDegrees(Math.atan2(iy, ix)), a2 = (float) Math.toDegrees(Math.atan2(iy, ix - off));
                c.drawArc(cx - outer, my - outer, cx + outer, my + outer, a1, 360f - 2f * a1, false, paint);
                c.drawArc(cx + off - inner, my - inner, cx + off + inner, my + inner, a2, 360f - 2f * a2, false, paint);
            }
            paint.setStyle(android.graphics.Paint.Style.FILL);
            paint.setShader(new android.graphics.LinearGradient(0, box.top + h * 0.45f, 0, box.bottom, 0x00000000, 0xA6000000,
                    android.graphics.Shader.TileMode.CLAMP));
            c.drawRect(box, paint);
            paint.setShader(null);
            c.restore();
            paint.setStyle(android.graphics.Paint.Style.STROKE);
            paint.setStrokeWidth((selected || focused ? 2.5f : 1f) * px);
            paint.setColor(focused ? 0xFFFFFFFF : selected ? GOLD : 0x33FFFFFF);
            float in = paint.getStrokeWidth() / 2f;
            c.drawRoundRect(box.left + in, box.top + in, box.right - in, box.bottom - in, r, r, paint);
        }

        @Override
        public void setAlpha(int alpha) {
        }

        @Override
        public void setColorFilter(android.graphics.ColorFilter filter) {
        }

        @Override
        public int getOpacity() {
            return android.graphics.PixelFormat.TRANSLUCENT;
        }
    }

    private TextView vulkanPill, glPill;
    private TextView play;      // starts the game (a normal launch waits for it)
    private boolean playPressed;

    private TextView sectionLabel(String text) {
        TextView t = new TextView(this);
        t.setTextColor(INK_MUTED);
        t.setTextSize(13);
        t.setText(text);
        return t;
    }

    /** The details on the right: HP2's saved games, the renderer. The renderer is for this and later runs: the same file the
     *  in-game switch and the automatic fallback write (FlipendoActivity.RENDERER_FILE, read by android_main.cpp). */
    private LinearLayout launchPanel() {
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);

        // HP2: the saved games (six slots, each with the picture of its level) or a new game
        slotsBox = new LinearLayout(this);
        slotsBox.setOrientation(LinearLayout.VERTICAL);
        slotsBox.addView(sectionLabel(tr("Saved game")));
        android.widget.HorizontalScrollView slotsScroll = new android.widget.HorizontalScrollView(this);
        slotsScroll.setHorizontalScrollBarEnabled(false);
        slotsScroll.setHorizontalFadingEdgeEnabled(true);
        slotsRow = new LinearLayout(this);
        slotsRow.setOrientation(LinearLayout.HORIZONTAL);
        slotsScroll.addView(slotsRow);
        LinearLayout.LayoutParams ssp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        ssp.topMargin = dp(6);
        slotsBox.addView(slotsScroll, ssp);
        deleteSave = new TextView(this);
        deleteSave.setTextColor(0xFFFF9C92);
        deleteSave.setTextSize(13);
        deleteSave.setPadding(dp(12), dp(6), dp(12), dp(6));
        deleteSave.setBackground(roundedFocus(0x10FF5252, 14, 0x40FF5252));
        deleteSave.setFocusable(true);
        deleteSave.setOnClickListener(new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                askDeleteSave();
            }
        });
        LinearLayout.LayoutParams dsp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        dsp.topMargin = dp(8);
        dsp.bottomMargin = dp(16);
        slotsBox.addView(deleteSave, dsp);
        col.addView(slotsBox);

        // the renderer and the HD textures are the SurrealEngine games' (HP1, HP2); HP3 has its own engine, the box hides for it
        engineOptions = new LinearLayout(this);
        engineOptions.setOrientation(LinearLayout.VERTICAL);
        col.addView(engineOptions);

        // the renderer: its name and a segmented control (OpenGL ES | Vulkan) on one line, what it is for under them
        LinearLayout rendererRow = new LinearLayout(this);
        rendererRow.setOrientation(LinearLayout.HORIZONTAL);
        rendererRow.setGravity(Gravity.CENTER_VERTICAL);
        rendererRow.addView(sectionLabel(tr("Renderer")), new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        LinearLayout segments = new LinearLayout(this);
        segments.setOrientation(LinearLayout.HORIZONTAL);
        segments.setPadding(dp(3), dp(3), dp(3), dp(3));
        segments.setBackground(rounded(0x14FFFFFF, 14, 0x22FFFFFF));
        glPill = segment("OpenGL ES", "opengl");
        vulkanPill = segment("Vulkan", "vulkan");
        segments.addView(glPill, new LinearLayout.LayoutParams(dp(124), dp(38)));
        LinearLayout.LayoutParams vp = new LinearLayout.LayoutParams(dp(92), dp(38));
        vp.leftMargin = dp(3);
        segments.addView(vulkanPill, vp);
        rendererRow.addView(segments);
        engineOptions.addView(rendererRow);

        TextView hint = new TextView(this);
        hint.setTextColor(INK_MUTED);
        hint.setTextSize(12);
        hint.setLineSpacing(0f, 1.15f);
        hint.setText(tr("Which one runs better depends on the phone. If the game is slow, looks wrong or does not start, try the other."));
        LinearLayout.LayoutParams hp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        hp.topMargin = dp(8);
        engineOptions.addView(hint, hp);

        // tr("Remember my choice"): ticked, the renderer picked here stays for later launches; unticked (the default) it is for
        // this launch only, and the next launch goes back to the one from before (restoreRenderer)
        remember = new android.widget.CheckBox(this);
        remember.setText(tr("Remember my choice"));
        remember.setTextColor(PARCHMENT);
        remember.setTextSize(14);
        remember.setButtonTintList(android.content.res.ColorStateList.valueOf(GOLD));
        remember.setChecked(getSharedPreferences("setup", MODE_PRIVATE).getBoolean(PREF_KEEP, false));
        remember.setOnCheckedChangeListener(new android.widget.CompoundButton.OnCheckedChangeListener() {
            public void onCheckedChanged(android.widget.CompoundButton b, boolean on) {
                android.content.SharedPreferences.Editor e = getSharedPreferences("setup", MODE_PRIVATE).edit().putBoolean(PREF_KEEP, on);
                if (on) {
                    e.remove(restoreKey(false)).remove(restoreKey(true)); // the renderers now selected are the ones to keep
                }
                e.apply();
            }
        });
        LinearLayout.LayoutParams rp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        rp.topMargin = dp(10); // clear of the hint above: the box is taller than its text
        remember.setPadding(dp(8), dp(4), dp(8), dp(4));
        engineOptions.addView(remember, rp);

        // HD textures: the self pack carries the packs; any other APK asks for the pack's zip when the box is ticked (once)
        {
            android.widget.CheckBox hd = new android.widget.CheckBox(this);
            hd.setText(tr("HD textures"));
            hd.setTextColor(PARCHMENT);
            hd.setTextSize(14);
            hd.setButtonTintList(android.content.res.ColorStateList.valueOf(GOLD));
            hd.setChecked(hdTexturesWanted(this));
            hd.setOnCheckedChangeListener(new android.widget.CompoundButton.OnCheckedChangeListener() {
                public void onCheckedChanged(android.widget.CompoundButton b, boolean on) {
                    getSharedPreferences("setup", MODE_PRIVATE).edit().putBoolean(PREF_HD, on).apply();
                    if (on && !carriesTexturePacks() && getSharedPreferences("setup", MODE_PRIVATE).getString(PREF_HD_ZIP, null) == null) {
                        pickHdZip(b);
                    }
                }
            });
            hd.setPadding(dp(8), dp(4), dp(8), dp(4));
            engineOptions.addView(hd, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT));
            TextView hdHint = new TextView(this);
            hdHint.setTextColor(INK_MUTED);
            hdHint.setTextSize(12);
            hdHint.setLineSpacing(0f, 1.15f);
            hdHint.setText(tr(carriesTexturePacks() ? "Sharper walls, doors, characters, the main menu and the storybooks (ESRGAN upscale pack, about 1.3 GB on the phone). Needs a lot of memory: untick it if the game stutters or closes." : "Choose the zip of an HD texture pack (the ESRGAN upscale pack: a zip with a Textures folder in it). It is unpacked into the game folder on Play, about 1.3 GB; untick the box to take it out again."));
            LinearLayout.LayoutParams hhp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            hhp.leftMargin = dp(8);
            engineOptions.addView(hdHint, hhp);
            hdOptions = new android.view.View[] { hd, hdHint };
        }
        return col;
    }

    static final String PREF_HD = "hdTextures"; // tr("HD textures"): on by default where the APK carries the packs

    static final String PREF_HD_ZIP = "hdZip"; // an APK without packs: the zip the player chose (a content:// URI)
    private static final int REQUEST_PICK_HD = 13;

    static boolean hdTexturesWanted(android.content.Context context) {
        // on by default where the APK carries the packs; elsewhere only once a zip is chosen
        boolean carried;
        try {
            String[] names = context.getAssets().list("texpacks");
            carried = names != null && names.length > 0;
        } catch (IOException e) {
            carried = false;
        }
        return context.getSharedPreferences("setup", MODE_PRIVATE).getBoolean(PREF_HD, carried);
    }

    private void pickHdZip(final android.widget.CompoundButton box) {
        try {
            Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            pick.addCategory(Intent.CATEGORY_OPENABLE);
            pick.setType("*/*");
            pick.putExtra(Intent.EXTRA_MIME_TYPES, new String[] { "application/zip", "application/x-zip-compressed", "application/octet-stream" });
            pick.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(pick, REQUEST_PICK_HD);
        } catch (RuntimeException e) {
            box.setChecked(false);
            android.widget.Toast.makeText(this, tr("No file picker on this phone"), android.widget.Toast.LENGTH_LONG).show();
        }
        hdBox = box;
    }

    private android.widget.CompoundButton hdBox;

    private boolean carriesTexturePacks() {
        try {
            String[] names = getAssets().list("texpacks");
            return names != null && names.length > 0;
        } catch (IOException e) {
            return false;
        }
    }

    /** Under the details, always in view: what the launcher is doing (unpacking, permissions) and its progress, then Play. */
    private LinearLayout footer() {
        LinearLayout f = new LinearLayout(this);
        f.setOrientation(LinearLayout.VERTICAL);
        f.setPadding(0, dp(10), 0, 0);
        status = new TextView(this);
        status.setTextColor(PARCHMENT);
        status.setTextSize(13);
        status.setText("");
        f.addView(status, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setIndeterminate(false);
        bar.setProgressTintList(android.content.res.ColorStateList.valueOf(GOLD));
        bar.setProgressBackgroundTintList(android.content.res.ColorStateList.valueOf(0x33FFFFFF));
        LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(6));
        bp.topMargin = dp(6);
        f.addView(bar, bp);

        play = new TextView(this);
        play.setTextSize(17);
        play.setGravity(Gravity.CENTER);
        play.setTextColor(0xFF1C1505);
        play.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        play.setFocusable(true);
        play.setOnClickListener(new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                if (isPs1()) {
                    startPs1();
                    return;
                }
                if (gameMissing) {
                    if (missingPicked != null) {
                        showWrongFolder(missingPicked);
                    } else {
                        showMissingGame();
                    }
                    return;
                }
                playPressed = true;
                splashUntil = 0;
                tryStart();
            }
        });
        play.setOnFocusChangeListener(new android.view.View.OnFocusChangeListener() {
            public void onFocusChange(android.view.View v, boolean focused) { stylePlay(); }
        });
        stylePlay();
        updatePlayLabel();
        play.setVisibility(android.view.View.GONE);
        f.addView(play, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(50)));
        return f;
    }

    /** Play says what it will do: HP1 plays, HP2 continues the chosen slot or starts a new game. */
    private void updatePlayLabel() {
        if (play == null) {
            return;
        }
        String text = tr("Play");
        if (isPs1()) {
            // PS1: just Play (the slots and the start choice are the PC game's)
        } else if (gameMissing) {
            text = tr("Add game files");
        } else if (isHp2()) {
            String start = getSharedPreferences("setup", MODE_PRIVATE).getString(PREF_START, "new");
            text = tr("Start a new game");
            if (start.startsWith("load:")) {
                try {
                    text = tr("Continue slot ") + (Integer.parseInt(start.substring(5)) + 1);
                } catch (NumberFormatException ignored) {
                }
            }
        }
        play.setText("\u25B6  " + text);
    }

    private android.view.View hp1Pill, hp2Pill, hp3Pill; // the covers
    private LinearLayout engineOptions; // the renderer and the HD textures
    private android.view.View[] hdOptions; // the HD textures box and its hint (not for HP3)
    private LinearLayout slotsBox, slotsRow;
    private TextView deleteSave; // under the slots: deletes the selected saved game
    static final String PREF_START = "hp2Start"; // "new" or "load:N": what Play does in HP2
    static final String START_FILE = "/sdcard/FlipendoHP/hp2_start.txt"; // read by HP2::OpenStartMenu

    /** One half of the game control (a gamepad moves between the halves and A picks one). */
    private TextView gamePill(String text, final String value) {
        TextView b = new TextView(this);
        b.setText(text);
        b.setTextSize(15);
        b.setGravity(Gravity.CENTER);
        b.setFocusable(true);
        b.setOnClickListener(new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) { chooseGame(value); }
        });
        b.setOnFocusChangeListener(new android.view.View.OnFocusChangeListener() {
            public void onFocusChange(android.view.View v, boolean focused) { showGame(); }
        });
        return b;
    }

    private void showGame() {
        String game = isPs1() ? ps1Game() : chosenGame();
        if (hp1Pill != null) {
            styleCover(hp1Pill, "hp1".equals(game));
            styleCover(hp2Pill, "hp2".equals(game));
            styleCover(hp3Pill, "hp3".equals(game));
        }
        // HP3 (its own engine) has the renderer choice but not the HD textures, which are SurrealEngine's
        if (hdOptions != null) {
            for (android.view.View v : hdOptions) {
                v.setVisibility("hp3".equals(game) ? android.view.View.GONE : android.view.View.VISIBLE);
            }
        }
        showRenderer(); // each game keeps its own renderer choice
        if (isPs1()) {
            refreshPs1();
        }
        refreshSlots();
        updatePlayLabel();
    }

    /** Picks the game for this and later launches; the game data is looked for again (its own folder). */
    private void chooseGame(String value) {
        if (isPs1()) {
            // the PS1 tab has its own choice (HP1 or HP2); the PC game's stays as it was
            if ("hp1".equals(value) || "hp2".equals(value)) {
                getSharedPreferences("setup", MODE_PRIVATE).edit().putString(Ps1Setup.PREF_GAME, value).apply();
                showGame();
            }
            return;
        }
        if (value.equals(chosenGame())) {
            return;
        }
        getSharedPreferences("setup", MODE_PRIVATE).edit().putString(PREF_GAME, value).apply();
        showGame();
        if (started) {
            gameReady = false;
            backToStart();
        }
    }

    // ---- PS1: the PlayStation versions of HP1 and HP2 in the SwanStation emulator (Ps1Activity, docs/ps1.md) ------------
    // A PC / PS1 tab above the covers. The PS1 tab shows the two covers, its own panel (where the disc is: a folder or an
    // image file the player picks, the resolution, the BIOS) and the same Play button. It keeps its own game choice
    // (ps1.game), so the PC game chosen on the other tab does not change.

    private static final int REQUEST_PS1_FOLDER = 31, REQUEST_PS1_DISC = 32, REQUEST_PS1_BIOS = 33;
    private TextView tabPc, tabPs1;
    private LinearLayout pcPanel, ps1Panel;
    private TextView ps1Title, ps1DiscText, ps1BiosText, ps1Note, ps1Message;
    private final TextView[] ps1ScaleButtons = new TextView[4];
    private TextView ps1GlPill, ps1SoftPill, ps1RendererHint;
    private String ps1PickGame = "hp1"; // the game a running picker is for
    private boolean ps1Starting;

    private boolean isPs1() {
        return "ps1".equals(getSharedPreferences("setup", MODE_PRIVATE).getString(Ps1Setup.PREF_PLATFORM, "pc"));
    }

    /** The asset folder of a game's disc in a self pack (build-apk.sh --ps1-hp1 / --ps1-hp2): assets/ps1data/hp1 with a .stamp. */
    private static String ps1Bundle(String game) {
        return "ps1data/" + game;
    }

    /** True when this APK carries the game's disc and it is not unpacked yet (or an older one is). */
    private boolean ps1BundlePending(String game) {
        String stamp;
        try (InputStream in = getAssets().open(ps1Bundle(game) + "/.stamp")) {
            byte[] data = new byte[64];
            int n = in.read(data);
            stamp = new String(data, 0, Math.max(n, 0)).trim();
        } catch (IOException e) {
            return false; // a plain APK: the player brings the disc
        }
        return !stamp.equals(readSmallFile(new File(Ps1Setup.defaultDir(game), MARKER)));
    }

    private String ps1Game() {
        return "hp2".equals(getSharedPreferences("setup", MODE_PRIVATE).getString(Ps1Setup.PREF_GAME, "hp1")) ? "hp2" : "hp1";
    }

    /** The two tabs and the covers below them (one column on the left of the start screen). */
    private LinearLayout shelfColumn(boolean wide) {
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        LinearLayout tabs = new LinearLayout(this);
        tabs.setOrientation(LinearLayout.HORIZONTAL);
        tabPc = platformTab("PC", "pc");
        tabPs1 = platformTab("PS1", "ps1");
        LinearLayout.LayoutParams t1 = new LinearLayout.LayoutParams(0, dp(40), 1f);
        LinearLayout.LayoutParams t2 = new LinearLayout.LayoutParams(0, dp(40), 1f);
        t2.leftMargin = dp(8);
        tabs.addView(tabPc, t1);
        tabs.addView(tabPs1, t2);
        col.addView(tabs, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        LinearLayout row = coverShelf(wide);
        LinearLayout.LayoutParams rl = wide ? new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, 0, 1f)
                : new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(170));
        rl.topMargin = dp(10);
        col.addView(row, rl);
        return col;
    }

    private TextView platformTab(String label, final String platform) {
        TextView b = new TextView(this);
        b.setText(label);
        b.setTextSize(15);
        b.setGravity(Gravity.CENTER);
        b.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        b.setFocusable(true);
        b.setOnClickListener(new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) { setPlatform(platform); }
        });
        return b;
    }

    private void setPlatform(String platform) {
        getSharedPreferences("setup", MODE_PRIVATE).edit().putString(Ps1Setup.PREF_PLATFORM, platform).apply();
        applyPlatform();
        if (started && !isPs1() && !pcBegun) {
            begin(); // the PC game was not looked for yet (the app started on the PS1 tab)
        }
    }

    // the PC game's files are missing (shown in place, not as a card that takes the menu away)
    private boolean gameMissing, pcBegun, inProblemCard;
    private String missingPicked;

    /** The PC panel (the saved games, the renderer) and the PS1 panel, one over the other; one is shown. */
    private LinearLayout detailsHolder() {
        LinearLayout holder = new LinearLayout(this);
        holder.setOrientation(LinearLayout.VERTICAL);
        pcPanel = launchPanel();
        ps1Panel = buildPs1Panel();
        holder.addView(pcPanel, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        holder.addView(ps1Panel, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        return holder;
    }

    /** Shows the tab that is on: its panel, its covers and what the footer says. */
    private void applyPlatform() {
        boolean ps1 = isPs1();
        styleTab(tabPc, !ps1);
        styleTab(tabPs1, ps1);
        if (pcPanel != null) pcPanel.setVisibility(ps1 ? android.view.View.GONE : android.view.View.VISIBLE);
        if (ps1Panel != null) ps1Panel.setVisibility(ps1 ? android.view.View.VISIBLE : android.view.View.GONE);
        if (hp3Pill != null) hp3Pill.setVisibility(ps1 ? android.view.View.GONE : android.view.View.VISIBLE);
        if (play != null) {
            if (ps1) {
                // PS1 needs no unpacking: Play is there at once, the PC game's progress is out of the way
                bar.setVisibility(android.view.View.GONE);
                status.setVisibility(android.view.View.GONE);
                play.setVisibility(android.view.View.VISIBLE);
            } else if (gameMissing) {
                bar.setVisibility(android.view.View.GONE);
                status.setVisibility(android.view.View.VISIBLE);
                status.setText(pick(chosenGame(), tr("Harry Potter 1"), tr("Harry Potter 2"), tr("Harry Potter 3"))
                        + tr(" game files not found on this phone. The other games and the PS1 tab work without them."));
                play.setVisibility(android.view.View.VISIBLE);
            } else {
                bar.setVisibility(gameReady ? android.view.View.GONE : android.view.View.VISIBLE);
                status.setVisibility(gameReady ? android.view.View.GONE : android.view.View.VISIBLE);
                play.setVisibility(gameReady ? android.view.View.VISIBLE : android.view.View.GONE);
            }
        }
        showGame();
    }

    private void styleTab(TextView tab, boolean on) {
        if (tab == null) return;
        tab.setTextColor(on ? 0xFF1A1A1A : 0xFFFFFFFF);
        tab.setBackground(rounded(on ? GOLD : 0x1FFFFFFF, 20, on ? 0 : 0x33FFFFFF));
    }

    private LinearLayout buildPs1Panel() {
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(14), dp(12), dp(14), dp(12));
        panel.setBackground(rounded(0x14FFFFFF, 14, 0x22FFFFFF));

        ps1Title = new TextView(this);
        ps1Title.setTextColor(GOLD);
        ps1Title.setTextSize(12);
        ps1Title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        ps1Title.setLetterSpacing(0.12f);
        panel.addView(ps1Title);

        ps1DiscText = new TextView(this);
        ps1DiscText.setTextSize(14);
        ps1DiscText.setTextIsSelectable(true);
        ps1DiscText.setLineSpacing(0, 1.1f);
        panel.addView(ps1DiscText, lp(6));

        LinearLayout discButtons = new LinearLayout(this);
        discButtons.setOrientation(LinearLayout.HORIZONTAL);
        discButtons.addView(smallPill(tr("Choose folder"), new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) { pickPs1Folder(); }
        }));
        discButtons.addView(spaced(smallPill(tr("Choose image file"), new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) { pickPs1File(REQUEST_PS1_DISC); }
        })));
        discButtons.addView(spaced(smallPill(tr("Use default place"), new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) { clearPs1Disc(); }
        })));
        panel.addView(discButtons, lp(8));

        // the renderer: its name and a segmented control (OpenGL ES | Software) on one line, as on the PC tab
        LinearLayout rendererRow = new LinearLayout(this);
        rendererRow.setOrientation(LinearLayout.HORIZONTAL);
        rendererRow.setGravity(Gravity.CENTER_VERTICAL);
        rendererRow.addView(sectionLabel(tr("Renderer")), new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        LinearLayout segments = new LinearLayout(this);
        segments.setOrientation(LinearLayout.HORIZONTAL);
        segments.setPadding(dp(3), dp(3), dp(3), dp(3));
        segments.setBackground(rounded(0x14FFFFFF, 14, 0x22FFFFFF));
        ps1GlPill = ps1RendererSegment("OpenGL ES", Ps1Setup.RENDERER_OPENGL);
        ps1SoftPill = ps1RendererSegment(tr("Software"), Ps1Setup.RENDERER_SOFTWARE);
        segments.addView(ps1GlPill, new LinearLayout.LayoutParams(dp(124), dp(38)));
        LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(dp(110), dp(38));
        sp.leftMargin = dp(3);
        segments.addView(ps1SoftPill, sp);
        rendererRow.addView(segments);
        panel.addView(rendererRow, lp(14));
        ps1RendererHint = new TextView(this);
        ps1RendererHint.setTextColor(INK_MUTED);
        ps1RendererHint.setTextSize(12);
        ps1RendererHint.setLineSpacing(0f, 1.15f);
        panel.addView(ps1RendererHint, lp(6));

        TextView scaleLabel = new TextView(this);
        scaleLabel.setTextColor(0xDDFFFFFF);
        scaleLabel.setTextSize(13);
        scaleLabel.setText(tr("Resolution (a higher one needs a faster phone)"));
        panel.addView(scaleLabel, lp(12));
        LinearLayout scales = new LinearLayout(this);
        scales.setOrientation(LinearLayout.HORIZONTAL);
        for (int i = 0; i < 4; i++) {
            final int scale = i + 1;
            ps1ScaleButtons[i] = smallPill(scale + "×", new android.view.View.OnClickListener() {
                public void onClick(android.view.View v) {
                    getSharedPreferences("setup", MODE_PRIVATE).edit().putInt(Ps1Setup.PREF_SCALE, scale).apply();
                    refreshPs1();
                }
            });
            scales.addView(i == 0 ? ps1ScaleButtons[i] : spaced(ps1ScaleButtons[i]));
        }
        panel.addView(scales, lp(6));

        ps1BiosText = new TextView(this);
        ps1BiosText.setTextSize(13);
        ps1BiosText.setLineSpacing(0, 1.1f);
        panel.addView(ps1BiosText, lp(12));
        LinearLayout biosButtons = new LinearLayout(this);
        biosButtons.setOrientation(LinearLayout.HORIZONTAL);
        biosButtons.addView(smallPill(tr("Choose BIOS file"), new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) { pickPs1File(REQUEST_PS1_BIOS); }
        }));
        biosButtons.addView(spaced(smallPill(tr("Use OpenBIOS"), new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                getSharedPreferences("setup", MODE_PRIVATE).edit().remove(Ps1Setup.PREF_BIOS).apply();
                refreshPs1();
            }
        })));
        panel.addView(biosButtons, lp(6));

        ps1Note = new TextView(this);
        ps1Note.setTextColor(MUTED);
        ps1Note.setTextSize(12);
        panel.addView(ps1Note, lp(12));

        ps1Message = new TextView(this);
        ps1Message.setTextColor(PARCHMENT);
        ps1Message.setTextSize(13);
        panel.addView(ps1Message, lp(8));
        return panel;
    }

    private LinearLayout.LayoutParams lp(int topDp) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        p.topMargin = dp(topDp);
        return p;
    }

    private android.view.View spaced(android.view.View v) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        p.leftMargin = dp(8);
        v.setLayoutParams(p);
        return v;
    }

    private TextView smallPill(String label, android.view.View.OnClickListener onClick) {
        TextView b = pill(label, false, onClick);
        b.setTextSize(14);
        b.setPadding(dp(16), dp(8), dp(16), dp(8));
        b.setFocusable(true);
        return b;
    }

    /** One half of the PS1 renderer control (a gamepad moves between the halves and A picks one). */
    private TextView ps1RendererSegment(String text, final String value) {
        TextView b = new TextView(this);
        b.setText(text);
        b.setTextSize(15);
        b.setGravity(Gravity.CENTER);
        b.setFocusable(true);
        b.setOnClickListener(new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                getSharedPreferences("setup", MODE_PRIVATE).edit().putString(Ps1Setup.PREF_RENDERER, value).apply();
                refreshPs1();
            }
        });
        b.setOnFocusChangeListener(new android.view.View.OnFocusChangeListener() {
            public void onFocusChange(android.view.View v, boolean focused) { refreshPs1(); }
        });
        return b;
    }

    /** The texts of the PS1 panel for the game shown: where its disc is (or why not), the renderer, the resolution, the BIOS. */
    private void refreshPs1() {
        if (ps1Panel == null) return;
        android.content.SharedPreferences p = getSharedPreferences("setup", MODE_PRIVATE);
        String game = ps1Game();
        ps1Title.setText("PS1  ·  " + pick(game, tr("HARRY POTTER 1: PHILOSOPHER'S STONE"), tr("HARRY POTTER 2: CHAMBER OF SECRETS"), ""));

        String pickedDisc = p.getString(Ps1Setup.PREF_DISC + game, null), pickedFolder = p.getString(Ps1Setup.PREF_FOLDER + game, null);
        File disc = hasStorageAccess(this) ? Ps1Setup.resolveDisc(pickedDisc, pickedFolder, Ps1Setup.defaultDir(game)) : null;
        if (disc != null) {
            ps1DiscText.setTextColor(0xFF9BE39B);
            ps1DiscText.setText(disc.getPath() + (Ps1DiscConverter.needsConversion(disc) ? tr("\n(converted for the emulator the first time you play)") : ""));
        } else if (pickedDisc == null && pickedFolder == null && ps1BundlePending(game)) {
            ps1DiscText.setTextColor(0xFF9BE39B);
            ps1DiscText.setText(tr("The game is inside this app. It unpacks into ") + Ps1Setup.defaultDir(game).getPath()
                    + tr(" when you press Play (the first time only)."));
        } else if (pickedDisc != null || pickedFolder != null) {
            ps1DiscText.setTextColor(0xFFFF8A80);
            ps1DiscText.setText(tr("No disc image in ") + (pickedDisc != null ? pickedDisc : pickedFolder));
        } else {
            ps1DiscText.setTextColor(MUTED);
            ps1DiscText.setText(tr("No disc yet. Copy the game's .cue and .bin (or a .chd, or an .mdf with its .mds) into ")
                    + Ps1Setup.defaultDir(game).getPath() + tr(", or choose where it is."));
        }

        // the software renderer draws at the PS1's own resolution: the scale buttons are dimmed and do nothing for it
        boolean software = Ps1Setup.RENDERER_SOFTWARE.equals(Ps1Setup.renderer(p.getString(Ps1Setup.PREF_RENDERER, null)));
        stylePill(ps1GlPill, !software);
        stylePill(ps1SoftPill, software);
        ps1RendererHint.setText(software
                ? tr("Software: works on any phone, at the PS1's own resolution.")
                : tr("OpenGL ES: sharper, the resolution below. If the picture is wrong or the game does not start, try Software."));
        int scale = Ps1Setup.clampScale(p.getInt(Ps1Setup.PREF_SCALE, 2));
        for (int i = 0; i < 4; i++) {
            boolean on = i + 1 == scale && !software;
            ps1ScaleButtons[i].setTextColor(on ? 0xFF1A1A1A : 0xFFFFFFFF);
            ps1ScaleButtons[i].setBackground(rounded(on ? GOLD : 0x1FFFFFFF, 24, on ? 0 : 0x33FFFFFF));
            ps1ScaleButtons[i].setAlpha(software ? 0.4f : 1f);
            ps1ScaleButtons[i].setEnabled(!software);
        }

        String bios = p.getString(Ps1Setup.PREF_BIOS, null);
        if (bios == null) {
            ps1BiosText.setTextColor(0xDDFFFFFF);
            ps1BiosText.setText(tr("BIOS: OpenBIOS, built in (no Sony BIOS needed)"));
        } else if (Ps1Setup.isBios(new File(bios))) {
            ps1BiosText.setTextColor(0xFF9BE39B);
            ps1BiosText.setText(tr("BIOS: ") + bios);
        } else {
            ps1BiosText.setTextColor(0xFFFF8A80);
            ps1BiosText.setText(tr("BIOS file not found or not 512 KB, OpenBIOS is used instead: ") + bios);
        }
        ps1Note.setText(tr("Memory cards and saved states: ") + new File(Ps1Setup.defaultDir(game), "saves").getPath());
        if (!ps1Starting) ps1Message.setText("");
    }

    private void clearPs1Disc() {
        String game = ps1Game();
        getSharedPreferences("setup", MODE_PRIVATE).edit().remove(Ps1Setup.PREF_DISC + game).remove(Ps1Setup.PREF_FOLDER + game).apply();
        refreshPs1();
    }

    private void pickPs1Folder() {
        ps1PickGame = ps1Game();
        try {
            startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_PS1_FOLDER);
        } catch (RuntimeException e) {
            toast(tr("No folder picker on this phone: copy the disc into ") + Ps1Setup.defaultDir(ps1PickGame).getPath() + tr(" instead"));
        }
    }

    /** The system file picker, for a disc image (REQUEST_PS1_DISC) or a BIOS file (REQUEST_PS1_BIOS). */
    private void pickPs1File(int request) {
        ps1PickGame = ps1Game();
        try {
            Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            pick.addCategory(Intent.CATEGORY_OPENABLE);
            pick.setType("*/*"); // .cue, .mdf, .chd have no usable type
            startActivityForResult(pick, request);
        } catch (RuntimeException e) {
            toast(tr("No file picker on this phone: copy the file into ") + Ps1Setup.defaultDir(ps1PickGame).getPath() + tr(" instead"));
        }
    }

    private void toast(String text) {
        android.widget.Toast.makeText(this, text, android.widget.Toast.LENGTH_LONG).show();
    }

    /** The answer of the folder, disc image and BIOS pickers. */
    private void handlePs1Pick(int request, int resultCode, Intent data) {
        if (resultCode == RESULT_OK && data != null && data.getData() != null) {
            android.content.SharedPreferences.Editor ed = getSharedPreferences("setup", MODE_PRIVATE).edit();
            String game = ps1PickGame;
            if (request == REQUEST_PS1_FOLDER) {
                String path = treeUriToPath(data.getData());
                if (path == null) {
                    toast(tr("That place can't be read as a folder: pick one on the phone or an SD card"));
                } else if (Ps1DiscConverter.findDisc(new File(path), 1) == null) {
                    toast(tr("No disc image (.cue, .chd, .mdf, .iso) in ") + path);
                } else {
                    ed.putString(Ps1Setup.PREF_FOLDER + game, path).remove(Ps1Setup.PREF_DISC + game).apply();
                }
            } else {
                String id = null;
                try {
                    id = android.provider.DocumentsContract.getDocumentId(data.getData());
                } catch (IllegalArgumentException ignored) {
                    // not a document: handled as "no path" below
                }
                String path = "com.android.externalstorage.documents".equals(data.getData().getAuthority())
                        || "com.android.providers.downloads.documents".equals(data.getData().getAuthority())
                        ? Ps1Setup.documentIdToPath(id, Environment.getExternalStorageDirectory().getPath()) : null;
                if (path == null) {
                    toast(tr("That file can't be read from there: pick it in Internal storage or on an SD card"));
                } else if (request == REQUEST_PS1_BIOS) {
                    if (Ps1Setup.isBios(new File(path))) {
                        ed.putString(Ps1Setup.PREF_BIOS, path).apply();
                    } else {
                        toast(tr("A PS1 BIOS file is 512 KB (for example scph5501.bin)"));
                    }
                } else if (!Ps1Setup.isImageName(new File(path).getName())) {
                    toast(tr("That is not a disc image (.cue, .chd, .mdf, .iso)"));
                } else {
                    File image = Ps1Setup.normalizePicked(new File(path));
                    if (image == null || !image.isFile()) {
                        toast(tr("The .mdf that goes with it has to be in the same folder"));
                    } else {
                        ed.putString(Ps1Setup.PREF_DISC + game, image.getPath()).apply();
                    }
                }
            }
        }
        refreshPs1();
    }

    /** Play on the PS1 tab: finds the disc, converts an .mdf to BIN/CUE once, and starts the emulator. */
    private void startPs1() {
        if (ps1Starting) return;
        if (!hasStorageAccess(this)) {
            ps1Message.setTextColor(0xFFFF8A80);
            ps1Message.setText(tr("Flipendo needs access to the phone's storage to read the disc. Open the app again to allow it."));
            return;
        }
        android.content.SharedPreferences p = getSharedPreferences("setup", MODE_PRIVATE);
        final String game = ps1Game();
        final String pickedDisc = p.getString(Ps1Setup.PREF_DISC + game, null), pickedFolder = p.getString(Ps1Setup.PREF_FOLDER + game, null);
        final File disc = Ps1Setup.resolveDisc(pickedDisc, pickedFolder, Ps1Setup.defaultDir(game));
        // a self pack carries the disc: it is unpacked the first time, unless the player picked their own
        final boolean unpack = pickedDisc == null && pickedFolder == null && ps1BundlePending(game);
        if (disc == null && !unpack) {
            ps1Message.setTextColor(0xFFFF8A80);
            ps1Message.setText(tr("There is no disc to play: choose a folder or an image file above."));
            return;
        }
        final int scale = Ps1Setup.clampScale(p.getInt(Ps1Setup.PREF_SCALE, 2));
        final String bios = p.getString(Ps1Setup.PREF_BIOS, null);
        ps1Starting = true;
        ps1Message.setTextColor(PARCHMENT);
        ps1Message.setText(unpack ? tr("Unpacking the game from the app (the first time only)...")
                : Ps1DiscConverter.needsConversion(disc) ? tr("Converting the disc image for the emulator (once)...") : tr("Starting..."));
        if (unpack) {
            status.setVisibility(android.view.View.VISIBLE); // the progress of the unpacking (installBundle) is shown there
            bar.setVisibility(android.view.View.VISIBLE);
        }
        new Thread(new Runnable() {
            public void run() {
                File source = disc;
                String error = null;
                if (unpack) {
                    installBundle(ps1Bundle(game), Ps1Setup.defaultDir(game), tr("Unpacking the PS1 game "), tr("Unpacking the PS1 game failed: "));
                    source = Ps1Setup.resolveDisc(pickedDisc, pickedFolder, Ps1Setup.defaultDir(game));
                    if (source == null) error = tr("the game could not be unpacked");
                }
                File toPlay = source;
                if (error == null && Ps1DiscConverter.needsConversion(source)) {
                    try {
                        toPlay = Ps1DiscConverter.convertMdf(source, new File(Ps1Setup.defaultDir(game), "disc"));
                    } catch (IOException e) {
                        error = e.getMessage();
                    }
                }
                final File image = toPlay;
                final String failure = error;
                ui.post(new Runnable() {
                    public void run() {
                        ps1Starting = false;
                        if (unpack) {
                            status.setVisibility(android.view.View.GONE);
                            bar.setVisibility(android.view.View.GONE);
                        }
                        if (failure != null) {
                            ps1Message.setTextColor(0xFFFF8A80);
                            ps1Message.setText(tr("The disc image can't be used: ") + failure);
                            return;
                        }
                        ps1Message.setText("");
                        Intent intent = new Intent(SetupActivity.this, Ps1Activity.class);
                        intent.putExtra(Ps1Activity.EXTRA_DISC, image.getPath()).putExtra(Ps1Activity.EXTRA_GAME, game)
                                .putExtra(Ps1Activity.EXTRA_SCALE, scale)
                                .putExtra(Ps1Activity.EXTRA_RENDERER, Ps1Setup.renderer(getSharedPreferences("setup", MODE_PRIVATE).getString(Ps1Setup.PREF_RENDERER, null)));
                        if (bios != null) intent.putExtra(Ps1Activity.EXTRA_BIOS, bios);
                        startActivity(intent); // the launcher stays behind: leaving the game comes back to it
                    }
                });
            }
        }).start();
    }

    // ---- HP2's saved games: six slots with a picture, or a new game --------------------------------------------------

    /** The HP2 folder, or null (looked for the way the launcher does). */
    private File hp2Folder() {
        return locateGame("hp2");
    }

    /** A game's folder: the one the player picked (Game folders), else the default place (a self pack unpacks there); null if neither has the game. */
    private File locateGame(String game) {
        String picked = getSharedPreferences("setup", MODE_PRIVATE).getString(dirKey(game), null);
        File found = picked != null ? findGameFor(game, new File(picked), 2) : null;
        if (found == null) {
            found = findGameFor(game, new File(defaultDir(game)), 2);
        }
        return found;
    }

    /** The folder the saves are in: Save (the ini's SavePath ../Save) or save (a Windows copy), whichever exists. */
    private static File saveFolder(File game) {
        File upper = new File(game, "Save");
        File lower = new File(game, "save");
        return upper.isDirectory() ? upper : (lower.isDirectory() ? lower : upper);
    }

    private static final class SlotInfo {
        boolean exists;
        String map = "";
        long time;
    }

    private SlotInfo[] readSlots() {
        SlotInfo[] slots = new SlotInfo[6];
        File game = hasStorageAccess(this) ? hp2Folder() : null;
        File folder = game != null ? saveFolder(game) : null;
        File[] files = folder != null ? folder.listFiles() : null;
        for (int i = 0; i < 6; i++) {
            slots[i] = new SlotInfo();
            if (files == null) {
                continue;
            }
            for (File f : files) {
                String n = f.getName().toLowerCase(java.util.Locale.ROOT);
                if (n.startsWith("save" + i + ".") && f.length() > 0) {
                    slots[i].exists = true;
                }
            }
            if (slots[i].exists) {
                String info = readSmallFile(new File(folder, "Slot" + i + ".txt"));
                if (info != null) {
                    String[] lines = info.split("\n");
                    slots[i].map = lines.length > 0 ? lines[0].trim() : "";
                    try {
                        slots[i].time = lines.length > 1 ? Long.parseLong(lines[1].trim()) : 0;
                    } catch (NumberFormatException ignored) {
                    }
                }
            }
        }
        return slots;
    }

    /** The level picture the original game ships (System/Thumbnails/<map>.bmp), or null. */
    private android.graphics.Bitmap thumbnail(File game, String map) {
        if (game == null || map == null || map.isEmpty()) {
            return null;
        }
        String base = map.endsWith(".unr") ? map.substring(0, map.length() - 4) : map;
        File thumbs = findIgnoringCase(new File(game, "System"), "Thumbnails");
        if (thumbs == null) {
            return null;
        }
        File file = findIgnoringCase(thumbs, base + ".bmp");
        return file != null ? android.graphics.BitmapFactory.decodeFile(file.getPath()) : null;
    }

    /** The slots row: New game, then slots 1..6 (a game's own picture and level, or tr("Empty")). */
    private void refreshSlots() {
        if (slotsBox == null) {
            return;
        }
        boolean two = isHp2();
        slotsBox.setVisibility(two ? android.view.View.VISIBLE : android.view.View.GONE);
        if (!two) {
            updatePlayLabel();
            return;
        }
        SlotInfo[] slots = readSlots();
        File game = hasStorageAccess(this) ? hp2Folder() : null;
        android.content.SharedPreferences prefs = getSharedPreferences("setup", MODE_PRIVATE);
        String start = prefs.getString(PREF_START, null);
        // the newest save is the default; without one, a new game
        int newest = -1;
        for (int i = 0; i < 6; i++) {
            if (slots[i].exists && (newest < 0 || slots[i].time > slots[newest].time)) {
                newest = i;
            }
        }
        boolean valid = "new".equals(start);
        if (start != null && start.startsWith("load:")) {
            try {
                int n = Integer.parseInt(start.substring(5));
                valid = n >= 0 && n < 6 && slots[n].exists;
            } catch (NumberFormatException ignored) {
            }
        }
        if (!valid) {
            start = newest >= 0 ? "load:" + newest : "new";
            prefs.edit().putString(PREF_START, start).apply();
        }

        // the button for the selected saved game
        if (start.startsWith("load:")) {
            deleteSave.setText(tr("Delete the saved game in slot ") + (Integer.parseInt(start.substring(5)) + 1));
            deleteSave.setVisibility(android.view.View.VISIBLE);
        } else {
            deleteSave.setVisibility(android.view.View.GONE);
        }

        slotsRow.removeAllViews();
        slotsRow.addView(slotCard(tr("New game"), tr("Start from the beginning"), null, "new", start.equals("new")));
        for (int i = 0; i < 6; i++) {
            String title = tr("Slot ") + (i + 1);
            String sub;
            android.graphics.Bitmap pic = null;
            if (slots[i].exists) {
                sub = slots[i].map.isEmpty() ? tr("Saved game") : slots[i].map;
                if (slots[i].time > 0) {
                    sub += "\n" + android.text.format.DateFormat.format("d MMM, HH:mm", slots[i].time * 1000L);
                }
                pic = thumbnail(game, slots[i].map);
            } else {
                sub = tr("Empty");
            }
            slotsRow.addView(slotCard(title, sub, pic, slots[i].exists ? "load:" + i : null, start.equals("load:" + i)));
        }
        updatePlayLabel();
    }

    /** Asks, then deletes the selected saved game: its Save<N> file and the slot's info (Slot<N>.txt) in the save folder. */
    private void askDeleteSave() {
        String start = getSharedPreferences("setup", MODE_PRIVATE).getString(PREF_START, "new");
        if (!start.startsWith("load:")) {
            return;
        }
        final int slot;
        try {
            slot = Integer.parseInt(start.substring(5));
        } catch (NumberFormatException e) {
            return;
        }
        new android.app.AlertDialog.Builder(this)
                .setTitle(tr("Delete the saved game?"))
                .setMessage(tr("Slot ") + (slot + 1) + tr(" of Harry Potter 2 will be deleted for good."))
                .setPositiveButton(tr("Delete"), new android.content.DialogInterface.OnClickListener() {
                    public void onClick(android.content.DialogInterface d, int which) {
                        deleteSlot(slot);
                        getSharedPreferences("setup", MODE_PRIVATE).edit().remove(PREF_START).apply(); // the newest save, or a new game
                        refreshSlots();
                    }
                })
                .setNegativeButton(tr("Cancel"), null)
                .show();
    }

    private void deleteSlot(int slot) {
        File game = hasStorageAccess(this) ? hp2Folder() : null;
        File folder = game != null ? saveFolder(game) : null;
        File[] files = folder != null ? folder.listFiles() : null;
        if (files == null) {
            return;
        }
        for (File f : files) {
            String n = f.getName().toLowerCase(java.util.Locale.ROOT);
            if (n.startsWith("save" + slot + ".") || n.equals("slot" + slot + ".txt")) {
                if (!f.delete()) {
                    android.util.Log.e("flipendo", "could not delete " + f);
                }
            }
        }
    }

    /** One card of the row: a picture (or a dark box), the title and a line under it. `value` null: not selectable. */
    private android.view.View slotCard(String title, String sub, android.graphics.Bitmap pic, final String value, boolean selected) {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setPadding(dp(6), dp(6), dp(6), dp(8));
        c.setBackground(roundedFocus(selected ? 0x1FE8C15A : 0x10FFFFFF, 14, selected ? GOLD : 0x1FFFFFFF));
        android.widget.ImageView image = new android.widget.ImageView(this);
        image.setScaleType(android.widget.ImageView.ScaleType.CENTER_CROP);
        image.setBackground(rounded(0xFF0E1130, 10, 0));
        image.setClipToOutline(true);
        if (pic != null) {
            image.setImageBitmap(pic);
        }
        c.addView(image, new LinearLayout.LayoutParams(dp(112), dp(63)));
        TextView t = new TextView(this);
        t.setText(title);
        t.setTextColor(selected ? GOLD : PARCHMENT);
        t.setTextSize(13);
        t.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        LinearLayout.LayoutParams tlp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        tlp.topMargin = dp(6);
        c.addView(t, tlp);
        TextView s = new TextView(this);
        s.setText(sub);
        s.setTextColor(INK_MUTED);
        s.setTextSize(11);
        s.setMaxLines(3);
        c.addView(s, new LinearLayout.LayoutParams(dp(112), LinearLayout.LayoutParams.WRAP_CONTENT));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.rightMargin = dp(8);
        c.setLayoutParams(lp);
        c.setAlpha(value == null ? 0.5f : 1f);
        if (value != null) {
            c.setFocusable(true);
            c.setOnClickListener(new android.view.View.OnClickListener() {
                public void onClick(android.view.View v) {
                    getSharedPreferences("setup", MODE_PRIVATE).edit().putString(PREF_START, value).apply();
                    refreshSlots();
                }
            });
        }
        return c;
    }

    /** What Play does in HP2, for HP2::OpenStartMenu: "load N" or "new N" (N: the slot, 0..5). Removed for HP1. */
    private void writeStartFile() {
        File file = new File(START_FILE);
        if (!isHp2()) {
            file.delete();
            return;
        }
        String start = getSharedPreferences("setup", MODE_PRIVATE).getString(PREF_START, "new");
        String line;
        if (start.startsWith("load:")) {
            line = "load " + start.substring(5);
        } else {
            // a new game goes to the first empty slot (the first one when all six are used)
            SlotInfo[] slots = readSlots();
            int slot = 0;
            for (int i = 0; i < 6; i++) {
                if (!slots[i].exists) {
                    slot = i;
                    break;
                }
            }
            line = "new " + slot;
        }
        try {
            new File(FlipendoActivity.GAME_DIR).mkdirs();
            writeSmallFile(file, line);
        } catch (IOException e) {
            android.util.Log.e("flipendo", "could not write the start choice: " + e);
        }
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
        play.setBackground(rounded(GOLD, 25, play.isFocused() ? 0xFFFFFFFF : 0));
    }

    static final String PREF_KEEP = "keepRenderer";       // tr("Remember my choice")
    static final String PREF_RESTORE = "restoreRenderer"; // a one-launch choice is in the file: what to put back ("" = none)
    private android.widget.CheckBox remember;
    private boolean restored;

    /** PREF_RESTORE of a game: HP3 has a renderer choice of its own (FlipendoActivity.RENDERER_FILE_HP3). */
    static String restoreKey(boolean hp3) {
        return hp3 ? PREF_RESTORE + "Hp3" : PREF_RESTORE;
    }

    /** The renderer choice file of the game picked here. */
    private File rendererFile() {
        return new File(FlipendoActivity.GAME_DIR, FlipendoActivity.rendererFile(isHp3()));
    }

    /** A normal launch after a one-launch choice puts the renderer from before it back. A restart from inside the game
     *  (language, mod, renderer switch, the OpenGL ES fallback) is the same session and keeps it. */
    private void restoreRenderer() {
        android.content.SharedPreferences prefs = getSharedPreferences("setup", MODE_PRIVATE);
        if (restored || getIntent().getBooleanExtra("restart", false)) {
            return;
        }
        restored = true;
        for (boolean hp3 : new boolean[] { false, true }) {
            String key = restoreKey(hp3);
            if (!prefs.contains(key)) {
                continue;
            }
            String previous = prefs.getString(key, "");
            File file = new File(FlipendoActivity.GAME_DIR, FlipendoActivity.rendererFile(hp3));
            try {
                if (previous.isEmpty()) {
                    file.delete();
                } else {
                    writeSmallFile(file, previous);
                }
                prefs.edit().remove(key).apply();
            } catch (IOException e) {
                android.util.Log.e("flipendo", "could not put the renderer back: " + e);
            }
        }
    }

    private void showRenderer() {
        if (glPill == null) {
            return;
        }
        String r = readSmallFile(rendererFile());
        // SurrealEngine: OpenGL ES unless Vulkan was picked (android_main.cpp); HP3: Vulkan unless OpenGL ES was
        boolean gl = isHp3() ? "opengl".equals(r) : !"vulkan".equals(r);
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
        File file = rendererFile();
        String key = restoreKey(isHp3());
        android.content.SharedPreferences prefs = getSharedPreferences("setup", MODE_PRIVATE);
        if (!remember.isChecked() && !prefs.contains(key)) {
            // for this launch only: keep what to put back on the next one
            String previous = readSmallFile(file);
            prefs.edit().putString(key, previous != null ? previous : "").apply();
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
        refreshSlots();
        if (splashUntil == Long.MAX_VALUE) {
            splashUntil = android.os.SystemClock.uptimeMillis() + 800; // back from the channel
            tryStart();
        }
        if (started) {
            return;
        }
        if (!hasStorageAccess(this)) {
            if (Build.VERSION.SDK_INT >= 30) {
                status.setText(tr("Allow \"All files access\" for Flipendo, then come back."));
                if (!askedPermission) {
                    askedPermission = true;
                    try {
                        startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                Uri.parse("package:" + getPackageName())));
                    } catch (RuntimeException e) {
                        // an Android TV often has no per-app page: the general list, and if even that is missing, the way to grant it by hand
                        try {
                            startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                        } catch (RuntimeException e2) {
                            status.setText("This device has no \"All files access\" screen. Allow it from a computer: adb shell appops set "
                                    + getPackageName() + " MANAGE_EXTERNAL_STORAGE allow");
                        }
                    }
                }
            } else {
                // Android 10: no "All files access" (isExternalStorageManager is Android 11; calling it crashed the app at
                // once, POCO X3): the storage permission, with the legacy whole-storage view from the manifest
                status.setText(tr("Allow access to the phone's storage for Flipendo: it reads the game from there."));
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
                status.setText(tr("Flipendo needs access to the phone's storage to read the game. Open the app again to allow it."));
            }
        }
    }

    /** Unpacks the game data if this APK carries it, then starts the game. */
    private void begin() {
        if (isPs1()) {
            // The PS1 games need no PC files: the PC game is looked for when the PC tab is opened.
            pcBegun = false;
            applyPlatform();
            return;
        }
        pcBegun = true;
        gameMissing = false;
        new Thread(new Runnable() {
            public void run() {
                // a folder with only Game.exe picked under the HP1 key (an early build) belongs to HP2
                {
                    android.content.SharedPreferences p = getSharedPreferences("setup", MODE_PRIVATE);
                    String old = p.getString(PREF_GAME_DIR, null);
                    if (old != null && findGameFor(false, new File(old), 2) == null && findGameFor(true, new File(old), 2) != null) {
                        p.edit().putString(PREF_GAME_DIR_HP2, old).remove(PREF_GAME_DIR).apply();
                    }
                }
                // The two games are independent: only the chosen one's files are touched, and the other may be missing altogether.
                // /sdcard/FlipendoHP also holds the port's own files (log, renderer choice) for both, so it has to exist.
                new File(FlipendoActivity.GAME_DIR).mkdirs();
                final String chosen = chosenGame();
                final boolean hp1 = "hp1".equals(chosen), hp2 = "hp2".equals(chosen);
                boolean text = false;
                if (hp1) {
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
                    text = languageHasText(SetupActivity.this, chosenLanguage(SetupActivity.this));
                    if (!text) {
                        // before a self pack puts its editions' files in: the originals the translation replaced go back first
                        try {
                            removeTextTranslation(new File(FlipendoActivity.GAME_DIR));
                        } catch (IOException e) {
                            android.util.Log.e("flipendo", "removing the text translation failed: " + e);
                        }
                    }
                    installGameData();
                } else if (hp2) {
                    installHp2Data();
                } else {
                    installBundle(HP3_BUNDLE, new File(HP3_DIR), tr("Installing Harry Potter 3 "), tr("Installing Harry Potter 3 failed: "));
                }
                // the folder the player picked, else the default place (a self pack unpacks there)
                File found = locateGame(chosen);
                final String picked = getSharedPreferences("setup", MODE_PRIVATE).getString(gameDirKey(), null);
                // HP2: the language the in-game switch wished for becomes the choice, then the language's files go in
                if (found != null && hp2) {
                    File wish2 = new File(found, LANG_WANT);
                    String wished2 = readSmallFile(wish2);
                    if (wished2 != null && !wished2.isEmpty()) {
                        try {
                            writeSmallFile(new File(found, CHOSEN_LANG), wished2);
                        } catch (IOException e) {
                            android.util.Log.e("flipendo", "could not keep the HP2 language choice: " + e);
                        }
                    }
                    wish2.delete();
                    try {
                        applyHp2Language(found);
                    } catch (IOException e) {
                        android.util.Log.e("flipendo", "the HP2 texts failed: " + e);
                    }
                }
                // HP3: the phone ini (HP3's own Default.ini with the Android drivers), made once
                if (found != null && "hp3".equals(chosen)) {
                    try {
                        Hp3Setup.prepareIni(found);
                    } catch (IOException e) {
                        android.util.Log.e("flipendo", "the HP3 ini failed: " + e);
                    }
                    try {
                        applyHp3Language(found);
                    } catch (IOException e) {
                        android.util.Log.e("flipendo", "the HP3 language failed: " + e);
                    }
                }
                // the translation, dubbing and movement mod are HP1's
                if (found != null && hp1) {
                    try {
                        if (text) {
                            applyTextTranslation(found);
                        } else {
                            removeTextTranslation(found);
                        }
                    } catch (IOException e) {
                        android.util.Log.e("flipendo", "the text translation failed: " + e);
                    }
                    applyAddonLanguage(found, chosenLanguage(SetupActivity.this));
                    try {
                        applyVoice(found, VOICE_LANG.equals(chosenLanguage(SetupActivity.this)));
                    } catch (IOException e) {
                        android.util.Log.e("flipendo", "the dubbing failed: " + e);
                    }
                    // the mod to have: the in-game wish (the Devil's Snare prompt asks for "none" once), else the movement mod, which is on
                    // by default (the first one this APK carries or has imported; none if there is none)
                    File modWish = new File(FlipendoActivity.GAME_DIR, MOD_WANT);
                    String modWanted = readSmallFile(modWish);
                    if (modWanted == null || modWanted.isEmpty()) {
                        List<String[]> available = readMods(SetupActivity.this);
                        modWanted = !available.isEmpty() ? available.get(0)[0] : readSmallFile(new File(found, MOD_MARKER));
                    }
                    try {
                        applyMod(found, modWanted, new byte[1 << 20]);
                    } catch (IOException e) {
                        android.util.Log.e("flipendo", "switching the mod failed: " + e);
                        say(tr("Switching the mod failed: ") + e.getMessage(), 0, 1);
                        try {
                            Thread.sleep(6000);
                        } catch (InterruptedException ignored) {
                        }
                    }
                    modWish.delete();
                    // texture packs go into the folder in use (the Russian menu art with a Russian language)
                    try {
                        applyTexturePacks(found, chosenLanguage(SetupActivity.this).startsWith("ru"), hdTexturesWanted(SetupActivity.this));
                    } catch (IOException e) {
                        android.util.Log.e("flipendo", "the texture packs failed: " + e);
                        say(e.getMessage() != null ? e.getMessage() : tr("HD textures"), 0, 1);
                        try {
                            Thread.sleep(6000);
                        } catch (InterruptedException ignored) {
                        }
                    }
                }
                // the game's pictures (thumbnails, save snaps, textures) stay out of the phone's gallery
                if (found != null) {
                    try {
                        File nomedia = new File(found, ".nomedia");
                        if (!nomedia.exists()) {
                            nomedia.createNewFile();
                        }
                    } catch (IOException e) {
                        android.util.Log.e("flipendo", "could not make .nomedia: " + e);
                    }
                }
                final File game = found;
                ui.post(new Runnable() {
                    public void run() {
                        if (game == null) {
                            // The menu stays usable (other games, the PS1 tab, the folder settings): the missing files are said
                            // in place, and Play turns into "Add game files", which opens the card that explains it.
                            gameMissing = true;
                            missingPicked = picked;
                            gameReady = false;
                            applyPlatform();
                            return;
                        }
                        gameMissing = false;
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
        // a restart from inside the game keeps the start choice, except a switch to the other game: HP2 needs its own (load a slot or begin)
        if (!getIntent().getBooleanExtra("restart", false) || getIntent().hasExtra("game")) {
            writeStartFile();
        }
        gameReady = false;
        Intent intent;
        if (isHp3()) {
            // HP3 runs on its own engine (flipendo-droid-2004) in its own process, straight into the game
            intent = new Intent().setClassName(this, HP3_ACTIVITY);
            intent.putExtra(HP3_EXTRA_DATA_ROOT, gameDir).putExtra(HP3_EXTRA_HP3, true);
        } else {
            intent = new Intent(this, FlipendoActivity.class);
            if (gameDir != null) {
                intent.putExtra(EXTRA_GAME_DIR, gameDir);
            }
        }
        startActivity(intent);
        finish();
    }

    /** Which game the launcher starts: "hp1" (default), "hp2" or "hp3". Each has its own game folder (a pref and a default place). */
    static final String PREF_GAME = "game";
    static final String HP2_DIR = "/sdcard/FlipendoHP2";
    static final String PREF_GAME_DIR_HP2 = "gameDir.hp2";
    static final String HP3_DIR = "/sdcard/FlipendoHP3";
    static final String PREF_GAME_DIR_HP3 = "gameDir.hp3";
    // HP3's engine (flipendo-droid-2004): its activity and the intent extras it reads
    static final String HP3_ACTIVITY = "com.ut2004.android.GameActivity";
    static final String HP3_EXTRA_DATA_ROOT = "com.ut2004.android.DATA_ROOT";
    static final String HP3_EXTRA_HP3 = "com.ut2004.android.HP3";

    /** Whether this APK carries Harry Potter 3's engine (a build without its libraries has no GameActivity). */
    private static boolean hp3Present() {
        try {
            Class.forName("com.ut2004.android.GameActivity", false, SetupActivity.class.getClassLoader());
            return true;
        } catch (Throwable t) {
            return false;
        }
    }

    /** The chosen game: "hp1", "hp2" or "hp3" (anything else is HP1; HP3 only when this APK carries its engine). */
    private String chosenGame() {
        String game = getSharedPreferences("setup", MODE_PRIVATE).getString(PREF_GAME, "hp1");
        return "hp2".equals(game) || ("hp3".equals(game) && hp3Present()) ? game : "hp1";
    }

    private boolean isHp2() {
        return "hp2".equals(chosenGame());
    }

    private boolean isHp3() {
        return "hp3".equals(chosenGame());
    }

    /** The pref that holds the folder the player picked for a game. */
    static String dirKey(String game) {
        return pick(game, PREF_GAME_DIR, PREF_GAME_DIR_HP2, PREF_GAME_DIR_HP3);
    }

    /** A game's default folder (a self pack unpacks there). */
    static String defaultDir(String game) {
        return pick(game, FlipendoActivity.GAME_DIR, HP2_DIR, HP3_DIR);
    }

    /** The file a game folder is recognised by, in its System folder. */
    static String exeName(String game) {
        return pick(game, "HP.exe", "Game.exe", Hp3Setup.EXE);
    }

    /** A folder with the game: HP3's by Hp3Setup, HP1's and HP2's as findGameFor(boolean) looks. */
    static File findGameFor(String game, File dir, int depth) {
        return "hp3".equals(game) ? Hp3Setup.findGame(dir, depth) : findGameFor("hp2".equals(game), dir, depth);
    }

    /** The pref that holds the folder the player picked for the chosen game. */
    private String gameDirKey() {
        return dirKey(chosenGame());
    }

    /** A folder with the chosen game: System/Game.exe for HP2, System/HP.exe for HP1. */
    static File findGameFor(boolean hp2, File dir, int depth) {
        String exe = hp2 ? "Game.exe" : "HP.exe";
        if (new File(dir, "System/" + exe).isFile() || new File(dir, "system/" + exe).isFile()) {
            return dir;
        }
        File[] children = depth > 0 ? dir.listFiles() : null;
        if (children != null) {
            for (File child : children) {
                if (child.isDirectory() && !child.getName().startsWith(".")) {
                    File found = findGameFor(hp2, child, depth - 1);
                    if (found != null) {
                        return found;
                    }
                }
            }
        }
        return null;
    }

    /** HP2's own Russian text files, shipped with the Russian edition in a harry2 folder beside System (Core.rus, Engine.rus,
     *  Game.rus, HpDialog.rus, HpMenu.rus, Startup.rus...). The game's ini says Language=rus, so with the files in System it
     *  reads them instead of the .int texts. Put in for a Russian language choice, taken out for any other; HP1's translation,
     *  dubbing and mod (the rest of the language machinery) are not used for HP2. */
    /**
     * The game's text language in its ini files (Engine.Engine Language=): rus for the Russian choice; for English the .usa texts
     * (HpDialog.usa, HpMenu.usa...) when the folder has them, which is the case in the Russian edition whose .int files are
     * Russian; else int. Without it an English choice on that edition read the Russian .int texts and showed no subtitles.
     */
    private void setHp2IniLanguage(File system, boolean russian) {
        String value = russian ? "rus" : (findIgnoringCase(system, "HpDialog.usa").isFile() ? "usa" : "int");
        writeIniLanguage(system, value, "default.ini", "SE-Game.ini");
    }

    private void applyHp2Language(File game) throws IOException {
        File pack = findIgnoringCase(game, "harry2");
        File system = systemDir(game);
        String code = chosenHp2Language(game);
        boolean edition = new File(new File(game, HP2_EDITIONS), code).isDirectory();
        boolean russian = edition || "rutext".equals(code);
        // an edition of the game (its own voices and texts): installed over the base game, the previous one taken out first
        applyHp2Edition(game, edition ? code : null);
        // the Russian texts (the .int files of that edition are UTF-16, the harry2 .rus Windows-1251) need the Cyrillic glyphs
        File textMarker = new File(game, TEXT_MARKER);
        if (russian) {
            writeSmallFile(textMarker, "hp2-rus");
        } else {
            textMarker.delete();
        }
        if (system != null) {
            setHp2IniLanguage(system, russian);
        }
        if (edition || pack == null || !pack.isDirectory() || system == null) {
            return;
        }
        File[] files = pack.listFiles();
        if (files == null) {
            return;
        }
        byte[] buffer = new byte[1 << 16];
        for (File f : files) {
            String name = f.getName();
            // the text packs only: the DLLs and default.ini of that folder are for the Windows game
            if (!f.isFile() || !name.toLowerCase(java.util.Locale.ROOT).endsWith(".rus") || name.equalsIgnoreCase("SAPFont.rus")) {
                continue;
            }
            File target = new File(system, name);
            if (russian) {
                if (!target.isFile() || target.length() != f.length()) {
                    java.io.InputStream in = new java.io.FileInputStream(f);
                    java.io.OutputStream out = new java.io.FileOutputStream(target);
                    try {
                        int n;
                        while ((n = in.read(buffer)) > 0) {
                            out.write(buffer, 0, n);
                        }
                    } finally {
                        in.close();
                        out.close();
                    }
                }
            } else if (target.isFile()) {
                target.delete();
            }
        }
    }

    // ---- HP2 editions: a Russian release's voices and texts, next to the base game ----
    // An edition is a folder <HP2>/editions/<code>/ holding the files that differ from the base game (Sounds/AllDialog.uax,
    // System/*.rus ...) and edition.txt (its name). A self pack carries them (build-apk.sh --hp2-edition), a light install gets
    // them from a cache zip. Choosing one puts its files over the game, keeping what they replace in .hp2-edition-backup;
    // choosing English or the Russian text only gives the originals back.
    static final String HP2_EDITIONS = "editions";
    static final String HP2_EDITION_MARKER = ".hp2-edition";
    static final String HP2_EDITION_BACKUP = ".hp2-edition-backup";

    /** The languages an HP2 folder offers: English, the Russian text (harry2 folder), and each edition. */
    static List<String[]> readHp2Languages(File game) {
        List<String[]> out = new ArrayList<>();
        out.add(new String[] { "en", "English" });
        File pack = findIgnoringCase(game, "harry2");
        File[] packFiles = pack.isDirectory() ? pack.listFiles() : null;
        if (packFiles != null) {
            for (File f : packFiles) {
                if (f.isFile() && f.getName().toLowerCase(java.util.Locale.ROOT).endsWith(".rus")) {
                    out.add(new String[] { "rutext", "Русский (текст)" });
                    break;
                }
            }
        }
        File[] editions = new File(game, HP2_EDITIONS).listFiles();
        if (editions != null) {
            java.util.Arrays.sort(editions);
            for (File e : editions) {
                if (e.isDirectory()) {
                    String label = readSmallFile(new File(e, "edition.txt"));
                    out.add(new String[] { e.getName(), label != null && !label.isEmpty() ? label : e.getName() });
                }
            }
        }
        return out;
    }

    /** The language chosen for an HP2 folder (CHOSEN_LANG in it); the first time the Russian text when HP1 is set to a Russian one. */
    static String chosenHp2Language(File game) {
        String chosen = readSmallFile(new File(game, CHOSEN_LANG));
        List<String[]> languages = readHp2Languages(game);
        for (String[] l : languages) {
            if (l[0].equals(chosen)) {
                return chosen;
            }
        }
        String hp1 = readSmallFile(new File(FlipendoActivity.GAME_DIR, CHOSEN_LANG));
        if (hp1 != null && hp1.toLowerCase(java.util.Locale.ROOT).startsWith("ru")) {
            for (String[] l : languages) {
                if (l[0].equals("rutext")) {
                    return "rutext";
                }
            }
        }
        return "en";
    }

    /** Puts the edition's files over the game (null: none, the originals back). */
    private void applyHp2Edition(File game, String code) throws IOException {
        File marker = new File(game, HP2_EDITION_MARKER);
        File backup = new File(game, HP2_EDITION_BACKUP);
        String installed = readSmallFile(marker);
        byte[] buffer = new byte[1 << 20];
        List<String> files = new ArrayList<>();
        if (code != null) {
            listFiles(new File(new File(game, HP2_EDITIONS), code), "", files);
            files.remove("edition.txt");
        }
        // what is installed is checked against the edition's files: a changed or missing one means a fresh install
        boolean current = code != null && code.equals(installed);
        if (current) {
            File source = new File(new File(game, HP2_EDITIONS), code);
            for (String relative : files) {
                if (findSound(game, relative).length() != new File(source, relative).length()) {
                    current = false;
                }
            }
        }
        if (current) {
            return;
        }
        if (installed != null && !installed.isEmpty()) {
            // take the installed edition out: its files go, the originals come back
            List<String> mine = new ArrayList<>();
            listFiles(new File(new File(game, HP2_EDITIONS), installed), "", mine);
            mine.remove("edition.txt");
            for (String relative : mine) {
                findSound(game, relative).delete();
            }
            List<String> originals = new ArrayList<>();
            listFiles(backup, "", originals);
            for (String relative : originals) {
                File target = findSound(game, relative);
                target.getParentFile().mkdirs();
                target.delete();
                new File(backup, relative).renameTo(target);
            }
            deleteTree(backup);
            marker.delete();
        }
        if (code == null) {
            return;
        }
        File source = new File(new File(game, HP2_EDITIONS), code);
        int done = 0;
        for (String relative : files) {
            File target = findSound(game, relative);
            File saved = new File(backup, relative);
            say(tr("Installing the edition ") + (done + 1) + " / " + files.size() + "\n" + relative, done, files.size());
            if (target.exists() && !saved.exists()) {
                saved.getParentFile().mkdirs();
                target.renameTo(saved);
            }
            if (relative.toLowerCase(java.util.Locale.ROOT).startsWith("sounds/alldialog.")) {
                // the base game's spoken dialogue (AllDialog.USA_uax, .RUS_uax ...) is put aside: the edition's own is the one to load
                File sounds = soundsDir(game);
                File[] others = sounds.listFiles();
                if (others != null) {
                    for (File other : others) {
                        String n = other.getName().toLowerCase(java.util.Locale.ROOT);
                        if (n.startsWith("alldialog.") && n.endsWith("_uax")) {
                            File moved = new File(backup, "Sounds/" + other.getName());
                            moved.getParentFile().mkdirs();
                            if (!moved.exists()) {
                                other.renameTo(moved);
                            }
                        }
                    }
                }
            }
            copyFile(new File(source, relative), findSound(game, relative), buffer);
            done++;
        }
        writeSmallFile(marker, code);
    }

    /** Whether the folder is HP2's (System/Game.exe, no System/HP.exe). */
    static boolean isHp2Folder(String dir) {
        if (dir == null) {
            return false;
        }
        File d = new File(dir);
        return findGameFor(true, d, 0) != null && findGameFor(false, d, 0) == null;
    }

    /** The folder with System/HP.exe: the game folder itself, or one or two levels inside it (a copy made one folder too deep). */
    static File findGame(File dir, int depth) {
        if (new File(dir, "System/HP.exe").isFile() || new File(dir, "system/HP.exe").isFile()
                || new File(dir, "System/Game.exe").isFile() || new File(dir, "system/Game.exe").isFile()) {
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

    /** Like rounded(), and with a white ring while a remote or gamepad has the focus (a TV has no touch to show what is selected). */
    private android.graphics.drawable.Drawable roundedFocus(int color, float radiusDp, int strokeColor) {
        android.graphics.drawable.StateListDrawable states = new android.graphics.drawable.StateListDrawable();
        android.graphics.drawable.GradientDrawable focused = rounded(color, radiusDp, 0xFFFFFFFF);
        focused.setStroke(dp(2), 0xFFFFFFFF);
        states.addState(new int[] { android.R.attr.state_focused }, focused);
        states.addState(new int[0], rounded(color, radiusDp, strokeColor));
        return states;
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

    /** The card of the problem and settings screens, with its header (gold accent, title, the app's name) and a divider. */
    private LinearLayout cardWithHeader(android.widget.FrameLayout screen, String title) {
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
        app.setText(tr("Flipendo for Android ") + LogReport.version(this));
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
        return card;
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

        LinearLayout card = cardWithHeader(screen, title);

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
        inProblemCard = false;
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

    // ---- Game folders: where each game is, picked separately ----
    private static final int REQUEST_PICK_HP1 = 11, REQUEST_PICK_HP2 = 12, REQUEST_PICK_HP3 = 14;
    private boolean foldersChanged; // a folder was picked or reset: the game is looked for again when the screen closes

    private void pickFolderFor(String game) {
        try {
            startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE),
                    "hp3".equals(game) ? REQUEST_PICK_HP3 : "hp2".equals(game) ? REQUEST_PICK_HP2 : REQUEST_PICK_HP1);
        } catch (RuntimeException e) {
            android.widget.Toast.makeText(this, tr("No folder picker on this phone: copy the game to ")
                    + defaultDir(game) + tr(" instead"), android.widget.Toast.LENGTH_LONG).show();
        }
    }

    /** One game's folder in the settings: its name, where it is (or why not), Choose folder and Use default. */
    private android.view.View folderRow(final String game) {
        final String picked = getSharedPreferences("setup", MODE_PRIVATE).getString(dirKey(game), null);
        String fallback = defaultDir(game);
        File found = hasStorageAccess(this) ? locateGame(game) : null;

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(dp(14), dp(12), dp(14), dp(12));
        row.setBackground(rounded(0x14FFFFFF, 14, 0x22FFFFFF));

        TextView name = new TextView(this);
        name.setTextColor(GOLD);
        name.setTextSize(12);
        name.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        name.setLetterSpacing(0.12f);
        name.setText(pick(game, tr("HARRY POTTER 1: PHILOSOPHER'S STONE"), tr("HARRY POTTER 2: CHAMBER OF SECRETS"),
                tr("HARRY POTTER 3: PRISONER OF AZKABAN")));
        row.addView(name);

        TextView where = new TextView(this);
        where.setTextSize(14);
        where.setTextIsSelectable(true);
        where.setLineSpacing(0, 1.1f);
        if (found != null) {
            where.setTextColor(0xFF9BE39B);
            where.setText(found.getPath() + (picked == null || !new File(picked).equals(found) ? tr("  (default place)") : ""));
        } else if (picked != null) {
            where.setTextColor(0xFFFF8A80);
            where.setText(tr("No game in ") + picked + tr(" (looked for System/") + exeName(game) + tr(" there and one or two folders deeper)"));
        } else {
            where.setTextColor(MUTED);
            where.setText(tr("Not found yet. Looked in ") + fallback + ".");
        }
        LinearLayout.LayoutParams wp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        wp.topMargin = dp(6);
        row.addView(where, wp);

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        TextView choose = pill(tr("Choose folder"), false, new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                pickFolderFor(game);
            }
        });
        choose.setTextSize(14);
        choose.setPadding(dp(16), dp(8), dp(16), dp(8));
        buttons.addView(choose);
        if (picked != null) {
            TextView reset = pill(tr("Use default"), false, new android.view.View.OnClickListener() {
                public void onClick(android.view.View v) {
                    getSharedPreferences("setup", MODE_PRIVATE).edit().remove(dirKey(game)).apply();
                    foldersChanged = true;
                    showFolderSettings();
                }
            });
            reset.setTextSize(14);
            reset.setPadding(dp(16), dp(8), dp(16), dp(8));
            LinearLayout.LayoutParams rp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            rp.leftMargin = dp(10);
            buttons.addView(reset, rp);
        }
        LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        bp.topMargin = dp(10);
        row.addView(buttons, bp);
        return row;
    }

    private boolean inFolderSettings;

    /** The launcher's settings page: the folder of each game (the two are independent; either may be missing). */
    private void showFolderSettings() {
        inFolderSettings = true;
        android.widget.FrameLayout screen = new android.widget.FrameLayout(this);
        screen.setBackgroundColor(0xFF000000);
        screen.setPadding(dp(16), dp(16), dp(16), dp(16));
        LinearLayout card = cardWithHeader(screen, tr("Game folders"));

        android.widget.ScrollView scroll = new android.widget.ScrollView(this);
        LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL);
        TextView intro = new TextView(this);
        intro.setTextColor(0xDDFFFFFF);
        intro.setTextSize(14);
        intro.setLineSpacing(0, 1.15f);
        intro.setText(tr("Where each game is on this phone. Pick the folder you copied from the PC (the one with System, Maps, ")
                + tr("Textures and Sounds); a folder one or two levels above it works too. The games are independent: ")
                + tr("either one starts without the other's files."));
        body.addView(intro);
        for (String game : hp3Present() ? new String[] { "hp1", "hp2", "hp3" } : new String[] { "hp1", "hp2" }) {
            LinearLayout.LayoutParams rp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            rp.topMargin = dp(12);
            body.addView(folderRow(game), rp);
        }
        scroll.addView(body);
        card.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout footer = new LinearLayout(this);
        footer.setOrientation(LinearLayout.HORIZONTAL);
        footer.setGravity(Gravity.CENTER_VERTICAL | Gravity.END);
        footer.addView(pill(tr("Done"), true, new android.view.View.OnClickListener() {
            public void onClick(android.view.View v) {
                closeFolderSettings();
            }
        }));
        LinearLayout.LayoutParams fl = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        fl.topMargin = dp(16);
        card.addView(footer, fl);

        int width = Math.min(getResources().getDisplayMetrics().widthPixels - dp(32), dp(760));
        screen.addView(card, new android.widget.FrameLayout.LayoutParams(width, android.widget.FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER));
        setContentView(screen);
    }

    private void closeFolderSettings() {
        inFolderSettings = false;
        inProblemCard = false;
        if (foldersChanged && started) {
            foldersChanged = false;
            backToStart(); // the game is looked for again in its folder
        } else {
            setContentView(startScreen);
        }
        refreshSlots();
    }

    @Override
    public void onBackPressed() {
        if (inFolderSettings) {
            closeFolderSettings();
            return;
        }
        if (inProblemCard) {
            leaveProblemCard();
            return;
        }
        super.onBackPressed();
    }

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
                android.widget.Toast.makeText(SetupActivity.this, tr("No folder picker on this phone: copy the game to ")
                        + FlipendoActivity.GAME_DIR + tr(" instead"), android.widget.Toast.LENGTH_LONG).show();
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
        if (requestCode == REQUEST_PS1_FOLDER || requestCode == REQUEST_PS1_DISC || requestCode == REQUEST_PS1_BIOS) {
            handlePs1Pick(requestCode, resultCode, data);
            return;
        }
        if (requestCode == REQUEST_PICK_HD) {
            Uri zip = resultCode == RESULT_OK && data != null ? data.getData() : null;
            if (zip == null) {
                if (hdBox != null) {
                    hdBox.setChecked(false); // nothing chosen: the box goes back to off
                }
                return;
            }
            try {
                getContentResolver().takePersistableUriPermission(zip, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (RuntimeException ignored) {
                // all files access still lets the read work for this launch
            }
            getSharedPreferences("setup", MODE_PRIVATE).edit().putString(PREF_HD_ZIP, zip.toString()).putBoolean(PREF_HD, true).apply();
            android.widget.Toast.makeText(this, displayName(this, zip) + tr(" will be unpacked when you press Play"), android.widget.Toast.LENGTH_LONG).show();
            return;
        }
        if (requestCode == REQUEST_PICK_HP1 || requestCode == REQUEST_PICK_HP2 || requestCode == REQUEST_PICK_HP3) {
            String game = requestCode == REQUEST_PICK_HP3 ? "hp3" : requestCode == REQUEST_PICK_HP2 ? "hp2" : "hp1";
            if (resultCode == RESULT_OK && data != null) {
                String picked = treeUriToPath(data.getData());
                if (picked == null) {
                    android.widget.Toast.makeText(this, tr("That place can't be read as a folder: pick one on the phone or an SD card"), android.widget.Toast.LENGTH_LONG).show();
                } else if (findGameFor(game, new File(picked), 2) == null) {
                    android.widget.Toast.makeText(this, "No " + pick(game, tr("Harry Potter 1"), tr("Harry Potter 2"), tr("Harry Potter 3")) + " in " + picked
                            + " (System/" + exeName(game) + tr(" not found)"), android.widget.Toast.LENGTH_LONG).show();
                } else {
                    getSharedPreferences("setup", MODE_PRIVATE).edit().putString(dirKey(game), picked).apply();
                    foldersChanged = true;
                }
            }
            showFolderSettings();
            return;
        }
        if (requestCode != REQUEST_PICK_FOLDER || resultCode != RESULT_OK || data == null) {
            return;
        }
        String path = treeUriToPath(data.getData());
        if (path == null) {
            showWrongFolder(tr("(a place the app can't read as a folder)"));
            return;
        }
        // remembered even when it's the wrong one: the next start says so instead of asking from scratch
        getSharedPreferences("setup", MODE_PRIVATE).edit().putString(gameDirKey(), path).apply();
        if (findGameFor(chosenGame(), new File(path), 2) == null) {
            showWrongFolder(path);
        } else {
            backToStart();
        }
    }

    /** There is no game on the phone yet: say where to put it, or let the player pick the folder it is in. */
    private void showMissingGame() {
        String game = chosenGame();
        String name = pick(game, tr("Harry Potter 1"), tr("Harry Potter 2"), tr("Harry Potter 3"));
        inProblemCard = true;
        showCard(name + tr(": game data not found"),
                tr("Copy the game folder from your PC (the one that contains System, Maps, Textures, Sounds and Music) to the phone, ")
                        + tr("into Internal storage / ") + new File(defaultDir(game)).getName() + tr(", then tap Check again.\n\n")
                        + tr("Already copied it somewhere else? Tap Choose folder and pick it (or the folder it is in).") + "\n\n"
                        + tr("Only this game needs these files. The other games and the PS1 tab work without them."),
                tr("Choose folder"), pickFolder, tr("Check again"), checkAgain, tr("Back"), backFromProblem);
    }

    private final android.view.View.OnClickListener backFromProblem = new android.view.View.OnClickListener() {
        public void onClick(android.view.View v) {
            leaveProblemCard();
        }
    };

    /** Back from the missing-files card to the menu, without looking for the game again. */
    private void leaveProblemCard() {
        inProblemCard = false;
        setContentView(startScreen);
        applyPlatform();
    }

    /** The folder the player picked has no game in it. */
    private void showWrongFolder(String path) {
        inProblemCard = true;
        showCard(tr("This isn't the game folder"),
                tr("There is no game in ") + path + tr(" (looked for System/") + exeName(chosenGame()) + tr(" there and one or two folders deeper)") + ".\n\n"
                        + tr("Pick the folder you copied from the PC: it contains System, Maps, Textures, Sounds and Music. ")
                        + tr("Or copy the game into Internal storage / ") + new File(defaultDir(chosenGame())).getName() + tr(" and tap Check again."),
                tr("Choose another folder"), pickFolder, tr("Check again"), checkAgain, tr("Back"), backFromProblem);
    }

    /** Set when the game stopped on its own (FlipendoActivity.onGameStopped) or the last run of the app crashed. */
    private String previousProblem() {
        if (getIntent().getBooleanExtra("restart", false)) {
            return null; // the game was closed on purpose (the language switch)
        }
        String stopped = getIntent().getStringExtra("stopped");
        if (stopped != null) {
            String fatal = LogReport.lastFatal();
            return tr("The game stopped (") + stopped + ")." + (fatal != null ? "\n\n" + fatal : "");
        }
        if (Build.VERSION.SDK_INT >= 30) {
            long acknowledged = getSharedPreferences("setup", MODE_PRIVATE).getLong("ack", 0);
            ActivityManager am = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
            for (ApplicationExitInfo info : am.getHistoricalProcessExitReasons(getPackageName(), 0, 3)) {
                int reason = info.getReason();
                if (info.getTimestamp() > acknowledged
                        && (reason == ApplicationExitInfo.REASON_CRASH_NATIVE || reason == ApplicationExitInfo.REASON_CRASH
                        || reason == ApplicationExitInfo.REASON_ANR || reason == ApplicationExitInfo.REASON_INITIALIZATION_FAILURE)) {
                    return tr("The game crashed last time (") + LogReport.exitReason(reason) + ").";
                }
            }
        }
        return null;
    }

    /** The game crashed or stopped: say so, and offer to send the logs right away (the share sheet opens by itself, once). */
    private void showProblem(String text) {
        showCard(tr("Something went wrong"),
                text + tr("\n\nPlease send the logs: they help fix it. Pick an app to send them with (Telegram, mail, ...), ")
                        + tr("or share them in t.me/flipendodroid. A copy is also saved in Download."),
                tr("Send logs"), sendLogs,
                tr("Play anyway"), new android.view.View.OnClickListener() {
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
        out.addAll(findAddonLanguages(hp1Folder(context)));
        return out;
    }

    // Another release's language files (the German edition's HPMenu.ger, Sounds/AllDialog.ger_uax, ...) copied into the HP1 folder
    // are a language of their own: the game takes its texts and voices from them when its ini says Language=<ext>.
    static final String ADDON_PREFIX = "addon-";
    private static final String[][] ADDON_NAMES = {
        { "ger", "Deutsch" }, { "fra", "Français" }, { "est", "Español" }, { "ita", "Italiano" }, { "dut", "Nederlands" },
        { "swe", "Svenska" }, { "dan", "Dansk" }, { "nor", "Norsk" }, { "fin", "Suomi" }, { "por", "Português" },
        { "pol", "Polski" }, { "cze", "Čeština" }, { "hun", "Magyar" },
    };

    /** The HP1 folder in use (the one the player picked, else the default place); null when there is none. */
    static File hp1Folder(android.content.Context context) {
        String picked = context.getSharedPreferences("setup", android.content.Context.MODE_PRIVATE).getString(dirKey("hp1"), null);
        File found = picked != null ? findGameFor("hp1", new File(picked), 2) : null;
        return found != null ? found : findGameFor("hp1", new File(defaultDir("hp1")), 2);
    }

    /** The language add-ons a game folder holds as {"addon-<ext>", name}: its System folder has an HpMenu.<ext> besides the English .int. */
    static List<String[]> findAddonLanguages(File game) {
        List<String[]> out = new ArrayList<>();
        File[] files = game != null ? systemDir(game).listFiles() : null;
        if (files == null) {
            return out;
        }
        for (String[] known : ADDON_NAMES) {
            for (File f : files) {
                if (f.isFile() && f.getName().equalsIgnoreCase("HPMenu." + known[0])) {
                    out.add(new String[] { ADDON_PREFIX + known[0], known[1] });
                    break;
                }
            }
        }
        return out;
    }

    /**
     * Puts the chosen language in the engine's ini files ([Engine.Engine] Language=): an add-on's extension, or int when the
     * choice is any other language (the translations that replace the .int files). The engine reads SE-HP.ini before Default.ini.
     */
    private static void applyAddonLanguage(File game, String chosen) {
        File system = systemDir(game);
        String value = chosen.startsWith(ADDON_PREFIX) ? chosen.substring(ADDON_PREFIX.length()) : "int";
        if (!chosen.startsWith(ADDON_PREFIX)) {
            // only an add-on's own setting is taken back; whatever else the ini says stays
            String current = readIniLanguage(system, "SE-HP.ini");
            boolean addon = false;
            for (String[] a : findAddonLanguages(game)) {
                addon |= a[0].equals(ADDON_PREFIX + current);
            }
            if (!addon) {
                return;
            }
        }
        writeIniLanguage(system, value, "Default.ini", "SE-HP.ini");
    }

    private static String readIniLanguage(File system, String name) {
        File ini = findIgnoringCase(system, name);
        try {
            if (ini.isFile()) {
                java.util.regex.Matcher m = java.util.regex.Pattern.compile("(?m)^Language=(.*?)\r?$")
                        .matcher(new String(readAll(new java.io.FileInputStream(ini)), java.nio.charset.StandardCharsets.ISO_8859_1));
                if (m.find()) {
                    return m.group(1).trim().toLowerCase(java.util.Locale.ROOT);
                }
            }
        } catch (IOException e) {
            android.util.Log.e("flipendo", "could not read the language in " + name + ": " + e);
        }
        return "";
    }

    /** Sets Language= in the given ini files of a System folder (the ones that exist, and only when it changes). */
    private static void writeIniLanguage(File system, String value, String... names) {
        for (String name : names) {
            File ini = findIgnoringCase(system, name);
            if (!ini.isFile()) {
                continue;
            }
            try {
                String text = new String(readAll(new java.io.FileInputStream(ini)), java.nio.charset.StandardCharsets.ISO_8859_1);
                String changed = text.replaceFirst("(?m)^Language=.*?(\r?)$", "Language=" + value + "$1");
                if (!changed.equals(text)) {
                    java.io.FileOutputStream out = new java.io.FileOutputStream(ini);
                    try {
                        out.write(changed.getBytes(java.nio.charset.StandardCharsets.ISO_8859_1));
                    } finally {
                        out.close();
                    }
                }
            } catch (IOException e) {
                android.util.Log.e("flipendo", "could not set the language in " + name + ": " + e);
            }
        }
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
            return tr("The game folder has no Sounds folder");
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
                return tr("No sound files of the game in ") + folder + tr(": choose the dubbing's Sounds folder (AllDialog.uax and the others)");
            }
            if (kept == 0) {
                return tr("These sound files are the same as the game's: nothing to change");
            }
            deleteTree(voices);
            voices.getParentFile().mkdirs();
            if (!temp.renameTo(voices)) {
                return tr("Could not keep the dubbing in the app's files");
            }
            return null;
        } catch (IOException e) {
            return tr("Could not read the folder: ") + e.getMessage();
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
                say(tr("Installing the dubbing ") + (done + 1) + " / " + files.size() + "\n" + relative, done, files.size());
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
                say(tr("Restoring ") + relative, 0, 1);
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
            say(tr("Installing the Russian translation ") + (done + 1) + " / " + files.size() + "\n" + name, done, files.size());
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
                say(tr("Restoring ") + original.getName(), 0, 1);
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
                        return tr("Could not open ") + name;
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
                        return name + tr(" is not a zip archive or a .u package");
                    }
                }
            }
            if (packages == 0) {
                return unknown.isEmpty() ? tr("No .u packages in what you picked: choose the mod's zip (or its .u files)")
                        : tr("These packages are not the game's own: ") + android.text.TextUtils.join(", ", unknown)
                        + tr(". Choose a mod that replaces the game's scripts (HarryPotter.u, HPBase.u, ...).");
            }
            writeSmallFile(new File(temp, MOD_LABEL), "Movement mod (your file)");
            File target = new File(mods, IMPORTED_MOD);
            deleteTree(target);
            if (!temp.renameTo(target)) {
                return tr("Could not keep the mod in the app's files");
            }
            return null;
        } catch (IOException e) {
            return tr("Could not read the file: ") + e.getMessage();
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

    /**
     * HP3's interface texts follow the launcher's language: the translations (assets/hp3lang: HpMenu and SaveTips per
     * language) go into the game's System folder, and the language is written next to it for the engine. The edition's own
     * language (Russian) leaves no file.
     */
    private void applyHp3Language(File game) throws IOException {
        String[] files = getAssets().list("hp3lang");
        if (files != null) {
            byte[] buffer = new byte[64 * 1024];
            File system = Hp3Setup.systemDir(game);
            // An install that carries the retail English texts (HpDialog.int is the biggest of them) has the full English
            // HpMenu.int and SaveTips.int: the launcher's own English, made for an edition without it, would replace them.
            boolean retailEnglish = new File(system, "HpDialog.int").isFile();
            for (String name : files) {
                if (retailEnglish && name.toLowerCase(java.util.Locale.ROOT).endsWith(".int")) {
                    continue;
                }
                copyAsset("hp3lang/" + name, new File(system, name), buffer);
            }
        }
        File choice = new File(game, Hp3Setup.LANGUAGE_FILE);
        String code = Hp3Setup.gameLanguage(Tr.CODES[Tr.index()]);
        if (code == null) {
            choice.delete();
            return;
        }
        try (OutputStream os = new FileOutputStream(choice)) {
            os.write(code.getBytes("UTF-8"));
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
            say(tr("Switching to ") + label + " " + (done + 1) + " / " + files.size() + "\n" + relative, done, files.size());
            copyAsset(path, new File(gameDir, relative), buffer);
            done++;
        }
        writeSmallFile(new File(gameDir, LANG_MARKER), code);
    }

    /**
     * An APK without packs: unpacks the zip the player chose (PREF_HD_ZIP) into the game folder, straight from the picker's stream
     * (no copy of the zip on the phone). Every .dds below a Textures folder in the zip goes to Textures/<the rest of its path>;
     * the files are listed in the manifest, so unticking the box takes them out again. Done once per zip (the marker).
     */
    private void importTextureZip(File gameDir, File marker, File manifest) throws IOException {
        String uriText = getSharedPreferences("setup", MODE_PRIVATE).getString(PREF_HD_ZIP, null);
        if (uriText == null || ("zip:" + uriText).equals(readSmallFile(marker))) {
            return;
        }
        Uri uri = Uri.parse(uriText);
        File textures = findIgnoringCase(gameDir, "Textures");
        StringBuilder installed = new StringBuilder();
        String old = readSmallFile(manifest);
        if (old != null) {
            installed.append(old);
        }
        byte[] buffer = new byte[1 << 20];
        int count = 0;
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            if (in == null) {
                throw new IOException(tr("Could not open ") + displayName(this, uri));
            }
            java.util.zip.ZipInputStream zip = new java.util.zip.ZipInputStream(new java.io.BufferedInputStream(in, 1 << 16));
            java.util.zip.ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                String[] parts = entry.getName().replace('\\', '/').split("/");
                int at = -1;
                for (int i = 0; i < parts.length - 1 && at < 0; i++) {
                    if (parts[i].equalsIgnoreCase("Textures")) {
                        at = i;
                    }
                }
                // a .dds at least a package folder deep (Textures/<package>/.../<name>.dds)
                if (entry.isDirectory() || at < 0 || parts.length - at < 3 || !parts[parts.length - 1].toLowerCase(java.util.Locale.ROOT).endsWith(".dds")) {
                    continue;
                }
                StringBuilder relative = new StringBuilder();
                for (int i = at + 1; i < parts.length; i++) {
                    if (parts[i].isEmpty() || parts[i].equals("..")) {
                        relative = null;
                        break;
                    }
                    relative.append(i > at + 1 ? "/" : "").append(parts[i]);
                }
                if (relative == null) {
                    continue;
                }
                say(tr("Installing textures ") + (count + 1) + "\n" + parts[parts.length - 1], count, count + 1);
                File target = new File(textures, relative.toString());
                target.getParentFile().mkdirs();
                try (OutputStream os = new FileOutputStream(target)) {
                    int n;
                    while ((n = zip.read(buffer)) > 0) {
                        os.write(buffer, 0, n);
                    }
                }
                installed.append(textures.getName()).append('/').append(relative).append('\n');
                count++;
            }
        }
        if (count == 0) {
            throw new IOException(displayName(this, uri) + tr(" has no Textures folder with .dds files in it"));
        }
        writeSmallFile(manifest, installed.toString());
        writeSmallFile(marker, "zip:" + uriText);
    }

    /**
     * Texture packs (the texture-overrides mod draws Textures/<package>/<group>/<name>.dds from the game folder): assets/texpacks/common
     * goes into the game folder as it is, and the main menu art of the game's language (menuart/ru for the Russian editions, else en)
     * replaces the previous language's, so one language's extra files (the Russian moontitle*.dds) never show in another. A self pack
     * carries them; for the game folder in use, whichever the player picked.
     */
    private void applyTexturePacks(File gameDir, boolean russian, boolean enabled) throws IOException {
        List<String> common = new ArrayList<>();
        collect("texpacks/common", common);
        String set = russian ? "ru" : "en";
        List<String> art = new ArrayList<>();
        collect("texpacks/menuart/" + set, art);
        File marker = new File(gameDir, ".flipendo-texpacks");
        File manifest = new File(gameDir, ".flipendo-texpacks.list"); // the files put in, relative to the game folder
        boolean carried = !common.isEmpty() || !art.isEmpty();
        if (!carried && enabled) {
            importTextureZip(gameDir, marker, manifest);
            return;
        }
        if (!enabled) {
            // switched off: what was put in comes out again
            String list = readSmallFile(manifest);
            if (list != null) {
                String[] lines = list.split("\n");
                int n = 0;
                for (String relative : lines) {
                    say(tr("Removing the HD textures ") + (n + 1) + " / " + lines.length, n, lines.length);
                    if (!relative.isEmpty()) {
                        new File(gameDir, relative).delete();
                    }
                    n++;
                }
            }
            manifest.delete();
            marker.delete();
            return;
        }
        // done already for this APK's data and this language (the assets are compressed: no cheap size check per file)
        String state = readStamp() + "|" + set;
        if (state.equals(readSmallFile(marker)) && !art.isEmpty() && new File(findIgnoringCase(findIgnoringCase(gameDir, "Textures"), "MenuArt"),
                art.get(0).substring(art.get(0).lastIndexOf('/') + 1)).isFile()) {
            return;
        }
        byte[] buffer = new byte[1 << 20];
        File textures = findIgnoringCase(gameDir, "Textures");
        int total = common.size() + art.size();
        int done = 0;
        java.util.Set<String> commonArt = new java.util.HashSet<>(); // the common packs' own main menu art (lower case names)
        StringBuilder installed = new StringBuilder();
        for (String path : common) {
            String relative = path.substring("texpacks/common/Textures/".length());
            if (relative.toLowerCase(java.util.Locale.ROOT).startsWith("menuart/")) {
                commonArt.add(relative.substring("menuart/".length()).toLowerCase(java.util.Locale.ROOT));
            }
            File target = new File(textures, relative);
            installed.append(textures.getName()).append('/').append(relative).append('\n');
            say(tr("Installing textures ") + (done + 1) + " / " + total + "\n" + target.getName(), done, total);
            copyAsset(path, target, buffer);
            done++;
        }
        File menuArt = findIgnoringCase(textures, "MenuArt");
        File[] old = menuArt.listFiles();
        if (old != null) {
            for (File f : old) {
                // a leftover of the other language: not in this set, and not one the common packs (rewritten above) bring
                if (f.getName().toLowerCase(java.util.Locale.ROOT).endsWith(".dds") && !art.contains("texpacks/menuart/" + set + "/" + f.getName())
                        && !commonArt.contains(f.getName().toLowerCase(java.util.Locale.ROOT))) {
                    f.delete();
                }
            }
        }
        for (String path : art) {
            File target = new File(menuArt, path.substring(path.lastIndexOf('/') + 1));
            installed.append(textures.getName()).append('/').append(menuArt.getName()).append('/').append(target.getName()).append('\n');
            say(tr("Installing textures ") + (done + 1) + " / " + total + "\n" + target.getName(), done, total);
            copyAsset(path, target, buffer);
            done++;
        }
        writeSmallFile(manifest, installed.toString());
        writeSmallFile(marker, state);
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
            if (wanted.equals(current)) {
                return; // installed by a self pack and not carried by this APK (a plain update): it stays as it is
            }
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
                    say(tr("Restoring ") + original.getName(), 0, 1);
                    copyFile(original, findIgnoringCase(system, original.getName()), buffer);
                }
            }
        }
        if (!wanted.equals("none")) {
            List<String[]> files = modPackages(wanted);
            int done = 0;
            for (String[] p : files) {
                String name = p[0];
                say(tr("Installing the mod ") + (done + 1) + " / " + files.size() + "\n" + name, done, files.size());
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
    /** A self pack that carries Harry Potter 2 (assets/hp2data) unpacks it into FlipendoHP2 once, when the data changes (its stamp);
     *  the player's saves are never replaced. */
    private void installHp2Data() {
        installBundle(HP2_BUNDLE, new File(HP2_DIR), tr("Installing Harry Potter 2 "), tr("Installing Harry Potter 2 failed: "));
    }

    /** Unpacks a game the self pack carries (assets/<bundle>, with a .stamp) into gameDir, once per stamp; the player's saves stay. */
    private void installBundle(String bundle, File gameDir, String installing, String failed) {
        String stamp;
        try (InputStream in = getAssets().open(bundle + "/.stamp")) {
            byte[] data = new byte[64];
            int n = in.read(data);
            stamp = new String(data, 0, Math.max(n, 0)).trim();
        } catch (IOException e) {
            return; // this APK does not carry the game
        }
        File marker = new File(gameDir, MARKER);
        if (marker.exists() && stamp.equals(readSmallFile(marker))) {
            return;
        }
        try {
            List<String> files = new ArrayList<>();
            collect(bundle, files);
            byte[] buffer = new byte[1 << 20];
            int done = 0;
            for (String path : files) {
                String relative = path.substring(bundle.length() + 1);
                done++;
                if (relative.equals(".stamp")) {
                    continue;
                }
                File out = new File(gameDir, relative);
                say(installing + done + " / " + files.size() + "\n" + relative, done, files.size());
                if (out.exists() && relative.toLowerCase().startsWith("save/")) {
                    continue; // the player's saves stay
                }
                copyAsset(path, out, buffer);
            }
            writeSmallFile(marker, stamp);
        } catch (IOException e) {
            android.util.Log.e("flipendo", "installing " + bundle + " failed: " + e);
            say(failed + e.getMessage(), 0, 1);
            try {
                Thread.sleep(8000);
            } catch (InterruptedException ignored) {
            }
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
                    say(tr("Installing game data ") + (done + 1) + " / " + files.size() + "\n" + relative, done, files.size());
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
            say(tr("Installing the game data failed: ") + e.getMessage(), 0, 1);
            try {
                Thread.sleep(8000);
            } catch (InterruptedException ignored) {
            }
        }
    }
}
