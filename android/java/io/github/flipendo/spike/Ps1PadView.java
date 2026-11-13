package io.github.flipendo.spike;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

/**
 * The PS1 pad drawn over the game, its multi-touch handling and its layout editor. Where things are and what a touch presses
 * is Ps1PadLayout (tested); this view draws that and sends the combined bit mask of all fingers to the native host.
 * The look follows the PC games' controls: dark see-through discs with a light rim, bright while pressed; the shoulder
 * buttons, Start and Select are pills with their name inside, the face buttons carry the PlayStation symbols in their colours.
 * The editor (opened from the menu, {@link #startEditing}) moves and resizes the groups of controls; the layout is kept in
 * the preferences file "ps1pad" and is the same for both PS1 games.
 */
final class Ps1PadView extends View {
    interface Listener {
        void onMenu();
        /** The editor closed (Done). */
        void onEditDone();
    }

    private static final int DISC = 0x80181722, DISC_DOWN = 0xC0FFFFFF, RIM = 0x66FFFFFF, LABEL = 0xE6FFFFFF, LABEL_DOWN = 0xFF1A1A1A;
    private static final int GOLD = 0xFFE8C66A, CARD = 0xF0201E2A, MUTED = 0xFFB8B2A6;
    // the face buttons' symbols, softened for the overlay: triangle green, circle red, cross blue, square pink
    private static final int SYM_TRIANGLE = 0xFF6FD3A8, SYM_CIRCLE = 0xFFFF7A7A, SYM_CROSS = 0xFF8AB4FF, SYM_SQUARE = 0xFFF5A3D3;
    private static final String PREFS = "ps1pad";

    private final Listener listener;
    private Ps1PadLayout layout;
    private int pressed;
    private int sent = -1;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final RectF rect = new RectF();

    // the editor
    private boolean editing;
    private int selected = Ps1PadLayout.G_DPAD, dragPointer = -1;
    private float lastX, lastY;
    private final RectF panel = new RectF(), minus = new RectF(), plus = new RectF(), reset = new RectF(), done = new RectF();

    Ps1PadView(Context c, Listener l) {
        super(c);
        listener = l;
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeCap(Paint.Cap.ROUND);
        line.setStrokeJoin(Paint.Join.ROUND);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        setClickable(false);
    }

    static String tr(String en) {
        return Tr.tr(en);
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        layout = new Ps1PadLayout(w, h);
        load();
        float pw = Math.min(0.6f * w, 1.3f * h), ph = 0.3f * h, top = insetTop() + 0.03f * h;
        panel.set((w - pw) / 2f, top, (w + pw) / 2f, top + ph);
        float bh = 0.085f * h, bw = 0.12f * pw, gap = 0.02f * pw, y0 = panel.bottom - 0.03f * h - bh;
        minus.set(panel.left + 0.03f * pw, y0, panel.left + 0.03f * pw + bw, y0 + bh);
        plus.set(minus.right + gap, y0, minus.right + gap + bw, y0 + bh);
        done.set(panel.right - 0.03f * pw - 0.24f * pw, y0, panel.right - 0.03f * pw, y0 + bh);
        reset.set(done.left - gap - 0.24f * pw, y0, done.left - gap, y0 + bh);
    }

    // ---- the saved layout ---------------------------------------------------------------------------------------

    private void load() {
        SharedPreferences p = getContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        for (int g = 0; g < Ps1PadLayout.GROUPS; g++) {
            layout.setGroup(g, p.getFloat("g" + g + "_x", 0f), p.getFloat("g" + g + "_y", 0f), p.getFloat("g" + g + "_s", 1f));
            keepInside(g);
        }
    }

    private void save() {
        SharedPreferences.Editor e = getContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit();
        for (int g = 0; g < Ps1PadLayout.GROUPS; g++) {
            e.putFloat("g" + g + "_x", layout.groupDx(g)).putFloat("g" + g + "_y", layout.groupDy(g)).putFloat("g" + g + "_s", layout.groupScale(g));
        }
        e.apply();
    }

    // the screen less its cut-outs (a notch or rounded corners must not hide a control)
    private float insetLeft() { return inset(0); }
    private float insetTop() { return inset(1); }
    private float insetRight() { return inset(2); }
    private float insetBottom() { return inset(3); }

