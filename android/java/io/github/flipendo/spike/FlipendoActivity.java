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
                .putFloat("fovOffset", fovOffset).putBoolean("debugMode", debugMode).putBoolean("autoJump", autoJump).apply();
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

        String languageText() {
            if (languageAsk >= 0) {
                return "Restart in " + languages.get(languageAsk)[1] + "? Tap again (unsaved progress is lost)";
            }
            return "Language: " + languages.get(languageIndex(currentLanguage()))[1] + "  (tap to change)";
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

        String modText() {
            String name = mods.get(0)[1];
            if (modAsk) {
                return "Restart " + (modOn ? "without" : "with") + " the mod? Tap again (unsaved progress is lost)";
            }
            return name + ": " + (modOn ? "ON" : "OFF") + "  (tap to change)";
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

        boolean settingsOpen;    // the settings panel is up: it takes every touch
        final android.graphics.RectF panel = new android.graphics.RectF();
        final android.graphics.RectF segStick = new android.graphics.RectF(), segDpad = new android.graphics.RectF();
        final android.graphics.RectF closeBtn = new android.graphics.RectF(), debugBtn = new android.graphics.RectF(), autoJumpBtn = new android.graphics.RectF(), telegramBtn = new android.graphics.RectF(), languageBtn = new android.graphics.RectF(), modBtn = new android.graphics.RectF();

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
            // with the mod the wand button is a stick whose range ring must clear the jump button: it sits higher
            cast.cx = w - 0.20f * u; cast.cy = h - (modOn ? 0.40f : 0.22f) * u; cast.r = 0.13f * u;
            jump.cx = w - 0.42f * u; jump.cy = h - 0.12f * u; jump.r = 0.085f * u;
            menu.cx = w - 0.08f * u; menu.cy = 0.08f * u; menu.r = 0.06f * u;
            save.cx = w - 0.21f * u; save.cy = 0.08f * u; save.r = 0.06f * u;
            skip.cx = save.cx; skip.cy = save.cy; skip.r = save.r; // the book's place: the book is hidden in cutscenes
            text.setTextSize(0.035f * u);
            aimR = 0.12f * u; // how far the wand button follows the finger

            gear.cx = w - 0.34f * u; gear.cy = 0.08f * u; gear.r = 0.06f * u;
            float ax = 0.27f * u, ay = h - 0.27f * u, ad = 0.155f * u;
            arrows[0].cx = ax; arrows[0].cy = ay - ad;
            arrows[1].cx = ax; arrows[1].cy = ay + ad;
            arrows[2].cx = ax - ad; arrows[2].cy = ay;
            arrows[3].cx = ax + ad; arrows[3].cy = ay;
            for (Btn a : arrows) a.r = 0.08f * u;

            // The settings panel, centred: controls switch, the two sliders, Close.
            float pw = Math.min(0.9f * w, 1.5f * h);
            panel.set((w - pw) / 2f, 0.03f * h, (w + pw) / 2f, 0.97f * h);
            float gap = 0.02f * pw;
            // rows, top to bottom: movement controls, three sliders, Auto jump | Debug, Language and Mod (when the APK has them), Telegram | Close
            float rowH = 0.075f * h;
            segStick.set(panel.left + 0.06f * pw, 0.19f * h, w / 2f - gap, 0.19f * h + rowH);
            segDpad.set(w / 2f + gap, 0.19f * h, panel.right - 0.06f * pw, 0.19f * h + rowH);
            sliderX0 = panel.left + 0.1f * pw;
            sliderX1 = panel.right - 0.1f * pw;
            sliderY[0] = 0.335f * h;
            sliderY[1] = 0.415f * h;
            sliderY[2] = 0.495f * h;
            autoJumpBtn.set(panel.left + 0.06f * pw, 0.545f * h, w / 2f - gap, 0.545f * h + rowH);
            debugBtn.set(w / 2f + gap, 0.545f * h, panel.right - 0.06f * pw, 0.545f * h + rowH);
            languageBtn.set(panel.left + 0.06f * pw, 0.635f * h, panel.right - 0.06f * pw, 0.635f * h + rowH);
            modBtn.set(panel.left + 0.06f * pw, 0.725f * h, panel.right - 0.06f * pw, 0.725f * h + rowH);
            telegramBtn.set(panel.left + 0.06f * pw, 0.84f * h, w / 2f - gap, 0.84f * h + rowH);
            closeBtn.set(w / 2f + gap, 0.84f * h, panel.right - 0.06f * pw, 0.84f * h + rowH);
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
                if (Math.abs(y - sliderY[i]) < 0.08f * u && x > sliderX0 - 0.05f * u && x < sliderX1 + 0.05f * u) return i;
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
            fill.setColor(0xB0000000);
            c.drawRect(0, 0, getWidth(), getHeight(), fill);
            fill.setColor(0xF01C1C26);
            c.drawRoundRect(panel, 0.03f * u, 0.03f * u, fill);
            line.setColor(0x66FFFFFF);
            line.setStrokeWidth(3f);
            c.drawRoundRect(panel, 0.03f * u, 0.03f * u, line);
            float size = text.getTextSize();
            text.setTextSize(size * 1.5f);
            text.setColor(0xFFFFFFFF);
            c.drawText("Settings", getWidth() / 2f, 0.095f * getHeight(), text);
            text.setTextSize(size);
            text.setColor(0xCCFFFFFF);
            c.drawText("Movement controls", getWidth() / 2f, 0.165f * getHeight(), text);
            drawSegment(c, segStick, "Floating stick", !dpadMode);
            drawSegment(c, segDpad, "Arrow buttons", dpadMode);
            drawSliders(c);
            drawSegment(c, autoJumpBtn, autoJump ? "Auto jump: ON" : "Auto jump: OFF", autoJump);
            drawSegment(c, debugBtn, debugMode ? "Debug mode: ON" : "Debug mode: OFF", debugMode);
            if (!mods.isEmpty()) {
                drawSegment(c, modBtn, modText(), modAsk || modOn);
            }
            if (languages.size() > 1) {
                drawSegment(c, languageBtn, languageText(), languageAsk >= 0);
            }
            fill.setColor(0x44FFFFFF);
            c.drawRoundRect(closeBtn, closeBtn.height() / 2f, closeBtn.height() / 2f, fill);
            text.setColor(0xFFFFFFFF);
            c.drawText("Close", closeBtn.centerX(), closeBtn.centerY() + size * 0.35f, text);
            fill.setColor(0x445EB8FF);
            c.drawRoundRect(telegramBtn, telegramBtn.height() / 2f, telegramBtn.height() / 2f, fill);
            c.drawText("Telegram channel", telegramBtn.centerX(), telegramBtn.centerY() + size * 0.35f, text);
            text.setColor(0xCCFFFFFF);
        }

        void drawSliders(Canvas c) {
            float u = Math.min(getWidth(), getHeight());
            android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
            int longSide = Math.max(dm.widthPixels, dm.heightPixels), shortSide = Math.min(dm.widthPixels, dm.heightPixels);
            for (int i = 0; i < 3; i++) {
                float v = sliderValue(i);
                String name = i == 0
                        ? "Render scale " + Math.round(v * 100) + "%  (" + (int) (longSide * v) + "x" + (int) (shortSide * v) + ")"
                        : i == 1 ? "Subtitles and HUD size " + Math.round(v * 100) + "%"
                        : "Field of view " + (v > 0 ? "+" : "") + Math.round(v) + (v == 0 ? " (default)" : " degrees");
                c.drawText(name, sliderX0, sliderY[i] - 0.05f * u, label);
                float t = (v - sliderMin(i)) / (sliderMax(i) - sliderMin(i));
                float kx = sliderX0 + t * (sliderX1 - sliderX0);
                line.setStrokeWidth(0.012f * u);
                line.setColor(0x88FFFFFF);
                c.drawLine(sliderX0, sliderY[i], sliderX1, sliderY[i], line);
                line.setColor(0xFFFFD84A);
                c.drawLine(sliderX0, sliderY[i], kx, sliderY[i], line);
                fill.setColor(activeSlider == i ? 0xFFFFD84A : 0xFFFFFFFF);
                c.drawCircle(kx, sliderY[i], 0.03f * u, fill);
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

        @Override
        protected void onDraw(Canvas c) {
            for (Btn b : buttons) {
                if (!visible(b)) continue;
                float scale = b.down ? 0.9f : 1f;
                // the top row (settings, save or skip, menu) is 30% visible
                boolean faint = b == gear || b == save || b == menu || b == skip;
                int layer = faint ? c.saveLayerAlpha(b.cx - b.r * 1.3f, b.cy - b.r * 1.3f, b.cx + b.r * 1.3f, b.cy + b.r * 1.3f, 77) : 0;
                if (b == gear || isArrow(b)) {
                    fill.setColor(b.down ? 0x99FFD84A : 0x55000000);
                    c.drawCircle(b.cx, b.cy, b.r * scale, fill);
                    line.setColor(0xAAFFFFFF);
                    line.setStrokeWidth(4f);
                    c.drawCircle(b.cx, b.cy, b.r * scale, line);
                    fill.setColor(b.down ? 0xFFFFD84A : 0xCCFFFFFF);
                    if (b == gear) {
                        // a gear: a ring with eight teeth
                        line.setStrokeWidth(b.r * 0.22f);
                        c.drawCircle(b.cx, b.cy, b.r * 0.32f, line);
                        for (int k = 0; k < 8; k++) {
                            double a = k * Math.PI / 4;
                            float cs = (float) Math.cos(a), sn = (float) Math.sin(a);
                            c.drawLine(b.cx + cs * b.r * 0.42f, b.cy + sn * b.r * 0.42f,
                                    b.cx + cs * b.r * 0.62f, b.cy + sn * b.r * 0.62f, line);
                        }
                        line.setStrokeWidth(4f);
                    } else {
                        // a triangle pointing up, rotated: up, down, left, right
                        float[] angle = { 0f, 180f, 270f, 90f };
                        float k = b.r * 0.45f;
                        arrowPath.reset();
                        arrowPath.moveTo(b.cx, b.cy - k);
                        arrowPath.lineTo(b.cx - k, b.cy + k * 0.7f);
                        arrowPath.lineTo(b.cx + k, b.cy + k * 0.7f);
                        arrowPath.close();
                        c.save();
                        c.rotate(angle[arrowIndex(b)], b.cx, b.cy);
                        c.drawPath(arrowPath, fill);
                        c.restore();
                    }
                    if (faint) c.restoreToCount(layer);
                    continue;
                }
                if (b.icon == null) {
                    fill.setColor(b.down ? 0x88FFD84A : 0x55000000);
                    c.drawCircle(b.cx, b.cy, b.r, fill);
                    line.setColor(0xAAFFFFFF);
                    c.drawCircle(b.cx, b.cy, b.r, line);
                    c.drawText(b.label, b.cx, b.cy + text.getTextSize() * 0.35f, text);
                    if (faint) c.restoreToCount(layer);
                    continue;
                }
                // an icon on a soft dark disc (the jump button's), also behind the coloured wand and book
                c.save();
                if (b == cast && modOn && aimPointer != -1) c.translate(aimDx, aimDy); // the wand button is the stick: it follows the finger
                fill.setColor(b.down ? 0x99000000 : 0x66000000);
                c.drawCircle(b.cx, b.cy, b.r * scale, fill);
                line.setColor(0xAAFFFFFF); // the white rim, as on the settings and arrow buttons
                line.setStrokeWidth(4f);
                c.drawCircle(b.cx, b.cy, b.r * scale, line);
                float half = b.r * 0.82f * scale;
                iconRect.set(b.cx - half, b.cy - half, b.cx + half, b.cy + half);
                c.drawBitmap(b.icon, null, iconRect, b.tint ? (b.down ? yellowPaint : whitePaint) : iconPaint);
                c.restore();
                if (faint) c.restoreToCount(layer);
            }
            if (aimVisible()) {
                // the range of the wand stick: the finger's offset from where it went down turns the camera
                line.setColor(aimPointer != -1 ? 0x88FFD84A : 0x33FFFFFF);
                line.setStrokeWidth(3f);
                c.drawCircle(cast.cx, cast.cy, aimR + cast.r, line);
            }
            if (stickPointer != -1) {
                float r = 0.12f * Math.min(getWidth(), getHeight());
                fill.setColor(0x33000000);
                c.drawCircle(stickX, stickY, r, fill);
                line.setColor(0x88FFFFFF);
                c.drawCircle(stickX, stickY, r, line);
                fill.setColor(0x88FFFFFF);
                c.drawCircle(knobX, knobY, r * 0.4f, fill);
            }
            drawSettings(c);
        }
    }
}
