package io.github.flipendo.spike;

import java.util.List;

/**
 * What the launcher's updater decides without touching the network or Android: how version names compare, which of the
 * GitHub releases is the one to offer, and when the next automatic check is due. Plain Java, so android/tests/run.sh tests it.
 */
final class UpdateRules {
    /** Releases of the Android app are tagged android-v<android/VERSION> (the other tags belong to the desktop builds). */
    static final String TAG_PREFIX = "android-v";

    /** What the launcher offers: the release's light APK (the self packs carry game data and are never published). */
    static final String APK_NAME = "flipendo-droid.apk";

    private UpdateRules() {
    }

    /** One release as the updater needs it. */
    static final class Candidate {
        String tag = "";
        String name = "";
        String notes = "";
        String apkUrl = "";
        /** Lower-case hex of the APK's SHA-256 when GitHub states it, else empty. */
        String sha256 = "";
        long size;
        boolean draft;

        /** The version part of the tag ("0.5.3"). */
        String version() {
            return tag.startsWith(TAG_PREFIX) ? tag.substring(TAG_PREFIX.length()) : tag;
        }
    }

    /**
     * The numbers of a version name: "0.5.3", "v0.5.3", "android-v0.5.3", "0.5.3-dev+1a2b3c4". Null when it has none.
     * A name with fewer than three numbers is padded with zeros ("1.2" is 1.2.0).
     */
    static int[] parseVersion(String name) {
        if (name == null) {
            return null;
        }
        int start = 0;
        while (start < name.length() && !Character.isDigit(name.charAt(start))) {
            start++;
        }
        int[] parts = new int[4];
        int count = 0;
        int i = start;
        while (i < name.length() && count < parts.length) {
            int from = i;
            while (i < name.length() && Character.isDigit(name.charAt(i))) {
                i++;
            }
            if (i == from || i - from > 9) {
                break;
            }
            parts[count++] = Integer.parseInt(name.substring(from, i));
            if (i < name.length() && name.charAt(i) == '.') {
                i++;
            } else {
                break;
            }
        }
        if (count == 0) {
            return null;
        }
        return parts;
    }

    /** True when {@code candidate} is a later version than {@code installed}; a name without numbers is never later. */
    static boolean newer(String candidate, String installed) {
        int[] c = parseVersion(candidate);
        int[] n = parseVersion(installed);
        if (c == null) {
            return false;
        }
        if (n == null) {
            return true;
        }
        for (int i = 0; i < c.length; i++) {
            if (c[i] != n[i]) {
                return c[i] > n[i];
            }
        }
        return false; // the same numbers: a "-dev" build of 0.5.3 is not offered 0.5.3
    }

    /** Whether an asset is the APK to install: the light build, never a self pack. */
    static boolean isApkAsset(String assetName) {
        if (assetName == null) {
            return false;
        }
        String n = assetName.toLowerCase(java.util.Locale.ROOT);
        return n.endsWith(".apk") && n.startsWith("flipendo-droid") && !n.contains("selfpack");
    }

    /**
     * The release to offer: the latest published Android release that has an APK and is later than {@code installed};
     * null when there is none (the installed one is the latest, or nothing usable is published).
     */
    static Candidate pick(List<Candidate> releases, String installed) {
        Candidate best = null;
        for (Candidate c : releases) {
            if (c.draft || !c.tag.startsWith(TAG_PREFIX) || c.apkUrl.isEmpty() || parseVersion(c.tag) == null) {
                continue;
            }
            if (best == null || newer(c.tag, best.tag)) {
                best = c;
            }
        }
        return best != null && newer(best.tag, installed) ? best : null;
    }

    /** Whether an automatic check is due: never made, made over {@code intervalMs} ago, or the clock went back. */
    static boolean due(long now, long lastCheck, long intervalMs) {
        return lastCheck <= 0 || now < lastCheck || now - lastCheck >= intervalMs;
    }

    /** The first lines of a release's notes, for the dialog: at most {@code maxChars} characters, cut at a line end. */
    static String excerpt(String notes, int maxChars) {
        if (notes == null) {
            return "";
        }
        String text = notes.replace("\r", "").trim();
        if (text.length() <= maxChars) {
            return text;
        }
        int cut = text.lastIndexOf('\n', maxChars);
        return text.substring(0, cut > maxChars / 2 ? cut : maxChars).trim() + "\n...";
    }
}
