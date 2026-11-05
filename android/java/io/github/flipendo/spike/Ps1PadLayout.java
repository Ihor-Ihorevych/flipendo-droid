package io.github.flipendo.spike;

/**
 * Where the PS1 pad's on-screen controls sit and what a touch at a point presses (plain Java, no Android:
 * android/tests/Ps1PadLayoutTest). Ps1PadView draws these shapes and feeds every finger's point through {@link #maskAt};
 * the OR of all fingers goes to the native host as one bit mask. Ids are libretro's joypad ids, which the host reads as they are.
 * Everything scales with the screen height h; the pad sits in the lower corners and the shoulder buttons above them.
 */
public final class Ps1PadLayout {
    public static final int B = 0, Y = 1, SELECT = 2, START = 3, UP = 4, DOWN = 5, LEFT = 6, RIGHT = 7,
            A = 8, X = 9, L1 = 10, R1 = 11, L2 = 12, R2 = 13;

    /** PS1 names for the face buttons, by libretro id (B is the cross, A the circle, Y the square, X the triangle). */
    public static String label(int id) {
        switch (id) {
            case B: return "✕";
            case A: return "○";
            case Y: return "□";
            case X: return "△";
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
    private static final float DPAD_DEAD = 0.04f; // of h
    private static final float DPAD_AXIS = 0.38f; // share of the finger's distance that counts as a direction

    private final float w, h;

    public Ps1PadLayout(float width, float height) {
        this.w = width;
        this.h = height;
    }

    public float dpadX() { return 0.34f * h; }
    public float dpadY() { return 0.68f * h; }
    public float dpadRadius() { return 0.20f * h; }

    public float menuX() { return w / 2; }
    public float menuY() { return 0.08f * h; }
    public float menuRadius() { return 0.05f * h; }

    private float faceX() { return w - 0.34f * h; }
    private float faceY() { return 0.68f * h; }
    private float faceSpread() { return 0.12f * h; }

    public float centerX(int id) {
        switch (id) {
            case B: case X: return faceX();
            case A: return faceX() + faceSpread();
            case Y: return faceX() - faceSpread();
            case L1: case L2: return 0.30f * h;
            case R1: case R2: return w - 0.30f * h;
            case START: return w / 2 + 0.10f * h;
            case SELECT: return w / 2 - 0.10f * h;
            default: throw new IllegalArgumentException("not a button: " + id);
        }
    }

    public float centerY(int id) {
        switch (id) {
            case X: return faceY() - faceSpread();
            case B: return faceY() + faceSpread();
            case A: case Y: return faceY();
            case L2: case R2: return 0.12f * h;
            case L1: case R1: return 0.28f * h;
            case START: case SELECT: return 0.90f * h;
            default: throw new IllegalArgumentException("not a button: " + id);
        }
    }

    public float buttonRadius(int id) {
        switch (id) {
            case B: case A: case Y: case X: return 0.08f * h;
            case L1: case L2: case R1: case R2: return 0.07f * h;
            case START: case SELECT: return 0.05f * h;
            default: throw new IllegalArgumentException("not a button: " + id);
        }
    }

    /** The buttons that can be drawn, in drawing order. */
    public int[] buttons() { return BUTTONS.clone(); }

    public boolean isMenu(float x, float y) {
        return dist(x, y, menuX(), menuY()) <= menuRadius() * HIT_SLOP;
    }

    /** The bit mask (1 &lt;&lt; id) of what a finger at (x, y) presses: a direction or two for the D-pad, or the nearest button. */
    public int maskAt(float x, float y) {
        float dx = x - dpadX(), dy = y - dpadY(), d = (float) Math.sqrt(dx * dx + dy * dy);
        if (d <= dpadRadius() * DPAD_SLOP) {
            int m = 0;
            if (d > DPAD_DEAD * h) {
                if (dy / d < -DPAD_AXIS) m |= 1 << UP;
                if (dy / d > DPAD_AXIS) m |= 1 << DOWN;
                if (dx / d < -DPAD_AXIS) m |= 1 << LEFT;
                if (dx / d > DPAD_AXIS) m |= 1 << RIGHT;
            }
            return m;
        }
        int best = -1;
        float bestDist = Float.MAX_VALUE;
        for (int id : BUTTONS) {
            float dd = dist(x, y, centerX(id), centerY(id));
            if (dd <= buttonRadius(id) * HIT_SLOP && dd < bestDist) { best = id; bestDist = dd; }
        }
        return best < 0 ? 0 : 1 << best;
    }

    private static float dist(float x1, float y1, float x2, float y2) {
        float dx = x1 - x2, dy = y1 - y2;
        return (float) Math.sqrt(dx * dx + dy * dy);
    }
}
