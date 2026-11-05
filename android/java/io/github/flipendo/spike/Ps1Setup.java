package io.github.flipendo.spike;

import java.io.File;
import java.util.Locale;

/**
 * Where the PS1 games' discs, BIOS and settings come from (plain Java, no Android: android/tests/Ps1SetupTest). The launcher
 * keeps the player's picks in its preferences and asks this class what they mean; the emulator itself is Ps1Activity.
 */
public final class Ps1Setup {
    private Ps1Setup() {}

    /** Where the PS1 games keep their files (discs the player copies there, memory cards, states). */
    public static final String ROOT = "/sdcard/FlipendoPS1";
    static final long BIOS_SIZE = 512 * 1024;

    // the launcher's preferences ("setup"), per game where it says so
    static final String PREF_PLATFORM = "platform";            // "pc" (default) or "ps1"
    static final String PREF_GAME = "ps1.game";                // "hp1" (default) or "hp2": the PS1 game shown
    static final String PREF_FOLDER = "ps1.folder.";           // + game: a folder the player picked
    static final String PREF_DISC = "ps1.disc.";               // + game: an image file the player picked (wins over the folder)
    static final String PREF_SCALE = "ps1.scale";              // resolution scale 1 to 4 (default 2)
    static final String PREF_BIOS = "ps1.bios";                // a BIOS file the player picked (default: the built-in OpenBIOS)

    /** The folder a game's disc is looked for in when the player picked nothing: /sdcard/FlipendoPS1/HP1 or HP2. */
    public static File defaultDir(String game) {
        return new File(ROOT, "hp2".equals(game) ? "HP2" : "HP1");
    }

    /** True for a file name the emulator side can use as, or turn into, a disc image. */
    public static boolean isImageName(String name) {
        String n = name.toLowerCase(Locale.ROOT);
        for (String ext : new String[] {".cue", ".chd", ".mdf", ".mds", ".iso", ".bin", ".img"}) {
            if (n.endsWith(ext) && n.length() > ext.length()) return true;
        }
        return false;
    }

    /**
     * What a picked file stands for: an .mds is its .mdf, a .bin is the .cue that names it (when there is one beside it).
     * Null for an .mds without its .mdf, since there is nothing to play.
     */
    public static File normalizePicked(File picked) {
        String n = picked.getName();
        String lower = n.toLowerCase(Locale.ROOT);
        int dot = n.lastIndexOf('.');
        String base = dot < 0 ? n : n.substring(0, dot);
        if (lower.endsWith(".mds")) {
            File mdf = sibling(picked, base, ".mdf");
            return mdf;
        }
        if (lower.endsWith(".bin")) {
            File cue = sibling(picked, base, ".cue");
            return cue != null ? cue : picked;
        }
        return picked;
    }

    /** A file beside f with the given base name and extension, in any letter case; null if there is none. */
    private static File sibling(File f, String base, String ext) {
        File[] files = f.getParentFile() == null ? null : f.getParentFile().listFiles();
        if (files == null) return null;
        for (File c : files) {
            if (c.isFile() && c.getName().equalsIgnoreCase(base + ext)) return c;
        }
        return null;
    }

    /**
     * The disc to play: the image file the player picked, else the first disc in the folder the player picked (and one folder
     * deeper), else the first one in the default folder. A pick that is gone is skipped. Null when there is no disc.
     */
    public static File resolveDisc(String pickedDisc, String pickedFolder, File defaultFolder) {
        if (pickedDisc != null) {
            File f = new File(pickedDisc);
            if (f.isFile()) return f;
        }
        if (pickedFolder != null) {
            File found = Ps1DiscConverter.findDisc(new File(pickedFolder), 1);
            if (found != null) return found;
        }
        return Ps1DiscConverter.findDisc(defaultFolder, 1);
    }

    /**
     * A document id from the system file picker as a path: "primary:Games/a.cue" is on internal storage, "1A2B-3C4D:HP/a.cue" on
     * an SD card, "raw:/storage/..." (Downloads) is the path itself. Ids that name nothing on disk ("msf:1234") give null.
     */
    public static String documentIdToPath(String id, String primaryRoot) {
        if (id == null) return null;
        if (id.startsWith("raw:")) return id.substring(4);
        int colon = id.indexOf(':');
        if (colon <= 0) return null;
        String volume = id.substring(0, colon), relative = id.substring(colon + 1);
        if (!volume.equalsIgnoreCase("primary") && !volume.matches("[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}")) return null;
        String base = "primary".equalsIgnoreCase(volume) ? primaryRoot : "/storage/" + volume;
        return relative.isEmpty() ? base : base + "/" + relative;
    }

    /** True for a file that can serve as the PS1 BIOS: 512 KB. */
    public static boolean isBios(File f) {
        return f != null && f.isFile() && f.length() == BIOS_SIZE;
    }

    public static int clampScale(int scale) {
        return Math.max(1, Math.min(4, scale));
    }
}
