package io.github.flipendo.spike;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;

/** Desktop tests of IniRepair (no Android): android/tests/run.sh compiles and runs them. */
public final class IniRepairTest {
    private static int failures = 0;

    private static void check(boolean ok, String name) {
        System.out.println((ok ? "PASS " : "FAIL ") + name);
        if (!ok) failures++;
    }

    private static void write(File f, String text) throws IOException {
        f.getParentFile().mkdirs();
        Files.write(f.toPath(), text.getBytes(StandardCharsets.ISO_8859_1));
    }

    private static String read(File f) throws IOException {
        return new String(Files.readAllBytes(f.toPath()), StandardCharsets.ISO_8859_1);
    }

    static final String DEFAULT = "; HP2 default\r\n[Engine.Engine]\r\nLanguage=int\r\nGameRenderDevice=D3DDrv.D3DRenderDevice\r\n"
            + "[Core.System]\r\nPaths=../System/*.u\r\nPaths=../Maps/*.unr\r\nPaths=../Textures/*.utx\r\nPaths=../Sounds/*.uax\r\n"
            + "[WinDrv.WindowsClient]\r\nMinDesiredFrameRate=119.0\r\nBrightness=0.5\r\n";

    public static void main(String[] args) throws Exception {
        File dir = Files.createTempDirectory("inirepair").toFile();
        File system = new File(dir, "System");
        File def = new File(system, "default.ini");
        write(def, DEFAULT);

        // An SE file the engine wrote fine (with the player's own brightness and language, and the engine's section): untouched.
        File se = new File(system, "SE-Game.ini");
        String good = "[Engine.Engine]\nLanguage=rus\nGameRenderDevice=D3DDrv.D3DRenderDevice\n\n[Core.System]\nPaths=../System/*.u\n"
                + "Paths=../Maps/*.unr\nPaths=../Textures/*.utx\nPaths=../Sounds/*.uax\n\n[WinDrv.WindowsClient]\nMinDesiredFrameRate=119.0\n"
                + "Brightness=0.8\n\n[Engine.SurrealClient]\nTextureDetail=High\nFullscreenViewportX=1920\n\n";
        write(se, good);
        List<String> fixed = IniRepair.repairHp2(dir);
        check(fixed.isEmpty(), "a good SE-Game.ini is left alone: " + fixed);
        check(read(se).equals(good), "and not rewritten");
        check(!new File(system, "SE-Game.ini.flipendo-bak").exists(), "and no backup made");

        // Paths blanked and lost, a section gone, a value blanked, the engine's section with junk in it.
        write(se, "[Engine.Engine]\nLanguage=rus\nGameRenderDevice=\n\n[Core.System]\nPaths=../System/*.u\nPaths=\nPaths=\n\n"
                + "[Engine.SurrealClient]\nTextureDetail=ÿþ\nFullscreenViewportX=abc\nFullscreenViewportY=1080\nDecals=True\n\n");
        fixed = IniRepair.repairHp2(dir);
        String now = read(se);
        List<IniRepair.Section> s = IniRepair.parse(now);
        IniRepair.Section paths = IniRepair.find(s, "Core.System");
        check(paths != null && paths.values("Paths").size() == 4 && !paths.values("Paths").contains(""),
                "every original Paths= is back and the blank ones are gone: " + (paths == null ? null : paths.values("Paths")));
        check(IniRepair.find(s, "WinDrv.WindowsClient") != null && "119.0".equals(IniRepair.find(s, "WinDrv.WindowsClient").values("MinDesiredFrameRate").get(0)),
                "a lost section comes back from the original");
        check("D3DDrv.D3DRenderDevice".equals(IniRepair.find(s, "Engine.Engine").values("GameRenderDevice").get(0)), "a blanked value gets the original's");
        check("rus".equals(IniRepair.find(s, "Engine.Engine").values("Language").get(0)), "the player's language stays");
        IniRepair.Section client = IniRepair.find(s, "Engine.SurrealClient");
        check(client.values("TextureDetail").isEmpty() && client.values("FullscreenViewportX").isEmpty(), "junk in the engine's section goes");
        check("1080".equals(client.values("FullscreenViewportY").get(0)) && "True".equals(client.values("Decals").get(0)), "its good values stay");
        check(new File(system, "SE-Game.ini.flipendo-bak").isFile(), "the damaged file is kept as a backup");
        check(IniRepair.repairHp2(dir).isEmpty(), "a repaired file needs nothing on the next start: " + IniRepair.repairHp2(dir));

        // Not text any more: the original, with the player's language when it can still be read.
        write(se, "\0\0\0garbage");
        fixed = IniRepair.repairHp2(dir);
        check(!fixed.isEmpty() && IniRepair.parse(read(se)).size() == 3, "a binary SE file is replaced by the original: " + fixed);
        write(se, "[Some.Thing]\nA=1\n[Engine.Engine]\nLanguage=rus\n");
        IniRepair.repairHp2(dir);
        check(IniRepair.find(IniRepair.parse(read(se)), "Core.System") != null, "a file with the original's sections partly missing is filled in");

        // No SE file yet (the first start): nothing to do, nothing made.
        se.delete();
        check(IniRepair.repairHp2(dir).isEmpty() && !se.exists(), "no SE file: nothing is made");

        // The real game's default.ini, when it is here: the engine's rewrite of it (no comments, \n) needs no repair.
        File retail = new File("../eagames/hp2/System/default.ini");
        if (retail.isFile()) {
            List<IniRepair.Section> r = IniRepair.parse(read(retail));
            List<IniRepair.Section> copy = IniRepair.parse(IniRepair.write(r));
            check(IniRepair.repairSections(copy, r).isEmpty(), "HP2's own default.ini, written out again, needs no repair");
        }

        System.out.println(failures == 0 ? "IniRepairTest: all passed" : "IniRepairTest: " + failures + " failed");
        if (failures != 0) System.exit(1);
    }
}
