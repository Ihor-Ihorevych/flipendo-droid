package io.github.flipendo.spike;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

/**
 * The PS2 pad drawn over the game (Ps2Activity): a left stick that can be switched to arrow buttons (Settings > Controls), the four face
 * buttons, L1 L2 R1 R2, Start and Select, and the menu button (it opens the settings). Multi-touch; every change goes to the runner as the pad's
 * bit mask and the left stick (host/android_touch.cpp: the bits are the Button enum of host/window.cpp). Two skins draw the same controls
 * (Settings > Controls > Controls skin, kept in the preferences "ps2play" as "skin"): "fancy", the default, the look of the Flipendo game's own touch
 * controls (FlipendoActivity.Overlay: dark translucent discs with a thin rim, gold when pressed, the arrows pad's ring and chevrons, a gold knob)
 * as vector art (assets/ps2pad/*.svg through Ps2Svg), with symbols and no words, in a layout of its own, and "ps2", the first look: translucent
 * PS2 glyph buttons. Settings > Controls > Control layout lets the player drag each control where they want it and resize
 * it, like the PC games' layout editor; the choices are kept in the preferences "ps2pad", separately for each skin.
 */
public final class Ps2Pad extends View {
    /** The bridge to the runner (host/android_touch.cpp, the class name is part of the symbol). */
    static native void nativeSet(int buttons, int lx, int ly);
    static native void nativeDisplayMode(int mode);

    // PS2 pad bits, as the Button enum of host/window.cpp
    static final int SELECT = 1, START = 1 << 3, UP = 1 << 4, RIGHT = 1 << 5, DOWN = 1 << 6, LEFT = 1 << 7,
            L2 = 1 << 8, R2 = 1 << 9, L1 = 1 << 10, R1 = 1 << 11, TRI = 1 << 12, CIRCLE = 1 << 13, CROSS = 1 << 14, SQUARE = 1 << 15;

    public interface Listener {
        void onMenu();
    }

    /** The size the fancy layout was drawn for (a 1376 x 768 picture): its numbers are in these units and follow the screen's height. */
    private static final float REF_W = 1376f, REF_H = 768f;
    private static final int WHITE = 0xFFFFFFFF, DARK = 0xFF1A1A1A;   // the Flipendo icon colours: white, dark on a gold (pressed) button

    /** One round or rounded control: where it sits by default (dp from an anchor corner), its size, its bit and its label. */
    private static final class Control {
        final String label;
        final String key;                   // the name of its place in the preferences
        final int mask;
        final boolean round;
        final float ax, ay, dx, dy, w, h;   // anchor 0 = left/top, 1 = right/bottom (0.5 = middle); offset and size in dp (fancy: in reference units)
        final RectF box = new RectF();
        float cx = -1, cy = -1;             // the player's place for it: its centre as a fraction of the pad's width and height (-1 = the default)
        float size = 1f;                    // the player's size for it

        // the fancy skin
        final boolean mock;
        String artName, glyphName;
        Ps2Svg art, glyph;
        final RectF glyphBox = new RectF();

        Control(String label, int mask, boolean round, float ax, float ay, float dx, float dy, float w, float h) {
            this(label, label, mask, round, ax, ay, dx, dy, w, h, false);
        }

        Control(String label, String key, int mask, boolean round, float ax, float ay, float dx, float dy, float w, float h, boolean mock) {
            this.label = label;
            this.key = key;
            this.mask = mask;
            this.round = round;
            this.ax = ax;
            this.ay = ay;
            this.dx = dx;
            this.dy = dy;
            this.w = w;
            this.h = h;
            this.mock = mock;
        }
    }

    /** A control of the fancy skin: its centre (cx, cy) and size in the 1376 x 768 picture, anchored to the nearest screen edge. */
    private static Control fancy(String label, int mask, boolean round, float ax, float ay, float cx, float cy, float w, float h, String art, String glyph) {
        float dx = (ax == 0 ? cx : ax == 1 ? cx - REF_W : cx - REF_W / 2f) - w / 2f;
        float dy = (ay == 0 ? cy : ay == 1 ? cy - REF_H : cy - REF_H / 2f) - h / 2f;
        Control c = new Control(label, "fancy_" + label, mask, round, ax, ay, dx, dy, w, h, true);
        c.artName = art;
        c.glyphName = glyph;
        return c;
    }

