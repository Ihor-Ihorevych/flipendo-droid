package io.github.flipendo.spike;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

/** Desktop tests of Ps1Setup, and of Ps1DiscConverter's folder search (no Android): android/tests/run.sh runs them. */
public final class Ps1SetupTest {
    private static int failures = 0;

    private static void check(boolean ok, String name) {
        System.out.println((ok ? "PASS " : "FAIL ") + name);
        if (!ok) failures++;
    }

    private static File dir() throws IOException {
        return Files.createTempDirectory("ps1setup").toFile();
    }

    private static File touch(File dir, String name) throws IOException {
        File f = new File(dir, name);
        f.getParentFile().mkdirs();
        Files.write(f.toPath(), new byte[1]);
        return f;
    }

    private static String name(File f) {
        return f == null ? null : f.getName();
    }

    public static void main(String[] args) throws Exception {
        // a document id from the system file picker becomes a path
        check("/storage/emulated/0/Games/HP2/a.cue".equals(Ps1Setup.documentIdToPath("primary:Games/HP2/a.cue", "/storage/emulated/0")), "primaryDocumentId");
        check("/storage/1A2B-3C4D/HP/a.cue".equals(Ps1Setup.documentIdToPath("1A2B-3C4D:HP/a.cue", "/storage/emulated/0")), "sdCardDocumentId");
        check("/storage/emulated/0/Download/a.cue".equals(Ps1Setup.documentIdToPath("raw:/storage/emulated/0/Download/a.cue", "/storage/emulated/0")), "downloadsRawDocumentId");
        check(Ps1Setup.documentIdToPath("msf:1234", "/storage/emulated/0") == null, "opaqueDocumentIdHasNoPath");
        check(Ps1Setup.documentIdToPath(null, "/storage/emulated/0") == null, "nullDocumentIdHasNoPath");
        check("/storage/emulated/0".equals(Ps1Setup.documentIdToPath("primary:", "/storage/emulated/0")), "rootDocumentId");

        // which files are disc images
        check(Ps1Setup.isImageName("a.cue") && Ps1Setup.isImageName("A.CHD") && Ps1Setup.isImageName("a.mdf") && Ps1Setup.isImageName("a.iso")
                && Ps1Setup.isImageName("a.mds") && Ps1Setup.isImageName("a.bin") && Ps1Setup.isImageName("a.img"), "imageNames");
        check(!Ps1Setup.isImageName("a.txt") && !Ps1Setup.isImageName("cue") && !Ps1Setup.isImageName("a.zip"), "otherNamesAreNotImages");

        // what a pick means: the descriptor stands for its image, a bin for its cue
        File d = dir();
        File mdf = touch(d, "g.mdf"), mds = touch(d, "g.mds");
        check(name(Ps1Setup.normalizePicked(mds)).equals("g.mdf"), "mdsPickMeansItsMdf");
        File cue = touch(d, "h.cue"), bin = touch(d, "h.bin");
        check(name(Ps1Setup.normalizePicked(bin)).equals("h.cue"), "binPickMeansItsCue");
        File lonely = touch(d, "x.bin");
        check(name(Ps1Setup.normalizePicked(lonely)).equals("x.bin"), "binWithoutCueStaysABin");
        check(name(Ps1Setup.normalizePicked(cue)).equals("h.cue") && name(Ps1Setup.normalizePicked(mdf)).equals("g.mdf"), "cueAndMdfStay");
        File mdsAlone = touch(dir(), "only.mds");
        check(Ps1Setup.normalizePicked(mdsAlone) == null, "mdsWithoutMdfIsRejected");

        // the folder search: cue or chd first, then mdf with mds, then a bare iso or bin
        File f = dir();
        touch(f, "a.iso");
        check(name(Ps1DiscConverter.findDisc(f)).equals("a.iso"), "bareIsoIsFoundLast");
        touch(f, "b.mdf"); touch(f, "b.mds");
        check(name(Ps1DiscConverter.findDisc(f)).equals("b.mdf"), "mdfBeatsIso");
        touch(f, "c.cue");
        check(name(Ps1DiscConverter.findDisc(f)).equals("c.cue"), "cueBeatsMdf");
        File deep = dir();
        touch(deep, "HP1 disc/game.chd");
        check(Ps1DiscConverter.findDisc(deep) == null, "oneLevelSearchDoesNotLookInside");
        check(name(Ps1DiscConverter.findDisc(deep, 1)).equals("game.chd"), "searchLooksOneFolderDeeper");
        check(Ps1DiscConverter.findDisc(deep, 0) == null, "depthZeroIsTheFolderOnly");

        // which disc is used: a picked file, else a picked folder, else the default place
        File pickedDisc = touch(dir(), "mine.cue");
        File pickedFolder = dir(); touch(pickedFolder, "folder.cue");
        File defaultFolder = dir(); touch(defaultFolder, "default.cue");
        check(name(Ps1Setup.resolveDisc(pickedDisc.getPath(), pickedFolder.getPath(), defaultFolder)).equals("mine.cue"), "pickedFileWins");
        check(name(Ps1Setup.resolveDisc(null, pickedFolder.getPath(), defaultFolder)).equals("folder.cue"), "pickedFolderNext");
        check(name(Ps1Setup.resolveDisc(null, null, defaultFolder)).equals("default.cue"), "defaultPlaceLast");
        check(name(Ps1Setup.resolveDisc(new File(pickedDisc.getParentFile(), "gone.cue").getPath(), pickedFolder.getPath(), defaultFolder)).equals("folder.cue"),
                "aPickedFileThatIsGoneFallsBack");
        check(Ps1Setup.resolveDisc(null, dir().getPath(), dir()) == null, "nothingFoundIsNull");
        check(name(Ps1Setup.resolveDisc(null, new File(pickedFolder, "nope").getPath(), defaultFolder)).equals("default.cue"), "aMissingPickedFolderFallsBack");

        // the default folders of the two games
        check(Ps1Setup.defaultDir("hp1").getPath().replace('\\', '/').equals("/sdcard/FlipendoPS1/HP1"), "defaultFolderHp1");
        check(Ps1Setup.defaultDir("hp2").getPath().replace('\\', '/').equals("/sdcard/FlipendoPS1/HP2"), "defaultFolderHp2");
        check(Ps1Setup.defaultDir("anything").getPath().replace('\\', '/').equals("/sdcard/FlipendoPS1/HP1"), "unknownGameIsHp1");

        // a BIOS file must be 512 KB
        File bios = new File(dir(), "bios.bin");
        Files.write(bios.toPath(), new byte[512 * 1024]);
        check(Ps1Setup.isBios(bios) && !Ps1Setup.isBios(touch(dir(), "small.bin")) && !Ps1Setup.isBios(new File("nope")), "biosIs512KB");

        // the resolution scale is kept between 1 and 4
        check(Ps1Setup.clampScale(0) == 1 && Ps1Setup.clampScale(2) == 2 && Ps1Setup.clampScale(9) == 4, "scaleIsClamped");

        System.out.println(failures == 0 ? "ALL PASS" : failures + " FAILED");
        System.exit(failures == 0 ? 0 : 1);
    }
}