    private float inset(int side) {
        android.view.WindowInsets wi = getRootWindowInsets();
        if (wi == null || android.os.Build.VERSION.SDK_INT < 28 || wi.getDisplayCutout() == null) return 0f;
        android.view.DisplayCutout c = wi.getDisplayCutout();
        switch (side) {
            case 0: return c.getSafeInsetLeft();
            case 1: return c.getSafeInsetTop();
            case 2: return c.getSafeInsetRight();
            default: return c.getSafeInsetBottom();
        }
    }

    private void keepInside(int g) {
        layout.keepInside(g, insetLeft(), insetTop(), getWidth() - insetRight(), getHeight() - insetBottom());
    }

    // ---- drawing --------------------------------------------------------------------------------------------------

    /** A real gamepad is in use: the touch controls are hidden until the next touch (not while the layout editor is open). */
    private boolean padActive;

    /** Called from the UI thread by Ps1Activity when a button or stick of a physical controller is used. */
    void onRealPadInput() {
        if (padActive || editing) return;
        padActive = true;
        if (sent > 0) release(); // a finger left on a control must not stay pressed under the controller's input
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (layout == null) return;
        if (padActive && !editing) return;
        float h = getHeight();
        line.setStrokeWidth(Math.max(2f, h * 0.004f));
        drawDpad(canvas);
        for (int id : layout.buttons()) {
            boolean down = (pressed & (1 << id)) != 0;
            if (Ps1PadLayout.isPill(id)) drawPill(canvas, id, down);
            else drawFace(canvas, id, down);
        }
        drawMenuButton(canvas);
        if (editing) drawEditor(canvas);
    }

    private void drawDpad(Canvas canvas) {
        float r = layout.dpadRadius(), cx = layout.dpadX(), cy = layout.dpadY(), arm = r * 0.36f;
        // four arms that meet at the edges of a small centre square (they must not overlap each other)
        drawArm(canvas, cx - arm, cy - r, cx + arm, cy - arm, Ps1PadLayout.UP, 0, -1);
        drawArm(canvas, cx - arm, cy + arm, cx + arm, cy + r, Ps1PadLayout.DOWN, 0, 1);
        drawArm(canvas, cx - r, cy - arm, cx - arm, cy + arm, Ps1PadLayout.LEFT, -1, 0);
        drawArm(canvas, cx + arm, cy - arm, cx + r, cy + arm, Ps1PadLayout.RIGHT, 1, 0);
        fill.setColor(DISC);
        canvas.drawRect(cx - arm, cy - arm, cx + arm, cy + arm, fill);
    }

    /** One arm of the D-pad with a small arrow pointing out (dx, dy). */
    private void drawArm(Canvas canvas, float l, float t, float r, float b, int id, int dx, int dy) {
        boolean down = (pressed & (1 << id)) != 0;
        rect.set(l, t, r, b);
        float round = Math.min(rect.width(), rect.height()) * 0.25f;
        fill.setColor(down ? DISC_DOWN : DISC);
        canvas.drawRoundRect(rect, round, round, fill);
        line.setColor(RIM);
        canvas.drawRoundRect(rect, round, round, line);
        float ax = rect.centerX() + dx * rect.width() * 0.12f, ay = rect.centerY() + dy * rect.height() * 0.12f;
        float s = Math.min(rect.width(), rect.height()) * 0.22f;
        path.reset();
        path.moveTo(ax + dx * s, ay + dy * s);
        path.lineTo(ax - dx * s * 0.6f + dy * s, ay - dy * s * 0.6f + dx * s);
        path.lineTo(ax - dx * s * 0.6f - dy * s, ay - dy * s * 0.6f - dx * s);
        path.close();
        fill.setColor(down ? LABEL_DOWN : LABEL);
        canvas.drawPath(path, fill);
    }

    private void drawPill(Canvas canvas, int id, boolean down) {
        float cx = layout.centerX(id), cy = layout.centerY(id), hw = layout.halfWidth(id), hh = layout.halfHeight(id);
        rect.set(cx - hw, cy - hh, cx + hw, cy + hh);
        fill.setColor(down ? DISC_DOWN : DISC);
        canvas.drawRoundRect(rect, hh, hh, fill);
        line.setColor(RIM);
        canvas.drawRoundRect(rect, hh, hh, line);
        text.setColor(down ? LABEL_DOWN : LABEL);
        String label = Ps1PadLayout.label(id);
        text.setTextSize(hh * (label.length() > 2 ? 0.8f : 1.05f));
        canvas.drawText(label, cx, cy + text.getTextSize() * 0.36f, text);
    }

