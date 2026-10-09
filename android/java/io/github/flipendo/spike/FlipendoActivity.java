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
    /** Ratio of the render surface to the screen (the surface is a fixed-size SurfaceView scaled up by the compositor). */
    static final float SURFACE_SCALE = 0.5f;

    /** True while a menu wants the touch cursor (the engine unlocks the mouse); implemented in android_main.cpp. */
    static native boolean nativeMenuActive();

    /** True while a spell lesson is in its Draw state: the finger draws the symbol. */
    static native boolean nativeLessonDrawing();

    /** The finger position (0..1 of the view) that drives the wand in a lesson; implemented in android_main.cpp. */
    static native void nativeSetTouch(float x, float y, boolean down);

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
        // Spike: render at a reduced fixed surface size; the compositor scales it to the screen.
        if (mSurface != null) {
            android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
            int longSide = Math.max(dm.widthPixels, dm.heightPixels), shortSide = Math.min(dm.widthPixels, dm.heightPixels);
            float scale = SURFACE_SCALE;
            mSurface.getHolder().setFixedSize((int) (longSide * scale), (int) (shortSide * scale));
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
        enum Role { STICK, LOOK, BUTTON, MENU, DRAW }

        boolean menuShown;       // last polled menu state: hides the gameplay buttons
        boolean lessonShown;     // last polled lesson state: hides the gameplay buttons too
        long menuDownTime;       // when the menu pointer went down
        float menuX, menuY;      // last menu pointer position, in surface pixels

        final Runnable poll = new Runnable() {
            public void run() {
                boolean m = menuActive(), l = lessonDrawing();
                if (m != menuShown || l != lessonShown) {
                    menuShown = m;
                    lessonShown = l;
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
            return b == menu || !(menuShown || lessonShown);
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
        final Btn[] buttons = { cast, jump, menu };

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
        }

        /** The button icons ship in the APK (assets/icons/, copied from images/android/ by build-apk.sh). */
        static android.graphics.Bitmap loadIcon(android.content.Context c, String name) {
            try (InputStream in = c.getAssets().open("icons/" + name)) {
                return android.graphics.BitmapFactory.decodeStream(in);
            } catch (IOException e) {
                return null; // the button falls back to its text label
            }
        }

        @Override
        protected void onSizeChanged(int w, int h, int ow, int oh) {
            float u = Math.min(w, h);
            cast.cx = w - 0.20f * u; cast.cy = h - 0.22f * u; cast.r = 0.13f * u;
            jump.cx = w - 0.42f * u; jump.cy = h - 0.12f * u; jump.r = 0.085f * u;
            menu.cx = w - 0.08f * u; menu.cy = 0.08f * u; menu.r = 0.06f * u;
            text.setTextSize(0.035f * u);
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
            if (b == jump) key(KeyEvent.KEYCODE_SPACE, down); // Space skips cutscenes; Ctrl is the game's jump key
            b.down = down;
            if (b.mouse != 0)
                SDLActivity.onNativeMouse(down ? b.mouse : 0, down ? MotionEvent.ACTION_DOWN : MotionEvent.ACTION_UP,
                        getWidth() / 2f * SURFACE_SCALE, getHeight() / 2f * SURFACE_SCALE, false);
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
            final float x = sx * SURFACE_SCALE, y = sy * SURFACE_SCALE;
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
                    Btn b = hitButton(x, y);
                    if (b != null) {
                        roles.put(id, Role.BUTTON);
                        b.pointer = id;
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
                        menuX = x * SURFACE_SCALE;
                        menuY = y * SURFACE_SCALE;
                        SDLActivity.onNativeMouse(0, MotionEvent.ACTION_MOVE, menuX, menuY, false);
                        postDelayed(new Runnable() { public void run() {
                            SDLActivity.onNativeMouse(MotionEvent.BUTTON_PRIMARY, MotionEvent.ACTION_DOWN, menuX, menuY, false);
                        } }, 60);
                    } else if (lessonDrawing()) {
                        // Spell lesson: the finger is the wand. Put it under the finger, then hold AltFire (left mouse).
                        roles.put(id, Role.DRAW);
                        nativeSetTouch(x / getWidth(), y / getHeight(), true);
                        SDLActivity.onNativeMouse(MotionEvent.BUTTON_PRIMARY, MotionEvent.ACTION_DOWN,
                                getWidth() / 2f * SURFACE_SCALE, getHeight() / 2f * SURFACE_SCALE, false);
                    } else {
                        roles.put(id, x < getWidth() / 2f ? Role.STICK : Role.LOOK);
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
                        if (role == Role.DRAW) {
                            nativeSetTouch(x / getWidth(), y / getHeight(), true);
                            continue;
                        }
                        if (role == Role.MENU) {
                            menuX = x * SURFACE_SCALE;
                            menuY = y * SURFACE_SCALE;
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
                    if (role == Role.DRAW) {
                        nativeSetTouch(e.getX(idx) / getWidth(), e.getY(idx) / getHeight(), false);
                        SDLActivity.onNativeMouse(0, MotionEvent.ACTION_UP,
                                getWidth() / 2f * SURFACE_SCALE, getHeight() / 2f * SURFACE_SCALE, false);
                    } else if (role == Role.MENU) {
                        // Release after the press has been sent (it goes out 60 ms after the touch) and has had a frame.
                        long held = e.getEventTime() - menuDownTime;
                        long delay = Math.max(40, 140 - held);
                        final float ux = menuX, uy = menuY;
                        postDelayed(new Runnable() { public void run() {
                            SDLActivity.onNativeMouse(0, MotionEvent.ACTION_UP, ux, uy, false);
                        } }, delay);
                    } else if (role == Role.BUTTON) {
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
                if (b.icon == null) {
                    fill.setColor(b.down ? 0x88FFD84A : 0x55000000);
                    c.drawCircle(b.cx, b.cy, b.r, fill);
                    line.setColor(0xAAFFFFFF);
                    c.drawCircle(b.cx, b.cy, b.r, line);
                    c.drawText(b.label, b.cx, b.cy + text.getTextSize() * 0.35f, text);
                    continue;
                }
                // an icon on a soft disc: dark behind the white glyphs, light behind the coloured wand
                fill.setColor(b.tint ? (b.down ? 0x99000000 : 0x66000000) : (b.down ? 0xCCFFFFFF : 0x99FFFFFF));
                c.drawCircle(b.cx, b.cy, b.r * scale, fill);
                float half = b.r * 0.82f * scale;
                iconRect.set(b.cx - half, b.cy - half, b.cx + half, b.cy + half);
                c.drawBitmap(b.icon, null, iconRect, b.tint ? (b.down ? yellowPaint : whitePaint) : iconPaint);
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
        }
    }
}
