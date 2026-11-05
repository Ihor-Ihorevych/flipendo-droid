package io.github.flipendo.spike;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

/**
 * The PS1 pad drawn over the game and its multi-touch handling. Where things are and what a touch presses is
 * Ps1PadLayout (tested); this view draws that and sends the combined bit mask of all fingers to the native host.
 */
final class Ps1PadView extends View {
    interface Listener {
        void onMenu();
    }

    private final Listener listener;
    private Ps1PadLayout layout;
    private int pressed;
    private int sent = -1;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);

    Ps1PadView(Context c, Listener l) {
        super(c);
        listener = l;
        line.setStyle(Paint.Style.STROKE);
        line.setColor(0xB0FFFFFF);
        text.setColor(0xE0FFFFFF);
        text.setTextAlign(Paint.Align.CENTER);
        setClickable(false);
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        layout = new Ps1PadLayout(w, h);
        line.setStrokeWidth(Math.max(2f, h * 0.004f));
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (layout == null) return;
        float r = layout.dpadRadius();
        float cx = layout.dpadX(), cy = layout.dpadY();
        float arm = r * 0.36f;
        // four arms that meet at the edges of a small centre square (they must not overlap each other)
        drawArm(canvas, new RectF(cx - arm, cy - r, cx + arm, cy - arm), Ps1PadLayout.UP);
        drawArm(canvas, new RectF(cx - arm, cy + arm, cx + arm, cy + r), Ps1PadLayout.DOWN);
        drawArm(canvas, new RectF(cx - r, cy - arm, cx - arm, cy + arm), Ps1PadLayout.LEFT);
        drawArm(canvas, new RectF(cx + arm, cy - arm, cx + r, cy + arm), Ps1PadLayout.RIGHT);

        for (int id : layout.buttons()) {
            float x = layout.centerX(id), y = layout.centerY(id), br = layout.buttonRadius(id);
            boolean down = (pressed & (1 << id)) != 0;
            fill.setColor(down ? 0x90FFFFFF : 0x40000000);
            canvas.drawCircle(x, y, br, fill);
            canvas.drawCircle(x, y, br, line);
            String label = Ps1PadLayout.label(id);
            if (id == Ps1PadLayout.START || id == Ps1PadLayout.SELECT) {
                text.setTextSize(br * 0.8f);
                canvas.drawText(label, x, y - br * 1.25f, text); // above the button: below it the label ran off the screen
            } else {
                text.setTextSize(br * 0.9f);
                canvas.drawText(label, x, y + br * 0.32f, text);
            }
        }
        // menu button
        float mx = layout.menuX(), my = layout.menuY(), mr = layout.menuRadius();
        fill.setColor(0x40000000);
        canvas.drawCircle(mx, my, mr, fill);
        canvas.drawCircle(mx, my, mr, line);
        text.setTextSize(mr * 1.2f);
        canvas.drawText("≡", mx, my + mr * 0.42f, text);
    }

    private void drawArm(Canvas canvas, RectF rect, int id) {
        fill.setColor((pressed & (1 << id)) != 0 ? 0x90FFFFFF : 0x40000000);
        float round = rect.width() * 0.2f;
        canvas.drawRoundRect(rect, round, round, fill);
        canvas.drawRoundRect(rect, round, round, line);
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (layout == null) return false;
        int action = e.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            int i = e.getActionIndex();
            if (layout.isMenu(e.getX(i), e.getY(i))) {
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