    /** A face button: the disc and its symbol drawn as lines (fonts draw ✕○□△ at odd sizes and off centre). */
    private void drawFace(Canvas canvas, int id, boolean down) {
        float cx = layout.centerX(id), cy = layout.centerY(id), r = layout.buttonRadius(id);
        fill.setColor(down ? DISC_DOWN : DISC);
        canvas.drawCircle(cx, cy, r, fill);
        line.setColor(RIM);
        canvas.drawCircle(cx, cy, r, line);
        float s = r * 0.42f, stroke = line.getStrokeWidth();
        line.setStrokeWidth(Math.max(3f, r * 0.11f));
        switch (id) {
            case Ps1PadLayout.X: // triangle
                line.setColor(down ? LABEL_DOWN : SYM_TRIANGLE);
                path.reset();
                path.moveTo(cx, cy - s);
                path.lineTo(cx + s * 0.95f, cy + s * 0.65f);
                path.lineTo(cx - s * 0.95f, cy + s * 0.65f);
                path.close();
                canvas.drawPath(path, line);
                break;
            case Ps1PadLayout.A: // circle
                line.setColor(down ? LABEL_DOWN : SYM_CIRCLE);
                canvas.drawCircle(cx, cy, s * 0.85f, line);
                break;
            case Ps1PadLayout.B: // cross
                line.setColor(down ? LABEL_DOWN : SYM_CROSS);
                float c = s * 0.75f;
                canvas.drawLine(cx - c, cy - c, cx + c, cy + c, line);
                canvas.drawLine(cx - c, cy + c, cx + c, cy - c, line);
                break;
            default: // square
                line.setColor(down ? LABEL_DOWN : SYM_SQUARE);
                float q = s * 0.72f;
                canvas.drawRect(cx - q, cy - q, cx + q, cy + q, line);
                break;
        }
        line.setStrokeWidth(stroke);
    }

    private void drawMenuButton(Canvas canvas) {
        float mx = layout.menuX(), my = layout.menuY(), mr = layout.menuRadius();
        fill.setColor(DISC);
        canvas.drawCircle(mx, my, mr, fill);
        line.setColor(RIM);
        canvas.drawCircle(mx, my, mr, line);
        // three short bars
        float bw = mr * 0.5f, gap = mr * 0.3f, stroke = line.getStrokeWidth();
        line.setStrokeWidth(Math.max(2f, mr * 0.13f));
        line.setColor(LABEL);
        for (int i = -1; i <= 1; i++) canvas.drawLine(mx - bw, my + i * gap, mx + bw, my + i * gap, line);
        line.setStrokeWidth(stroke);
    }

    // ---- the editor -----------------------------------------------------------------------------------------------

    private static String groupName(int g) {
        switch (g) {
            case Ps1PadLayout.G_DPAD: return tr("Arrow pad");
            case Ps1PadLayout.G_FACE: return tr("Face buttons");
            case Ps1PadLayout.G_LEFT: return "L1 · L2";
            case Ps1PadLayout.G_RIGHT: return "R1 · R2";
            case Ps1PadLayout.G_MIDDLE: return "SELECT · START";
            default: return tr("Menu button");
        }
    }

    boolean isEditing() {
        return editing;
    }

    /** Shows every group outlined; a drag moves one, the panel resizes it, resets all or closes the editor. */
    void startEditing() {
        editing = true;
        selected = Ps1PadLayout.G_DPAD;
        dragPointer = -1;
        release();
        invalidate();
    }

    private void stopEditing() {
        editing = false;
        dragPointer = -1;
        save();
        invalidate();
        listener.onEditDone();
    }

