package io.github.flipendo.spike;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Desktop tests of Ps2Setup (no Android): android/tests/run.sh runs them. */
public final class Ps2SetupTest {
    private static int failures = 0;

    private static void check(boolean ok, String name) {
        System.out.println((ok ? "PASS " : "FAIL ") + name);
        if (!ok) failures++;
    }

    private static File dir() throws IOException {
        return Files.createTempDirectory("ps2setup").toFile();
    }

    private static File write(File dir, String name, byte[] bytes) throws IOException {
        File f = new File(dir, name);
        f.getParentFile().mkdirs();
        Files.write(f.toPath(), bytes);
        return f;
    }

    private static File touch(File dir, String name) throws IOException {
        return write(dir, name, new byte[1]);
    }

    private static String name(File f) {
        return f == null ? null : f.getName();
    }

    private static void put32(byte[] b, int at, long v) {
        for (int i = 0; i < 4; i++) b[at + i] = (byte) (v >> (8 * i));
    }

    /** A small ISO 9660 image: SYSTEM.CNF in the root. raw: every sector 2352 bytes, mode 2, the data after 24 bytes. */
    private static byte[] iso(String cnf, boolean raw) {
        byte[] data = new byte[40 * 2048];
        byte[] pvd = new byte[2048];
        pvd[0] = 1;
        System.arraycopy("CD001".getBytes(StandardCharsets.ISO_8859_1), 0, pvd, 1, 5);
        pvd[6] = 1;
        put32(pvd, 158, 20);       // the root record's extent
        put32(pvd, 166, 2048);     // and its size
        System.arraycopy(pvd, 0, data, 16 * 2048, 2048);
        byte[] cnfBytes = cnf.getBytes(StandardCharsets.ISO_8859_1);
        byte[] dir = new byte[2048];
        int at = 0;
        String[] names = {"\0", "\1"};
        for (String n : names) {                       // "." and ".."
            dir[at] = 34;
            put32(dir, at + 2, 20);
            put32(dir, at + 10, 2048);
            dir[at + 25] = 2;
            dir[at + 32] = 1;
            dir[at + 33] = (byte) n.charAt(0);
            at += 34;
        }
        String file = "SYSTEM.CNF;1";
        int len = (33 + file.length() + 1) & ~1;
        dir[at] = (byte) len;
        put32(dir, at + 2, 30);
        put32(dir, at + 10, cnfBytes.length);
        dir[at + 32] = (byte) file.length();
        System.arraycopy(file.getBytes(StandardCharsets.ISO_8859_1), 0, dir, at + 33, file.length());
        System.arraycopy(dir, 0, data, 20 * 2048, 2048);
        System.arraycopy(cnfBytes, 0, data, 30 * 2048, cnfBytes.length);
        if (!raw) return data;
        byte[] out = new byte[40 * 2352];
        for (int s = 0; s < 40; s++) {
            int base = s * 2352;
            for (int i = 1; i <= 10; i++) out[base + i] = -1;
            out[base + 15] = 2;
            System.arraycopy(data, s * 2048, out, base + 24, 2048);
        }
        return out;
    }

