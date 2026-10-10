package io.github.flipendo.spike;

import android.content.Intent;
import android.graphics.Canvas;
import android.net.Uri;
import android.os.Environment;
import android.provider.Settings;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import android.graphics.Paint;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.RelativeLayout;
import org.libsdl.app.SDLActivity;

import java.util.HashMap;

/** Spike: SDL activity with an on-screen touch overlay that injects keys and mouse input. */
public class FlipendoActivity extends SDLActivity {
    /**
     * Ratio of the render surface to the screen (the surface is a fixed-size SurfaceView scaled up by the compositor).
     * The "Render scale" slider of the menu changes it; it is kept in the preferences.
     */
    static volatile float surfaceScale = 0.5f;
    static final float RENDER_SCALE_MIN = 0.3f, RENDER_SCALE_MAX = 1.0f;

    /** Size of the subtitles and the HUD, 1 = the default; the engine reads it (android_main.cpp, HP1Canvas.cpp). */
    static float uiScale = 1.0f;
    static final float UI_SCALE_MIN = 0.6f, UI_SCALE_MAX = 1.8f;

    static native void nativeSetUiScale(float scale);

    /** Movement control: false = a floating stick under the left thumb, true = four arrow buttons that are always visible. */
    static volatile boolean dpadMode = false;

    /** Degrees added to the game's field of view (engine side: KW::ViewFovAngle). */
    static float fovOffset = 0f;
    static final float FOV_MIN = -20f, FOV_MAX = 40f;
    /** HP's debug mode (Level Select in the main menu, debug text); off by default. */
    static boolean debugMode = false;

    static native void nativeSetFovOffset(float degrees);

    static native void nativeSetDebugMode(boolean on);

    /** HP's Auto Jump option (Harry jumps by himself at ledges); on by default. */
    static boolean autoJump = true;

    static native void nativeSetAutoJump(boolean on);

    void applyAutoJump(boolean on, boolean save) {
        autoJump = on;
        try {
            nativeSetAutoJump(on);
        } catch (UnsatisfiedLinkError ignored) {
        }
        if (save) saveScales();
    }

    void applyFovOffset(float degrees, boolean save) {
        fovOffset = Math.max(FOV_MIN, Math.min(FOV_MAX, degrees));
        try {
            nativeSetFovOffset(fovOffset);
        } catch (UnsatisfiedLinkError ignored) {
        }
        if (save) saveScales();
    }

    /**
     * Switches the game's language (voices, fonts, texts): the wish goes into a file, the launcher (SetupActivity, in its own
     * process) is started, and this process, with the engine in it, is killed so the game starts again from nothing.
     */
    void switchLanguage(String code) {
        restartWith(SetupActivity.LANG_WANT, code);
    }

    /** Switches a mod on or off ("none" for the originals), the same way: wish file, launcher, fresh process. */
    void switchMod(String id) {
        restartWith(SetupActivity.MOD_WANT, id);
    }

    private void restartWith(String wishFile, String value) {
        try {
            SetupActivity.writeSmallFile(new File(GAME_DIR, wishFile), value);
        } catch (IOException e) {
            android.util.Log.e("flipendo", "could not write the wish " + wishFile + ": " + e);
            return;
        }
        Intent intent = new Intent(this, SetupActivity.class);
        intent.putExtra("restart", true);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(intent);
        new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(new Runnable() {
            public void run() {
                android.os.Process.killProcess(android.os.Process.myPid());
            }
        }, 600);
    }

    void applyDebugMode(boolean on, boolean save) {
        debugMode = on;
        try {
            nativeSetDebugMode(on);
        } catch (UnsatisfiedLinkError ignored) {
        }
        if (save) saveScales();
    }

    private void saveScales() {
        getSharedPreferences("flipendo", MODE_PRIVATE).edit()
                .putFloat("renderScale", surfaceScale).putFloat("uiScale", uiScale)
                .putFloat("fovOffset", fovOffset).putBoolean("debugMode", debugMode).putBoolean("showFps", showFps).putBoolean("autoJump", autoJump).apply();
    }

    /** Resizes the render surface (the engine rebuilds its scene textures and swapchain for the new size). */
    void applyRenderScale(float scale) {
        surfaceScale = Math.max(RENDER_SCALE_MIN, Math.min(RENDER_SCALE_MAX, scale));
        if (mSurface != null) {
            android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
            int longSide = Math.max(dm.widthPixels, dm.heightPixels), shortSide = Math.min(dm.widthPixels, dm.heightPixels);
            mSurface.getHolder().setFixedSize((int) (longSide * surfaceScale), (int) (shortSide * surfaceScale));
        }
        saveScales();
    }

    /** Subtitles and HUD size: takes effect on the next frame. */
    void applyUiScale(float scale, boolean save) {
        uiScale = Math.max(UI_SCALE_MIN, Math.min(UI_SCALE_MAX, scale));
        try {
            nativeSetUiScale(uiScale);
        } catch (UnsatisfiedLinkError ignored) {
        }
        if (save) {
            saveScales();
        }
    }

    /** True while a menu wants the touch cursor (the engine unlocks the mouse); implemented in android_main.cpp. */
    static native boolean nativeMenuActive();

    /** True while a spell lesson is in its Draw state: the finger draws the symbol. */
    static native boolean nativeLessonDrawing();

    /** The finger position (0..1 of the view) that drives the wand in a lesson; implemented in android_main.cpp. */
    static native void nativeSetTouch(float x, float y, boolean down);

    /** Asks the game thread to save in the player's slot (android_main.cpp, HP1::TickSaveRequest). */
    static native void nativeRequestSave();

    /** Called from the game thread when the save is done. */
    public void onSaved(final int slot) {
        runOnUiThread(new Runnable() {
            public void run() {
                android.widget.Toast.makeText(FlipendoActivity.this,
                        "Game saved (slot " + (slot + 1) + ")",
                        android.widget.Toast.LENGTH_SHORT).show();
                if (modOffAfterSave) {
                    // the mod can't aim in this level: switch it off, restart, and the game loads this save by itself
                    modOffAfterSave = false;
                    try {
                        SetupActivity.writeSmallFile(new File(GAME_DIR, ".selfpack-autoload"), String.valueOf(slot));
                    } catch (IOException e) {
                        android.util.Log.e("flipendo", "could not write the autoload wish: " + e);
                    }
                    switchMod("none");
                }
            }
        });
    }

    /**
     * Called from the game thread (android_main.cpp) when the game ends. Code 0: the player quit. Otherwise the game
     * stopped on an error: show the screen with the way to send the logs.
     */
    public void onGameStopped(final int code) {
        runOnUiThread(new Runnable() {
            public void run() {
                if (code != 0) {
                    Intent intent = new Intent(FlipendoActivity.this, SetupActivity.class);
                    intent.putExtra("stopped", "exit code " + code);
                    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
                    startActivity(intent);
                }
                finish();
            }
        });
    }

    /** Frames the engine has drawn (android_main.cpp): the FPS counter. */
    static native int nativeFrameCount();

    /** The FPS counter next to the toolbar (Settings > FPS counter), off by default. */
    static boolean showFps = false;

    /** The map being played (android_main.cpp), for prompts that depend on the level. */
    static native String nativeLevelName();

    static String levelName() {
        try {
            String name = nativeLevelName();
            return name == null ? "" : name;
        } catch (UnsatisfiedLinkError e) {
            return "";
        }
    }

    /** Levels where the game fixes the camera and aims with its own cursor: the movement mod aims along the camera and can't there. */
    static final String[] FIXED_CAMERA_LEVELS = { "lev5_snare" };

    static boolean isFixedCameraLevel(String map) {
        String key = map.toLowerCase();
        key = key.substring(key.lastIndexOf('/') + 1);
        if (key.endsWith(".unr")) key = key.substring(0, key.length() - 4);
        for (String level : FIXED_CAMERA_LEVELS) {
            if (level.equals(key)) return true;
        }
        return false;
    }

    /** The prompt for such a level was accepted: when the save is done, the mod is switched off and the game comes back to the save. */
    static volatile boolean modOffAfterSave;

    /** True while a cutscene holds Harry; implemented in android_main.cpp. */
    static native boolean nativeCutsceneActive();

