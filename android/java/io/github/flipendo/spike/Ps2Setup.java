package io.github.flipendo.spike;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * Where the PS2 game's disc comes from and what it is (plain Java, no Android: android/tests/Ps2SetupTest). The player gives a disc
 * image (an ISO, or a BIN with its CUE sheet) or the folder of an unpacked disc; the runner (libps2main.so, Ps2Activity) takes any of
 * them as it is, and translates the game's code on the phone, once (docs/ps2.md). This class finds the disc, reads its serial from
 * SYSTEM.CNF and says whether the game is one the app knows.
 */
public final class Ps2Setup {
    private Ps2Setup() {}

    /** Where the PS2 games keep their files: the disc the player copies there, memory cards, saves. */
    public static final String ROOT = "/sdcard/FlipendoPS2";

    // the launcher's preferences ("setup")
    static final String PREF_GAME = "ps2.game";        // "hp2" (the only game so far)
    static final String PREF_FOLDER = "ps2.folder.";   // + game: a folder the player picked
    static final String PREF_DISC = "ps2.disc.";       // + game: an image file the player picked (wins over the folder)
    static final String PREF_SCALE = "ps2.scale";      // resolution, percent of the phone's pixels (default 100)

    /** The games the app knows: their key, the disc serial and the name for messages. */
    static final String[][] GAMES = {{"hp2", "SLES-51192", "Harry Potter and the Chamber of Secrets (PAL)"}};

    /** The folder a game's disc is looked for in when the player picked nothing: /sdcard/FlipendoPS2/HP2. */
    public static File defaultDir(String game) {
        return new File(ROOT, game.toUpperCase(Locale.ROOT));
    }

    /** True for a file name the runner opens as a disc image. */
    public static boolean isImageName(String name) {
        String n = name.toLowerCase(Locale.ROOT);
        for (String ext : new String[] {".cue", ".iso", ".bin", ".img"}) {
            if (n.endsWith(ext) && n.length() > ext.length()) return true;
        }
        return false;
    }

    /** True for a folder that holds the files of a disc: a SYSTEM.CNF in any letter case. */
    public static boolean isDiscFolder(File dir) {
        return dir != null && dir.isDirectory() && child(dir, "SYSTEM.CNF") != null;
    }

    private static File child(File dir, String name) {
        File[] files = dir.listFiles();
        if (files == null) return null;
        for (File f : files) {
            if (f.isFile() && f.getName().equalsIgnoreCase(name)) return f;
        }
        return null;
    }

    /**
     * What a picked file stands for: a .bin is the .cue beside it when there is one (the runner reads either, but the sheet says
     * the image is a raw one); anything else stays.
     */
    public static File normalizePicked(File picked) {
        String n = picked.getName();
        if (n.toLowerCase(Locale.ROOT).endsWith(".bin")) {
            File cue = child(picked.getParentFile(), n.substring(0, n.length() - 4) + ".cue");
            if (cue != null) return cue;
        }
        return picked;
    }

    /**
     * The disc to use: the image the player picked, else the folder the player picked (a disc folder itself, or a folder with an
     * image in it, one level deeper too), else the same in the default folder. A pick that is gone is skipped. Null when there is
     * nothing. The result is an image file or a disc folder; both are handed to the runner as they are.
     */
    public static File resolveDisc(String pickedDisc, String pickedFolder, File defaultFolder) {
        if (pickedDisc != null) {
            File f = new File(pickedDisc);
            if (f.isFile()) return f;
        }
        if (pickedFolder != null) {
            File found = findIn(new File(pickedFolder));
            if (found != null) return found;
        }
        return findIn(defaultFolder);
    }

    private static File findIn(File dir) {
        if (dir == null || !dir.isDirectory()) return null;
        if (isDiscFolder(dir)) return dir;
        File image = firstImage(dir);
        if (image != null) return image;
        File[] subs = dir.listFiles();
        if (subs != null) {
            java.util.Arrays.sort(subs);
            for (File sub : subs) {
                if (sub.isDirectory()) {
                    if (isDiscFolder(sub)) return sub;
                    File deeper = firstImage(sub);
                    if (deeper != null) return deeper;
                }
            }
        }
        return null;
    }

    /** A .cue first (it says what the BIN is), then an .iso, then a bare .bin or .img. */
    static File firstImage(File dir) {
        File[] files = dir.listFiles();
        if (files == null) return null;
        java.util.Arrays.sort(files);
        for (String ext : new String[] {".cue", ".iso", ".bin", ".img"}) {
            for (File f : files) {
                if (f.isFile() && isImageName(f.getName()) && f.getName().toLowerCase(Locale.ROOT).endsWith(ext)) return f;
            }
        }
        return null;
    }

    /** The image file a CUE sheet names; when that file is not there, the only image in the sheet's folder. Null if none. */
    public static File cueImage(File cue) {
        File dir = cue.getParentFile();
        try {
            String text = new String(java.nio.file.Files.readAllBytes(cue.toPath()), StandardCharsets.ISO_8859_1);
            java.util.regex.Matcher m = java.util.regex.Pattern.compile("(?i)FILE\\s+(?:\"([^\"]+)\"|(\\S+))").matcher(text);
            if (m.find()) {
                String name = m.group(1) != null ? m.group(1) : m.group(2);
                File named = new File(dir, name);
                if (named.isFile()) return named;
                File byName = new File(dir, new File(name.replace('\\', '/')).getName());
                if (byName.isFile()) return byName;
            }
        } catch (IOException e) {
            return null;
        }
        File only = null;
        int count = 0;
        File[] files = dir == null ? null : dir.listFiles();
        if (files != null) {
            for (File f : files) {
                String n = f.getName().toLowerCase(Locale.ROOT);
                if (f.isFile() && (n.endsWith(".bin") || n.endsWith(".img") || n.endsWith(".iso"))) {
                    only = f;
                    count++;
                }
            }
        }
        return count == 1 ? only : null;
    }