    // the console's own look (the first skin)
    private static final Control[] PS2_CONTROLS = {
        new Control("X", CROSS, true, 1, 1, -130, -110, 70, 70),
        new Control("O", CIRCLE, true, 1, 1, -62, -176, 70, 70),
        new Control("□", SQUARE, true, 1, 1, -198, -176, 70, 70),
        new Control("△", TRI, true, 1, 1, -130, -242, 70, 70),
        new Control("L1", L1, false, 0, 0, 20, 64, 76, 44),
        new Control("L2", L2, false, 0, 0, 20, 14, 76, 44),
        new Control("R1", R1, false, 1, 0, -96, 64, 76, 44),
        new Control("R2", R2, false, 1, 0, -96, 14, 76, 44),
        new Control("SELECT", SELECT, false, 0.5f, 1, -92, -52, 84, 40),
        new Control("START", START, false, 0.5f, 1, 8, -52, 84, 40),
    };
    private static final Control PS2_MENU = new Control("≡", 0, false, 0.5f, 0, -30, 10, 60, 40);
    /** The stick is a control of the layout too (its size is its radius); its mask is 0. */
    private static final Control PS2_STICK = new Control("STICK", 0, true, 0, 1, 28, -184, 156, 156);
    private static final Control[] PS2_ALL = join(PS2_CONTROLS, PS2_STICK, PS2_MENU);

    // the fancy look (the default skin): the layout of the mock-up, the look of the Flipendo game's controls, symbols instead of words
    private static final Control[] FANCY_CONTROLS = {
        fancy("X", CROSS, true, 1, 1, 1110, 598, 200, 200, "round", "glyph_cross"),
        fancy("O", CIRCLE, true, 1, 1, 1267, 532, 116, 116, "round", "glyph_circle"),
        fancy("□", SQUARE, true, 1, 1, 953, 532, 116, 116, "round", "glyph_square"),
        fancy("△", TRI, true, 1, 1, 1112, 428, 110, 110, "round", "glyph_triangle"),
        fancy("L1", L1, true, 0, 0, 96, 205, 104, 104, "round", "glyph_l1"),
        fancy("L2", L2, true, 1, 0, 1280, 205, 104, 104, "round", "glyph_l2"),
        fancy("R1", R1, true, 1, 0, 945, 333, 144, 144, "round", "glyph_r1"),
        fancy("R2", R2, true, 1, 0, 1280, 323, 104, 104, "round", "glyph_r2"),
        fancy("SELECT", SELECT, false, 1, 0, 1077, 75, 180, 72, "pill_wide", "glyph_select"),
        fancy("START", START, false, 1, 0, 1258, 75, 150, 72, "pill_narrow", "glyph_start"),
    };
    private static final Control FANCY_MENU = fancy("≡", 0, false, 0.5f, 0, 688, 52, 117, 56, "pill_narrow", "glyph_menu");
    private static final Control FANCY_STICK = fancy("STICK", 0, true, 0, 1, 245, 525, 330, 330, "stick", null);
    private static final Control[] FANCY_ALL = join(FANCY_CONTROLS, FANCY_STICK, FANCY_MENU);

    private static Control[] join(Control[] buttons, Control stick, Control menu) {
        Control[] all = new Control[buttons.length + 2];
        System.arraycopy(buttons, 0, all, 0, buttons.length);
        all[buttons.length] = stick;
        all[buttons.length + 1] = menu;
        return all;
    }

