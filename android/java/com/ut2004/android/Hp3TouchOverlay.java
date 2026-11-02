package com.ut2004.android;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

import org.libsdl.app.SDLActivity;

/**
 * Harry Potter 3's touch controls, drawn over the game's surface. A floating stick on the left walks (as W, A, S, D),
 * a drag anywhere on the right turns the view (relative mouse motion), and round buttons cast (left mouse button),
 * jump, use/skip, open the menu and the map, and cycle the spells (mouse wheel). Everything goes to the engine as the
 * keyboard and mouse events SDL already delivers, so it plays with the keys the game binds. While a menu is open the
 * overlay hides and gives the touches back to SDL, which turns them into clicks.
 */
final class Hp3TouchOverlay extends View {
    /** How many pixels the view turns per pixel a finger drags. */
    private static final float LOOK_SPEED = 0.55f;
    private static final int NONE = -1;

    private static final int KIND_KEY = 0;
    private static final int KIND_FIRE = 1;
    private static final int KIND_WHEEL = 2;

    /** A round button: where it sits (dp from the right and bottom edges), its size, and what it does. */
    private static final class Button {
        final String label;
        final float rightDp, bottomDp, radiusDp;
        final int kind, key;
        int pointer = NONE;
        float cx, cy, r;

        Button(String label, float rightDp, float bottomDp, float radiusDp, int kind, int key) {
            this.label = label;
            this.rightDp = rightDp;
            this.bottomDp = bottomDp;
            this.radiusDp = radiusDp;
            this.kind = kind;
            this.key = key;
        }
    }

    private final Button[] buttons = {
        new Button("Cast", 104, 104, 46, KIND_FIRE, 0),
        new Button("Jump", 214, 70, 36, KIND_KEY, KeyEvent.KEYCODE_SPACE),
        new Button("Use", 62, 214, 34, KIND_KEY, KeyEvent.KEYCODE_ENTER),
    };
    /** The top-edge buttons: (label, dp from the right edge, dp from the top edge, radius, kind, key). */
    private final Button[] topButtons = {
        new Button("Menu", 44, 38, 28, KIND_KEY, KeyEvent.KEYCODE_ESCAPE),
        new Button("Map", 124, 38, 28, KIND_KEY, KeyEvent.KEYCODE_TAB),
        new Button("<", 204, 38, 28, KIND_WHEEL, -1),
        new Button(">", 284, 38, 28, KIND_WHEEL, 1),
    };

    // The walking stick: where the finger went down and where it is now, in pixels.
    private int stickPointer = NONE;
    private float stickX, stickY, stickNowX, stickNowY;
    private final boolean[] walking = new boolean[4]; // forward, back, left, right
    private static final int[] WALK_KEYS = {
        KeyEvent.KEYCODE_W, KeyEvent.KEYCODE_S, KeyEvent.KEYCODE_A, KeyEvent.KEYCODE_D };

    private int lookPointer = NONE;
    private float lookX, lookY;

    private final float density;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private boolean menuOpen = true;
    private final Runnable poll = new Runnable() {
        @Override public void run() {
            boolean open = GameActivity.nativeInMenu();
            if (open != menuOpen) {
                menuOpen = open;
                releaseAll();
                invalidate();
            }
            postDelayed(this, 150);
        }
    };

