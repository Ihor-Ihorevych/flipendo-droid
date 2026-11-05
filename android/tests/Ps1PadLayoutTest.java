package io.github.flipendo.spike;

/** Desktop tests of Ps1PadLayout (no Android): android/tests/run.sh compiles and runs them. */
public final class Ps1PadLayoutTest {
    private static int failures = 0;

    private static void check(boolean ok, String name) {
        System.out.println((ok ? "PASS " : "FAIL ") + name);
        if (!ok) failures++;
    }

    static int bit(int id) { return 1 << id; }

    static final int[] IDS = {Ps1PadLayout.B, Ps1PadLayout.A, Ps1PadLayout.Y, Ps1PadLayout.X, Ps1PadLayout.L1, Ps1PadLayout.R1,
            Ps1PadLayout.L2, Ps1PadLayout.R2, Ps1PadLayout.START, Ps1PadLayout.SELECT};

    static boolean everyButtonAtItsCentre(Ps1PadLayout l) {
        boolean all = true;
        for (int id : IDS) all &= l.maskAt(l.centerX(id), l.centerY(id)) == bit(id);
        return all;
    }

    static boolean boxesApart(float[] a, float[] b) {
        return a[2] <= b[0] || b[2] <= a[0] || a[3] <= b[1] || b[3] <= a[1];
    }

    static float[] box(Ps1PadLayout l, int id) {
        return new float[] {l.centerX(id) - l.halfWidth(id), l.centerY(id) - l.halfHeight(id), l.centerX(id) + l.halfWidth(id), l.centerY(id) + l.halfHeight(id)};
    }