    private void drawEditor(Canvas canvas) {
        float u = Math.min(getWidth(), getHeight());
        for (int g = 0; g < Ps1PadLayout.GROUPS; g++) {
            float[] b = layout.groupBounds(g);
            rect.set(b[0] - 0.012f * u, b[1] - 0.012f * u, b[2] + 0.012f * u, b[3] + 0.012f * u);
            line.setColor(g == selected ? GOLD : 0x66FFFFFF);
            line.setStrokeWidth(g == selected ? 5f : 2f);
            canvas.drawRoundRect(rect, 0.03f * u, 0.03f * u, line);
        }
        line.setStrokeWidth(Math.max(2f, getHeight() * 0.004f));
        fill.setColor(CARD);
        canvas.drawRoundRect(panel, 0.03f * u, 0.03f * u, fill);
        line.setColor(0x33FFFFFF);
        canvas.drawRoundRect(panel, 0.03f * u, 0.03f * u, line);
        float h = getHeight();
        text.setColor(0xFFFFFFFF);
        text.setTextSize(0.045f * h);
        canvas.drawText(tr("Drag a control to move it"), panel.centerX(), panel.top + 0.075f * h, text);
        text.setColor(MUTED);
        text.setTextSize(0.036f * h);
        canvas.drawText(groupName(selected) + tr("  ·  size ") + Math.round(layout.groupScale(selected) * 100) + "%", panel.centerX(), panel.top + 0.135f * h, text);
        text.setTextSize(0.042f * h);
        drawPanelButton(canvas, minus, "–", false);
        drawPanelButton(canvas, plus, "+", false);
        drawPanelButton(canvas, reset, tr("Reset"), false);
        drawPanelButton(canvas, done, tr("Done"), true);
    }

    private void drawPanelButton(Canvas canvas, RectF r, String label, boolean primary) {
        fill.setColor(primary ? GOLD : 0x33FFFFFF);
        canvas.drawRoundRect(r, r.height() / 2f, r.height() / 2f, fill);
        text.setColor(primary ? LABEL_DOWN : 0xFFFFFFFF);
        canvas.drawText(label, r.centerX(), r.centerY() + text.getTextSize() * 0.35f, text);
    }

    private void editTouch(MotionEvent e) {
        int action = e.getActionMasked(), idx = e.getActionIndex(), id = e.getPointerId(idx);
        float x = e.getX(idx), y = e.getY(idx);
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                if (done.contains(x, y)) {
                    stopEditing();
                    return;
                } else if (reset.contains(x, y)) {
                    layout.resetGroups();
                    save();
                } else if (minus.contains(x, y) || plus.contains(x, y)) {
                    layout.stepScale(selected, plus.contains(x, y) ? 1 : -1);
                    keepInside(selected);
                    save();
                } else if (!panel.contains(x, y) && dragPointer == -1) {
                    int g = layout.groupAt(x, y);
                    if (g >= 0) {
                        selected = g;
                        dragPointer = id;
                        lastX = x;
                        lastY = y;
                    }
                }
                break;
            case MotionEvent.ACTION_MOVE:
                if (dragPointer != -1) {
                    int i = e.findPointerIndex(dragPointer);
                    if (i >= 0) {
                        layout.moveGroup(selected, e.getX(i) - lastX, e.getY(i) - lastY);
                        lastX = e.getX(i);
                        lastY = e.getY(i);
                        keepInside(selected);
                    }
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL:
                if (id == dragPointer || action == MotionEvent.ACTION_CANCEL) {
                    dragPointer = -1;
                    save();
                }
                break;
            default:
                break;
        }
        invalidate();
    }

    // ---- playing --------------------------------------------------------------------------------------------------

    /** Lets go of every button (the editor or the menu took the fingers). */
    void release() {
        pressed = 0;
        if (sent != 0) {
            sent = 0;
            Ps1Activity.nativeSetPad(0);
        }
        invalidate();
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (layout == null) return false;
        if (editing) {
            editTouch(e);
            return true;
        }
        int action = e.getActionMasked();
        if (padActive && action == MotionEvent.ACTION_DOWN) {
            // a touch brings the controls back and acts as usual
            padActive = false;
            invalidate();
        }
        if (padActive) return false; // a stray pointer while hidden does nothing
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            int i = e.getActionIndex();
            if (layout.isMenu(e.getX(i), e.getY(i))) {
                release();
                listener.onMenu();
                return true;
            }
        }
        int skip = (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) ? e.getActionIndex() : -1;
        int mask = 0;
        if (action != MotionEvent.ACTION_CANCEL) {
            for (int i = 0; i < e.getPointerCount(); i++) {
                if (i != skip) mask |= layout.maskAt(e.getX(i), e.getY(i));
            }
        }
        pressed = mask;
        if (mask != sent) {
            sent = mask;
            Ps1Activity.nativeSetPad(mask);
            invalidate();
        }
        return true;
    }
}