    Hp3TouchOverlay(Context context) {
        super(context);
        density = context.getResources().getDisplayMetrics().density;
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeWidth(2 * density);
        text.setTextAlign(Paint.Align.CENTER);
        text.setColor(0xFFFFFFFF);
        text.setTextSize(15 * density);
        text.setFakeBoldText(true);
        setFocusable(false);
    }

    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        post(poll);
    }

    @Override protected void onDetachedFromWindow() {
        removeCallbacks(poll);
        releaseAll();
        super.onDetachedFromWindow();
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        for (Button b : buttons) {
            b.r = b.radiusDp * density;
            b.cx = w - b.rightDp * density;
            b.cy = h - b.bottomDp * density;
        }
        for (Button b : topButtons) {
            b.r = b.radiusDp * density;
            b.cx = w - b.rightDp * density;
            b.cy = b.bottomDp * density;
        }
    }

    private Button hit(float x, float y) {
        for (Button b : buttons) {
            float dx = x - b.cx, dy = y - b.cy;
            if (dx * dx + dy * dy <= b.r * b.r * 1.5f) return b;
        }
        for (Button b : topButtons) {
            float dx = x - b.cx, dy = y - b.cy;
            if (dx * dx + dy * dy <= b.r * b.r * 1.5f) return b;
        }
        return null;
    }

    private void press(Button b) {
        switch (b.kind) {
            case KIND_KEY: SDLActivity.onNativeKeyDown(b.key); break;
            case KIND_FIRE: SDLActivity.onNativeMouse(MotionEvent.BUTTON_PRIMARY, MotionEvent.ACTION_DOWN, 0, 0, true); break;
            case KIND_WHEEL: SDLActivity.onNativeMouse(0, MotionEvent.ACTION_SCROLL, 0, b.key, false); break;
            default: break;
        }
    }

    private void release(Button b) {
        switch (b.kind) {
            case KIND_KEY: SDLActivity.onNativeKeyUp(b.key); break;
            case KIND_FIRE: SDLActivity.onNativeMouse(0, MotionEvent.ACTION_UP, 0, 0, true); break;
            default: break;
        }
    }

    private void look(float dx, float dy) {
        if (dx != 0 || dy != 0) {
            SDLActivity.onNativeMouse(0, MotionEvent.ACTION_MOVE, dx * LOOK_SPEED, dy * LOOK_SPEED, true);
        }
    }

    /** Sets the four walking keys from where the stick is: pressed once it is a quarter of its reach off centre. */
    private void updateWalk() {
        float reach = 70 * density, dead = reach * 0.28f;
        float dx = stickNowX - stickX, dy = stickNowY - stickY;
        boolean[] want = {
            stickPointer != NONE && dy < -dead, stickPointer != NONE && dy > dead,
            stickPointer != NONE && dx < -dead, stickPointer != NONE && dx > dead };
        for (int i = 0; i < 4; i++) {
            if (want[i] != walking[i]) {
                walking[i] = want[i];
                if (want[i]) SDLActivity.onNativeKeyDown(WALK_KEYS[i]); else SDLActivity.onNativeKeyUp(WALK_KEYS[i]);
            }
        }
    }

    private void releaseAll() {
        stickPointer = NONE;
        lookPointer = NONE;
        updateWalk();
        for (Button b : buttons) {
            if (b.pointer != NONE) { b.pointer = NONE; release(b); }
        }
        for (Button b : topButtons) {
            if (b.pointer != NONE) { b.pointer = NONE; release(b); }
        }
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        if (menuOpen) {
            return false;
        }
        int action = e.getActionMasked();
        int index = e.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int id = e.getPointerId(index);
                float x = e.getX(index), y = e.getY(index);
                Button b = hit(x, y);
                if (b != null && b.pointer == NONE) {
                    b.pointer = id;
                    press(b);
                    if (b.kind == KIND_FIRE) { lookX = x; lookY = y; }
                } else if (x < getWidth() * 0.42f && stickPointer == NONE) {
                    stickPointer = id;
                    stickX = stickNowX = x;
                    stickY = stickNowY = y;
                } else if (lookPointer == NONE) {
                    lookPointer = id;
                    lookX = x;
                    lookY = y;
                }
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < e.getPointerCount(); i++) {
                    int id = e.getPointerId(i);
                    float x = e.getX(i), y = e.getY(i);
                    if (id == stickPointer) {
                        stickNowX = x;
                        stickNowY = y;
                        updateWalk();
                    } else if (id == lookPointer || buttons[0].pointer == id) {
                        // dragging a finger that holds the cast button aims as well
                        look(x - lookX, y - lookY);
                        lookX = x;
                        lookY = y;
                    }
                }
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL: {
                int id = e.getPointerId(index);
                if (action == MotionEvent.ACTION_CANCEL) {
                    releaseAll();
                } else if (id == stickPointer) {
                    stickPointer = NONE;
                    updateWalk();
                } else if (id == lookPointer) {
                    lookPointer = NONE;
                } else {
                    for (Button b : buttons) {
                        if (b.pointer == id) { b.pointer = NONE; release(b); }
                    }
                    for (Button b : topButtons) {
                        if (b.pointer == id) { b.pointer = NONE; release(b); }
                    }
                }
                invalidate();
                return true;
            }
            default:
                return true;
        }
    }

    @Override protected void onDraw(Canvas canvas) {
        if (menuOpen) {
            return;
        }
        // the stick: where the finger went down (or a faint ring where it would)
        float reach = 70 * density;
        float sx = stickPointer != NONE ? stickX : 120 * density;
        float sy = stickPointer != NONE ? stickY : getHeight() - 120 * density;
        fill.setColor(stickPointer != NONE ? 0x33FFFFFF : 0x1AFFFFFF);
        canvas.drawCircle(sx, sy, reach, fill);
        line.setColor(0x66FFFFFF);
        canvas.drawCircle(sx, sy, reach, line);
        if (stickPointer != NONE) {
            float dx = stickNowX - stickX, dy = stickNowY - stickY;
            float len = (float) Math.sqrt(dx * dx + dy * dy);
            if (len > reach) { dx *= reach / len; dy *= reach / len; }
            fill.setColor(0x88FFFFFF);
            canvas.drawCircle(sx + dx, sy + dy, 26 * density, fill);
        }
        for (Button b : buttons) {
            drawButton(canvas, b);
        }
        for (Button b : topButtons) {
            drawButton(canvas, b);
        }
    }

    private void drawButton(Canvas canvas, Button b) {
        boolean down = b.pointer != NONE;
        fill.setColor(down ? 0x99E6C060 : 0x44000000);
        canvas.drawCircle(b.cx, b.cy, b.r, fill);
        line.setColor(down ? 0xFFE6C060 : 0x88FFFFFF);
        canvas.drawCircle(b.cx, b.cy, b.r, line);
        text.setTextSize((b.radiusDp > 40 ? 17 : b.radiusDp > 30 ? 14 : 12) * density);
        canvas.drawText(b.label, b.cx, b.cy + text.getTextSize() * 0.35f, text);
    }
}