    public static void main(String[] args) {
        for (float[] size : new float[][] {{2400, 1080}, {1280, 720}, {2000, 1200}}) {
            String at = " at " + (int) size[0] + "x" + (int) size[1];
            Ps1PadLayout l = new Ps1PadLayout(size[0], size[1]);

            check(everyButtonAtItsCentre(l), "everyButtonIsHitAtItsCentre" + at);

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

            // a shoulder pill is pressed along its whole width, not only at a circle in its middle
            int s = Ps1PadLayout.L1;
            float edge = l.centerX(s) + l.halfWidth(s) * 0.9f;
            check(l.maskAt(edge, l.centerY(s)) == bit(s), "shoulderPillIsHitNearItsEnd" + at);
            check(l.maskAt(l.centerX(s), l.centerY(s) + l.halfHeight(s) * 2.5f) != bit(s), "shoulderPillIsThin" + at);

            // the buttons stay on the screen and do not overlap each other or the D-pad
            boolean inside = true;
            for (int id : IDS) {
                float[] b = box(l, id);
                inside &= b[0] >= 0 && b[2] <= size[0] && b[1] >= 0 && b[3] <= size[1];
            }
            check(inside, "buttonsAreOnTheScreen" + at);
            boolean apart = true;
            float[] dpad = l.groupBounds(Ps1PadLayout.G_DPAD), menu = l.groupBounds(Ps1PadLayout.G_MENU);
            for (int i = 0; i < IDS.length; i++) {
                apart &= boxesApart(box(l, IDS[i]), dpad) && boxesApart(box(l, IDS[i]), menu);
                for (int j = i + 1; j < IDS.length; j++) {
                    if (Ps1PadLayout.isPill(IDS[i]) || Ps1PadLayout.isPill(IDS[j])) {
                        apart &= boxesApart(box(l, IDS[i]), box(l, IDS[j]));
                    } else {
                        float ddx = l.centerX(IDS[i]) - l.centerX(IDS[j]), ddy = l.centerY(IDS[i]) - l.centerY(IDS[j]);
                        apart &= Math.sqrt(ddx * ddx + ddy * ddy) >= l.halfWidth(IDS[i]) + l.halfWidth(IDS[j]);
                    }
                }
            }
            check(apart, "buttonsDoNotOverlap" + at);
        }

        Ps1PadLayout l = new Ps1PadLayout(2400, 1080);

        // several fingers: the mask of every touch is combined
        int two = l.maskAt(l.centerX(Ps1PadLayout.B), l.centerY(Ps1PadLayout.B)) | l.maskAt(l.dpadX(), l.dpadY() - l.dpadRadius() * 0.7f);
        check(two == (bit(Ps1PadLayout.B) | bit(Ps1PadLayout.UP)), "fingersCombineByOr");

        // the editor: a moved group's buttons go with it, and the old place no longer presses them
        float oldX = l.centerX(Ps1PadLayout.R1), oldY = l.centerY(Ps1PadLayout.R1);
        l.moveGroup(Ps1PadLayout.G_RIGHT, -300, 200);
        check(Math.abs(l.centerX(Ps1PadLayout.R1) - (oldX - 300)) < 0.5f && Math.abs(l.centerY(Ps1PadLayout.R1) - (oldY + 200)) < 0.5f, "movedGroupMovesItsButtons");
        check(l.maskAt(l.centerX(Ps1PadLayout.R2), l.centerY(Ps1PadLayout.R2)) == bit(Ps1PadLayout.R2), "movedButtonIsHitAtItsNewPlace");
        check(l.maskAt(oldX, oldY) == 0, "oldPlaceOfAMovedButtonIsNothing");
        check(l.groupAt(l.centerX(Ps1PadLayout.R1), l.centerY(Ps1PadLayout.R1)) == Ps1PadLayout.G_RIGHT, "groupAtFindsTheMovedGroup");
        check(l.groupAt(l.dpadX(), l.dpadY()) == Ps1PadLayout.G_DPAD && l.groupAt(l.menuX(), l.menuY()) == Ps1PadLayout.G_MENU, "groupAtDpadAndMenu");

        // scaling grows sizes around the group's own place, within the limits
        float before = l.dpadRadius();
        l.stepScale(Ps1PadLayout.G_DPAD, +1);
        check(Math.abs(l.dpadRadius() - before * 1.1f) < 0.5f, "stepScaleGrowsTenPercent");
        for (int i = 0; i < 30; i++) l.stepScale(Ps1PadLayout.G_DPAD, +1);
        check(l.groupScale(Ps1PadLayout.G_DPAD) == Ps1PadLayout.SCALE_MAX, "scaleStopsAtTheMaximum");
        for (int i = 0; i < 30; i++) l.stepScale(Ps1PadLayout.G_DPAD, -1);
        check(l.groupScale(Ps1PadLayout.G_DPAD) == Ps1PadLayout.SCALE_MIN, "scaleStopsAtTheMinimum");
        check(Ps1PadLayout.clampScale(Float.NaN) == 1f && Ps1PadLayout.clampScale(5f) == Ps1PadLayout.SCALE_MAX, "clampScale");

        // keepInside: a group dragged off the screen comes back to its edge
        l.moveGroup(Ps1PadLayout.G_FACE, 5000, 0);
        l.keepInside(Ps1PadLayout.G_FACE, 0, 0, 2400, 1080);
        float[] face = l.groupBounds(Ps1PadLayout.G_FACE);
        check(Math.abs(face[2] - 2400) < 0.5f && face[0] >= 0, "keepInsidePullsAGroupBack");

        // reset puts everything back, and the set values round-trip
        l.resetGroups();
        check(everyButtonAtItsCentre(l) && l.groupDx(Ps1PadLayout.G_RIGHT) == 0f && l.groupScale(Ps1PadLayout.G_DPAD) == 1f, "resetPutsGroupsBack");
        l.setGroup(Ps1PadLayout.G_MIDDLE, 0.1f, -0.2f, 1.3f);
        check(l.groupDx(Ps1PadLayout.G_MIDDLE) == 0.1f && l.groupDy(Ps1PadLayout.G_MIDDLE) == -0.2f && l.groupScale(Ps1PadLayout.G_MIDDLE) == 1.3f, "setGroupRoundTrips");
        check(everyButtonAtItsCentre(l), "everyButtonIsHitAfterChanges");

        // libretro's joypad ids (the native host reads the same numbers)
        check(Ps1PadLayout.B == 0 && Ps1PadLayout.Y == 1 && Ps1PadLayout.SELECT == 2 && Ps1PadLayout.START == 3
                && Ps1PadLayout.UP == 4 && Ps1PadLayout.DOWN == 5 && Ps1PadLayout.LEFT == 6 && Ps1PadLayout.RIGHT == 7
                && Ps1PadLayout.A == 8 && Ps1PadLayout.X == 9 && Ps1PadLayout.L1 == 10 && Ps1PadLayout.R1 == 11
                && Ps1PadLayout.L2 == 12 && Ps1PadLayout.R2 == 13, "idsAreLibretroJoypadIds");

        System.out.println(failures == 0 ? "ALL PASS" : failures + " FAILED");
        System.exit(failures == 0 ? 0 : 1);
    }
}