    private final Listener listener;
    private final SharedPreferences prefs, skinPrefs;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), line = new Paint(Paint.ANTI_ALIAS_FLAG), text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float dp;
    private int buttons;
    private int lx = 128, ly = 128;
    private boolean dpadMode;
    private int opacity = 50;   // percent, 50 = the first look
    private int fm = 256;       // the fancy skin's alpha factor for that opacity (256 = as the Flipendo game draws its controls, at 50 %)
    private float stickCx, stickCy, stickR, knobX, knobY;   // the stick where it is now (at its normal place, the temporary one, or on the way)

    // "Stick moves right while R1 held" (Settings > Controls): the stick has a normal place (its layout) and a temporary one on the right,
    // smaller, so one thumb can hold R1 (the Target button, often at the top left) while the other steers. It moves only while idle.
    /** The temporary stick's size, as a fraction of the normal one. */
    private static final float STICK_AWAY_SCALE = 0.6f;
    /** Its centre: the fancy skin's in the 1376 x 768 reference units from the right and bottom edges (left of the face buttons' fan, low middle) ... */
    private static final float STICK_AWAY_FROM_RIGHT = 646f, STICK_AWAY_FROM_BOTTOM = 228f;
    /** ... and the PS2 skin's in dp. */
    private static final float STICK_AWAY_FROM_RIGHT_DP = 280f, STICK_AWAY_FROM_BOTTOM_DP = 100f;
    /** How long the stick takes to slide (milliseconds). */
    private static final float STICK_AWAY_MS = 130f;
    private float nCx, nCy, nR, tCx, tCy, tR;               // the normal and the temporary place
    private boolean stickAway, awayOn = true;               // the stick is (going) to the temporary place; the setting
    private float anim;                                     // 0 = normal place, 1 = temporary place
    private long animLast;
    private final RectF stickDraw = new RectF();
    private int stickPointer = -1;
    private final java.util.HashMap<Integer, Control> held = new java.util.HashMap<Integer, Control>();
    private int lastButtons = -1, lastLx = -1, lastLy = -1;

    // the skin in use
    private boolean fancy;
    private Control[] controls, all;
    private Control menu, stick, crossCtl;
    private Ps2Svg knobArt, dpadArt, fanArt;
    private final RectF knobBox = new RectF();

    // layout editing
    private boolean editing;
    private Control selected;
    private int editPointer = -1;
    private float editDx, editDy;
    private Runnable editDone;
    private final RectF leMinus = new RectF(), lePlus = new RectF(), leReset = new RectF(), leDone = new RectF(), lePanel = new RectF();

    public Ps2Pad(Context context, Listener listener) {
        super(context);
        this.listener = listener;
        dp = context.getResources().getDisplayMetrics().density;
        prefs = context.getSharedPreferences("ps2pad", Context.MODE_PRIVATE);
        skinPrefs = context.getSharedPreferences("ps2play", Context.MODE_PRIVATE);
        dpadMode = prefs.getBoolean("dpad", false);
        awayOn = skinPrefs.getBoolean("r1stick", true);
        opacity = prefs.getInt("opacity", 50);
        fm = fancyFactor(opacity);
        for (Control[] set : new Control[][] { PS2_ALL, FANCY_ALL }) {
            for (Control c : set) {
                c.cx = prefs.getInt("x_" + c.key, -1) / 10000f;
                c.cy = prefs.getInt("y_" + c.key, -1) / 10000f;
                c.size = prefs.getInt("s_" + c.key, 100) / 100f;
            }
        }
        for (Control c : FANCY_ALL) {
            c.art = Ps2Svg.get(context, c.artName);
            c.glyph = c.glyphName == null ? null : Ps2Svg.get(context, c.glyphName);
        }
        knobArt = Ps2Svg.get(context, "knob");
        dpadArt = Ps2Svg.get(context, "dpad");
        fanArt = Ps2Svg.get(context, "fan");
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeWidth(2 * dp);
        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
        // the fancy skin is the default; without its art (an APK built without assets/ps2pad) the console look is used
        useSkin(!"ps2".equals(skinPrefs.getString("skin", "fancy")) && Ps2Svg.get(context, "stick").ok);
        setFocusable(false);
    }

    private void useSkin(boolean fancySkin) {
        fancy = fancySkin;
        controls = fancy ? FANCY_CONTROLS : PS2_CONTROLS;
        menu = fancy ? FANCY_MENU : PS2_MENU;
        stick = fancy ? FANCY_STICK : PS2_STICK;
        all = fancy ? FANCY_ALL : PS2_ALL;
        crossCtl = null;
        for (Control c : controls) if (c.mask == CROSS) crossCtl = c;
    }

    /** Settings > Controls > Controls skin: true = fancy vector art, false = the console's PS2 buttons. */
    boolean fancySkin() { return fancy; }

    /** Switches the skin live: the controls stay where this skin has them, nothing stays pressed. */
    void setFancySkin(boolean on) {
        if (on && !Ps2Svg.get(getContext(), "stick").ok) on = false;
        skinPrefs.edit().putString("skin", on ? "fancy" : "ps2").apply();
        if (on == fancy) return;
        releaseAll();
        useSkin(on);
        if (getWidth() > 0) layoutAll(getWidth(), getHeight());
        invalidate();
    }

    boolean dpadMode() { return dpadMode; }

    /** Settings > Controls: the left thumb is a floating stick (false) or four arrow buttons (true). */
    void setDpadMode(boolean on) {
        dpadMode = on;
        prefs.edit().putBoolean("dpad", on).apply();
        releaseStick();
        invalidate();
    }

    int opacity() { return opacity; }

    /** The controls' opacity in percent (10 to 100). */
    void setOpacity(int percent) {
        opacity = Math.max(10, Math.min(100, percent));
        fm = fancyFactor(opacity);
        prefs.edit().putInt("opacity", opacity).apply();
        invalidate();
    }

    /** The fancy art is drawn with the Flipendo game's own alphas at the first look's 50 %; the setting scales them (up to double). */
    private static int fancyFactor(int percent) {
        return Math.max(10, Math.min(100, percent)) * 256 / 50;
    }

    /** Settings > Controls > Control layout: drag the controls where they should be; done() is called when the player is finished. */
    void startEdit(Runnable done) {
        editing = true;
        editDone = done;
        selected = stick;
        releaseStick();
        held.clear();
        buttons = 0;
        publish();
        // the layout is edited with the stick at its own place (the temporary one is not editable)
        stickAway = false;
        anim = 0f;
        applyStickGeom();
        invalidate();
    }

    boolean editing() { return editing; }

    /** Every finger lifted (the settings panel opens over the pad): nothing stays pressed. */
    void releaseAll() {
        held.clear();
        releaseStick();
        buttons = 0;
        publish();
        updateStickPlace();
        invalidate();
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        layoutAll(w, h);
    }

    /** The stick's current place and size: between the normal and the temporary one by the slide's progress (eased). */
    private void applyStickGeom() {
        final float e = anim * anim * (3f - 2f * anim);
        stickCx = nCx + (tCx - nCx) * e;
        stickCy = nCy + (tCy - nCy) * e;
        stickR = nR + (tR - nR) * e;
        stickDraw.set(stickCx - stickR, stickCy - stickR, stickCx + stickR, stickCy + stickR);
        if (stickPointer < 0) {
            knobX = stickCx;
            knobY = stickCy;
        }
    }

    /**
     * Decides where the stick should be: at the temporary place while R1 is held (and the setting is on), else at its normal one. It only moves
     * while no finger holds it: a finger that has the stick keeps it where it is (even after R1 is released, or pressed) until it lifts.
     */
    private void updateStickPlace() {
        final boolean want = awayOn && !editing && (buttons & R1) != 0;
        if (stickPointer < 0 && want != stickAway) {
            stickAway = want;
            animLast = android.os.SystemClock.uptimeMillis();
            invalidate();
        }
    }

    /** A new touch while the stick is sliding: it arrives at once, so the touch is tested against (and starts the stick at) the final place. */
    private void finishStickSlide() {
        final float target = stickAway ? 1f : 0f;
        if (anim != target) {
            anim = target;
            applyStickGeom();
        }
    }

    /** Advances the slide (onDraw). */
    private void stepStickSlide() {
        final float target = stickAway ? 1f : 0f;
        if (anim == target) return;
        final long now = android.os.SystemClock.uptimeMillis();
        final float step = Math.max(1L, now - animLast) / STICK_AWAY_MS;
        animLast = now;
        anim = anim < target ? Math.min(target, anim + step) : Math.max(target, anim - step);
        applyStickGeom();
        postInvalidateOnAnimation();
    }

    /** Settings > Controls: the stick moves to the right, smaller, while R1 is held. */
    boolean stickFollowsR1() { return awayOn; }

    void setStickFollowsR1(boolean on) {
        awayOn = on;
        skinPrefs.edit().putBoolean("r1stick", on).apply();
        updateStickPlace();
        invalidate();
    }

    private void layoutAll(int w, int h) {
        for (Control c : controls) place(c, w, h);
        place(menu, w, h);
        place(stick, w, h);
        nR = stick.box.width() / 2f;
        nCx = stick.box.centerX();
        nCy = stick.box.centerY();
        tR = nR * STICK_AWAY_SCALE;
        tCx = Math.max(tR, Math.min(w - tR, w - (fancy ? STICK_AWAY_FROM_RIGHT * Math.min(w / REF_W, h / REF_H) : STICK_AWAY_FROM_RIGHT_DP * dp)));
        tCy = Math.max(tR, Math.min(h - tR, h - (fancy ? STICK_AWAY_FROM_BOTTOM * Math.min(w / REF_W, h / REF_H) : STICK_AWAY_FROM_BOTTOM_DP * dp)));
        applyStickGeom();
        knobX = stickCx;
        knobY = stickCy;
        if (fancy) {
            for (Control c : controls) layoutGlyph(c);
            layoutGlyph(menu);
        }
        float u = Math.min(w, h);
        float bw = 0.12f * u, bh = 0.075f * u, y1 = h - 0.04f * u, gap = 0.02f * u, cx = w / 2f;
        lePanel.set(cx - 2.6f * bw, y1 - bh - 0.17f * u, cx + 2.6f * bw, y1 + 0.03f * u);
        leMinus.set(cx - 2.4f * bw, y1 - bh, cx - 1.4f * bw, y1);
        lePlus.set(cx - 1.3f * bw, y1 - bh, cx - 0.3f * bw, y1);
        leReset.set(cx - 0.2f * bw, y1 - bh, cx + 1.1f * bw, y1);
        leDone.set(cx + 1.2f * bw, y1 - bh, cx + 2.4f * bw, y1);
    }

    private void place(Control c, int w, int h) {
        // the fancy skin's sizes follow the screen (its reference picture is 1376 x 768), the console look's are in dp
        float k = c.mock ? Math.min(w / REF_W, h / REF_H) : dp;
        float cw = c.w * k * c.size, ch = c.h * k * c.size;
        float x, y;
        if (c.cx >= 0 && c.cy >= 0) {
            x = c.cx * w - cw / 2f;
            y = c.cy * h - ch / 2f;
        } else {
            // the default place, kept about its own centre when the size changes
            float x0 = c.ax * w + c.dx * k, y0 = c.ay * h + c.dy * k;
            x = x0 + (c.w * k - cw) / 2f;
            y = y0 + (c.h * k - ch) / 2f;
        }
        c.box.set(x, y, x + cw, y + ch);
    }

    private int alpha(int percentOfFull) {
        return Math.max(0, Math.min(255, percentOfFull * opacity / 50 * 255 / 100));
    }

    // ---- the fancy skin ------------------------------------------------------------------------------------------------

    /** Where a fancy control's symbol goes: Flipendo's icons span 1.25 radii on a round button; on a pill the symbol is 0.72 of its height. */
    private void layoutGlyph(Control c) {
        final RectF b = c.box;
        final float side = c.round ? 0.625f * Math.min(b.width(), b.height()) : 0.72f * b.height();
        c.glyphBox.set(b.centerX() - side / 2f, b.centerY() - side / 2f, b.centerX() + side / 2f, b.centerY() + side / 2f);
    }

    private void drawFancy(Canvas canvas) {
        // the plate behind the face buttons, while they are where the picture has them
        if (crossCtl != null) {
            boolean home = true;
            for (Control c : controls) if ((c.mask & (TRI | CIRCLE | CROSS | SQUARE)) != 0 && (c.cx >= 0 || c.size != 1f)) home = false;
            if (home) fanArt.draw(canvas, crossCtl.box, "base", fm, 0);
        }
        // the stick: the arrows pad's ring, and in it the gold knob (or, with arrow buttons, the arrows that light up)
        final Control s = stick;
        s.art.draw(canvas, stickDraw, "base", fm, 0);
        if (stickPointer >= 0) s.art.draw(canvas, stickDraw, "glow", 256, 0);
        if (dpadMode) {
            if (stickPointer >= 0) {
                if ((buttons & UP) != 0) dpadArt.draw(canvas, stickDraw, "up", Math.max(fm, 256), 0);
                if ((buttons & DOWN) != 0) dpadArt.draw(canvas, stickDraw, "down", Math.max(fm, 256), 0);
                if ((buttons & LEFT) != 0) dpadArt.draw(canvas, stickDraw, "left", Math.max(fm, 256), 0);
                if ((buttons & RIGHT) != 0) dpadArt.draw(canvas, stickDraw, "right", Math.max(fm, 256), 0);
            }
        } else {
            // the knob (0.4 of the ring) stays inside the ring: its centre moves 0.6 as far as the finger (the stick value still takes the whole ring)
            final float kr = stickR * 0.4f;
            final float kx = stickCx + (knobX - stickCx) * 0.6f, ky = stickCy + (knobY - stickCy) * 0.6f;
            knobBox.set(kx - kr, ky - kr, kx + kr, ky + kr);
            knobArt.draw(canvas, knobBox, "base", fm, 0);
        }
        drawFancyControl(canvas, menu, false);
        for (Control c : controls) drawFancyControl(canvas, c, (buttons & c.mask) != 0);
    }

    /** A disc (or pill) and its symbol as FlipendoActivity draws a button: pressed it is gold with a dark symbol and 0.94 of the size. */
    private void drawFancyControl(Canvas canvas, Control c, boolean pressed) {
        final RectF b = c.box;
        canvas.save();
        if (pressed) canvas.scale(0.94f, 0.94f, b.centerX(), b.centerY());
        c.art.draw(canvas, b, pressed ? "down" : "base", pressed ? Math.max(fm, 256) : fm, 0);
        if (c.glyph != null) c.glyph.draw(canvas, c.glyphBox, "base", Math.min(256, pressed ? Math.max(fm, 256) : fm), pressed ? DARK : WHITE);
        canvas.restore();
    }

    // ---- drawing ---------------------------------------------------------------------------------------------------------

    @Override
    protected void onDraw(Canvas canvas) {
        stepStickSlide();
        if (fancy) {
            drawFancy(canvas);
            if (editing) drawEdit(canvas);
            return;
        }
        fill.setStyle(Paint.Style.FILL);
        // the stick (or the arrow cross)
        fill.setColor((alpha(20) << 24) | 0xFFFFFF);
        canvas.drawCircle(stickCx, stickCy, stickR, fill);
        line.setColor((alpha(53) << 24) | 0xFFFFFF);
        canvas.drawCircle(stickCx, stickCy, stickR, line);
        if (dpadMode) {
            fill.setColor((alpha(33) << 24) | 0xFFFFFF);
            float a = stickR * 0.34f;
            canvas.drawRect(stickCx - a, stickCy - stickR * 0.9f, stickCx + a, stickCy + stickR * 0.9f, fill);
            canvas.drawRect(stickCx - stickR * 0.9f, stickCy - a, stickCx + stickR * 0.9f, stickCy + a, fill);
        } else {
            fill.setColor((alpha(47) << 24) | 0xFFFFFF);
            canvas.drawCircle(knobX, knobY, stickR * 0.42f, fill);
        }
        drawControl(canvas, menu, false);
        for (Control c : controls) drawControl(canvas, c, (buttons & c.mask) != 0);
        if (editing) drawEdit(canvas);
    }

    private void drawControl(Canvas canvas, Control c, boolean pressed) {
        fill.setColor(pressed ? ((alpha(60) << 24) | 0xFFFFFF) : ((alpha(25) << 24) | 0xFFFFFF));
        line.setColor((alpha(60) << 24) | 0xFFFFFF);
        if (c.round) {
            canvas.drawOval(c.box, fill);
            canvas.drawOval(c.box, line);
        } else {
            float r = 10 * dp * c.size;
            canvas.drawRoundRect(c.box, r, r, fill);
            canvas.drawRoundRect(c.box, r, r, line);
        }
        text.setColor(pressed ? 0xFF000000 : ((Math.max(60, alpha(87)) << 24) | 0xFFFFFF));
        text.setTextSize(Math.min(c.box.width(), c.box.height()) * (c.label.length() > 2 ? 0.34f : 0.5f));
        canvas.drawText(c.label, c.box.centerX(), c.box.centerY() + text.getTextSize() * 0.35f, text);
    }

    /** The layout editor: an outline round every control (gold on the chosen one) and the card with its size and buttons, as the PC games' one. */
    private void drawEdit(Canvas canvas) {
        float u = Math.min(getWidth(), getHeight());
        for (Control c : all) {
            RectF r = new RectF(c.box);
            r.inset(-0.012f * u, -0.012f * u);
            line.setColor(c == selected ? 0xFFE8C15A : 0x66FFFFFF);
            line.setStrokeWidth(c == selected ? 5f : 2f);
            canvas.drawRoundRect(r, 0.03f * u, 0.03f * u, line);
        }
        line.setStrokeWidth(2 * dp);
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(0xF2181722);
        canvas.drawRoundRect(lePanel, 0.03f * u, 0.03f * u, fill);
        line.setColor(0x33FFFFFF);
        line.setStrokeWidth(2f);
        canvas.drawRoundRect(lePanel, 0.03f * u, 0.03f * u, line);
        line.setStrokeWidth(2 * dp);
        text.setColor(0xFFFFFFFF);
        text.setFakeBoldText(false);
        text.setTextSize(0.034f * u);
        canvas.drawText(Tr.tr("Drag a control to move it"), lePanel.centerX(), lePanel.top + 0.055f * u, text);
        text.setColor(0x99FFFFFF);
        text.setTextSize(0.026f * u);
        String name = selected == stick ? Tr.tr("Stick") : selected == menu ? Tr.tr("Menu") : selected.label;
        canvas.drawText(name + Tr.tr("  ·  size ") + Math.round(selected.size * 100) + "%", lePanel.centerX(), lePanel.top + 0.1f * u, text);
        fill.setColor(0x33FFFFFF);
        for (RectF b : new RectF[] { leMinus, lePlus, leReset }) canvas.drawRoundRect(b, b.height() / 2f, b.height() / 2f, fill);
        fill.setColor(0xFFE8C15A);
        canvas.drawRoundRect(leDone, leDone.height() / 2f, leDone.height() / 2f, fill);
        text.setColor(0xFFFFFFFF);
        text.setTextSize(0.034f * u);
        float base = text.getTextSize() * 0.35f;
        canvas.drawText("–", leMinus.centerX(), leMinus.centerY() + base, text);
        canvas.drawText("+", lePlus.centerX(), lePlus.centerY() + base, text);
        canvas.drawText(Tr.tr("Reset"), leReset.centerX(), leReset.centerY() + base, text);
        text.setColor(0xFF1A1A1A);
        canvas.drawText(Tr.tr("Done"), leDone.centerX(), leDone.centerY() + base, text);
        text.setFakeBoldText(true);
    }

    private static boolean hit(Control c, float x, float y, float slop) {
        if (c.round) {
            float rx = c.box.width() / 2 + slop, ry = c.box.height() / 2 + slop;
            float nx = (x - c.box.centerX()) / rx, ny = (y - c.box.centerY()) / ry;
            return nx * nx + ny * ny <= 1;
        }
        return x >= c.box.left - slop && x <= c.box.right + slop && y >= c.box.top - slop && y <= c.box.bottom + slop;
    }

    private void saveControl(Control c) {
        SharedPreferences.Editor e = prefs.edit();
        e.putInt("x_" + c.key, Math.round(c.cx * 10000)).putInt("y_" + c.key, Math.round(c.cy * 10000)).putInt("s_" + c.key, Math.round(c.size * 100));
        e.apply();
    }

    /** A control moved or resized: its box becomes its place. */
    private void remember(Control c) {
        if (c.cx < 0) {
            c.cx = c.box.centerX() / getWidth();
            c.cy = c.box.centerY() / getHeight();
        }
        saveControl(c);
    }

    private void resize(Control c, float delta) {
        c.size = Math.max(0.6f, Math.min(1.8f, Math.round((c.size + delta) * 20f) / 20f));
        remember(c);
        layoutAll(getWidth(), getHeight());
        invalidate();
    }

    private boolean editTouch(MotionEvent e) {
        final int action = e.getActionMasked();
        final int index = e.getActionIndex();
        float x = e.getX(index), y = e.getY(index);
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            if (leDone.contains(x, y)) {
                editing = false;
                selected = null;
                invalidate();
                if (editDone != null) editDone.run();
            } else if (leMinus.contains(x, y)) {
                if (selected != null) resize(selected, -0.05f);
            } else if (lePlus.contains(x, y)) {
                if (selected != null) resize(selected, 0.05f);
            } else if (leReset.contains(x, y)) {
                SharedPreferences.Editor ed = prefs.edit();
                for (Control c : all) {
                    c.cx = c.cy = -1;
                    c.size = 1f;
                    ed.remove("x_" + c.key).remove("y_" + c.key).remove("s_" + c.key);
                }
                ed.apply();
                layoutAll(getWidth(), getHeight());
                invalidate();
            } else if (editPointer < 0) {
                Control found = null;
                for (Control c : all) if (hit(c, x, y, 8 * dp)) found = c;
                if (found != null) {
                    selected = found;
                    editPointer = e.getPointerId(index);
                    editDx = x - found.box.centerX();
                    editDy = y - found.box.centerY();
                    invalidate();
                }
            }
        } else if (action == MotionEvent.ACTION_MOVE && editPointer >= 0 && selected != null) {
            int i = e.findPointerIndex(editPointer);
            if (i >= 0) {
                float w = getWidth(), h = getHeight();
                selected.cx = Math.max(0.03f, Math.min(0.97f, (e.getX(i) - editDx) / w));
                selected.cy = Math.max(0.05f, Math.min(0.95f, (e.getY(i) - editDy) / h));
                layoutAll((int) w, (int) h);
                invalidate();
            }
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP || action == MotionEvent.ACTION_CANCEL) {
            if (editPointer == e.getPointerId(index) || action == MotionEvent.ACTION_CANCEL) {
                if (selected != null && selected.cx >= 0) saveControl(selected);
                editPointer = -1;
            }
        }
        return true;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (editing) return editTouch(e);
        final int action = e.getActionMasked();
        final int index = e.getActionIndex();
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            int id = e.getPointerId(index);
            float x = e.getX(index), y = e.getY(index);
            if (hit(menu, x, y, 6 * dp)) {
                if (listener != null) listener.onMenu();
                return true;
            }
            // a stick that is sliding arrives first (it moves only while no finger holds it); at the temporary place only the stick's own
            // circle takes a new touch (it wins over the buttons there), at the normal place a wider one, as ever
            if (stickPointer < 0) finishStickSlide();
            float dxs = x - stickCx, dys = y - stickCy;
            float reach = stickAway ? stickR + 4 * dp : stickR * 1.4832f;
            if (stickPointer < 0 && dxs * dxs + dys * dys <= reach * reach) {
                stickPointer = id;
            } else {
                for (Control c : controls) {
                    if (hit(c, x, y, 8 * dp)) {
                        held.put(id, c);
                        break;
                    }
                }
            }
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP || action == MotionEvent.ACTION_CANCEL) {
            int id = e.getPointerId(index);
            if (action == MotionEvent.ACTION_CANCEL) {
                held.clear();
                releaseStick();
            } else if (id == stickPointer) {
                releaseStick();
            } else {
                held.remove(id);
            }
        }
        // the sliding fingers: a finger that moves from one button to another presses the other
        if (stickPointer >= 0) {
            int i = e.findPointerIndex(stickPointer);
            if (i >= 0) moveStick(e.getX(i) - stickCx, e.getY(i) - stickCy);
        }
        for (java.util.Map.Entry<Integer, Control> entry : held.entrySet()) {
            int i = e.findPointerIndex(entry.getKey());
            if (i < 0) continue;
            float x = e.getX(i), y = e.getY(i);
            if (!hit(entry.getValue(), x, y, 14 * dp)) {
                for (Control c : controls) {
                    if (hit(c, x, y, 0)) {
                        entry.setValue(c);
                        break;
                    }
                }
            }
        }
        int mask = 0;
        for (Control c : held.values()) mask |= c.mask;
        if (dpadMode && stickPointer >= 0) mask |= dpadBits(knobX - stickCx, knobY - stickCy);
        buttons = mask;
        updateStickPlace();
        publish();
        invalidate();
        return true;
    }

    private void moveStick(float dx, float dy) {
        float len = (float) Math.sqrt(dx * dx + dy * dy);
        if (len > stickR) {
            dx *= stickR / len;
            dy *= stickR / len;
        }
        knobX = stickCx + dx;
        knobY = stickCy + dy;
        if (dpadMode) {
            lx = ly = 128;
        } else {
            lx = Math.max(0, Math.min(255, Math.round(128 + dx / stickR * 127)));
            ly = Math.max(0, Math.min(255, Math.round(128 + dy / stickR * 127)));
        }
    }

    private int dpadBits(float dx, float dy) {
        int m = 0;
        float t = stickR * 0.35f;
        if (dx > t) m |= RIGHT;
        if (dx < -t) m |= LEFT;
        if (dy > t) m |= DOWN;
        if (dy < -t) m |= UP;
        return m;
    }

    private void releaseStick() {
        stickPointer = -1;
        knobX = stickCx;
        knobY = stickCy;
        lx = ly = 128;
        buttons &= ~(UP | DOWN | LEFT | RIGHT);
        publish();
        invalidate();
    }

    private void publish() {
        if (buttons == lastButtons && lx == lastLx && ly == lastLy) return;
        lastButtons = buttons;
        lastLx = lx;
        lastLy = ly;
        try {
            nativeSet(buttons, lx, ly);
        } catch (UnsatisfiedLinkError ignored) {
            // the runner library is not loaded yet
        }
    }

    /** A real controller took over (or the player touched again): hide the pad until the next touch. */
    void setShown(boolean shown) {
        setVisibility(shown ? View.VISIBLE : View.INVISIBLE);
        if (!shown) {
            held.clear();
            releaseStick();
            buttons = 0;
            publish();
            updateStickPlace();
        }
    }
}