    static boolean cutsceneActive() {
        try {
            return nativeCutsceneActive();
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
    }

    static boolean lessonDrawing() {
        try {
            return nativeLessonDrawing();
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
    }

    static boolean menuActive() {
        try {
            return nativeMenuActive();
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
    }

    /** The game folder: the user's own HP1 install (Maps, System, Textures...) lives here. */
    static final String GAME_DIR = "/sdcard/FlipendoHP";

    /** Needs "All files access" to read the game data; opens the system page for it on first launch. */
    private boolean ensureStorageAccess() {
        if (Environment.isExternalStorageManager()) {
            return true;
        }
        Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:" + getPackageName()));
        startActivity(intent);
        return false;
    }

    /** The engine's own UI resources (SurrealEngine.pk3) ship inside the APK; the engine looks for them in $HOME. */
    private void installResources() {
        File target = new File(GAME_DIR + "/.surrealengine/SurrealEngine.pk3");
        try {
            target.getParentFile().mkdirs();
            try (InputStream in = getAssets().open("SurrealEngine.pk3")) {
                if (target.exists() && target.length() == in.available()) {
                    return;
                }
                try (OutputStream out = new FileOutputStream(target)) {
                    byte[] buffer = new byte[1 << 16];
                    int n;
                    while ((n = in.read(buffer)) > 0) {
                        out.write(buffer, 0, n);
                    }
                }
            }
        } catch (IOException e) {
            android.util.Log.e("flipendo", "could not install SurrealEngine.pk3: " + e);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        if (ensureStorageAccess()) {
            installResources();
        }
        super.onCreate(savedInstanceState);
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // The player's scales (menu sliders), then render at a reduced fixed surface size: the compositor scales it up.
        android.content.SharedPreferences prefs = getSharedPreferences("flipendo", MODE_PRIVATE);
        dpadMode = prefs.getBoolean("dpad", false);
        applyUiScale(prefs.getFloat("uiScale", 1.0f), false);
        applyFovOffset(prefs.getFloat("fovOffset", 0f), false);
        applyDebugMode(prefs.getBoolean("debugMode", false), false);
        showFps = prefs.getBoolean("showFps", false);
        applyAutoJump(prefs.getBoolean("autoJump", true), false);
        if (mSurface != null) {
            applyRenderScale(prefs.getFloat("renderScale", 0.5f));
            // SDL adds the surface as WRAP_CONTENT, which would shrink the view to the fixed size: stretch it over the screen.
            mSurface.setLayoutParams(new RelativeLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }
        if (mLayout != null) {
            mLayout.addView(new Overlay(this), new RelativeLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }
    }

    static final class Overlay extends View {
        enum Role { STICK, LOOK, BUTTON, MENU, DRAW, SLIDER }

        // Two sliders in the left margin of the menu: 0 = render scale, 1 = subtitles and HUD size.
        float sliderX0, sliderX1;
        final float[] sliderY = new float[3];
        int activeSlider = -1;
        float pendingRender = 0.5f; // the render scale being dragged; it is applied when the finger lifts

        boolean menuShown;       // last polled menu state: hides the gameplay buttons
        boolean lessonShown;     // last polled lesson state: hides the gameplay buttons too
        boolean cutsceneShown;   // last polled cutscene state: only SKIP, settings and menu are shown, the screen does not steer or look
        long menuDownTime;       // when the menu pointer went down
        float menuX, menuY;      // last menu pointer position, in surface pixels

        final Runnable poll = new Runnable() {
            public void run() {
                boolean m = menuActive(), l = lessonDrawing(), cs = cutsceneActive();
                if (m != menuShown || l != lessonShown || cs != cutsceneShown) {
                    menuShown = m;
                    lessonShown = l;
                    cutsceneShown = cs;
                    if (cs) releaseGameplayControls();
                    invalidate();
                }
                // a level where the game fixes the camera, with the mod on: offer to switch the mod off (once per level)
                levelShown = levelName();
                if (modOn && !promptOpen && !settingsOpen && !menuShown && !lessonShown && !cutsceneShown
                        && isFixedCameraLevel(levelShown) && !levelShown.equalsIgnoreCase(promptDeclinedLevel)) {
                    openPrompt();
                }
                sampleFps();
                postDelayed(this, 150);
            }
        };

        @Override
        protected void onAttachedToWindow() {
            super.onAttachedToWindow();
            post(poll);
        }

        @Override
        protected void onDetachedFromWindow() {
            removeCallbacks(poll);
            super.onDetachedFromWindow();
        }

        /** Gameplay buttons are hidden while a menu is open; MENU stays as the way back. */
        boolean visible(Btn b) {
            if (b == menu || b == gear) return true;
            if (b == skip) return cutsceneShown; // the skip button exists only in cutscenes
            if (cutsceneShown) return false;       // and they leave only the skip, settings and menu buttons
            boolean game = !(menuShown || lessonShown);
            for (Btn a : arrows) {
                if (a == b) return dpadMode && game;
            }
            return game;
        }

        final android.graphics.Path arrowPath = new android.graphics.Path();

        int arrowIndex(Btn b) {
            for (int i = 0; i < arrows.length; i++) {
                if (arrows[i] == b) return i;
            }
            return -1;
        }

        boolean isArrow(Btn b) { return arrowIndex(b) >= 0; }

        /** A finger held on an arrow button slides to another arrow without lifting: the old one is released, the new pressed. */
        final java.util.HashSet<Integer> arrowFingers = new java.util.HashSet<>(); // fingers that went down on an arrow

        void slideArrow(int pid, float x, float y) {
            if (!arrowFingers.contains(pid)) return;
            Btn held = null;
            for (Btn a : arrows) {
                if (a.pointer == pid) held = a;
            }
            Btn over = null;
            float best = Float.MAX_VALUE;
            for (Btn a : arrows) {
                float dx = x - a.cx, dy = y - a.cy, d = dx * dx + dy * dy;
                if (d <= a.r * a.r * 1.3f && d < best && (a.pointer == -1 || a == held)) {
                    best = d;
                    over = a;
                }
            }
            if (over == held) return;
            if (held != null) {
                setButton(held, false);
                held.pointer = -1;
            }
            if (over != null) {
                over.pointer = pid;
                setButton(over, true);
            }
            invalidate();
        }

        /** A cutscene starts: let go of whatever the fingers were holding (stick directions, arrows, the wand). */
        void releaseGameplayControls() {
            releaseStick();
            releaseAim();
            arrowFingers.clear();
            for (Btn a : arrows) {
                if (a.down) setButton(a, false);
                a.pointer = -1;
            }
            if (cast.down) setButton(cast, false);
            cast.pointer = -1;
            // fingers that were steering or looking stop being tracked
            java.util.Iterator<java.util.Map.Entry<Integer, Role>> it = roles.entrySet().iterator();
            while (it.hasNext()) {
                Role r = it.next().getValue();
                if (r == Role.STICK || r == Role.LOOK) it.remove();
            }
        }

        // Languages carried by this APK ({code, label}; only a self pack with several has more than one) and the Language button
        final java.util.List<String[]> languages = SetupActivity.readLanguages(getContext());
        int languageAsk = -1;       // the language a first tap asked for, until it is confirmed or times out
        long languageAskTime;

        String currentLanguage() {
            String code = SetupActivity.readSmallFile(new File(GAME_DIR, SetupActivity.LANG_MARKER));
            return code == null || code.isEmpty() ? languages.get(0)[0] : code;
        }

        int languageIndex(String code) {
            for (int i = 0; i < languages.size(); i++) {
                if (languages.get(i)[0].equals(code)) return i;
            }
            return 0;
        }

        /** First tap: picks the next language and asks to confirm; a second tap within 5 s switches (the game restarts). */
        void languageTap() {
            long now = android.os.SystemClock.uptimeMillis();
            if (languageAsk >= 0 && now - languageAskTime < 5000) {
                int target = languageAsk;
                languageAsk = -1;
                ((FlipendoActivity) getContext()).switchLanguage(languages.get(target)[0]);
                return;
            }
            languageAsk = (languageIndex(currentLanguage()) + 1) % languages.size();
            languageAskTime = now;
            postDelayed(new Runnable() { public void run() {
                if (languageAsk >= 0 && android.os.SystemClock.uptimeMillis() - languageAskTime >= 5000) {
                    languageAsk = -1;
                    invalidate();
                }
            } }, 5100);
        }

        // The movement mod (AdamJD): a replacement for the game's scripts that the APK can carry (self pack with --mod). While it is
        // installed the wand is aimed with a stick on the right (in the mod, holding CAST aims and lets the camera move).
        final java.util.List<String[]> mods = SetupActivity.readMods(getContext());
        final boolean modOn;
        boolean modAsk;
        long modAskTime;

        {
            String id = SetupActivity.readSmallFile(new File(GAME_DIR, SetupActivity.MOD_MARKER));
            modOn = id != null && !id.isEmpty() && !id.equals("none");
        }

        /** First tap asks to confirm; a second tap within 5 s switches the mod and restarts the game. */
        void modTap() {
            long now = android.os.SystemClock.uptimeMillis();
            if (modAsk && now - modAskTime < 5000) {
                modAsk = false;
                ((FlipendoActivity) getContext()).switchMod(modOn ? "none" : mods.get(0)[0]);
                return;
            }
            modAsk = true;
            modAskTime = now;
            postDelayed(new Runnable() { public void run() {
                if (modAsk && android.os.SystemClock.uptimeMillis() - modAskTime >= 5000) {
                    modAsk = false;
                    invalidate();
                }
            } }, 5100);
        }

        // The right stick (only with the mod): deflection turns the camera, that is the wand's aim, 60 times a second.
        float aimCx, aimCy, aimR;
        int aimPointer = -1;
        float aimDx, aimDy; // knob offset from the centre, in pixels (clamped to aimR)
        static final float AIM_GAIN = 24f;      // mouse counts per frame at full deflection
        static final float AIM_DEADZONE = 0.12f;

        final Runnable aimTick = new Runnable() {
            public void run() {
                if (aimPointer == -1) return;
                float mag = (float) Math.hypot(aimDx, aimDy) / aimR;
                if (mag > AIM_DEADZONE) {
                    float scaled = (Math.min(mag, 1f) - AIM_DEADZONE) / (1f - AIM_DEADZONE);
                    float length = (float) Math.hypot(aimDx, aimDy); // the knob offset, at most aimR
                    SDLActivity.onNativeMouse(0, MotionEvent.ACTION_MOVE,
                            aimDx / length * scaled * AIM_GAIN, aimDy / length * scaled * AIM_GAIN, true);
                }
                postDelayed(this, 16);
            }
        };

        boolean aimVisible() {
            return modOn && !settingsOpen && !menuShown && !lessonShown && !cutsceneShown;
        }

        void setAim(float x, float y) {
            float dx = x - aimCx, dy = y - aimCy;
            float d = (float) Math.hypot(dx, dy);
            if (d > aimR) {
                dx *= aimR / d;
                dy *= aimR / d;
            }
            aimDx = dx;
            aimDy = dy;
        }

        void releaseAim() {
            aimPointer = -1;
            aimDx = aimDy = 0;
        }

        void openSettings() {
            releaseStick();
            for (Btn a : arrows) {
                if (a.down) setButton(a, false);
                a.pointer = -1;
            }
            arrowFingers.clear();
            settingsOpen = true;
            invalidate();
        }

        void setDpadMode(boolean on) {
            if (dpadMode == on) return;
            releaseStick();
            dpadMode = on;
            ((FlipendoActivity) getContext()).getSharedPreferences("flipendo", MODE_PRIVATE).edit().putBoolean("dpad", on).apply();
            invalidate();
        }

        static final class Btn {
            final String label;
            final int key;
            final int mouse;
            final boolean toggle;
            android.graphics.Bitmap icon;
            boolean tint; // black glyph: drawn white (yellow while pressed) so it shows on dark scenes
            float cx, cy, r;
            boolean down;
            int pointer = -1;

            Btn(String label, int key, int mouse, boolean toggle) {
                this.label = label;
                this.key = key;
                this.mouse = mouse;
                this.toggle = toggle;
            }
        }

        final Btn cast = new Btn("CAST", 0, MotionEvent.BUTTON_PRIMARY, false);
        final Btn jump = new Btn("JUMP", 0, MotionEvent.BUTTON_SECONDARY, false);
        final Btn menu = new Btn("MENU", KeyEvent.KEYCODE_ESCAPE, 0, false);
        final Btn save = new Btn("SAVE", 0, 0, false); // save anywhere (the icon, images/android/save.png, is optional)
        final Btn skip = new Btn("SKIP", KeyEvent.KEYCODE_SPACE, 0, false); // skips a cutscene (Space); shown only in cutscenes
        final Btn gear = new Btn("SET", 0, 0, false); // opens the settings panel
        // arrow buttons (movement in dpadMode): up, down, left, right, in the order of DIR_KEYS
        final Btn[] arrows = {
            new Btn("UP", KeyEvent.KEYCODE_DPAD_UP, 0, false), new Btn("DOWN", KeyEvent.KEYCODE_DPAD_DOWN, 0, false),
            new Btn("LEFT", KeyEvent.KEYCODE_DPAD_LEFT, 0, false), new Btn("RIGHT", KeyEvent.KEYCODE_DPAD_RIGHT, 0, false) };
        final Btn[] buttons = { cast, jump, menu, save, gear, skip, arrows[0], arrows[1], arrows[2], arrows[3] };

        // The prompt for a fixed-camera level (the movement mod can't aim there): a small panel that takes every touch.
        boolean promptOpen;
        String levelShown = "";
        String promptDeclinedLevel = "";
        final android.graphics.RectF promptPanel = new android.graphics.RectF(), promptYes = new android.graphics.RectF(), promptNo = new android.graphics.RectF();

        void openPrompt() {
            releaseGameplayControls();
            promptOpen = true;
            invalidate();
        }

        void acceptPrompt() {
            promptOpen = false;
            promptDeclinedLevel = levelShown; // whatever happens, no second prompt for this level
            modOffAfterSave = true;
            nativeRequestSave(); // the game thread saves at its next tick: onSaved switches the mod off and restarts
            postDelayed(new Runnable() { public void run() {
                if (modOffAfterSave) {
                    modOffAfterSave = false;
                    android.widget.Toast.makeText(getContext(), "Could not save: the mod stays on", android.widget.Toast.LENGTH_LONG).show();
                }
            } }, 8000);
            invalidate();
        }

        void drawPrompt(Canvas c) {
            if (!promptOpen) return;
            float u = Math.min(getWidth(), getHeight());
            fill.setColor(0xB0000000);
            c.drawRect(0, 0, getWidth(), getHeight(), fill);
            fill.setColor(0xF01C1C26);
            c.drawRoundRect(promptPanel, 0.03f * u, 0.03f * u, fill);
            line.setColor(0x66FFFFFF);
            line.setStrokeWidth(3f);
            c.drawRoundRect(promptPanel, 0.03f * u, 0.03f * u, line);
            float size = text.getTextSize();
            float cx = promptPanel.centerX();
            text.setTextSize(size * 1.4f);
            text.setColor(0xFFFFFFFF);
            c.drawText("Fixed camera level", cx, promptPanel.top + 0.12f * promptPanel.height(), text);
            text.setTextSize(size);
            text.setColor(0xCCFFFFFF);
            c.drawText("The game fixes the camera here and the movement mod aims along it,", cx, promptPanel.top + 0.30f * promptPanel.height(), text);
            c.drawText("so you can't aim. Switch the mod off? The game saves,", cx, promptPanel.top + 0.42f * promptPanel.height(), text);
            c.drawText("restarts and loads this save by itself.", cx, promptPanel.top + 0.54f * promptPanel.height(), text);
            drawSegment(c, promptYes, "Switch off and restart", true);
            drawSegment(c, promptNo, "Keep the mod", false);
        }

        // the settings card's geometry (onSizeChanged) and its text styles
        float headerY, dividerY, colL0, colL1, colR0, colR1, secDisplayY, secControlsY, secGameY;
        final float[] sliderLabelY = new float[3];
        static final int GOLD = 0xFFE8C15A, CARD = 0xF2181722, ROW = 0x14FFFFFF, MUTED = 0x99FFFFFF;
        final Paint titlePaint = new Paint(Paint.ANTI_ALIAS_FLAG), sectionPaint = new Paint(Paint.ANTI_ALIAS_FLAG),
                rowPaint = new Paint(Paint.ANTI_ALIAS_FLAG), subPaint = new Paint(Paint.ANTI_ALIAS_FLAG),
                valuePaint = new Paint(Paint.ANTI_ALIAS_FLAG);

        {
            titlePaint.setColor(0xFFFFFFFF);
            titlePaint.setFakeBoldText(true);
            sectionPaint.setColor(GOLD);
            sectionPaint.setFakeBoldText(true);
            sectionPaint.setLetterSpacing(0.12f);
            rowPaint.setColor(0xFFFFFFFF);
            subPaint.setColor(MUTED);
            valuePaint.setColor(GOLD);
            valuePaint.setTextAlign(Paint.Align.RIGHT);
        }

        boolean settingsOpen;    // the settings panel is up: it takes every touch
        final android.graphics.RectF panel = new android.graphics.RectF();
        final android.graphics.RectF segStick = new android.graphics.RectF(), segDpad = new android.graphics.RectF();
        final android.graphics.RectF closeBtn = new android.graphics.RectF(), debugBtn = new android.graphics.RectF(), autoJumpBtn = new android.graphics.RectF(), telegramBtn = new android.graphics.RectF(), languageBtn = new android.graphics.RectF(), modBtn = new android.graphics.RectF(), fpsBtn = new android.graphics.RectF();

        final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
        final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
        final Paint iconPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        final Paint whitePaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        final Paint yellowPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        final android.graphics.RectF iconRect = new android.graphics.RectF();

        final HashMap<Integer, Role> roles = new HashMap<>();
        final HashMap<Integer, float[]> last = new HashMap<>();   // pointer -> last x,y
        final HashMap<Integer, float[]> start = new HashMap<>();  // pointer -> start x,y
        int stickPointer = -1;
        float stickX, stickY, knobX, knobY;
        final boolean[] dirDown = new boolean[4]; // up, down, left, right
        static final int[] DIR_KEYS = { KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN,
                KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT };

        Runnable menuHeld; // pending "hold MENU to send the logs"

        Overlay(android.content.Context c) {
            super(c);
            fill.setStyle(Paint.Style.FILL);
            line.setStyle(Paint.Style.STROKE);
            line.setStrokeWidth(4f);
            text.setTextAlign(Paint.Align.CENTER);
            text.setColor(0xCCFFFFFF);
            setFocusable(false);
            whitePaint.setColorFilter(new android.graphics.PorterDuffColorFilter(0xFFFFFFFF, android.graphics.PorterDuff.Mode.SRC_IN));
            yellowPaint.setColorFilter(new android.graphics.PorterDuffColorFilter(0xFFFFD84A, android.graphics.PorterDuff.Mode.SRC_IN));
            cast.icon = loadIcon(c, "wand.png");
            jump.icon = loadIcon(c, "jump.png");
            jump.tint = true;
            menu.icon = loadIcon(c, "menu.png");
            menu.tint = true;
            save.icon = loadIcon(c, "save.png"); // a coloured picture (book and quill), drawn like the wand: on a light disc
        }

        /** The button icons ship in the APK (assets/icons/, copied from images/android/ by build-apk.sh). */
        static android.graphics.Bitmap loadIcon(android.content.Context c, String name) {
            try {
                // decode at about 256 px: the pictures can be much larger than the buttons (save.png is 2500x2500)
                android.graphics.BitmapFactory.Options opts = new android.graphics.BitmapFactory.Options();
                opts.inJustDecodeBounds = true;
                try (InputStream in = c.getAssets().open("icons/" + name)) {
                    android.graphics.BitmapFactory.decodeStream(in, null, opts);
                }
                opts.inJustDecodeBounds = false;
                opts.inSampleSize = 1;
                while (Math.max(opts.outWidth, opts.outHeight) / (opts.inSampleSize * 2) >= 256) opts.inSampleSize *= 2;
                try (InputStream in = c.getAssets().open("icons/" + name)) {
                    return android.graphics.BitmapFactory.decodeStream(in, null, opts);
                }
            } catch (IOException e) {
                return null; // the button falls back to its text label
            }
        }

        @Override
        protected void onSizeChanged(int w, int h, int ow, int oh) {
            float u = Math.min(w, h);
            // Controls, in the settings card's style, with one margin from every edge:
            float m = 0.05f * u;
            // top right: a toolbar (settings | save or skip | menu), three slots in one pill
            float barSlot = 0.12f * u, barY = m + 0.04f * u;
            menu.cx = w - m - barSlot / 2f; menu.cy = barY; menu.r = 0.05f * u;
            save.cx = menu.cx - barSlot; save.cy = barY; save.r = 0.05f * u;
            skip.cx = save.cx; skip.cy = save.cy; skip.r = save.r; // the book's place: the book is hidden in cutscenes
            gear.cx = save.cx - barSlot; gear.cy = barY; gear.r = 0.05f * u;
            toolbar.set(gear.cx - barSlot / 2f, barY - 0.055f * u, menu.cx + barSlot / 2f, barY + 0.055f * u);
            // bottom right: the wand, and the jump to its lower left; with the mod the wand is a stick whose range ring must
            // clear the jump button: it sits higher
            cast.r = 0.104f * u;
            cast.cx = w - m - cast.r - 0.03f * u; cast.cy = h - (modOn ? 0.40f : 0.21f) * u;
            jump.r = 0.08f * u;
            jump.cx = cast.cx - 0.23f * u; jump.cy = h - m - jump.r;
            text.setTextSize(0.035f * u);
            aimR = 0.12f * u; // how far the wand button follows the finger

            // bottom left: the arrows on one round pad
            float ad = 0.15f * u;
            padR = ad + 0.085f * u;
            padX = m + padR; padY = h - m - padR;
            arrows[0].cx = padX; arrows[0].cy = padY - ad;
            arrows[1].cx = padX; arrows[1].cy = padY + ad;
            arrows[2].cx = padX - ad; arrows[2].cy = padY;
            arrows[3].cx = padX + ad; arrows[3].cy = padY;
            for (Btn a : arrows) a.r = 0.08f * u;

            // The settings panel: a card with a header, two columns (Display: the sliders | Controls and Game: switches) and a footer.
            float pw = Math.min(0.94f * w, 2.1f * h);
            float ph = 0.9f * h;
            panel.set((w - pw) / 2f, (h - ph) / 2f, (w + pw) / 2f, (h + ph) / 2f);
            float pad = 0.035f * pw;
            float gap = 0.02f * pw;
            headerY = panel.top + 0.1f * h;
            dividerY = panel.top + 0.145f * h;
            float contentTop = dividerY + 0.03f * h;
            float footerTop = panel.bottom - 0.115f * h;
            colL0 = panel.left + pad;
            colL1 = panel.centerX() - pad / 2f;
            colR0 = panel.centerX() + pad / 2f;
            colR1 = panel.right - pad;

            // left: Display, three sliders sharing the column's height
            secDisplayY = contentTop + 0.03f * h;
            float knobR = 0.026f * u;
            sliderX0 = colL0 + knobR;
            sliderX1 = colL1 - knobR;
            // the FPS counter switch at the bottom of the column, the sliders share what is above it
            fpsBtn.set(colL0, footerTop - 0.035f * h - 0.078f * h, colL1, footerTop - 0.035f * h);
            float slot = (fpsBtn.top - 0.02f * h - (secDisplayY + 0.02f * h)) / 3f;
            for (int s = 0; s < 3; s++) {
                sliderLabelY[s] = secDisplayY + 0.02f * h + s * slot + 0.055f * h;
                sliderY[s] = sliderLabelY[s] + 0.05f * h;
            }

            // right: Controls (movement segments, Auto jump) and Game (Language, Movement mod, Debug mode)
            float rowH = 0.078f * h, rowGap = 0.012f * h;
            secControlsY = contentTop + 0.03f * h;
            float y = secControlsY + 0.025f * h;
            segStick.set(colR0, y, (colR0 + colR1) / 2f, y + rowH);
            segDpad.set((colR0 + colR1) / 2f, y, colR1, y + rowH);
            y += rowH + rowGap;
            autoJumpBtn.set(colR0, y, colR1, y + rowH);
            y += rowH + 0.02f * h;
            secGameY = y + 0.03f * h;
            y = secGameY + 0.02f * h;
            if (languages.size() > 1) {
                languageBtn.set(colR0, y, colR1, y + rowH);
                y += rowH + rowGap;
            } else {
                languageBtn.setEmpty();
            }
            if (!mods.isEmpty()) {
                modBtn.set(colR0, y, colR1, y + rowH);
                y += rowH + rowGap;
            } else {
                modBtn.setEmpty();
            }
            debugBtn.set(colR0, y, colR1, y + rowH);

            // footer: the channel on the left, Done on the right
            float footH = 0.075f * h;
            float footY = panel.bottom - 0.03f * h - footH;
            telegramBtn.set(colL0, footY, colL0 + 0.36f * pw, footY + footH);
            closeBtn.set(colR1 - 0.2f * pw, footY, colR1, footY + footH);

            float ppw = Math.min(0.8f * w, 1.7f * h);
            promptPanel.set((w - ppw) / 2f, 0.22f * h, (w + ppw) / 2f, 0.78f * h);
            promptYes.set(promptPanel.left + 0.05f * ppw, promptPanel.bottom - 0.28f * promptPanel.height(), w / 2f - gap, promptPanel.bottom - 0.08f * promptPanel.height());
            promptNo.set(w / 2f + gap, promptYes.top, promptPanel.right - 0.05f * ppw, promptYes.bottom);

            titlePaint.setTextSize(0.055f * u);
            sectionPaint.setTextSize(0.026f * u);
            rowPaint.setTextSize(0.033f * u);
            subPaint.setTextSize(0.024f * u);
            valuePaint.setTextSize(0.031f * u);
            label.setTextSize(0.03f * u);
            label.setColor(0xFFFFFFFF);
            label.setShadowLayer(4f, 0f, 0f, 0xFF000000);
        }

        final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);

        // sliders: 0 = render scale, 1 = subtitles and HUD size, 2 = field of view offset
        float sliderMin(int i) { return i == 0 ? RENDER_SCALE_MIN : i == 1 ? UI_SCALE_MIN : FOV_MIN; }

        float sliderMax(int i) { return i == 0 ? RENDER_SCALE_MAX : i == 1 ? UI_SCALE_MAX : FOV_MAX; }

        float sliderStep(int i) { return i == 0 ? 0.05f : i == 1 ? 0.1f : 5f; }

        float sliderValue(int i) {
            return i == 0 ? (activeSlider == 0 ? pendingRender : surfaceScale) : i == 1 ? uiScale : fovOffset;
        }

        /** The slider under a touch (menu open only), or -1. */
        int hitSlider(float x, float y) {
            if (!settingsOpen) return -1;
            float u = Math.min(getWidth(), getHeight());
            for (int i = 0; i < 3; i++) {
                if (Math.abs(y - sliderY[i]) < 0.055f * u && x > sliderX0 - 0.05f * u && x < sliderX1 + 0.05f * u) return i;
            }
            return -1;
        }

        /** Moves a slider to the finger; the UI size is live, the render scale is applied when the finger lifts (commit). */
        void setSliderFromX(int i, float x, boolean commit) {
            float t = Math.max(0f, Math.min(1f, (x - sliderX0) / (sliderX1 - sliderX0)));
            float v = sliderMin(i) + t * (sliderMax(i) - sliderMin(i));
            float step = sliderStep(i);
            v = Math.round(v / step) * step;
            FlipendoActivity activity = (FlipendoActivity) getContext();
            if (i == 1) {
                activity.applyUiScale(v, commit);
            } else if (i == 2) {
                activity.applyFovOffset(v, commit);
            } else {
                pendingRender = v;
                if (commit) activity.applyRenderScale(v);
            }
            invalidate();
        }

        void drawSegment(Canvas c, android.graphics.RectF r, String name, boolean on) {
            fill.setColor(on ? 0xFFFFD84A : 0x33FFFFFF);
            c.drawRoundRect(r, r.height() / 2f, r.height() / 2f, fill);
            text.setColor(on ? 0xFF000000 : 0xFFFFFFFF);
            c.drawText(name, r.centerX(), r.centerY() + text.getTextSize() * 0.35f, text);
            text.setColor(0xCCFFFFFF);
        }

        void drawSettings(Canvas c) {
            if (!settingsOpen) return;
            float u = Math.min(getWidth(), getHeight());
            float h = getHeight();
            fill.setColor(0xB8000000);
            c.drawRect(0, 0, getWidth(), h, fill);
            float r = 0.035f * u;
            fill.setColor(CARD);
            c.drawRoundRect(panel, r, r, fill);
            line.setColor(0x33FFFFFF);
            line.setStrokeWidth(2f);
            c.drawRoundRect(panel, r, r, line);

            // header: a gold accent, the title, and a hairline under it
            fill.setColor(GOLD);
            c.drawRoundRect(colL0, headerY - 0.045f * h, colL0 + 0.008f * u, headerY + 0.005f * h, 4f, 4f, fill);
            titlePaint.setTextAlign(Paint.Align.LEFT);
            c.drawText("Settings", colL0 + 0.025f * u, headerY, titlePaint);
            subPaint.setTextAlign(Paint.Align.RIGHT);
            c.drawText("Flipendo for Android", colR1, headerY, subPaint);
            subPaint.setTextAlign(Paint.Align.LEFT);
            line.setColor(0x22FFFFFF);
            line.setStrokeWidth(2f);
            c.drawLine(colL0, dividerY, colR1, dividerY, line);
            c.drawLine(panel.centerX(), dividerY + 0.03f * h, panel.centerX(), telegramBtn.top - 0.03f * h, line);

            // left column
            drawSection(c, "DISPLAY", colL0, secDisplayY);
            drawSliders(c);
            drawSwitchRow(c, fpsBtn, "FPS counter", "Frames per second next to the toolbar", showFps, false);

            // right column
            drawSection(c, "CONTROLS", colR0, secControlsY);
            drawSegments(c);
            drawSwitchRow(c, autoJumpBtn, "Auto jump", "Harry jumps by himself at ledges", autoJump, false);
            drawSection(c, "GAME", colR0, secGameY);
            if (languages.size() > 1) {
                String current = languages.get(languageIndex(currentLanguage()))[1];
                if (languageAsk >= 0) {
                    drawValueRow(c, languageBtn, "Language", "Tap again: restart in " + languages.get(languageAsk)[1], "Restart", true);
                } else {
                    drawValueRow(c, languageBtn, "Language", "Voices, texts and fonts", current, false);
                }
            }
            if (!mods.isEmpty()) {
                drawSwitchRow(c, modBtn, "Movement mod",
                        modAsk ? "Tap again to restart " + (modOn ? "without" : "with") + " it (unsaved progress is lost)" : "By AdamJD: camera, climbing, aim",
                        modOn, modAsk);
            }
            drawSwitchRow(c, debugBtn, "Debug mode", "Level Select in the main menu", debugMode, false);

            // footer
            fill.setColor(0x334A9EE8);
            c.drawRoundRect(telegramBtn, telegramBtn.height() / 2f, telegramBtn.height() / 2f, fill);
            rowPaint.setTextAlign(Paint.Align.CENTER);
            rowPaint.setColor(0xFF8CC8FF);
            c.drawText("Telegram: t.me/flipendodroid", telegramBtn.centerX(), telegramBtn.centerY() + rowPaint.getTextSize() * 0.35f, rowPaint);
            fill.setColor(GOLD);
            c.drawRoundRect(closeBtn, closeBtn.height() / 2f, closeBtn.height() / 2f, fill);
            rowPaint.setColor(0xFF1A1A1A);
            c.drawText("Done", closeBtn.centerX(), closeBtn.centerY() + rowPaint.getTextSize() * 0.35f, rowPaint);
            rowPaint.setColor(0xFFFFFFFF);
            rowPaint.setTextAlign(Paint.Align.LEFT);
        }

        void drawSection(Canvas c, String name, float x, float y) {
            sectionPaint.setTextAlign(Paint.Align.LEFT);
            c.drawText(name, x, y, sectionPaint);
        }

        final android.graphics.RectF segAll = new android.graphics.RectF();

        /** Stick | Arrows as one control: a track with the chosen half filled. */
        void drawSegments(Canvas c) {
            segAll.set(segStick.left, segStick.top, segDpad.right, segDpad.bottom);
            float rr = segAll.height() / 2f;
            fill.setColor(ROW);
            c.drawRoundRect(segAll, rr, rr, fill);
            android.graphics.RectF on = dpadMode ? segDpad : segStick;
            float inset = 0.08f * segAll.height();
            fill.setColor(GOLD);
            c.drawRoundRect(on.left + inset, on.top + inset, on.right - inset, on.bottom - inset, rr - inset, rr - inset, fill);
            rowPaint.setTextAlign(Paint.Align.CENTER);
            float base = rowPaint.getTextSize() * 0.35f;
            rowPaint.setColor(dpadMode ? 0xFFFFFFFF : 0xFF1A1A1A);
            c.drawText("Floating stick", segStick.centerX(), segStick.centerY() + base, rowPaint);
            rowPaint.setColor(dpadMode ? 0xFF1A1A1A : 0xFFFFFFFF);
            c.drawText("Arrow buttons", segDpad.centerX(), segDpad.centerY() + base, rowPaint);
            rowPaint.setColor(0xFFFFFFFF);
            rowPaint.setTextAlign(Paint.Align.LEFT);
        }

        float rowPad(android.graphics.RectF row) {
            return 0.02f * (colR1 - colR0) + 0.035f * row.height();
        }

        /** A row with a title, a hint under it and a switch on the right; pending: a confirmation is asked (gold outline). */
        void drawSwitchRow(Canvas c, android.graphics.RectF row, String title, String hint, boolean on, boolean pending) {
            drawRowBase(c, row, pending);
            drawRowTexts(c, row, title, hint, pending);
            float th = 0.42f * row.height(), tw = 1.8f * th;
            float tx1 = row.right - rowPad(row), tx0 = tx1 - tw, ty0 = row.centerY() - th / 2f, ty1 = ty0 + th;
            fill.setColor(on ? GOLD : 0x33FFFFFF);
            c.drawRoundRect(tx0, ty0, tx1, ty1, th / 2f, th / 2f, fill);
            fill.setColor(0xFFFFFFFF);
            c.drawCircle(on ? tx1 - th / 2f : tx0 + th / 2f, row.centerY(), 0.38f * th, fill);
        }

        /** A row with a title, a hint and a value on the right (opens a choice). */
        void drawValueRow(Canvas c, android.graphics.RectF row, String title, String hint, String value, boolean pending) {
            drawRowBase(c, row, pending);
            drawRowTexts(c, row, title, hint, pending);
            c.drawText(value + "  ›", row.right - rowPad(row), row.centerY() + valuePaint.getTextSize() * 0.35f, valuePaint);
        }

        void drawRowBase(Canvas c, android.graphics.RectF row, boolean pending) {
            float rr = 0.25f * row.height();
            fill.setColor(pending ? 0x22E8C15A : ROW);
            c.drawRoundRect(row, rr, rr, fill);
            if (pending) {
                line.setColor(GOLD);
                line.setStrokeWidth(2f);
                c.drawRoundRect(row, rr, rr, line);
            }
        }

        void drawRowTexts(Canvas c, android.graphics.RectF row, String title, String hint, boolean pending) {
            float x = row.left + rowPad(row);
            rowPaint.setTextAlign(Paint.Align.LEFT);
            c.drawText(title, x, row.centerY() - 0.04f * row.height(), rowPaint);
            subPaint.setTextAlign(Paint.Align.LEFT);
            subPaint.setColor(pending ? GOLD : MUTED);
            c.drawText(hint, x, row.centerY() + subPaint.getTextSize() * 1.15f, subPaint);
            subPaint.setColor(MUTED);
        }

        void drawSliders(Canvas c) {
            float u = Math.min(getWidth(), getHeight());
            android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
            int longSide = Math.max(dm.widthPixels, dm.heightPixels), shortSide = Math.min(dm.widthPixels, dm.heightPixels);
            for (int i = 0; i < 3; i++) {
                float v = sliderValue(i);
                String name = i == 0 ? "Render scale" : i == 1 ? "Subtitles and HUD size" : "Field of view";
                String value = i == 0 ? Math.round(v * 100) + "%  ·  " + (int) (longSide * v) + "×" + (int) (shortSide * v)
                        : i == 1 ? Math.round(v * 100) + "%"
                        : (v > 0 ? "+" : "") + Math.round(v) + "°" + (v == 0 ? "  (default)" : "");
                rowPaint.setTextAlign(Paint.Align.LEFT);
                c.drawText(name, colL0, sliderLabelY[i], rowPaint);
                c.drawText(value, colL1, sliderLabelY[i], valuePaint);
                float t = (v - sliderMin(i)) / (sliderMax(i) - sliderMin(i));
                float kx = sliderX0 + t * (sliderX1 - sliderX0);
                line.setStrokeCap(Paint.Cap.ROUND);
                line.setStrokeWidth(0.01f * u);
                line.setColor(0x33FFFFFF);
                c.drawLine(sliderX0, sliderY[i], sliderX1, sliderY[i], line);
                line.setColor(GOLD);
                c.drawLine(sliderX0, sliderY[i], kx, sliderY[i], line);
                line.setStrokeCap(Paint.Cap.BUTT);
                float kr = activeSlider == i ? 0.03f * u : 0.024f * u;
                fill.setColor(0xFFFFFFFF);
                c.drawCircle(kx, sliderY[i], kr, fill);
                line.setColor(GOLD);
                line.setStrokeWidth(0.005f * u);
                c.drawCircle(kx, sliderY[i], kr, line);
            }
            line.setStrokeWidth(4f);
        }

        Btn hitButton(float x, float y) {
            for (Btn b : buttons) {
                if (!visible(b)) continue;
                float dx = x - b.cx, dy = y - b.cy;
                if (dx * dx + dy * dy <= b.r * b.r * 1.3f) return b;
            }
            return null;
        }

        void key(int keycode, boolean down) {
            if (down) SDLActivity.onNativeKeyDown(keycode); else SDLActivity.onNativeKeyUp(keycode);
        }

        void setButton(Btn b, boolean down) {
            if (b == save) {
                b.down = down;
                if (down) nativeRequestSave(); // saved by the game thread at its next tick, see onSaved
                return;
            }
            if (b == gear) {
                b.down = false;
                if (down) openSettings();
                return;
            }
            b.down = down;
            if (b.mouse != 0)
                SDLActivity.onNativeMouse(down ? b.mouse : 0, down ? MotionEvent.ACTION_DOWN : MotionEvent.ACTION_UP,
                        0f, 0f, true); // a click without moving the cursor: an absolute position here turned the mod's camera
            else
                key(b.key, down);
        }

        void setDir(int i, boolean down) {
            if (dirDown[i] != down) {
                dirDown[i] = down;
                key(DIR_KEYS[i], down);
            }
        }

        void updateStick() {
            float dx = knobX - stickX, dy = knobY - stickY;
            float dead = 0.04f * Math.min(getWidth(), getHeight());
            setDir(0, dy < -dead);
            setDir(1, dy > dead);
            setDir(2, dx < -dead);
            setDir(3, dx > dead);
        }

        void releaseStick() {
            stickPointer = -1;
            for (int i = 0; i < 4; i++) setDir(i, false);
        }

        void click(final float sx, final float sy) {
            // The menu reads the cursor position once per frame: move first, then press and release on later frames.
            final float x = sx * surfaceScale, y = sy * surfaceScale;
            SDLActivity.onNativeMouse(0, MotionEvent.ACTION_MOVE, x, y, false);
            postDelayed(new Runnable() { public void run() {
                SDLActivity.onNativeMouse(MotionEvent.BUTTON_PRIMARY, MotionEvent.ACTION_DOWN, x, y, false);
            } }, 80);
            postDelayed(new Runnable() { public void run() {
                SDLActivity.onNativeMouse(0, MotionEvent.ACTION_UP, x, y, false);
            } }, 200);
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            int action = e.getActionMasked();
            int idx = e.getActionIndex();
            int id = e.getPointerId(idx);
            float slop = 0.03f * Math.min(getWidth(), getHeight());
            switch (action) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_POINTER_DOWN: {
                    float x = e.getX(idx), y = e.getY(idx);
                    if (promptOpen) {
                        // the prompt takes every touch
                        if (promptYes.contains(x, y)) {
                            acceptPrompt();
                        } else if (promptNo.contains(x, y)) {
                            promptOpen = false;
                            promptDeclinedLevel = levelShown;
                        }
                        break;
                    }
                    if (settingsOpen) {
                        // the panel takes every touch
                        if (hitSlider(x, y) >= 0) {
                            roles.put(id, Role.SLIDER);
                            activeSlider = hitSlider(x, y);
                            pendingRender = surfaceScale;
                            setSliderFromX(activeSlider, x, false);
                        } else if (segStick.contains(x, y)) {
                            setDpadMode(false);
                        } else if (segDpad.contains(x, y)) {
                            setDpadMode(true);
                        } else if (autoJumpBtn.contains(x, y)) {
                            ((FlipendoActivity) getContext()).applyAutoJump(!autoJump, true);
                        } else if (fpsBtn.contains(x, y)) {
                            showFps = !showFps;
                            fpsFrames = -1;
                            ((FlipendoActivity) getContext()).saveScales();
                        } else if (debugBtn.contains(x, y)) {
                            ((FlipendoActivity) getContext()).applyDebugMode(!debugMode, true);
                        } else if (!mods.isEmpty() && modBtn.contains(x, y)) {
                            modTap();
                        } else if (languages.size() > 1 && languageBtn.contains(x, y)) {
                            languageTap();
                        } else if (telegramBtn.contains(x, y)) {
                            try {
                                getContext().startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(SetupActivity.CHANNEL_URL)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                            } catch (RuntimeException ignored) {
                            }
                        } else if (closeBtn.contains(x, y) || !panel.contains(x, y)) {
                            settingsOpen = false;
                        }
                        break;
                    }
                    Btn b = hitButton(x, y);
                    if (b != null) {
                        roles.put(id, Role.BUTTON);
                        b.pointer = id;
                        if (isArrow(b)) arrowFingers.add(id);
                        if (b == cast && modOn) {
                            // the wand button is a stick too: where the finger went down is its centre
                            aimPointer = id;
                            aimCx = x;
                            aimCy = y;
                            aimDx = aimDy = 0;
                            post(aimTick);
                        }
                        if (b.toggle) setButton(b, !b.down); else setButton(b, true);
                        if (b == menu) {
                            // Holding MENU for a moment sends the logs (also when the game is stuck or black).
                            menuHeld = new Runnable() { public void run() {
                                menuHeld = null;
                                LogReport.share((FlipendoActivity) getContext());
                            } };
                            postDelayed(menuHeld, 1500);
                        }
                    } else if (menuActive()) {
                        // Menu: the finger is the mouse. Move the cursor now, press a few frames later and keep
                        // following the finger so sliders can be dragged.
                        roles.put(id, Role.MENU);
                        menuDownTime = e.getEventTime();
                        menuX = x * surfaceScale;
                        menuY = y * surfaceScale;
                        SDLActivity.onNativeMouse(0, MotionEvent.ACTION_MOVE, menuX, menuY, false);
                        postDelayed(new Runnable() { public void run() {
                            SDLActivity.onNativeMouse(MotionEvent.BUTTON_PRIMARY, MotionEvent.ACTION_DOWN, menuX, menuY, false);
                        } }, 60);
                    } else if (lessonDrawing()) {
                        // Spell lesson: the finger is the wand. Put it under the finger, then hold AltFire (left mouse).
                        roles.put(id, Role.DRAW);
                        nativeSetTouch(x / getWidth(), y / getHeight(), true);
                        SDLActivity.onNativeMouse(MotionEvent.BUTTON_PRIMARY, MotionEvent.ACTION_DOWN,
                                getWidth() / 2f * surfaceScale, getHeight() / 2f * surfaceScale, false);
                    } else if (cutsceneShown) {
                        // a cutscene holds Harry: the screen does not steer or look (the buttons above still work)
                    } else {
                        roles.put(id, !dpadMode && x < getWidth() / 2f ? Role.STICK : Role.LOOK);
                        last.put(id, new float[] { x, y });
                        start.put(id, new float[] { x, y });
                    }
                    break;
                }
                case MotionEvent.ACTION_MOVE:
                    for (int i = 0; i < e.getPointerCount(); i++) {
                        int pid = e.getPointerId(i);
                        Role role = roles.get(pid);
                        float x = e.getX(i), y = e.getY(i);
                        if (role == Role.SLIDER) {
                            setSliderFromX(activeSlider, x, false);
                            continue;
                        }
                        if (role == Role.BUTTON) {
                            slideArrow(pid, x, y);
                            if (aimPointer == pid) setAim(x, y); // the wand button as a stick (mod on)
                            continue;
                        }
                        if (role == Role.DRAW) {
                            nativeSetTouch(x / getWidth(), y / getHeight(), true);
                            continue;
                        }
                        if (role == Role.MENU) {
                            menuX = x * surfaceScale;
                            menuY = y * surfaceScale;
                            SDLActivity.onNativeMouse(MotionEvent.BUTTON_PRIMARY, MotionEvent.ACTION_MOVE, menuX, menuY, false);
                            continue;
                        }
                        float[] s = start.get(pid), l = last.get(pid);
                        if (role == null || s == null || l == null) continue;
                        float moved = (float) Math.hypot(x - s[0], y - s[1]);
                        if (role == Role.STICK) {
                            if (stickPointer == -1 && moved > slop) {
                                stickPointer = pid;
                                stickX = s[0];
                                stickY = s[1];
                            }
                            if (stickPointer == pid) {
                                knobX = x;
                                knobY = y;
                                updateStick();
                            }
                        } else if (role == Role.LOOK && moved > slop) {
                            SDLActivity.onNativeMouse(0, MotionEvent.ACTION_MOVE,
                                    (x - l[0]) * 1.5f, (y - l[1]) * 1.5f, true);
                        }
                        l[0] = x;
                        l[1] = y;
                    }
                    break;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_POINTER_UP:
                case MotionEvent.ACTION_CANCEL: {
                    Role role = roles.remove(id);
                    float[] s = start.remove(id);
                    last.remove(id);
                    boolean cancelled = action == MotionEvent.ACTION_CANCEL;
                    if (role == Role.SLIDER) {
                        if (!cancelled) setSliderFromX(activeSlider, e.getX(idx), true);
                        activeSlider = -1;
                        invalidate();
                    } else if (role == Role.DRAW) {
                        nativeSetTouch(e.getX(idx) / getWidth(), e.getY(idx) / getHeight(), false);
                        SDLActivity.onNativeMouse(0, MotionEvent.ACTION_UP,
                                getWidth() / 2f * surfaceScale, getHeight() / 2f * surfaceScale, false);
                    } else if (role == Role.MENU) {
                        // Release after the press has been sent (it goes out 60 ms after the touch) and has had a frame.
                        long held = e.getEventTime() - menuDownTime;
                        long delay = Math.max(40, 140 - held);
                        final float ux = menuX, uy = menuY;
                        postDelayed(new Runnable() { public void run() {
                            SDLActivity.onNativeMouse(0, MotionEvent.ACTION_UP, ux, uy, false);
                        } }, delay);
                    } else if (role == Role.BUTTON) {
                        arrowFingers.remove(id);
                        if (aimPointer == id) releaseAim();
                        for (Btn b : buttons) {
                            if (b.pointer == id) {
                                b.pointer = -1;
                                if (b == menu && menuHeld != null) {
                                    removeCallbacks(menuHeld);
                                    menuHeld = null;
                                }
                                if (!b.toggle) setButton(b, false);
                            }
                        }
                    } else if (role == Role.STICK) {
                        if (stickPointer == id) releaseStick();
                        else if (s != null && !cancelled) click(s[0], s[1]);
                    } else if (role == Role.LOOK && s != null && !cancelled) {
                        float moved = (float) Math.hypot(e.getX(idx) - s[0], e.getY(idx) - s[1]);
                        if (moved <= slop) click(s[0], s[1]);
                    }
                    break;
                }
                default:
                    break;
            }
            invalidate();
            return true;
        }

        // The FPS counter: frames the engine drew over the last half second (nativeFrameCount), shown left of the toolbar.
        int fpsFrames = -1;
        long fpsTime;
        int fpsValue = -1;

        void sampleFps() {
            if (!showFps) {
                fpsFrames = -1;
                return;
            }
            int frames;
            try {
                frames = nativeFrameCount();
            } catch (UnsatisfiedLinkError e) {
                return;
            }
            long now = android.os.SystemClock.uptimeMillis();
            if (fpsFrames < 0) {
                fpsFrames = frames;
                fpsTime = now;
            } else if (now - fpsTime >= 500) {
                fpsValue = Math.round((frames - fpsFrames) * 1000f / (now - fpsTime));
                fpsFrames = frames;
                fpsTime = now;
                invalidate();
            }
        }

        final android.graphics.RectF fpsPill = new android.graphics.RectF();

        void drawFps(Canvas c) {
            if (!showFps || fpsValue < 0) return;
            float u = Math.min(getWidth(), getHeight());
            fpsPill.set(toolbar.left - 0.02f * u - 0.2f * u, toolbar.top, toolbar.left - 0.02f * u, toolbar.bottom);
            float r = fpsPill.height() / 2f;
            fill.setColor(DISC);
            c.drawRoundRect(fpsPill, r, r, fill);
            line.setColor(DISC_RIM);
            line.setStrokeWidth(3f);
            c.drawRoundRect(fpsPill, r, r, line);
            String number = String.valueOf(fpsValue);
            valuePaint.setTextAlign(Paint.Align.LEFT);
            int numberColor = fpsValue >= 50 ? 0xFF7FD88A : fpsValue >= 30 ? GOLD : 0xFFFF7A6B;
            valuePaint.setColor(numberColor);
            float numberW = valuePaint.measureText(number), unitW = subPaint.measureText(" FPS");
            float x = fpsPill.centerX() - (numberW + unitW) / 2f, baseY = fpsPill.centerY() + valuePaint.getTextSize() * 0.35f;
            c.drawText(number, x, baseY, valuePaint);
            subPaint.setTextAlign(Paint.Align.LEFT);
            c.drawText(" FPS", x + numberW, baseY, subPaint);
            valuePaint.setColor(GOLD);
            valuePaint.setTextAlign(Paint.Align.RIGHT);
        }

        // the controls' geometry (onSizeChanged) and look: the settings card's colours on the game
        final android.graphics.RectF toolbar = new android.graphics.RectF();
        float padR, padX, padY;
        static final int DISC = 0x80181722, DISC_RIM = 0x55FFFFFF, DARK = 0xFF1A1A1A;
        final Paint darkPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);

        {
            darkPaint.setColorFilter(new android.graphics.PorterDuffColorFilter(DARK, android.graphics.PorterDuff.Mode.SRC_IN));
        }

        /** A round button: a dark translucent disc with a thin rim, gold while pressed. */
        void drawDisc(Canvas c, float cx, float cy, float r, boolean down) {
            fill.setColor(down ? 0xE6E8C15A : DISC);
            c.drawCircle(cx, cy, r, fill);
            line.setColor(down ? GOLD : DISC_RIM);
            line.setStrokeWidth(3f);
            c.drawCircle(cx, cy, r, line);
        }

        /** A button's icon: black glyphs white (dark on gold while pressed), coloured pictures as they are. */
        void drawIcon(Canvas c, Btn b, float cx, float cy, float size) {
            iconRect.set(cx - size, cy - size, cx + size, cy + size);
            c.drawBitmap(b.icon, null, iconRect, b.tint ? (b.down ? darkPaint : whitePaint) : iconPaint);
        }

        void drawGear(Canvas c, float cx, float cy, float r, int color) {
            line.setColor(color);
            line.setStrokeWidth(r * 0.2f);
            c.drawCircle(cx, cy, r * 0.32f, line);
            line.setStrokeCap(Paint.Cap.ROUND);
            for (int k = 0; k < 8; k++) {
                double a = k * Math.PI / 4;
                float cs = (float) Math.cos(a), sn = (float) Math.sin(a);
                c.drawLine(cx + cs * r * 0.5f, cy + sn * r * 0.5f, cx + cs * r * 0.66f, cy + sn * r * 0.66f, line);
            }
            line.setStrokeCap(Paint.Cap.BUTT);
        }

        /** A chevron pointing up, rotated: up, down, left, right. */
        void drawChevron(Canvas c, Btn b, int color) {
            float[] angle = { 0f, 180f, 270f, 90f };
            float k = b.r * 0.38f;
            arrowPath.reset();
            arrowPath.moveTo(b.cx - k, b.cy + k * 0.45f);
            arrowPath.lineTo(b.cx, b.cy - k * 0.55f);
            arrowPath.lineTo(b.cx + k, b.cy + k * 0.45f);
            line.setColor(color);
            line.setStrokeWidth(b.r * 0.16f);
            line.setStrokeCap(Paint.Cap.ROUND);
            line.setStrokeJoin(Paint.Join.ROUND);
            c.save();
            c.rotate(angle[arrowIndex(b)], b.cx, b.cy);
            c.drawPath(arrowPath, line);
            c.restore();
            line.setStrokeCap(Paint.Cap.BUTT);
            line.setStrokeJoin(Paint.Join.MITER);
        }

        @Override
        protected void onDraw(Canvas c) {
            float u = Math.min(getWidth(), getHeight());

            // top right: the toolbar, 30% visible (gear | save or skip | menu)
            int layer = c.saveLayerAlpha(toolbar.left - 4f, toolbar.top - 4f, toolbar.right + 4f, toolbar.bottom + 4f, 77);
            float tr = toolbar.height() / 2f;
            fill.setColor(DISC);
            c.drawRoundRect(toolbar, tr, tr, fill);
            line.setColor(DISC_RIM);
            line.setStrokeWidth(3f);
            c.drawRoundRect(toolbar, tr, tr, line);
            line.setColor(0x33FFFFFF);
            line.setStrokeWidth(2f);
            float sepTop = toolbar.top + 0.25f * toolbar.height(), sepBottom = toolbar.bottom - 0.25f * toolbar.height();
            float sep1 = (gear.cx + save.cx) / 2f, sep2 = (save.cx + menu.cx) / 2f;
            c.drawLine(sep1, sepTop, sep1, sepBottom, line);
            c.drawLine(sep2, sepTop, sep2, sepBottom, line);
            for (Btn b : new Btn[] { gear, save, skip, menu }) {
                if (!visible(b)) continue;
                if (b.down) {
                    fill.setColor(0xE6E8C15A);
                    c.drawCircle(b.cx, b.cy, 0.045f * u, fill);
                }
                if (b == gear) {
                    drawGear(c, b.cx, b.cy, b.r, b.down ? DARK : 0xFFFFFFFF);
                } else if (b == skip) {
                    sectionPaint.setTextAlign(Paint.Align.CENTER);
                    sectionPaint.setColor(b.down ? DARK : GOLD);
                    c.drawText("SKIP ›", b.cx, b.cy + sectionPaint.getTextSize() * 0.35f, sectionPaint);
                    sectionPaint.setColor(GOLD);
                    sectionPaint.setTextAlign(Paint.Align.LEFT);
                } else if (b.icon != null) {
                    drawIcon(c, b, b.cx, b.cy, b.r * 0.8f);
                } else {
                    text.setColor(b.down ? DARK : 0xFFFFFFFF);
                    c.drawText(b.label, b.cx, b.cy + text.getTextSize() * 0.35f, text);
                    text.setColor(0xCCFFFFFF);
                }
            }
            c.restoreToCount(layer);
            drawFps(c);

            // bottom left: the arrows on one round pad
            if (visible(arrows[0])) {
                fill.setColor(0x55181722);
                c.drawCircle(padX, padY, padR, fill);
                line.setColor(0x33FFFFFF);
                line.setStrokeWidth(3f);
                c.drawCircle(padX, padY, padR, line);
                fill.setColor(0x22FFFFFF);
                c.drawCircle(padX, padY, 0.03f * u, fill);
                for (Btn a : arrows) {
                    if (a.down) {
                        fill.setColor(0xE6E8C15A);
                        c.drawCircle(a.cx, a.cy, a.r * 0.85f, fill);
                    }
                    drawChevron(c, a, a.down ? DARK : 0xE6FFFFFF);
                }
            }

            // bottom right: jump and the wand
            if (visible(jump)) {
                drawDisc(c, jump.cx, jump.cy, jump.down ? jump.r * 0.94f : jump.r, jump.down);
                if (jump.icon != null) drawIcon(c, jump, jump.cx, jump.cy, jump.r * 0.62f);
            }
            if (visible(cast)) {
                if (aimVisible()) {
                    // the range of the wand stick: the finger's offset from where it went down turns the camera
                    line.setColor(aimPointer != -1 ? 0x99E8C15A : 0x26FFFFFF);
                    line.setStrokeWidth(3f);
                    c.drawCircle(cast.cx, cast.cy, aimR + cast.r, line);
                }
                c.save();
                if (modOn && aimPointer != -1) c.translate(aimDx, aimDy); // the wand button is the stick: it follows the finger
                drawDisc(c, cast.cx, cast.cy, cast.down ? cast.r * 0.94f : cast.r, cast.down);
                if (cast.icon != null) drawIcon(c, cast, cast.cx, cast.cy, cast.r * 0.66f);
                c.restore();
            }

            // the floating stick, where the finger went down
            if (stickPointer != -1) {
                float r = 0.12f * u;
                fill.setColor(0x55181722);
                c.drawCircle(stickX, stickY, r, fill);
                line.setColor(0x44FFFFFF);
                line.setStrokeWidth(3f);
                c.drawCircle(stickX, stickY, r, line);
                fill.setColor(0xCCE8C15A);
                c.drawCircle(knobX, knobY, r * 0.4f, fill);
            }
            drawSettings(c);
            drawPrompt(c);
        }
    }
}
