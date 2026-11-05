package io.github.flipendo.spike;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;

/** Desktop tests of Ps1DiscConverter (no Android): android/tests/run.sh compiles and runs them. */
public final class Ps1DiscConverterTest {
    private static int failures = 0;

    private static void check(boolean ok, String name) {
        System.out.println((ok ? "PASS " : "FAIL ") + name);
        if (!ok) failures++;
    }

    private static File tempDir() throws IOException {
        return Files.createTempDirectory("ps1disc").toFile();
    }

    /** A minimal Alcohol 120% descriptor with one session and one data track of the given sector size (layout as HP1's). */
    static byte[] mds(int sectorSize) {
        byte[] b = new byte[486];
        byte[] magic = "MEDIA DESCRIPTOR".getBytes(StandardCharsets.US_ASCII);
        System.arraycopy(magic, 0, b, 0, magic.length);
        b[16] = 1; b[17] = 3;                 // version 1.3
        b[20] = 1;                             // sessions
        putInt(b, 0x50, 88);                   // sessions offset
        putInt(b, 88 + 20, 112);               // the session's track blocks offset
        b[88 + 10] = 4;                        // blocks: points 0xA0, 0xA1, 0xA2 and track 1
        b[88 + 11] = 3;                        // of which not tracks
        int[] points = {160, 161, 162, 1};
        for (int i = 0; i < 4; i++) {
            int o = 112 + i * 80;
            b[o] = (byte) (i == 3 ? 0xEC : 0x00);
            b[o + 4] = (byte) points[i];
            if (i == 3) { b[o + 16] = (byte) (sectorSize & 0xFF); b[o + 17] = (byte) (sectorSize >> 8); }
        }
        return b;
    }

    static void putInt(byte[] b, int off, int v) {
        b[off] = (byte) v; b[off + 1] = (byte) (v >> 8); b[off + 2] = (byte) (v >> 16); b[off + 3] = (byte) (v >> 24);
    }

    /** An image of n sectors of the given size, each filled with its own index byte. */
    static byte[] image(int sectors, int sectorSize) {
        byte[] b = new byte[sectors * sectorSize];
        for (int s = 0; s < sectors; s++) Arrays.fill(b, s * sectorSize, (s + 1) * sectorSize, (byte) (s + 1));
        return b;
    }

    public static void main(String[] args) throws Exception {
        File dir = tempDir();
        File out = tempDir();

        // 2448-byte sectors (HP1): the 96 subchannel bytes of every sector are dropped
        File mdf = new File(dir, "game.mdf");
        Files.write(mdf.toPath(), image(3, 2448));
        Files.write(new File(dir, "game.mds").toPath(), mds(2448));
        File cue = Ps1DiscConverter.convertMdf(mdf, out);
        File bin = new File(out, "game.bin");
        check(cue.getName().equals("game.cue") && cue.exists() && bin.exists(), "convertsMdfToBinAndCue");
        byte[] got = Files.readAllBytes(bin.toPath());
        boolean sectorsOk = got.length == 3 * 2352;
        for (int s = 0; sectorsOk && s < 3; s++)
            for (int i = 0; i < 2352; i++) if (got[s * 2352 + i] != (byte) (s + 1)) { sectorsOk = false; break; }
        check(sectorsOk, "keepsFirst2352BytesOfEachSector");
        String cueText = new String(Files.readAllBytes(cue.toPath()), StandardCharsets.US_ASCII);
        check(cueText.contains("FILE \"game.bin\" BINARY") && cueText.contains("TRACK 01 MODE2/2352")
                && cueText.contains("INDEX 01 00:00:00"), "cueNamesTheBinAsOneMode2Track");

        // the work is not repeated for an image that was converted already
        long stamp = bin.lastModified();
        Thread.sleep(20);
        Ps1DiscConverter.convertMdf(mdf, out);
        check(bin.lastModified() == stamp, "skipsAnImageThatIsConvertedAlready");

        // a 2352-byte descriptor is a plain copy
        File dir2 = tempDir();
        File out2 = tempDir();
        File mdf2 = new File(dir2, "raw.mdf");
        Files.write(mdf2.toPath(), image(2, 2352));
        Files.write(new File(dir2, "raw.mds").toPath(), mds(2352));
        Ps1DiscConverter.convertMdf(mdf2, out2);
        check(Arrays.equals(Files.readAllBytes(new File(out2, "raw.bin").toPath()), image(2, 2352)), "plain2352ImageIsCopied");

        // size that is not a whole number of sectors
        File dir3 = tempDir();
        File mdf3 = new File(dir3, "bad.mdf");
        Files.write(mdf3.toPath(), new byte[2448 * 2 + 5]);
        Files.write(new File(dir3, "bad.mds").toPath(), mds(2448));
        boolean refused = false;
        try { Ps1DiscConverter.convertMdf(mdf3, tempDir()); } catch (IOException e) { refused = e.getMessage().contains("sector"); }
        check(refused, "rejectsSizeNotMultipleOfSectorSize");

        // no descriptor next to it
        File dir4 = tempDir();
        File mdf4 = new File(dir4, "lonely.mdf");
        Files.write(mdf4.toPath(), new byte[2448]);
        refused = false;
        try { Ps1DiscConverter.convertMdf(mdf4, tempDir()); } catch (IOException e) { refused = true; }
        check(refused, "rejectsMdfWithoutMds");

        // a descriptor of a format we do not know
        File dir5 = tempDir();
        File mdf5 = new File(dir5, "odd.mdf");
        Files.write(mdf5.toPath(), new byte[2048 * 4]);
        Files.write(new File(dir5, "odd.mds").toPath(), mds(2048));
        refused = false;
        try { Ps1DiscConverter.convertMdf(mdf5, tempDir()); } catch (IOException e) { refused = e.getMessage().contains("2048"); }
        check(refused, "rejectsUnknownSectorSize");

        // finding the disc in a game folder
        File g = tempDir();
        check(Ps1DiscConverter.findDisc(g) == null, "findDiscOfEmptyFolderIsNull");
        check(Ps1DiscConverter.findDisc(new File(g, "missing")) == null, "findDiscOfMissingFolderIsNull");
        Files.write(new File(g, "a.mdf").toPath(), new byte[1]);
        check(Ps1DiscConverter.findDisc(g) == null, "mdfWithoutMdsIsNotADisc");
        Files.write(new File(g, "a.mds").toPath(), new byte[1]);
        check(Ps1DiscConverter.findDisc(g).getName().equals("a.mdf"), "findsMdfWithMds");
        Files.write(new File(g, "b.bin").toPath(), new byte[1]);
        Files.write(new File(g, "b.cue").toPath(), new byte[1]);
        check(Ps1DiscConverter.findDisc(g).getName().equals("b.cue"), "cuePreferredOverMdf");
        check(!Ps1DiscConverter.needsConversion(new File(g, "b.cue")) && Ps1DiscConverter.needsConversion(new File(g, "a.mdf")),
                "onlyMdfNeedsConversion");

        System.out.println(failures == 0 ? "ALL PASS" : failures + " FAILED");
        System.exit(failures == 0 ? 0 : 1);
    }
}
