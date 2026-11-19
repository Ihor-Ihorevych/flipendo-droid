package io.github.flipendo.spike;

import android.content.Context;
import android.content.Intent;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.net.Uri;
import android.view.MotionEvent;
import android.view.View;

/**
 * The settings panel of the PS2 game: the same card as the PC games' (FlipendoActivity's Settings: gold header with Done, a help and the
 * channel icon, the tabs Display / Controls / Game, two columns of rows and sliders, the version at the bottom), with the settings that
 * mean something for a recompiled PS2 game. Left out, because the console game cannot do them: Subtitles and HUD size and Field of view
 * (the HUD and the camera are drawn by the game's own code), First person, FPS limit (the game's logic runs per frame, a lower limit would
 * only slow the game down), Invert wand, Auto jump, No cutscene skip and No quick save (the game has its own), the camera and wand
 * sensitivity (no right stick), Language (the disc's), Graphics fix, Debug menu and the switch to another game. Added, because the PS2
 * picture needs them: Picture (4:3, stretched or zoomed), Button opacity and Controls skin (the fancy vector controls, or the console's PS2 buttons).
 */
public final class Ps2Settings extends View {
    static final int GOLD = 0xFFE8C15A, CARD = 0xF2181722, ROW = 0x14FFFFFF, MUTED = 0x99FFFFFF;

    static String tr(String en) {
        return Tr.tr(en);
    }

    private final Ps2Activity activity;
    private final Ps2Pad pad;
    private boolean open, helpOpen;
    private int tab;
    private float scrollY, maxScroll, contentH;

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint titlePaint = new Paint(Paint.ANTI_ALIAS_FLAG), sectionPaint = new Paint(Paint.ANTI_ALIAS_FLAG),
            rowPaint = new Paint(Paint.ANTI_ALIAS_FLAG), subPaint = new Paint(Paint.ANTI_ALIAS_FLAG), valuePaint = new Paint(Paint.ANTI_ALIAS_FLAG);

    // geometry (onSizeChanged)
    private float lh, lu, headerY, dividerY, colL0, colL1, colR0, colR1, secLeftY, secRightY, sliderX0, sliderX1, sliderY, sliderLabelY;
    private final RectF panel = new RectF(), closeBtn = new RectF(), helpBtn = new RectF(), telegramBtn = new RectF(), versionRect = new RectF(),
            viewport = new RectF();
    private static final int TABS = 3;
    private final RectF[] tabRects = new RectF[TABS];
    private final RectF segStick = new RectF(), segDpad = new RectF(), segAll = new RectF(), layoutBtn = new RectF(), skinBtn = new RectF(), r1StickBtn = new RectF(),opacityBtn = new RectF(),
            pictureBtn = new RectF(), wideBtn = new RectF(), fpsBtn = new RectF(), rendererBtn = new RectF(), perfBtn = new RectF(), quitBtn = new RectF();
    private String leftSection = "", rightSection = "";
    private final String version;

    // what the finger is doing
    private enum Role { NONE, SLIDER, PANEL }
    private Role role = Role.NONE;
    private int pointer = -1;
    private float touchY, scroll0, tapX, tapY;
    private boolean scrolling;
    private final float slop;
    private boolean rendererAsk, quitAsk, wideAsk;
    private int pendingScale;

    public Ps2Settings(Context context, Ps2Activity activity, Ps2Pad pad) {
        super(context);
        this.activity = activity;
        this.pad = pad;
        slop = 8 * context.getResources().getDisplayMetrics().density;
        version = LogReport.version(context);
        for (int i = 0; i < TABS; i++) tabRects[i] = new RectF();
        fill.setStyle(Paint.Style.FILL);
        line.setStyle(Paint.Style.STROKE);
        titlePaint.setColor(0xFFFFFFFF);
        titlePaint.setFakeBoldText(true);
        sectionPaint.setColor(GOLD);
        sectionPaint.setFakeBoldText(true);
        sectionPaint.setLetterSpacing(0.12f);
        rowPaint.setColor(0xFFFFFFFF);
        subPaint.setColor(MUTED);
        valuePaint.setColor(GOLD);
        setVisibility(View.GONE);
        setFocusable(false);
    }

