package io.github.flipendo.spike;

import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.system.Os;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.RelativeLayout;
import android.widget.TextView;

import org.libsdl.app.SDLActivity;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Plays a PS2 disc (Harry Potter and the Chamber of Secrets) with the recompiling runner (libps2main.so, built from ps2-native-recomp).
 * It runs in its own process (:ps2) on SDL3, with SDL2's API for the runner given by sdl2-compat (libSDL2ps2.so). The APK has no game code:
 * the first start with a disc has the runner translate the game's program and compile it with the compiler of the app (Ps2Toolchain),
 * once ("prepare" mode: no window, a progress bar, then the activity ends); later starts load that result from the cache and play.
 * Design: docs/ps2.md.
 */
public class Ps2Activity extends SDLActivity {
    /** The disc: an image file (.iso, .bin, .cue) or the folder of an unpacked disc. */
    public static final String EXTRA_DISC = "ps2.disc";
    /** "hp2": names the folders of this game's cache, memory card and saves. */
    public static final String EXTRA_GAME = "ps2.game";
    /** True: only translate and compile the game (the first time), show the progress, then end. */
    public static final String EXTRA_PREPARE = "ps2.prepare";
    /** Resolution, percent of the phone's pixels (25 to 100, default 100). */
    public static final String EXTRA_SCALE = "ps2.scale";

    static String tr(String en) {
        return Tr.tr(en);
    }

    private File disc, gameDir, saveDir, cacheDir, progressFile;
    private String serial = "SLES-51192", game = "hp2";
    private boolean prepare;
    private int scale = 100;
    private Ps2Pad pad;
    private Ps2Settings settings;
    private TextView fpsView;
    private android.content.SharedPreferences play;   // the in-game settings (this process only): scale, picture, FPS counter, renderer, log

    /** The FPS counter of the runner: { pictures per second, the console's rate }. */
    static native float[] nativeFps();
    private TextView progressText;
    private ProgressBar progressBar;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private PowerManager.WakeLock wake;

    /**
     * The surface is the screen's own size: the game is drawn at the internal resolution (Settings > Display > Internal resolution,
     * PS2_HWR_SCALE) and its picture is shown on the whole screen. Prepare mode has no picture.
     */
    private void applyScale() {
        if (prepare || mSurface == null) return;
        // pref "surf" (percent of the screen's pixels, default 100): a smaller surface is scaled up by the system, the final picture pass and the
        // compositor then handle fewer pixels (a test aid: adb shell run-as ... edit shared_prefs/ps2play.xml)
        final int surf = Math.max(30, Math.min(100, play.getInt("surf", 100)));
        if (surf >= 100) {
            mSurface.getHolder().setSizeFromLayout();
            return;
        }
        int[] size = screenSize();
        mSurface.getHolder().setFixedSize(Math.max(320, size[0] * surf / 100), Math.max(180, size[1] * surf / 100));
    }

    /** Fullscreen over the whole display: no status or navigation bar (a swipe from the edge shows them for a moment), and the picture reaches into the notch. */
    private void goImmersive() {
        android.view.Window w = getWindow();
        android.view.WindowManager.LayoutParams lp = w.getAttributes();
        lp.layoutInDisplayCutoutMode = android.os.Build.VERSION.SDK_INT >= 30
                ? android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                : android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        w.setAttributes(lp);
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            w.setDecorFitsSystemWindows(false);
            android.view.WindowInsetsController c = w.getInsetsController();
            if (c != null) {
                c.hide(android.view.WindowInsets.Type.systemBars() | android.view.WindowInsets.Type.displayCutout());
                c.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            w.getDecorView().setSystemUiVisibility(android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | android.view.View.SYSTEM_UI_FLAG_FULLSCREEN | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) goImmersive();
    }

    /** The game is a landscape one: SDL asks for its own orientation when it makes the window (portrait included); every request becomes landscape. */
    @Override
    public void setRequestedOrientation(int requestedOrientation) {
        super.setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "SDL2ps2", "ps2main"};
    }