    /** SYSTEM.CNF's BOOT2 name as a serial: "cdrom0:\SLES_511.92;1" is SLES-51192. Null when the text has none. */
    public static String serialFromCnf(String cnf) {
        for (String line : cnf.split("[\r\n]+")) {
            String t = line.trim();
            if (!t.toUpperCase(Locale.ROOT).startsWith("BOOT2")) continue;
            int eq = t.indexOf('=');
            if (eq < 0) return null;
            String path = t.substring(eq + 1).trim();
            int slash = Math.max(path.lastIndexOf('\\'), Math.max(path.lastIndexOf('/'), path.lastIndexOf(':')));
            String name = path.substring(slash + 1);
            int semi = name.indexOf(';');
            if (semi >= 0) name = name.substring(0, semi);
            java.util.regex.Matcher m = java.util.regex.Pattern.compile("(?i)^([A-Z]{4})[_-]?(\\d{3})\\.?(\\d{2})$").matcher(name);
            if (m.matches()) return (m.group(1) + "-" + m.group(2) + m.group(3)).toUpperCase(Locale.ROOT);
            return null;
        }
        return null;
    }

    /** The serial of a disc (an image, a CUE sheet or a disc folder), or null when it cannot be read. */
    public static String serialOf(File disc) {
        try {
            if (disc.isDirectory()) {
                File cnf = child(disc, "SYSTEM.CNF");
                return cnf == null ? null : serialFromCnf(new String(java.nio.file.Files.readAllBytes(cnf.toPath()), StandardCharsets.ISO_8859_1));
            }
            File image = disc.getName().toLowerCase(Locale.ROOT).endsWith(".cue") ? cueImage(disc) : disc;
            if (image == null) return null;
            byte[] cnf = readSystemCnf(image);
            return cnf == null ? null : serialFromCnf(new String(cnf, StandardCharsets.ISO_8859_1));
        } catch (IOException e) {
            return null;
        }
    }

    /** The key of the game a serial belongs to ("hp2"), or null for a disc the app does not know. */
    public static String gameOf(String serial) {
        if (serial == null) return null;
        for (String[] g : GAMES) {
            if (g[1].equalsIgnoreCase(serial)) return g[0];
        }
        return null;
    }

    // ---- SYSTEM.CNF from an image: an ISO 9660 read in 2048-byte sectors, or in raw 2352-byte ones (BIN) ----

    private static final byte[] SYNC = {0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0};

    static byte[] readSystemCnf(File image) throws IOException {
        try (RandomAccessFile f = new RandomAccessFile(image, "r")) {
            int stride = 2048, skip = 0;
            byte[] head = new byte[16];
            if (f.length() >= 16) {
                f.readFully(head);
                boolean raw = true;
                for (int i = 0; i < SYNC.length; i++) raw &= head[i] == SYNC[i];
                if (raw) {
                    stride = 2352;
                    skip = head[15] == 2 ? 24 : 16;
                }
            }
            byte[] pvd = sector(f, 16, stride, skip);
            if (pvd == null || pvd[0] != 1 || pvd[1] != 'C' || pvd[2] != 'D' || pvd[3] != '0' || pvd[4] != '0' || pvd[5] != '1') return null;
            long rootLba = u32(pvd, 158), rootSize = u32(pvd, 166);
            for (long off = 0; off < Math.min(rootSize, 64 * 2048L); off += 2048) {
                byte[] dir = sector(f, rootLba + off / 2048, stride, skip);
                if (dir == null) return null;
                int at = 0;
                while (at < 2048) {
                    int len = dir[at] & 0xFF;
                    if (len == 0) break;
                    int nameLen = dir[at + 32] & 0xFF;
                    if (at + 33 + nameLen <= 2048) {
                        String name = new String(dir, at + 33, nameLen, StandardCharsets.ISO_8859_1);
                        int semi = name.indexOf(';');
                        if ((semi >= 0 ? name.substring(0, semi) : name).equalsIgnoreCase("SYSTEM.CNF")) {
                            long lba = u32(dir, at + 2), size = Math.min(u32(dir, at + 10), 64 * 1024);
                            byte[] out = new byte[(int) size];
                            for (int done = 0; done < size; done += 2048) {
                                byte[] s = sector(f, lba + done / 2048, stride, skip);
                                if (s == null) return null;
                                System.arraycopy(s, 0, out, done, Math.min(2048, (int) size - done));
                            }
                            return out;
                        }
                    }
                    at += len;
                }
            }
        }
        return null;
    }

    private static byte[] sector(RandomAccessFile f, long lba, int stride, int skip) throws IOException {
        long at = lba * stride + skip;
        if (at + 2048 > f.length()) return null;
        f.seek(at);
        byte[] b = new byte[2048];
        f.readFully(b);
        return b;
    }

    private static long u32(byte[] b, int at) {
        return (b[at] & 0xFFL) | (b[at + 1] & 0xFFL) << 8 | (b[at + 2] & 0xFFL) << 16 | (b[at + 3] & 0xFFL) << 24;
    }

    /** The resolution the runner draws at, percent of the phone's pixels: 25 to 100 (default 100). */
    public static int clampScale(int percent) {
        return Math.max(25, Math.min(100, percent));
    }
}