    boolean isOpen() { return open; }

    void show() {
        open = true;
        helpOpen = false;
        rendererAsk = quitAsk = wideAsk = false;
        scrollY = 0;
        setVisibility(View.VISIBLE);
        layoutPage();
        invalidate();
    }

    void hide() {
        open = false;
        setVisibility(View.GONE);
        activity.settingsClosed();
    }

    // ---- geometry ----------------------------------------------------------------------------------------------------

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        float u = Math.min(w, h);
        // laid out for a wide phone (about 2.1:1): on a squarer screen the panel follows its width
        lh = Math.min(h, 0.94f * w / 2.1f);
        lu = Math.min(u, lh);
        float pw = Math.min(0.94f * w, 2.1f * lh), ph = Math.min(0.9f * lh, h);
        float pcx = w / 2f, pcy = h / 2f;
        panel.set(pcx - pw / 2f, pcy - ph / 2f, pcx + pw / 2f, pcy + ph / 2f);
        float pad = 0.035f * pw;
        headerY = panel.top + 0.1f * lh;
        dividerY = panel.top + 0.145f * lh;
        colL0 = panel.left + pad;
        colL1 = panel.centerX() - pad / 2f;
        colR0 = panel.centerX() + pad / 2f;
        colR1 = panel.right - pad;
        float doneH = 0.09f * lh, doneW = 0.17f * pw, top = panel.top + 0.025f * lh;
        closeBtn.set(colR1 - doneW, top, colR1, top + doneH);
        helpBtn.set(closeBtn.left - 0.018f * pw - doneH, top, closeBtn.left - 0.018f * pw, top + doneH);
        telegramBtn.set(helpBtn.left - 0.012f * pw - doneH, top, helpBtn.left - 0.012f * pw, top + doneH);
        float tabH = 0.085f * lh, tabW = 0.2f * pw, tabY = dividerY + 0.02f * lh;
        for (int i = 0; i < TABS; i++) tabRects[i].set(colL0 + i * (tabW + 0.012f * pw), tabY, colL0 + i * (tabW + 0.012f * pw) + tabW, tabY + tabH);
        float footerH = 0.07f * lh;
        versionRect.set(colL0, panel.bottom - 0.015f * lh - footerH, colR1, panel.bottom - 0.015f * lh);
        float pageTop = tabY + tabH + 0.02f * lh;
        viewport.set(panel.left + 0.012f * pw, pageTop, panel.right - 0.012f * pw, versionRect.top - 0.005f * lh);
        setPaintSizes(lu);
        layoutPage();
    }

    private void setPaintSizes(float unit) {
        titlePaint.setTextSize(0.058f * unit);
        sectionPaint.setTextSize(0.03f * unit);
        rowPaint.setTextSize(0.044f * unit);
        subPaint.setTextSize(0.031f * unit);
        valuePaint.setTextSize(0.041f * unit);
    }

    private void layoutPage() {
        if (getWidth() == 0) return;
        float bottom = layoutRows(viewport.top - scrollY);
        contentH = bottom - (viewport.top - scrollY);
        maxScroll = Math.max(0f, contentH - viewport.height());
        if (scrollY > maxScroll || scrollY < 0f) {
            scrollY = Math.max(0f, Math.min(maxScroll, scrollY));
            layoutRows(viewport.top - scrollY);
        }
    }

    private float layoutRows(float y0) {
        for (RectF r : new RectF[] { segStick, segDpad, layoutBtn, skinBtn, r1StickBtn, opacityBtn, pictureBtn, wideBtn, fpsBtn, rendererBtn, perfBtn, quitBtn }) r.setEmpty();
        float rowH = 0.115f * lh, rowGap = 0.02f * lh;
        float knobR = 0.026f * lu;
        sliderX0 = colL0 + knobR;
        sliderX1 = colL1 - knobR;
        y0 += 0.01f * lh;
        secLeftY = y0 + 0.045f * lh;
        secRightY = secLeftY;
        float yl = y0 + 0.08f * lh, yr = yl;
        if (tab == 0) {
            leftSection = tr("DISPLAY");
            rightSection = tr("VIEW");
            float slot = 0.14f * lh;
            sliderLabelY = yl + 0.06f * lh;
            sliderY = sliderLabelY + 0.052f * lh;
            yl += slot;
            wideBtn.set(colL0, yl, colL1, yl + rowH);
            yl += rowH + rowGap;
            pictureBtn.set(colR0, yr, colR1, yr + rowH);
            yr += rowH + rowGap;
            fpsBtn.set(colR0, yr, colR1, yr + rowH);
            yr += rowH + rowGap;
        } else if (tab == 1) {
            leftSection = tr("CONTROLS");
            rightSection = tr("LOOK");
            segStick.set(colL0, yl, (colL0 + colL1) / 2f, yl + rowH);
            segDpad.set((colL0 + colL1) / 2f, yl, colL1, yl + rowH);
            yl += rowH + rowGap;
            layoutBtn.set(colL0, yl, colL1, yl + rowH);
            yl += rowH + rowGap;
            skinBtn.set(colR0, yr, colR1, yr + rowH);
            yr += rowH + rowGap;
            r1StickBtn.set(colR0, yr, colR1, yr + rowH);
            yr += rowH + rowGap;
            opacityBtn.set(colR0, yr, colR1, yr + rowH);
            yr += rowH + rowGap;
        } else {
            leftSection = tr("GAME");
            rightSection = tr("MORE");
            rendererBtn.set(colL0, yl, colL1, yl + rowH);
            yl += rowH + rowGap;
            perfBtn.set(colR0, yr, colR1, yr + rowH);
            yr += rowH + rowGap;
            quitBtn.set(colR0, yr, colR1, yr + rowH);
            yr += rowH + rowGap;
        }
        return Math.max(yl, yr);
    }

    private void setTab(int t) {
        tab = t;
        scrollY = 0;
        rendererAsk = quitAsk = wideAsk = false;
        layoutPage();
        invalidate();
    }

    // ---- drawing -----------------------------------------------------------------------------------------------------

    @Override
    protected void onDraw(Canvas c) {
        if (!open) return;
        if (helpOpen) {
            drawHelp(c);
            return;
        }
        float u = lu;
        setPaintSizes(lu);
        fill.setColor(0xB8000000);
        c.drawRect(0, 0, getWidth(), getHeight(), fill);
        float r = 0.035f * u;
        fill.setColor(CARD);
        c.drawRoundRect(panel, r, r, fill);
        line.setColor(0x33FFFFFF);
        line.setStrokeWidth(2f);
        c.drawRoundRect(panel, r, r, line);

        // header: a gold accent and the title on the left; the channel and the help as icons and a large Done on the right
        fill.setColor(GOLD);
        c.drawRoundRect(colL0, headerY - 0.05f * lh, colL0 + 0.008f * u, headerY + 0.008f * lh, 4f, 4f, fill);
        titlePaint.setTextAlign(Paint.Align.LEFT);
        c.drawText(tr("Settings"), colL0 + 0.025f * u, headerY, titlePaint);
        drawIconButton(c, telegramBtn, true);
        drawIconButton(c, helpBtn, false);
        fill.setColor(GOLD);
        c.drawRoundRect(closeBtn, closeBtn.height() / 2f, closeBtn.height() / 2f, fill);
        rowPaint.setTextAlign(Paint.Align.CENTER);
        rowPaint.setColor(0xFF1A1A1A);
        rowPaint.setFakeBoldText(true);
        c.drawText(tr("Done"), closeBtn.centerX(), closeBtn.centerY() + rowPaint.getTextSize() * 0.35f, rowPaint);
        rowPaint.setFakeBoldText(false);
        rowPaint.setColor(0xFFFFFFFF);
        rowPaint.setTextAlign(Paint.Align.LEFT);
        line.setColor(0x22FFFFFF);
        line.setStrokeWidth(2f);
        c.drawLine(colL0, dividerY, colR1, dividerY, line);

        String[] names = { tr("Display"), tr("Controls"), tr("Game") };
        for (int i = 0; i < TABS; i++) {
            RectF t = tabRects[i];
            boolean on = i == tab;
            fill.setColor(on ? GOLD : 0x22FFFFFF);
            c.drawRoundRect(t, t.height() / 2f, t.height() / 2f, fill);
            rowPaint.setTextAlign(Paint.Align.CENTER);
            rowPaint.setColor(on ? 0xFF1A1A1A : 0xFFFFFFFF);
            c.drawText(names[i], t.centerX(), t.centerY() + rowPaint.getTextSize() * 0.35f, rowPaint);
            rowPaint.setColor(0xFFFFFFFF);
            rowPaint.setTextAlign(Paint.Align.LEFT);
        }
        line.setColor(0x22FFFFFF);
        line.setStrokeWidth(2f);
        c.drawLine(colL0, versionRect.top, colR1, versionRect.top, line);
        subPaint.setTextAlign(Paint.Align.CENTER);
        c.drawText(fit(tr("Flipendo for Android ") + version, subPaint, versionRect.width()), versionRect.centerX(),
                versionRect.centerY() + subPaint.getTextSize() * 0.35f, subPaint);
        subPaint.setTextAlign(Paint.Align.LEFT);

        // the page: it scrolls inside the viewport
        c.save();
        c.clipRect(viewport);
        line.setColor(0x22FFFFFF);
        line.setStrokeWidth(2f);
        c.drawLine(panel.centerX(), viewport.top, panel.centerX(), viewport.bottom, line);
        sectionPaint.setTextAlign(Paint.Align.LEFT);
        c.drawText(leftSection, colL0, secLeftY, sectionPaint);
        c.drawText(rightSection, colR0, secRightY, sectionPaint);
        if (tab == 0) {
            drawSlider(c);
            drawValueRow(c, wideBtn, tr("Game view"), wideAsk ? tr("Tap again: restart with ") + (activity.widescreen() ? tr("Original 4:3") : tr("Widescreen"))
                    : tr("Widescreen shows more of the world at the sides"), activity.widescreen() ? tr("Widescreen") : tr("Original 4:3"), wideAsk);
            drawValueRow(c, pictureBtn, tr("Picture"), tr("The game's own 4:3, or filling the screen"), activity.displayLabel(), false);
            drawSwitchRow(c, fpsBtn, tr("FPS counter"), tr("Frames per second by the toolbar"), activity.showFps(), false);
        } else if (tab == 1) {
            drawSegments(c);
            drawValueRow(c, layoutBtn, tr("Control layout"), tr("Move and resize the buttons"), tr("Edit"), false);
            drawValueRow(c, skinBtn, tr("Controls skin"), tr("Fancy art, or the console's PS2 buttons"), pad.fancySkin() ? tr("Fancy") : tr("PS2 buttons"), false);
            drawSwitchRow(c, r1StickBtn, tr("Stick moves right with R1"), tr("While R1 is held: smaller, so one thumb can do both"), pad.stickFollowsR1(), false);
            drawValueRow(c, opacityBtn, tr("Button opacity"), tr("How see-through the pad is"), pad.opacity() + "%", false);
        } else {
            drawValueRow(c, rendererBtn, tr("Renderer"), rendererAsk ? tr("Tap again: restart with ") + (activity.gles() ? "Vulkan" : "OpenGL ES")
                    : tr("Try the other one if the picture is wrong"), activity.gles() ? "OpenGL ES" : "Vulkan", rendererAsk);
            drawSwitchRow(c, perfBtn, tr("Performance log"), tr("Frame timings in the log, to send (next start)"), activity.perfLog(), false);
            drawValueRow(c, quitBtn, tr("Quit game"), quitAsk ? tr("Tap again to quit") : tr("Back to the launcher"), tr("Quit"), quitAsk);
        }
        c.restore();
    }

    private void drawSlider(Canvas c) {
        float v = (open && role == Role.SLIDER ? pendingScale : activity.scalePercent()) / 100f;
        float[] in = activity.internalScale(Math.round(v * 100));
        String value = Math.round(v * 100) + "%  ·  " + Math.round(in[2]) + "×" + Math.round(in[3]) + String.format(java.util.Locale.US, " (%.1fx)", in[0]);
        rowPaint.setTextAlign(Paint.Align.LEFT);
        c.drawText(tr("Internal resolution"), colL0, sliderLabelY, rowPaint);
        valuePaint.setTextAlign(Paint.Align.RIGHT);
        c.drawText(value, colL1, sliderLabelY, valuePaint);
        valuePaint.setTextAlign(Paint.Align.LEFT);
        float t = (v - 0.2f) / (1f - 0.2f);
        float kx = sliderX0 + t * (sliderX1 - sliderX0);
        line.setStrokeCap(Paint.Cap.ROUND);
        line.setStrokeWidth(0.01f * lu);
        line.setColor(0x33FFFFFF);
        c.drawLine(sliderX0, sliderY, sliderX1, sliderY, line);
        line.setColor(GOLD);
        c.drawLine(sliderX0, sliderY, kx, sliderY, line);
        line.setStrokeCap(Paint.Cap.BUTT);
        float kr = role == Role.SLIDER ? 0.03f * lu : 0.024f * lu;
        fill.setColor(0xFFFFFFFF);
        c.drawCircle(kx, sliderY, kr, fill);
        line.setColor(GOLD);
        line.setStrokeWidth(0.005f * lu);
        c.drawCircle(kx, sliderY, kr, line);
        line.setStrokeWidth(4f);
    }

    private void drawSegments(Canvas c) {
        segAll.set(segStick.left, segStick.top, segDpad.right, segDpad.bottom);
        float rr = segAll.height() / 2f;
        fill.setColor(ROW);
        c.drawRoundRect(segAll, rr, rr, fill);
        boolean dpad = pad.dpadMode();
        RectF on = dpad ? segDpad : segStick;
        float inset = 0.08f * segAll.height();
        fill.setColor(GOLD);
        c.drawRoundRect(on.left + inset, on.top + inset, on.right - inset, on.bottom - inset, rr - inset, rr - inset, fill);
        rowPaint.setTextAlign(Paint.Align.CENTER);
        float base = rowPaint.getTextSize() * 0.35f;
        rowPaint.setColor(dpad ? 0xFFFFFFFF : 0xFF1A1A1A);
        c.drawText(tr("Floating stick"), segStick.centerX(), segStick.centerY() + base, rowPaint);
        rowPaint.setColor(dpad ? 0xFF1A1A1A : 0xFFFFFFFF);
        c.drawText(tr("Arrow buttons"), segDpad.centerX(), segDpad.centerY() + base, rowPaint);
        rowPaint.setColor(0xFFFFFFFF);
        rowPaint.setTextAlign(Paint.Align.LEFT);
    }

    private float rowPad(RectF row) {
        return 0.01f * (colR1 - colR0) + 0.36f * row.height();
    }

    private String fit(String text, Paint paint, float maxWidth) {
        if (paint.measureText(text) <= maxWidth) return text;
        int n = text.length();
        while (n > 1 && paint.measureText(text, 0, n) + paint.measureText("…") > maxWidth) n--;
        return text.substring(0, n) + "…";
    }

    private void drawSwitchRow(Canvas c, RectF row, String title, String hint, boolean on, boolean pending) {
        drawRowBase(c, row, pending);
        drawRowTexts(c, row, title, hint, pending, 2.4f * 0.4f * row.height());
        float th = 0.4f * row.height(), tw = 1.8f * th;
        float tx1 = row.right - rowPad(row), tx0 = tx1 - tw, ty0 = row.centerY() - th / 2f, ty1 = ty0 + th;
        fill.setColor(on ? GOLD : 0x33FFFFFF);
        c.drawRoundRect(tx0, ty0, tx1, ty1, th / 2f, th / 2f, fill);
        fill.setColor(0xFFFFFFFF);
        c.drawCircle(on ? tx1 - th / 2f : tx0 + th / 2f, row.centerY(), 0.38f * th, fill);
    }

    private void drawValueRow(Canvas c, RectF row, String title, String hint, String value, boolean pending) {
        drawRowBase(c, row, pending);
        value = fit(value, valuePaint, 0.5f * row.width());
        drawRowTexts(c, row, title, hint, pending, valuePaint.measureText(value + "  ›") + 0.2f * row.height());
        valuePaint.setTextAlign(Paint.Align.LEFT);
        c.drawText(value + "  ›", row.right - rowPad(row) - valuePaint.measureText(value + "  ›"), row.centerY() + valuePaint.getTextSize() * 0.35f, valuePaint);
    }

    private void drawRowBase(Canvas c, RectF row, boolean pending) {
        float rr = 0.25f * row.height();
        fill.setColor(pending ? 0x22E8C15A : ROW);
        c.drawRoundRect(row, rr, rr, fill);
        if (pending) {
            line.setColor(GOLD);
            line.setStrokeWidth(2f);
            c.drawRoundRect(row, rr, rr, line);
        }
    }

    private void drawRowTexts(Canvas c, RectF row, String title, String hint, boolean pending, float reserve) {
        float x = row.left + rowPad(row);
        float room = row.width() - 2f * rowPad(row) - reserve;
        float block = rowPaint.getTextSize() + 0.1f * subPaint.getTextSize() + subPaint.getTextSize();
        float top = row.centerY() - block / 2f;
        rowPaint.setTextAlign(Paint.Align.LEFT);
        c.drawText(fit(title, rowPaint, room), x, top + rowPaint.getTextSize() * 0.82f, rowPaint);
        subPaint.setTextAlign(Paint.Align.LEFT);
        subPaint.setColor(pending ? GOLD : MUTED);
        c.drawText(fit(hint, subPaint, room), x, top + rowPaint.getTextSize() + 0.1f * subPaint.getTextSize() + subPaint.getTextSize() * 0.85f, subPaint);
        subPaint.setColor(MUTED);
    }

    private void drawIconButton(Canvas c, RectF r, boolean telegram) {
        float cx = r.centerX(), cy = r.centerY(), rad = r.width() / 2f;
        fill.setColor(telegram ? 0x334A9EE8 : 0x33E8C15A);
        c.drawCircle(cx, cy, rad, fill);
        line.setColor(telegram ? 0x884A9EE8 : 0x88E8C15A);
        line.setStrokeWidth(2f);
        c.drawCircle(cx, cy, rad - 1f, line);
        if (telegram) {
            float k = rad * 0.5f;
            android.graphics.Path plane = new android.graphics.Path();
            plane.moveTo(cx - k, cy + 0.05f * k);
            plane.lineTo(cx + k, cy - 0.85f * k);
            plane.lineTo(cx + 0.25f * k, cy + 0.9f * k);
            plane.lineTo(cx - 0.05f * k, cy + 0.2f * k);
            plane.close();
            fill.setColor(0xFF8CC8FF);
            c.drawPath(plane, fill);
            line.setColor(CARD);
            line.setStrokeWidth(0.06f * rad);
            c.drawLine(cx - 0.05f * k, cy + 0.2f * k, cx + k, cy - 0.85f * k, line);
        } else {
            rowPaint.setTextAlign(Paint.Align.CENTER);
            rowPaint.setColor(GOLD);
            rowPaint.setFakeBoldText(true);
            c.drawText("?", cx, cy + rowPaint.getTextSize() * 0.35f, rowPaint);
            rowPaint.setFakeBoldText(false);
            rowPaint.setColor(0xFFFFFFFF);
            rowPaint.setTextAlign(Paint.Align.LEFT);
        }
        line.setStrokeWidth(4f);
    }

    private void drawHelp(Canvas c) {
        setPaintSizes(lu);
        fill.setColor(0xDD000000);
        c.drawRect(0, 0, getWidth(), getHeight(), fill);
        fill.setColor(CARD);
        c.drawRoundRect(panel, 0.035f * lu, 0.035f * lu, fill);
        line.setColor(0x33FFFFFF);
        line.setStrokeWidth(2f);
        c.drawRoundRect(panel, 0.035f * lu, 0.035f * lu, line);
        titlePaint.setTextAlign(Paint.Align.LEFT);
        c.drawText(tr("How to play"), colL0, headerY, titlePaint);
        String[][] rows = {
            { tr("Left thumb"), tr("The stick walks (or the arrow buttons, if Settings > Controls has them)") },
            { tr("X O □ △"), tr("The game's own buttons: X confirms and jumps, O goes back, the others as on the console") },
            { tr("L1 L2 R1 R2"), tr("The shoulder buttons, at the top corners") },
            { tr("START SELECT"), tr("At the bottom middle: START pauses and skips a movie") },
            { tr("≡ button"), tr("At the top: this settings panel. The back key opens it too") },
            { tr("Control layout"), tr("Settings > Controls > Control layout: drag the buttons where you want them, and resize them") },
            { tr("Internal resolution"), tr("What the game is drawn at, in percent of your screen (the game restarts). 25 % is about the PS2's own; higher is sharper but heavier") },
        };
        float y = dividerY + 0.07f * lh;
        float step = (panel.bottom - 0.12f * lh - y) / rows.length;
        for (String[] row : rows) {
            rowPaint.setTextAlign(Paint.Align.LEFT);
            rowPaint.setColor(GOLD);
            c.drawText(row[0], colL0, y, rowPaint);
            subPaint.setTextAlign(Paint.Align.LEFT);
            subPaint.setColor(0xDDFFFFFF);
            c.drawText(fit(row[1], subPaint, colR1 - colL0 - 0.27f * (colR1 - colL0)), colL0 + 0.27f * (colR1 - colL0), y, subPaint);
            y += step;
        }
        subPaint.setColor(MUTED);
        subPaint.setTextAlign(Paint.Align.CENTER);
        c.drawText(tr("Touch anywhere to close"), panel.centerX(), panel.bottom - 0.04f * lh, subPaint);
        subPaint.setTextAlign(Paint.Align.LEFT);
        rowPaint.setColor(0xFFFFFFFF);
    }

    // ---- touch -------------------------------------------------------------------------------------------------------

    private boolean onSlider(float x, float y) {
        return tab == 0 && viewport.contains(x, y) && Math.abs(y - sliderY) < 0.055f * lu && x > sliderX0 - 0.05f * lu && x < sliderX1 + 0.05f * lu;
    }

    private void setSliderFromX(float x, boolean commit) {
        float t = Math.max(0f, Math.min(1f, (x - sliderX0) / (sliderX1 - sliderX0)));
        float v = 0.2f + t * 0.8f;
        pendingScale = Math.round(Math.round(v / 0.05f) * 0.05f * 100f);
        if (commit) activity.applyScalePercent(pendingScale);
        invalidate();
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (!open) return false;
        final int action = e.getActionMasked();
        final int index = e.getActionIndex();
        float x = e.getX(index), y = e.getY(index);
        switch (action) {
            case MotionEvent.ACTION_DOWN: {
                if (helpOpen) {
                    helpOpen = false;
                    invalidate();
                    return true;
                }
                if (closeBtn.contains(x, y) || !panel.contains(x, y)) {
                    hide();
                    return true;
                }
                if (helpBtn.contains(x, y)) {
                    helpOpen = true;
                    invalidate();
                    return true;
                }
                if (telegramBtn.contains(x, y)) {
                    try {
                        getContext().startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(SetupActivity.CHANNEL_URL)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                    } catch (RuntimeException ignored) {
                    }
                    return true;
                }
                for (int i = 0; i < TABS; i++) {
                    if (tabRects[i].contains(x, y)) {
                        setTab(i);
                        return true;
                    }
                }
                pointer = e.getPointerId(index);
                if (onSlider(x, y)) {
                    role = Role.SLIDER;
                    pendingScale = activity.scalePercent();
                    setSliderFromX(x, false);
                } else if (viewport.contains(x, y)) {
                    role = Role.PANEL;
                    touchY = y;
                    scroll0 = scrollY;
                    tapX = x;
                    tapY = y;
                    scrolling = false;
                }
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                int i = e.findPointerIndex(pointer);
                if (i < 0) return true;
                x = e.getX(i);
                y = e.getY(i);
                if (role == Role.SLIDER) {
                    setSliderFromX(x, false);
                } else if (role == Role.PANEL) {
                    if (!scrolling && Math.abs(y - touchY) > slop) scrolling = true;
                    if (scrolling) {
                        scrollY = scroll0 - (y - touchY);
                        layoutPage();
                        invalidate();
                    }
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL: {
                if (role == Role.SLIDER) {
                    if (action == MotionEvent.ACTION_UP) setSliderFromX(e.getX(index), true);
                } else if (role == Role.PANEL && !scrolling && action == MotionEvent.ACTION_UP) {
                    contentTap(tapX, tapY);
                }
                role = Role.NONE;
                pointer = -1;
                invalidate();
                return true;
            }
            default:
                return true;
        }
    }

    /** A tap (lifted without a scroll) on the page's content. */
    private void contentTap(float x, float y) {
        boolean wasRenderer = rendererAsk, wasQuit = quitAsk, wasWide = wideAsk;
        rendererAsk = quitAsk = wideAsk = false;
        if (segStick.contains(x, y)) {
            pad.setDpadMode(false);
        } else if (segDpad.contains(x, y)) {
            pad.setDpadMode(true);
        } else if (layoutBtn.contains(x, y)) {
            hide();
            activity.editLayout();
        } else if (skinBtn.contains(x, y)) {
            pad.setFancySkin(!pad.fancySkin());
        } else if (r1StickBtn.contains(x, y)) {
            pad.setStickFollowsR1(!pad.stickFollowsR1());
        } else if (opacityBtn.contains(x, y)) {
            int o = pad.opacity();
            pad.setOpacity(o < 30 ? 30 : o < 50 ? 50 : o < 70 ? 70 : o < 100 ? 100 : 30);
        } else if (wideBtn.contains(x, y)) {
            if (wasWide) activity.switchWidescreen(); else wideAsk = true;
        } else if (pictureBtn.contains(x, y)) {
            activity.cycleDisplay();
        } else if (fpsBtn.contains(x, y)) {
            activity.setShowFps(!activity.showFps());
        } else if (rendererBtn.contains(x, y)) {
            if (wasRenderer) activity.switchRenderer(); else rendererAsk = true;
        } else if (perfBtn.contains(x, y)) {
            activity.setPerfLog(!activity.perfLog());
        } else if (quitBtn.contains(x, y)) {
            if (wasQuit) activity.finish(); else quitAsk = true;
        }
        invalidate();
    }
}
