package io.github.flipendo.spike;

/** Desktop tests of Ps1PadLayout (no Android): android/tests/run.sh compiles and runs them. */
public final class Ps1PadLayoutTest {
    private static int failures = 0;

    private static void check(boolean ok, String name) {
        System.out.println((ok ? "PASS " : "FAIL ") + name);
        if (!ok) failures++;
    }

    static int bit(int id) { return 1 << id; }

    public static void main(String[] args) {
        for (float[] size : new float[][] {{2400, 1080}, {1280, 720}}) {
            String at = " at " + (int) size[0] + "x" + (int) size[1];
            Ps1PadLayout l = new Ps1PadLayout(size[0], size[1]);

            // every button is hit at its own centre and gives exactly its own bit
            int[] ids = {Ps1PadLayout.B, Ps1PadLayout.A, Ps1PadLayout.Y, Ps1PadLayout.X, Ps1PadLayout.L1, Ps1PadLayout.R1,
                    Ps1PadLayout.L2, Ps1PadLayout.R2, Ps1PadLayout.START, Ps1PadLayout.SELECT};
            boolean all = true;
            for (int id : ids) all &= l.maskAt(l.centerX(id), l.centerY(id)) == bit(id);
            check(all, "everyButtonIsHitAtItsCentre" + at);

            float dx = l.dpadX(), dy = l.dpadY(), r = l.dpadRadius();
            check(l.maskAt(dx, dy) == 0, "dpadCentreIsADeadZone" + at);
            check(l.maskAt(dx, dy - r * 0.7f) == bit(Ps1PadLayout.UP), "dpadUp" + at);
            check(l.maskAt(dx, dy + r * 0.7f) == bit(Ps1PadLayout.DOWN), "dpadDown" + at);
            check(l.maskAt(dx - r * 0.7f, dy) == bit(Ps1PadLayout.LEFT), "dpadLeft" + at);
            check(l.maskAt(dx + r * 0.7f, dy) == bit(Ps1PadLayout.RIGHT), "dpadRight" + at);
            check(l.maskAt(dx - r * 0.6f, dy - r * 0.6f) == (bit(Ps1PadLayout.UP) | bit(Ps1PadLayout.LEFT)), "dpadDiagonal" + at);
            check(l.maskAt(dx + r * 0.6f, dy + r * 0.6f) == (bit(Ps1PadLayout.DOWN) | bit(Ps1PadLayout.RIGHT)), "dpadOtherDiagonal" + at);
            check(l.maskAt(dx + r * 3f, dy) == 0, "farFromDpadIsNothing" + at);

            check(l.maskAt(size[0] / 2, size[1] / 2) == 0, "middleOfTheScreenIsNothing" + at);
            check(l.isMenu(l.menuX(), l.menuY()) && !l.isMenu(size[0] / 2, size[1] / 2), "menuButton" + at);
            check(l.maskAt(l.menuX(), l.menuY()) == 0, "menuButtonIsNotAPadButton" + at);

            // the buttons stay on the screen and do not overlap each other
            boolean inside = true;
            for (int id : ids) {
                float x = l.centerX(id), y = l.centerY(id), br = l.buttonRadius(id);
                inside &= x - br >= 0 && x + br <= size[0] && y - br >= 0 && y + br <= size[1];
            }
            check(inside, "buttonsAreOnTheScreen" + at);
            boolean apart = true;
            for (int i = 0; i < ids.length; i++)
                for (int j = i + 1; j < ids.length; j++) {
                    float ddx = l.centerX(ids[i]) - l.centerX(ids[j]), ddy = l.centerY(ids[i]) - l.centerY(ids[j]);
                    apart &= Math.sqrt(ddx * ddx + ddy * ddy) >= l.buttonRadius(ids[i]) + l.buttonRadius(ids[j]);
                }
            check(apart, "buttonsDoNotOverlap" + at);
        }

        // several fingers: the mask of every touch is combined
        Ps1PadLayout l = new Ps1PadLayout(2400, 1080);
        int two = l.maskAt(l.centerX(Ps1PadLayout.B), l.centerY(Ps1PadLayout.B)) | l.maskAt(l.dpadX(), l.dpadY() - l.dpadRadius() * 0.7f);
        check(two == (bit(Ps1PadLayout.B) | bit(Ps1PadLayout.UP)), "fingersCombineByOr");

        // libretro's joypad ids (the native host reads the same numbers)
        check(Ps1PadLayout.B == 0 && Ps1PadLayout.Y == 1 && Ps1PadLayout.SELECT == 2 && Ps1PadLayout.START == 3
                && Ps1PadLayout.UP == 4 && Ps1PadLayout.DOWN == 5 && Ps1PadLayout.LEFT == 6 && Ps1PadLayout.RIGHT == 7
                && Ps1PadLayout.A == 8 && Ps1PadLayout.X == 9 && Ps1PadLayout.L1 == 10 && Ps1PadLayout.R1 == 11
                && Ps1PadLayout.L2 == 12 && Ps1PadLayout.R2 == 13, "idsAreLibretroJoypadIds");

        System.out.println(failures == 0 ? "ALL PASS" : failures + " FAILED");
        System.exit(failures == 0 ? 0 : 1);
    }
}
