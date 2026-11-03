package io.github.flipendo.spike;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Desktop tests of Hp3Setup (no Android): android/tests/run.sh compiles and runs them. */
public final class Hp3SetupTest {
    private static int failures = 0;

    private static void check(boolean ok, String name) {
        System.out.println((ok ? "PASS " : "FAIL ") + name);
        if (!ok) failures++;
    }

    private static File tempDir() throws IOException {
        return Files.createTempDirectory("hp3setup").toFile();
    }

    private static void touch(File f) throws IOException {
        f.getParentFile().mkdirs();
        Files.write(f.toPath(), new byte[0]);
    }

    private static void write(File f, String text) throws IOException {
        f.getParentFile().mkdirs();
        Files.write(f.toPath(), text.getBytes(StandardCharsets.ISO_8859_1));
    }

    private static String read(File f) throws IOException {
        return new String(Files.readAllBytes(f.toPath()), StandardCharsets.ISO_8859_1);
    }

    static final String PC_INI = "[Engine.Engine]\r\nRenderDevice=D3DDrv.D3DRenderDevice\r\nViewportManager=WinDrv.WindowsClient\r\n"
            + "AudioDevice=ALAudio.ALAudioSubsystem\r\n[Other]\r\nRenderDevice=Keep\r\n";
    static final String PHONE_INI = "[Engine.Engine]\r\nRenderDevice=GL4ESDrv.GL4ESRenderDevice\r\nViewportManager=SDL2Drv.SDL2Client\r\n"
            + "AudioDevice=ALAudio.ALAudioSubsystem\r\n[Other]\r\nRenderDevice=Keep\r\n";

    public static void main(String[] args) throws Exception {
        File x = tempDir();
        touch(new File(x, "System/Core.u"));
        // The launcher languages the game has interface texts for (Tr.CODES): ours are German (ger) and Spanish (est) too.
        check("ger".equals(Hp3Setup.gameLanguage("de")) && "est".equals(Hp3Setup.gameLanguage("es")) && "int".equals(Hp3Setup.gameLanguage("en")) && "lav".equals(Hp3Setup.gameLanguage("lv")) && Hp3Setup.gameLanguage("ru") == null, "gameLanguages");
        check(x.equals(Hp3Setup.findGame(x, 2)), "findGameUpperSystem");

        x = tempDir();
        touch(new File(x, "hp3/system/Core.u"));
        check(new File(x, "hp3").equals(Hp3Setup.findGame(x, 2)), "findGameLowercaseSystem");

        x = tempDir();
        touch(new File(x, "System/HP.exe"));
        check(Hp3Setup.findGame(x, 2) == null, "findGameNone");

        // The self pack's system/ has no .exe (build-apk.sh strips Windows executables): Core.u, not hppoa.exe, is
        // what has to be there, or a self-pack install can never be found again after it unpacks.
        x = tempDir();
        touch(new File(x, "System/Core.u"));
        touch(new File(x, "System/Engine.u"));
        check(x.equals(Hp3Setup.findGame(x, 2)), "findGameSelfPackShape");

        check(PHONE_INI.equals(Hp3Setup.patchIni(PC_INI)), "patchIniReplacesDrivers");

        check(Hp3Setup.patchIni(Hp3Setup.patchIni(PC_INI)).equals(Hp3Setup.patchIni(PC_INI)), "patchIniIdempotent");

        x = tempDir();
        write(new File(x, "System/Default.ini"), PC_INI);
        Hp3Setup.prepareIni(x);
        File ini = new File(x, "System/UT2004.ini");
        check(ini.isFile() && PHONE_INI.equals(read(ini)), "prepareIniWritesFromDefault");

        x = tempDir();
        write(new File(x, "system/Default.ini"), PC_INI);
        Hp3Setup.prepareIni(x);
        ini = new File(x, "system/UT2004.ini");
        check(ini.isFile() && PHONE_INI.equals(read(ini)), "prepareIniLowercaseSystem");

        x = tempDir();
        write(new File(x, "System/Default.ini"), PC_INI);
        write(new File(x, "System/UT2004.ini"), PC_INI);
        Hp3Setup.prepareIni(x);
        check(PHONE_INI.equals(read(new File(x, "System/UT2004.ini"))), "prepareIniFixesPcIni");

        x = tempDir();
        write(new File(x, "System/Default.ini"), PC_INI);
        ini = new File(x, "System/UT2004.ini");
        String mine = PHONE_INI + "FpsLogSeconds=5\r\n";
        write(ini, mine);
        long stamp = 1000000000000L;
        ini.setLastModified(stamp);
        Hp3Setup.prepareIni(x);
        check(mine.equals(read(ini)) && ini.lastModified() == stamp, "prepareIniKeepsPhoneIni");

        System.out.println(failures == 0 ? "all passed" : failures + " failed");
        System.exit(failures == 0 ? 0 : 1);
    }
}
