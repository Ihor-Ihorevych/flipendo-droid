package io.github.flipendo.spike;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/**
 * Harry Potter 3's game folder for the launcher: finding a copied install (System/hppoa.exe) and the phone ini HP3's
 * engine reads (System/UT2004.ini): HP3's own Default.ini with the Android renderer and window drivers. Plain Java, so
 * android/tests/run.sh tests it on the desktop.
 */
final class Hp3Setup {
    static final String EXE = "hppoa.exe";
    static final String RENDER_DEVICE = "GL4ESDrv.GL4ESRenderDevice";
    static final String VIEWPORT_MANAGER = "SDL2Drv.SDL2Client";

    private Hp3Setup() {
    }

    /** dir, or a folder up to depth levels below it, holding System/hppoa.exe (system/ in the retail install); else null. */
    static File findGame(File dir, int depth) {
        if (new File(dir, "System/" + EXE).isFile() || new File(dir, "system/" + EXE).isFile()) {
            return dir;
        }
        File[] children = depth > 0 ? dir.listFiles() : null;
        if (children != null) {
            for (File child : children) {
                if (child.isDirectory() && !child.getName().startsWith(".")) {
                    File found = findGame(child, depth - 1);
                    if (found != null) {
                        return found;
                    }
                }
            }
        }
        return null;
    }

    /** The game's System folder: System, or system as the retail install names it. */
    static File systemDir(File game) {
        File upper = new File(game, "System");
        return upper.isDirectory() ? upper : new File(game, "system");
    }

    /** The ini with [Engine.Engine]'s RenderDevice and ViewportManager set to the Android drivers; all else as it was. */
    static String patchIni(String ini) {
        StringBuilder out = new StringBuilder(ini.length() + 64);
        boolean engine = false;
        int start = 0;
        while (start < ini.length()) {
            int end = ini.indexOf('\n', start);
            end = end < 0 ? ini.length() : end + 1;
            String line = ini.substring(start, end);
            String body = line.replaceAll("[\r\n]+$", "");
            String newline = line.substring(body.length());
            String trimmed = body.trim();
            if (trimmed.startsWith("[")) {
                engine = trimmed.equalsIgnoreCase("[Engine.Engine]");
            } else if (engine && keyIs(trimmed, "RenderDevice")) {
                body = "RenderDevice=" + RENDER_DEVICE;
            } else if (engine && keyIs(trimmed, "ViewportManager")) {
                body = "ViewportManager=" + VIEWPORT_MANAGER;
            }
            out.append(body).append(newline);
            start = end;
        }
        return out.toString();
    }

    /** True when patchIni would change the ini. */
    static boolean needsPatch(String ini) {
        return !patchIni(ini).equals(ini);
    }

    /** Writes System/UT2004.ini from Default.ini when it is missing, fixes one that names other drivers, else leaves it. */
    static void prepareIni(File game) throws IOException {
        File system = systemDir(game);
        File ini = new File(system, "UT2004.ini");
        if (ini.isFile()) {
            String text = read(ini);
            if (needsPatch(text)) {
                write(ini, patchIni(text));
            }
            return;
        }
        write(ini, patchIni(read(new File(system, "Default.ini"))));
    }

    private static boolean keyIs(String line, String key) {
        int eq = line.indexOf('=');
        return eq > 0 && line.substring(0, eq).trim().equalsIgnoreCase(key);
    }

    // The game's ini files are single-byte text; ISO-8859-1 keeps every byte as it is.
    private static String read(File f) throws IOException {
        return new String(Files.readAllBytes(f.toPath()), StandardCharsets.ISO_8859_1);
    }

    private static void write(File f, String text) throws IOException {
        Files.write(f.toPath(), text.getBytes(StandardCharsets.ISO_8859_1));
    }
}