    public static void main(String[] args) throws Exception {
        // BOOT2 as a serial
        check("SLES-51192".equals(Ps2Setup.serialFromCnf("BOOT2 = cdrom0:\\SLES_511.92;1\r\nVER = 1.00\r\nVMODE = PAL\r\n")), "serialFromCnf");
        check("SLUS-20001".equals(Ps2Setup.serialFromCnf("BOOT2=cdrom0:\\SLUS_200.01;1\n")), "serialWithoutSpaces");
        check("SCES-50001".equals(Ps2Setup.serialFromCnf("boot2 = cdrom0:SCES_500.01\n")), "serialLowerCaseNoSlash");
        check(Ps2Setup.serialFromCnf("BOOT = cdrom:\\SLPS_123.45;1\n") == null, "ps1BootLineIsNoPs2Serial");
        check(Ps2Setup.serialFromCnf("BOOT2 = cdrom0:\\GAME.ELF;1\n") == null, "oddNameHasNoSerial");
        check("hp2".equals(Ps2Setup.gameOf("SLES-51192")) && Ps2Setup.gameOf("SLUS-20001") == null && Ps2Setup.gameOf(null) == null, "knownGames");

        // image names
        check(Ps2Setup.isImageName("a.cue") && Ps2Setup.isImageName("A.ISO") && Ps2Setup.isImageName("a.bin") && Ps2Setup.isImageName("a.img"), "imageNames");
        check(!Ps2Setup.isImageName("a.chd") && !Ps2Setup.isImageName("iso") && !Ps2Setup.isImageName("a.zip"), "otherNamesAreNotImages");

        // a bin stands for its cue
        File d = dir();
        File cue = touch(d, "h.cue"), bin = touch(d, "h.bin"), lonely = touch(d, "x.bin");
        check(name(Ps2Setup.normalizePicked(bin)).equals("h.cue"), "binPickMeansItsCue");
        check(name(Ps2Setup.normalizePicked(lonely)).equals("x.bin"), "binWithoutCueStaysABin");

        // a cue names its image, or the only one beside it when the name is wrong
        File c = dir();
        write(c, "game.cue", "FILE \"Game (Europe).bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n".getBytes(StandardCharsets.ISO_8859_1));
        File renamed = touch(c, "Game_renamed.bin");
        check(renamed.equals(Ps2Setup.cueImage(new File(c, "game.cue"))), "cueWithWrongNameFindsTheOnlyImage");
        touch(c, "other.bin");
        check(Ps2Setup.cueImage(new File(c, "game.cue")) == null, "cueWithTwoImagesAndWrongNameIsRefused");
        File named = touch(c, "Game (Europe).bin");
        check(named.equals(Ps2Setup.cueImage(new File(c, "game.cue"))), "cueNamesItsImage");

        // a disc folder, with SYSTEM.CNF in any case
        File folder = dir();
        write(folder, "system.cnf", "BOOT2 = cdrom0:\\SLES_511.92;1\n".getBytes(StandardCharsets.ISO_8859_1));
        touch(folder, "SLES_511.92");
        check(Ps2Setup.isDiscFolder(folder), "discFolderIsRecognised");
        check("SLES-51192".equals(Ps2Setup.serialOf(folder)), "serialOfFolder");
        check(!Ps2Setup.isDiscFolder(dir()), "emptyFolderIsNoDisc");

        // the serial from an image: plain 2048-byte sectors, raw 2352-byte ones, through a cue sheet
        File imgs = dir();
        String cnf = "BOOT2 = cdrom0:\\SLES_511.92;1\nVER = 1.00\nVMODE = PAL\n";
        File plain = write(imgs, "plain.iso", iso(cnf, false));
        check("SLES-51192".equals(Ps2Setup.serialOf(plain)), "serialOfIso");
        File rawDir = dir();
        File rawBin = write(rawDir, "disc.bin", iso(cnf, true));
        write(rawDir, "disc.cue", "FILE \"Wrong Name.bin\" BINARY\n  TRACK 01 MODE2/2352\n".getBytes(StandardCharsets.ISO_8859_1));
        check("SLES-51192".equals(Ps2Setup.serialOf(rawBin)), "serialOfRawBin");
        check("SLES-51192".equals(Ps2Setup.serialOf(new File(rawDir, "disc.cue"))), "serialThroughCue");
        File junk = write(dir(), "junk.iso", new byte[5000]);
        check(Ps2Setup.serialOf(junk) == null, "serialOfJunkIsNull");

        // which disc to use: a picked image, else a picked folder, else the default folder; a pick that is gone is skipped
        File pickedDir = dir(), defDir = dir();
        File pickedImage = touch(pickedDir, "mine.iso");
        touch(defDir, "other.iso");
        check(name(Ps2Setup.resolveDisc(pickedImage.getPath(), null, defDir)).equals("mine.iso"), "pickedImageWins");
        check(name(Ps2Setup.resolveDisc("/nonexistent/x.iso", pickedDir.getPath(), defDir)).equals("mine.iso"), "goneImageFallsToFolder");
        check(name(Ps2Setup.resolveDisc(null, "/nonexistent", defDir)).equals("other.iso"), "goneFolderFallsToDefault");
        check(Ps2Setup.resolveDisc(null, null, dir()) == null, "nothingToPlay");
        check(folder.equals(Ps2Setup.resolveDisc(null, folder.getPath(), defDir)), "aDiscFolderIsTheDisc");
        File parent = dir();
        File inner = new File(parent, "HP2");
        inner.mkdirs();
        write(inner, "SYSTEM.CNF", "BOOT2 = cdrom0:\\SLES_511.92;1\n".getBytes(StandardCharsets.ISO_8859_1));
        check(inner.equals(Ps2Setup.resolveDisc(null, parent.getPath(), defDir)), "aDiscFolderOneLevelDeeper");
        File both = dir();
        touch(both, "a.iso");
        touch(both, "a.cue");
        check(name(Ps2Setup.resolveDisc(null, both.getPath(), defDir)).equals("a.cue"), "cueComesBeforeIso");

        check(Ps2Setup.clampScale(5) == 25 && Ps2Setup.clampScale(300) == 100 && Ps2Setup.clampScale(60) == 60, "scaleIsClamped");
        check(Ps2Setup.defaultDir("hp2").getPath().replace('\\', '/').endsWith("FlipendoPS2/HP2"), "defaultFolder");

        if (failures > 0) {
            System.out.println(failures + " FAILED");
            System.exit(1);
        }
        System.out.println("all passed");
    }
}