    /** What libps2main.so's main() gets: the disc, the game folder (logs, caches of the runner), the memory card; no window in prepare mode. */
    @Override
    protected String[] getArguments() {
        if (prepare) return new String[] {"--disc", disc.getPath(), "--game-dir", gameDir.getPath(), "--prebuild"};
        // a file start.state in the game's save folder is loaded at the start (a quick save made on the PC loads on the phone: profiling)
        File start = new File(saveDir, "start.state");
        File replay = new File(saveDir, "start.inputs");   // a recorded session (states/run_*.inputs) played back from the start state: the same input every run
        if (start.isFile() && replay.isFile()) {
            return new String[] {"--disc", disc.getPath(), "--game-dir", gameDir.getPath(), "--window", "--pal", "--memcard", new File(saveDir, "mc.ps2").getPath(),
                    "--load-state", start.getPath(), "--replay", replay.getPath()};
        }
        if (start.isFile()) {
            return new String[] {"--disc", disc.getPath(), "--game-dir", gameDir.getPath(), "--window", "--pal", "--memcard", new File(saveDir, "mc.ps2").getPath(),
                    "--load-state", start.getPath()};
        }
        // --pal: the disc is a PAL one (50 Hz, 256 lines); a state saved in a 60 Hz run must not turn the picture into the 224 lines of NTSC
        return new String[] {"--disc", disc.getPath(), "--game-dir", gameDir.getPath(), "--window", "--pal", "--memcard", new File(saveDir, "mc.ps2").getPath()};
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Tr.init(this);
        Intent intent = getIntent();
        game = intent != null && intent.getStringExtra(EXTRA_GAME) != null ? intent.getStringExtra(EXTRA_GAME) : "hp2";
        prepare = intent != null && intent.getBooleanExtra(EXTRA_PREPARE, false);
        play = getSharedPreferences("ps2play", MODE_PRIVATE);
        scale = clampInternal(play.getInt("iscale", 25));
        String discPath = intent != null ? intent.getStringExtra(EXTRA_DISC) : null;
        disc = new File(discPath != null ? discPath : "");
        File found = Ps2Setup.serialOf(disc) != null ? disc : null;
        String s = found != null ? Ps2Setup.serialOf(found) : null;
        if (s != null) serial = s;
        gameDir = new File(getFilesDir(), "ps2/" + serial + "/game");
        cacheDir = new File(getFilesDir(), "ps2/" + serial);
        saveDir = new File(Ps2Setup.defaultDir(game), "saves");
        if (!saveDir.isDirectory() && !saveDir.mkdirs()) {
            File fallback = getExternalFilesDir(null);
            saveDir = new File(fallback != null ? fallback : getFilesDir(), "ps2/" + serial + "/saves");
            saveDir.mkdirs();
        }
        gameDir.mkdirs();
        progressFile = new File(cacheDir, "prebuild.progress");
        progressFile.delete();
        exportEnvironment();
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        goImmersive();
        applyScale();
        if (mLayout == null) return;
        if (prepare) {
            showProgress();
        } else {
            pad = new Ps2Pad(this, new Ps2Pad.Listener() {
                @Override public void onMenu() { showMenu(); }
            });
            mLayout.addView(pad, new RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            settings = new Ps2Settings(this, this, pad);
            mLayout.addView(settings, new RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            fpsView = new TextView(this);
            fpsView.setTextColor(Color.WHITE);
            fpsView.setTextSize(13);
            fpsView.setShadowLayer(4f, 0f, 0f, Color.BLACK);
            fpsView.setVisibility(android.view.View.GONE);
            RelativeLayout.LayoutParams fp = new RelativeLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            int m = (int) (10 * getResources().getDisplayMetrics().density);
            fp.setMargins(m * 4, m, 0, 0);
            mLayout.addView(fpsView, fp);
            setShowFps(play.getBoolean("fps", false));
            askForPerformance();
        }
    }

    /** The runner's switches, set before its library is loaded (the library reads them with getenv). */
    private void exportEnvironment() {
        boolean ok = Ps2Toolchain.ensure(this);
        File vu = new File(cacheDir, "vu1-live"), ee = new File(cacheDir, "ee-live");
        String[][] env = {
            {"PS2_EE_LIVE", ee.getPath()},
            {"PS2_VU_LIVE", vu.getPath()},
            {"PS2_PREBUILD_PROGRESS", progressFile.getPath()},
            {"TMPDIR", getCacheDir().getPath()},
            {"PS2_RENDERER", "hw"},
            {"PS2_SCALE", "1"},
            {"PS2_GPU_BACKEND", play.getBoolean("gles", false) ? "gl" : "vulkan"},      // measured on the phone (Mali-G615) with HP2: Vulkan is as fast as GL and uses far less of the processor
            {"PS2_FRAME_LATENCY", "3"},         // pictures in flight in the swap chain: 32 fps against 26 with the default in the heaviest scene
            {"PS2_DISPLAY_MODE", String.valueOf(play.getInt("display", 0))},
            // the renderer's pipelines a game needed are listed here and made at the next start, not in the middle of play (each costs the phone's driver tens of ms)
            {"PS2_HWR_PIPELINES", new File(gameDir, "hwr_pipelines.txt").getPath()},
            {"PS2_LOOK", "2"},                  // soft: one filtered read per screen pixel; the default sharp + edge look reads ~40 and costs ~7 ms a field on a Mali-G615 (PS2_GPU_SWEEP part D)
            // all 512 PAL lines are shown: the band of repeated rows at the bottom was the renderer's (its targets stopped at 512 of the
            // 576 rows HP2 draws before shrinking them into its display buffer), and the old cut at 448 rows also cut low subtitles
            // Settings > Display > Internal resolution: the card texels per GS pixel the renderer draws its targets with
            {"PS2_HWR_SCALE", String.format(java.util.Locale.US, "%.4f", internalScale(scale)[0])},
            {"PS2_HWR_SCALE_Y", String.format(java.util.Locale.US, "%.4f", internalScale(scale)[1])},
            // compiler processes at a time: a clang on a 250 KB source takes a few hundred MB, so half the processors, and fewer on
            // phones with little memory (the system kills the app's process when the compilers take too much of 3-4 GB)
            {"PS2_EE_LIVE_JOBS", String.valueOf(compileJobs())},
        };
        try {
            for (String[] kv : env) Os.setenv(kv[0], kv[1], true);
            // the game's vector programs met on PC (ps2-native-recomp games/<name>/vu1, packed as assets/ps2/vu1seed/<serial>/): the
            // runner compiles them while preparing, not in the middle of play, and adds programs it meets later to the same folder
            File seed = new File(cacheDir, "vu1-seed");
            copySeed(serial, seed);
            Os.setenv("PS2_VU_SEED", seed.getPath(), true);
            if (play.getBoolean("perflog", false)) Os.setenv("PS2_SPEED_LOG", "1", true);
            // Game view: Widescreen. The game's camera is widened to the screen's shape through its aspect knobs (the code that
            // computes the camera's horizontal scale; ps2-native-recomp games/<name>/aspect.txt), so more of the world is seen at the sides
            // The knobs are given in 4:3 too (they leave the code's numbers as they are without PS2_ASPECT): they are part of the
            // translated code's identity, so switching the view must not make the phone translate the whole game again
            String knobs = aspectKnobs(serial);
            if (knobs != null) {
                gameDir.mkdirs();
                File knobFile = new File(gameDir, "aspect.txt");
                try (java.io.FileWriter w = new java.io.FileWriter(knobFile)) {
                    w.write(knobs);
                }
                Os.setenv("PS2_ASPECT_KNOBS", knobFile.getPath(), true);
                if (widescreen()) {
                    int[] size = screenSize();
                    Os.setenv("PS2_ASPECT", size[0] + ":" + size[1], true);
                }
            }
            if (ok) {
                Os.setenv("PS2_LIVE_CXX", Ps2Toolchain.clang(this).getPath(), true);
                Os.setenv("PS2_LIVE_FLAGS", Ps2Toolchain.flags(this), true);
            }
            // the player's own settings file wins (adb push env.txt into the game folder, one KEY=VALUE per line)
            File extra = new File(gameDir, "env.txt");
            if (extra.isFile()) {
                try (BufferedReader r = new BufferedReader(new FileReader(extra))) {
                    for (String line; (line = r.readLine()) != null; ) {
                        line = line.trim();
                        int eq = line.indexOf('=');
                        if (line.startsWith("#") || eq <= 0) continue;
                        Os.setenv(line.substring(0, eq).trim(), line.substring(eq + 1).trim(), true);
                    }
                }
            }
        } catch (Exception e) {
            android.util.Log.e("flipendo", "PS2 environment: " + e);
        }
    }

    // ---- prepare mode: the progress of the first translation --------------------------------------------------------

    private void showProgress() {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.CENTER);
        box.setBackgroundColor(Color.BLACK);
        int pad24 = (int) (24 * getResources().getDisplayMetrics().density);
        box.setPadding(pad24 * 2, pad24, pad24 * 2, pad24);
        TextView title = new TextView(this);
        title.setText(tr("Preparing the game"));
        title.setTextColor(Color.WHITE);
        title.setTextSize(22);
        title.setGravity(Gravity.CENTER);
        box.addView(title);
        TextView note = new TextView(this);
        note.setText(tr("The game's code is translated and compiled on this phone, once. It takes some minutes; keep the app open."));
        note.setTextColor(0xFFBBBBBB);
        note.setTextSize(14);
        note.setGravity(Gravity.CENTER);
        note.setPadding(0, pad24 / 2, 0, pad24);
        box.addView(note);
        progressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progressBar.setMax(1000);
        progressBar.setIndeterminate(true);
        box.addView(progressBar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        progressText = new TextView(this);
        progressText.setTextColor(Color.WHITE);
        progressText.setTextSize(14);
        progressText.setGravity(Gravity.CENTER);
        progressText.setPadding(0, pad24 / 2, 0, 0);
        box.addView(progressText);
        mLayout.addView(box, new RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
        if (pm != null) {
            wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "flipendo:ps2prepare");
            wake.acquire(3 * 60 * 60 * 1000L);
        }
        poll();
    }

    private static final Pattern COMPILED = Pattern.compile("compiled (\\d+) of (\\d+) files");

    private void poll() {
        String line = lastLine();
        if (line != null) {
            Matcher m = COMPILED.matcher(line);
            if (m.find()) {
                int done = Integer.parseInt(m.group(1)), total = Math.max(1, Integer.parseInt(m.group(2)));
                progressBar.setIndeterminate(false);
                progressBar.setProgress(done * 1000 / total);
                progressText.setText(tr("Compiling") + " " + (done * 100 / total) + " %");
            } else if (line.startsWith("failed")) {
                progressBar.setIndeterminate(false);
                progressText.setText(tr("It did not work") + ": " + line);
            } else if (line.startsWith("translating")) {
                progressText.setText(tr("Translating the game's code"));
            }
        }
        ui.postDelayed(new Runnable() {
            @Override public void run() { poll(); }
        }, 700);
    }

    private String lastLine() {
        try (BufferedReader r = new BufferedReader(new FileReader(progressFile))) {
            return r.readLine();
        } catch (IOException e) {
            return null;
        }
    }

    @Override
    protected void onDestroy() {
        ui.removeCallbacksAndMessages(null);
        if (hintSession != null) hintSession.close();
        if (prepare && "done".equals(lastLine())) Ps2Toolchain.markPrepared(this, serial);   // the launcher's Play now plays
        if (wake != null && wake.isHeld()) wake.release();
        super.onDestroy();
    }

    // ---- the menu -----------------------------------------------------------------------------------------------------

    // ---- asking Android for performance (PS2_ANDROID_PERF=0 in env.txt turns all of it off, to compare) ------------------------------
    // The runner's heavy threads (emulation, VU1, card submit) get a performance hint session (ADPF): the system then keeps them on the big
    // cores at a frequency that fits the frame time instead of letting a bursty load sit on little or slow cores. Ported from the CFR2 app
    // (SpikeActivity.askForPerformance), where it measured as a large gain on this phone.
    static native int[] nativeThreadIds();
    // the phone's thermal state, battery temperature and charging, for the runner's profile log ("[prof] device: ..." with PS2_PROF=1)
    static native void nativeDeviceNote(String text);

    private android.os.PerformanceHintManager.Session hintSession;
    private int hintThreadCount = -1;
    private boolean frameRateSet;
    private int perfTicks;

    /** One line about the phone's heat: thermal status (0 none .. 6 shutdown), headroom (1.0 = throttling starts), battery temperature. */
    private String deviceNote() {
        StringBuilder s = new StringBuilder();
        try {
            android.os.PowerManager pm = (android.os.PowerManager) getSystemService(POWER_SERVICE);
            s.append("thermal ").append(pm.getCurrentThermalStatus());
            if (android.os.Build.VERSION.SDK_INT >= 30) s.append(String.format(java.util.Locale.US, " headroom %.2f", pm.getThermalHeadroom(10)));
        } catch (Throwable e) {
            // no thermal service
        }
        try {
            android.content.Intent b = registerReceiver(null, new android.content.IntentFilter(android.content.Intent.ACTION_BATTERY_CHANGED));
            if (b != null) {
                s.append(String.format(java.util.Locale.US, " battery %.1f C", b.getIntExtra(android.os.BatteryManager.EXTRA_TEMPERATURE, 0) / 10f));
                if (b.getIntExtra(android.os.BatteryManager.EXTRA_PLUGGED, 0) != 0) s.append(" charging");
            }
        } catch (Throwable e) {
            // no battery
        }
        return s.toString();
    }

    private final Runnable perfTick = new Runnable() {
        @Override public void run() {
            if (++perfTicks % 25 == 1) {  // once a second
                try {
                    nativeDeviceNote(deviceNote());
                } catch (Throwable e) {
                    // a runner without the hook
                }
            }
            try {
                // the game's picture rate (PAL 50 Hz): the phone's frame governor (MediaTek FPSGO here) otherwise spends the first ~13 s learning it
                // and keeps the processor and graphics clocks low meanwhile (37 fps, then 50 with nothing changed)
                if (!frameRateSet && android.os.Build.VERSION.SDK_INT >= 30 && mSurface != null && mSurface.getHolder().getSurface().isValid()) {
                    mSurface.getHolder().getSurface().setFrameRate(50f, android.view.Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
                    frameRateSet = true;
                    android.util.Log.i("ps2perf", "surface frame rate set to 50");
                }
                int[] tids = nativeThreadIds();
                if (tids.length > 0 && tids.length != hintThreadCount) {
                    // a thread joined since the last session: a new one over all the heavy threads
                    if (hintSession != null) hintSession.close();
                    android.os.PerformanceHintManager phm = getSystemService(android.os.PerformanceHintManager.class);
                    hintSession = phm.createHintSession(tids, 20_000_000L);   // a field every 20 ms (50 Hz)
                    hintThreadCount = tids.length;
                    android.util.Log.i("ps2perf", "hint session over " + tids.length + " threads: " + (hintSession != null));
                }
                // the work took longer than the target: the system raises the processor frequency for those threads
                if (hintSession != null) hintSession.reportActualWorkDuration(30_000_000L);
            } catch (Throwable e) {
                // no such service, or the native side is not up yet
            }
            ui.postDelayed(this, 40);
        }
    };

    private void askForPerformance() {
        String off = null;
        try {
            off = Os.getenv("PS2_ANDROID_PERF");
        } catch (Throwable e) {
            // none
        }
        if (off != null && off.equals("0")) return;
        try {
            android.os.PowerManager pm = (android.os.PowerManager) getSystemService(POWER_SERVICE);
            if (pm.isSustainedPerformanceModeSupported()) getWindow().setSustainedPerformanceMode(true);
            android.util.Log.i("ps2perf", "sustained performance supported " + pm.isSustainedPerformanceModeSupported());
        } catch (Throwable e) {
            // none
        }
        try {
            // the game state API (Android 13+): gameplay that must not be interrupted or scaled back
            android.app.GameManager gm = getSystemService(android.app.GameManager.class);
            gm.setGameState(new android.app.GameState(false, android.app.GameState.MODE_GAMEPLAY_UNINTERRUPTIBLE));
            android.util.Log.i("ps2perf", "game mode " + gm.getGameMode());
        } catch (Throwable e) {
            // none
        }
        ui.postDelayed(perfTick, 500);
    }

    /** The menu button and the back key: the settings panel (it has Quit game on its Game tab). */
    void showMenu() {
        if (settings == null || pad == null || pad.editing()) return;
        if (settings.isOpen()) {
            settings.hide();
            return;
        }
        pad.releaseAll();
        settings.show();
    }

    void settingsClosed() {}

    // ---- what the settings panel asks of the game -----------------------------------------------------------------------

    int scalePercent() { return scale; }

    static int clampInternal(int percent) { return Math.max(20, Math.min(100, percent)); }

    /** Compiler processes for the translation of the game: half the processors, at most 3 with less than 6 GB, 2 with less than 4 GB. */
    int compileJobs() {
        int jobs = Math.max(2, Runtime.getRuntime().availableProcessors() / 2);
        try {
            android.app.ActivityManager am = (android.app.ActivityManager) getSystemService(ACTIVITY_SERVICE);
            android.app.ActivityManager.MemoryInfo mi = new android.app.ActivityManager.MemoryInfo();
            am.getMemoryInfo(mi);
            long gb = mi.totalMem >> 30;  // (a "4 GB" phone reports a little less than 4)
            if (gb < 4) jobs = Math.min(jobs, 2);
            else if (gb < 6) jobs = Math.min(jobs, 3);
        } catch (Throwable e) {
            // keep the processor count
        }
        return jobs;
    }

    /** The game's picture on the screen at a given internal resolution (percent of the screen's pixels):
     *  { card texels per GS pixel across, down, picture width, picture height } (the GS picture is 640 x 512). */
    float[] internalScale(int percent) {
        int[] size = screenSize();
        int mode = play.getInt("display", 0);
        // the screen's pixels the picture covers: all of the width in widescreen or stretched, a 4:3 box otherwise
        float picW = widescreen() || mode != 0 ? size[0] : Math.min(size[0], size[1] * 4f / 3f), picH = size[1];
        float w = picW * percent / 100f, h = picH * percent / 100f;
        // down to half the PS2's own resolution (0.5x), for phones whose card cannot fill even the PS2's pixels at full speed (the
        // renderer takes 0.25x and up; below half the picture's small text gets hard to read)
        float sx = Math.max(0.5f, Math.min(6f, w / 640f)), sy = Math.max(0.5f, Math.min(6f, h / 512f));
        return new float[] {sx, sy, 640f * sx, 512f * sy};
    }

    /** Settings > Display > Internal resolution: the game starts again drawing at the new one (kept for later starts). */
    void applyScalePercent(int percent) {
        int p = clampInternal(percent);
        if (p == scale) return;
        scale = p;
        // the renderer makes its targets at the start: the game starts again at the new resolution
        play.edit().putInt("iscale", scale).commit();
        restart();
    }

    /** The screen's pixels in landscape: { long side, short side }. */
    int[] screenSize() {
        android.util.DisplayMetrics m = new android.util.DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(m);
        return new int[] {Math.max(m.widthPixels, m.heightPixels), Math.min(m.widthPixels, m.heightPixels)};
    }

    String displayLabel() {
        int mode = play.getInt("display", 0);
        return mode == 1 ? tr("Stretched") : mode == 2 ? tr("Zoomed") : tr("Original 4:3");
    }

    void cycleDisplay() {
        int mode = (play.getInt("display", 0) + 1) % 3;
        play.edit().putInt("display", mode).apply();
        try {
            Ps2Pad.nativeDisplayMode(mode);
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    boolean showFps() { return play.getBoolean("fps", false); }

    void setShowFps(boolean on) {
        play.edit().putBoolean("fps", on).apply();
        if (fpsView == null) return;
        fpsView.setVisibility(on ? android.view.View.VISIBLE : android.view.View.GONE);
        ui.removeCallbacks(fpsTick);
        if (on) ui.post(fpsTick);
    }

    private final Runnable fpsTick = new Runnable() {
        @Override public void run() {
            try {
                float[] v = nativeFps();
                fpsView.setText(String.format(java.util.Locale.US, "%.0f / %.0f FPS", v[0], v[1]));
            } catch (UnsatisfiedLinkError ignored) {
            }
            ui.postDelayed(this, 500);
        }
    };

    /** Copies the vector programs this APK carries for a game into `dir` (files already there are kept). */
    private void copySeed(String serial, File dir) {
        String from = "ps2/vu1seed/" + serial;
        try {
            String[] names = getAssets().list(from);
            if (names == null || names.length == 0) return;
            dir.mkdirs();
            byte[] buf = new byte[16384];
            for (String name : names) {
                File to = new File(dir, name);
                if (to.length() == 16384) continue;
                try (java.io.InputStream in = getAssets().open(from + "/" + name);
                     java.io.FileOutputStream out = new java.io.FileOutputStream(to)) {
                    for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
                }
            }
        } catch (java.io.IOException e) {
            android.util.Log.e("flipendo", "PS2 vector program seed: " + e);
        }
    }

    /** The aspect knobs of a game (lines "ADDRESS POWER"), null when its camera cannot follow the screen's shape. */
    static String aspectKnobs(String serial) {
        if ("SLES-51192".equals(serial) || "SLES_511.92".equals(serial))
            // Harry Potter and the Chamber of Secrets (PAL): the screen matrix the 3D camera ends in is built at 0x1DC940; its
            // horizontal scale is the quotient across / 768 (div.s at 0x1DCA74), narrowed by the widening. The vertical scale and
            // the side clipping (1 / that scale) follow from it, so the world is drawn out to the new edges.
            return "001dca74 -1\n";
        return null;
    }

    /** Settings > Display > Game view: widescreen (default) or the game's own 4:3; read at the start. */
    boolean widescreen() { return play.getBoolean("widescreen", true); }

    void switchWidescreen() {
        play.edit().putBoolean("widescreen", !widescreen()).commit();
        restart();
    }

    boolean gles() { return play.getBoolean("gles", false); }

    /** Settings > Game > Renderer: the runner reads its graphics API at the start, so the game starts again with the other one. */
    void switchRenderer() {
        play.edit().putBoolean("gles", !gles()).commit();
        restart();
    }

    boolean perfLog() { return play.getBoolean("perflog", false); }

    void setPerfLog(boolean on) { play.edit().putBoolean("perflog", on).apply(); }

    /** Settings > Controls > Control layout. */
    void editLayout() {
        if (pad == null) return;
        pad.startEdit(new Runnable() {
            @Override public void run() { settings.show(); }
        });
    }

    /** The game's process ends and starts again with the same intent (the runner's switches are read once, at the start). */
    private void restart() {
        Intent again = new Intent(getIntent());
        again.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        android.app.AlarmManager am = (android.app.AlarmManager) getSystemService(ALARM_SERVICE);
        android.app.PendingIntent pi = android.app.PendingIntent.getActivity(this, 7, again,
                android.app.PendingIntent.FLAG_IMMUTABLE | android.app.PendingIntent.FLAG_CANCEL_CURRENT);
        if (am != null) am.set(android.app.AlarmManager.RTC, System.currentTimeMillis() + 500, pi);
        finish();
        ui.postDelayed(new Runnable() {
            @Override public void run() { android.os.Process.killProcess(android.os.Process.myPid()); }
        }, 150);
    }

    /** The back key opens the menu instead of ending the game. */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK && !prepare) {
            if (event.getAction() == KeyEvent.ACTION_UP) showMenu();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
}
