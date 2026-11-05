package io.github.flipendo.spike;

/**
 * Where the PS1 pad's on-screen controls sit and what a touch at a point presses (plain Java, no Android:
 * android/tests/Ps1PadLayoutTest). Ps1PadView draws these shapes and feeds every finger's point through {@link #maskAt};
 * the OR of all fingers goes to the native host as one bit mask. Ids are libretro's joypad ids, which the host reads as they are.
 *
 * The controls come in groups (the D-pad, the face buttons, each pair of shoulder buttons, Start and Select, the menu button)
 * that the player can move and resize: a group has an offset from its default place (fractions of the screen's width and
 * height) and a scale, and every centre and size below goes through it. Default places scale with the screen height h: the
 * pad in the lower corners, the shoulder buttons above it as wide pills (L2 over L1), Start and Select at the bottom middle.
 */
public final class Ps1PadLayout {
    public static final int B = 0, Y = 1, SELECT = 2, START = 3, UP = 4, DOWN = 5, LEFT = 6, RIGHT = 7,
            A = 8, X = 9, L1 = 10, R1 = 11, L2 = 12, R2 = 13;

    /** The groups the layout editor moves and resizes. */
    public static final int G_DPAD = 0, G_FACE = 1, G_LEFT = 2, G_RIGHT = 3, G_MIDDLE = 4, G_MENU = 5, GROUPS = 6;
    public static final float SCALE_MIN = 0.6f, SCALE_MAX = 1.8f, SCALE_STEP = 0.1f;

    /** What the shoulder, Start and Select pills say; the face buttons are drawn as their symbols. */
    public static String label(int id) {
        switch (id) {
            case L1: return "L1";
            case L2: return "L2";
            case R1: return "R1";
            case R2: return "R2";
            case START: return "START";
            case SELECT: return "SELECT";
            default: return "";
        }
    }

    private static final int[] BUTTONS = {B, A, Y, X, L1, R1, L2, R2, START, SELECT};
    private static final float HIT_SLOP = 1.3f;   // a finger a little off a button still presses it
    private static final float DPAD_SLOP = 1.25f;
    private static final float DPAD_DEAD = 0.04f; // of the D-pad's radius / 0.2
    private static final float DPAD_AXIS = 0.38f; // share of the finger's distance that counts as a direction

    private final float w, h;
    /** Per group: offset x (of w), offset y (of h), scale. */
    private final float[][] groups = new float[GROUPS][3];

    public Ps1PadLayout(float width, float height) {
        this.w = width;
        this.h = height;
        resetGroups();
    }

    public float width() { return w; }
    public float height() { return h; }

    // ---- the groups -----------------------------------------------------------------------------------------------

    public void resetGroups() {
        for (float[] g : groups) { g[0] = 0f; g[1] = 0f; g[2] = 1f; }
    }

    /** Puts a group at an offset (fractions of the screen) and a scale (clamped to SCALE_MIN..SCALE_MAX). */
    public void setGroup(int g, float dx, float dy, float scale) {
        groups[g][0] = dx;
        groups[g][1] = dy;
        groups[g][2] = clampScale(scale);
    }

    public float groupDx(int g) { return groups[g][0]; }
    public float groupDy(int g) { return groups[g][1]; }
    public float groupScale(int g) { return groups[g][2]; }

    public static float clampScale(float s) {
        if (Float.isNaN(s)) return 1f;
        return Math.max(SCALE_MIN, Math.min(SCALE_MAX, Math.round(s * 10f) / 10f));
    }

    /** Moves a group by a distance in pixels. */
    public void moveGroup(int g, float px, float py) {
        groups[g][0] += px / w;
        groups[g][1] += py / h;
    }

    /** One step bigger (+1) or smaller (-1). */
    public void stepScale(int g, int dir) {
        groups[g][2] = clampScale(groups[g][2] + dir * SCALE_STEP);
    }

    public static int groupOf(int id) {
        switch (id) {
            case B: case A: case Y: case X: return G_FACE;
            case L1: case L2: return G_LEFT;
            case R1: case R2: return G_RIGHT;
            case START: case SELECT: return G_MIDDLE;
            case UP: case DOWN: case LEFT: case RIGHT: return G_DPAD;
            default: throw new IllegalArgumentException("not a button: " + id);
        }
    }

