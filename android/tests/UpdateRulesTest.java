package io.github.flipendo.spike;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Desktop tests of UpdateRules (no Android): android/tests/run.sh compiles and runs them. */
public final class UpdateRulesTest {
    private static int failures = 0;

    private static void check(boolean ok, String name) {
        System.out.println((ok ? "PASS " : "FAIL ") + name);
        if (!ok) failures++;
    }

    private static UpdateRules.Candidate release(String tag, boolean draft, String apk) {
        UpdateRules.Candidate c = new UpdateRules.Candidate();
        c.tag = tag;
        c.draft = draft;
        c.apkUrl = apk;
        return c;
    }

    public static void main(String[] args) {
        check(Arrays.equals(UpdateRules.parseVersion("0.5.3"), new int[] { 0, 5, 3, 0 }), "parses 0.5.3");
        check(Arrays.equals(UpdateRules.parseVersion("android-v0.5.3"), new int[] { 0, 5, 3, 0 }), "parses a tag");
        check(Arrays.equals(UpdateRules.parseVersion("0.5.3-dev+1a2b3c4"), new int[] { 0, 5, 3, 0 }), "parses a dev build, ignoring the commit");
        check(Arrays.equals(UpdateRules.parseVersion("v1.2"), new int[] { 1, 2, 0, 0 }), "pads a short version");
        check(UpdateRules.parseVersion("unknown") == null, "no numbers, no version");
        check(UpdateRules.parseVersion(null) == null, "null has no version");

        check(UpdateRules.newer("android-v0.5.4", "0.5.3"), "0.5.4 is newer than 0.5.3");
        check(UpdateRules.newer("0.10.0", "0.9.9"), "numbers compare as numbers, not text");
        check(!UpdateRules.newer("0.5.3", "0.5.3"), "the same version is not newer");
        check(!UpdateRules.newer("0.5.2", "0.5.3"), "an older one is not newer");
        check(!UpdateRules.newer("0.5.3", "0.5.3-dev+abc"), "a dev build of the same numbers is not offered the release");
        check(UpdateRules.newer("0.5.4", "0.5.3-dev+abc"), "a dev build is offered a later release");
        check(!UpdateRules.newer("nightly", "0.5.3"), "a tag without numbers is never newer");
        check(UpdateRules.newer("0.5.3", "unknown"), "an unknown installed version is offered anything");

        check(UpdateRules.isApkAsset("flipendo-droid.apk"), "the light APK is the one");
        check(!UpdateRules.isApkAsset("flipendo-droid-selfpack.apk"), "a self pack is never offered");
        check(!UpdateRules.isApkAsset("flipendo-windows.zip"), "a zip is not an APK");
        check(!UpdateRules.isApkAsset(null), "no name, no APK");

        List<UpdateRules.Candidate> all = new ArrayList<>();
        all.add(release("v1.1", false, "x")); // a desktop release
        all.add(release("android-v0.5.2", false, "u2"));
        all.add(release("android-v0.5.4", true, "u4")); // a draft
        all.add(release("android-v0.5.3", false, "u3"));
        all.add(release("android-v0.5.5", false, "")); // no APK attached (yet)
        UpdateRules.Candidate best = UpdateRules.pick(all, "0.5.2");
        check(best != null && best.tag.equals("android-v0.5.3"), "picks the latest published Android release with an APK");
        check(UpdateRules.pick(all, "0.5.3") == null, "nothing to offer when the installed one is the latest");
        check(UpdateRules.pick(new ArrayList<UpdateRules.Candidate>(), "0.5.3") == null, "no releases, nothing to offer");
        check(best != null && best.version().equals("0.5.3"), "the version of a candidate is its tag without the prefix");

        long hour = 3600_000L;
        check(UpdateRules.due(1000 * hour, 0, 12 * hour), "a check never made is due");
        check(!UpdateRules.due(1000 * hour, 995 * hour, 12 * hour), "a check made 5 hours ago is not due");
        check(UpdateRules.due(1000 * hour, 980 * hour, 12 * hour), "a check made 20 hours ago is due");
        check(UpdateRules.due(10 * hour, 990 * hour, 12 * hour), "a clock that went back makes a check due");

        check(UpdateRules.excerpt("short", 100).equals("short"), "a short note is kept");
        String longNote = "line one\nline two\nline three\nline four";
        String cut = UpdateRules.excerpt(longNote, 20);
        check(cut.startsWith("line one") && cut.endsWith("...") && cut.length() < longNote.length(), "a long note is cut at a line end");

        System.out.println(failures == 0 ? "all update rule tests pass" : failures + " failed");
        if (failures != 0) System.exit(1);
    }
}