    // the default anchor of each group (the point it scales around)
    private float anchorX(int g) {
        switch (g) {
            case G_DPAD: return 0.34f * h;
            case G_FACE: return w - 0.34f * h;
            case G_LEFT: return 0.30f * h;
            case G_RIGHT: return w - 0.30f * h;
            default: return w / 2;
        }
    }

    private float anchorY(int g) {
        switch (g) {
            case G_DPAD: case G_FACE: return 0.68f * h;
            case G_LEFT: case G_RIGHT: return 0.19f * h;
            case G_MIDDLE: return 0.92f * h;
            default: return 0.08f * h;
        }
    }

    /** A default point of a group, moved and scaled with it. */
    private float tx(int g, float x) { return anchorX(g) + groups[g][0] * w + (x - anchorX(g)) * groups[g][2]; }
    private float ty(int g, float y) { return anchorY(g) + groups[g][1] * h + (y - anchorY(g)) * groups[g][2]; }
    private float ts(int g, float size) { return size * groups[g][2]; }

    // ---- the controls -------------------------------------------------------------------------------------------

    public float dpadX() { return tx(G_DPAD, anchorX(G_DPAD)); }
    public float dpadY() { return ty(G_DPAD, anchorY(G_DPAD)); }
    public float dpadRadius() { return ts(G_DPAD, 0.20f * h); }

    public float menuX() { return tx(G_MENU, anchorX(G_MENU)); }
    public float menuY() { return ty(G_MENU, anchorY(G_MENU)); }
    public float menuRadius() { return ts(G_MENU, 0.05f * h); }

    private float defaultX(int id) {
        float face = anchorX(G_FACE), spread = 0.12f * h;
        switch (id) {
            case B: case X: return face;
            case A: return face + spread;
            case Y: return face - spread;
            case L1: case L2: return anchorX(G_LEFT);
            case R1: case R2: return anchorX(G_RIGHT);
            case START: return w / 2 + 0.11f * h;
            case SELECT: return w / 2 - 0.11f * h;
            default: throw new IllegalArgumentException("not a button: " + id);
        }
    }

    private float defaultY(int id) {
        float face = anchorY(G_FACE), spread = 0.12f * h;
        switch (id) {
            case X: return face - spread;
            case B: return face + spread;
            case A: case Y: return face;
            case L2: case R2: return 0.11f * h;
            case L1: case R1: return 0.27f * h;
            case START: case SELECT: return anchorY(G_MIDDLE);
            default: throw new IllegalArgumentException("not a button: " + id);
        }
    }

    public float centerX(int id) { return tx(groupOf(id), defaultX(id)); }
    public float centerY(int id) { return ty(groupOf(id), defaultY(id)); }

    /** Whether the button is a pill (shoulders, Start, Select) rather than a round button (the face buttons). */
    public static boolean isPill(int id) {
        return id == L1 || id == L2 || id == R1 || id == R2 || id == START || id == SELECT;
    }

    /** Half the width of a button (its radius for a round one). */
    public float halfWidth(int id) {
        float s;
        switch (id) {
            case B: case A: case Y: case X: s = 0.08f * h; break;
            case L1: case L2: case R1: case R2: s = 0.15f * h; break;
            case START: case SELECT: s = 0.085f * h; break;
            default: throw new IllegalArgumentException("not a button: " + id);
        }
        return ts(groupOf(id), s);
    }

    /** Half the height of a button (its radius for a round one). */
    public float halfHeight(int id) {
        float s;
        switch (id) {
            case B: case A: case Y: case X: s = 0.08f * h; break;
            case L1: case L2: case R1: case R2: s = 0.055f * h; break;
            case START: case SELECT: s = 0.04f * h; break;
            default: throw new IllegalArgumentException("not a button: " + id);
        }
        return ts(groupOf(id), s);
    }

    /** The radius of a round button. */
    public float buttonRadius(int id) { return halfWidth(id); }

    /** The buttons that can be drawn, in drawing order. */
    public int[] buttons() { return BUTTONS.clone(); }

    public boolean isMenu(float x, float y) {
        return dist(x, y, menuX(), menuY()) <= menuRadius() * HIT_SLOP;
    }

    /** How far a point is from a button, as a share of its size (1 = on its edge); a pill is measured as a rounded box. */
    private float reach(int id, float x, float y) {
        float dx = Math.abs(x - centerX(id)), dy = Math.abs(y - centerY(id));
        if (!isPill(id)) return (float) Math.sqrt(dx * dx + dy * dy) / halfWidth(id);
        float hw = halfWidth(id), hh = halfHeight(id);
        // the straight middle of the pill counts by height only, its round ends by the distance to the end's centre
        float ex = Math.max(0f, dx - (hw - hh));
        return (float) Math.sqrt(ex * ex + dy * dy) / hh;
    }

    /** The bit mask (1 &lt;&lt; id) of what a finger at (x, y) presses: a direction or two for the D-pad, or the nearest button. */
    public int maskAt(float x, float y) {
        float dx = x - dpadX(), dy = y - dpadY(), d = (float) Math.sqrt(dx * dx + dy * dy);
        if (d <= dpadRadius() * DPAD_SLOP) {
            int m = 0;
            if (d > DPAD_DEAD * dpadRadius() / 0.2f) {
                if (dy / d < -DPAD_AXIS) m |= 1 << UP;
                if (dy / d > DPAD_AXIS) m |= 1 << DOWN;
                if (dx / d < -DPAD_AXIS) m |= 1 << LEFT;
                if (dx / d > DPAD_AXIS) m |= 1 << RIGHT;
            }
            return m;
        }
        int best = -1;
        float bestReach = Float.MAX_VALUE;
        for (int id : BUTTONS) {
            float r = reach(id, x, y);
            if (r <= HIT_SLOP && r < bestReach) { best = id; bestReach = r; }
        }
        return best < 0 ? 0 : 1 << best;
    }

    // ---- the editor -----------------------------------------------------------------------------------------------

    /** The box a group covers now: left, top, right, bottom. */
    public float[] groupBounds(int g) {
        if (g == G_DPAD) {
            float r = dpadRadius();
            return new float[] {dpadX() - r, dpadY() - r, dpadX() + r, dpadY() + r};
        }
        if (g == G_MENU) {
            float r = menuRadius();
            return new float[] {menuX() - r, menuY() - r, menuX() + r, menuY() + r};
        }
        float l = Float.MAX_VALUE, t = Float.MAX_VALUE, r = -Float.MAX_VALUE, b = -Float.MAX_VALUE;
        for (int id : BUTTONS) {
            if (groupOf(id) != g) continue;
            l = Math.min(l, centerX(id) - halfWidth(id));
            r = Math.max(r, centerX(id) + halfWidth(id));
            t = Math.min(t, centerY(id) - halfHeight(id));
            b = Math.max(b, centerY(id) + halfHeight(id));
        }
        return new float[] {l, t, r, b};
    }

    /** The group under a touch (the one whose middle is nearest when boxes overlap), or -1. */
    public int groupAt(float x, float y) {
        float pad = 0.03f * h;
        int best = -1;
        float bestD = Float.MAX_VALUE;
        for (int g = 0; g < GROUPS; g++) {
            float[] r = groupBounds(g);
            if (x < r[0] - pad || x > r[2] + pad || y < r[1] - pad || y > r[3] + pad) continue;
            float cx = (r[0] + r[2]) / 2, cy = (r[1] + r[3]) / 2, d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            if (d < bestD) { bestD = d; best = g; }
        }
        return best;
    }

    /** Shifts a group back inside the box left..right, top..bottom (the screen less its cut-outs); a group bigger than the
     *  box is centred in it. */
    public void keepInside(int g, float left, float top, float right, float bottom) {
        float[] r = groupBounds(g);
        float sx = 0f, sy = 0f;
        if (r[2] - r[0] > right - left) sx = (left + right) / 2 - (r[0] + r[2]) / 2;
        else if (r[0] < left) sx = left - r[0];
        else if (r[2] > right) sx = right - r[2];
        if (r[3] - r[1] > bottom - top) sy = (top + bottom) / 2 - (r[1] + r[3]) / 2;
        else if (r[1] < top) sy = top - r[1];
        else if (r[3] > bottom) sy = bottom - r[3];
        moveGroup(g, sx, sy);
    }

    private static float dist(float x1, float y1, float x2, float y2) {
        float dx = x1 - x2, dy = y1 - y2;
        return (float) Math.sqrt(dx * dx + dy * dy);
    }
}
