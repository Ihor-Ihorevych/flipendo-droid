package io.github.flipendo.spike;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Repairs the ini files SurrealEngine writes for HP2 (System/SE-Game.ini and System/SE-User.ini) against the game's own
 * shipped ones (default.ini and DefUser.ini), before every start of the game.
 *
 * Why: the first start reads default.ini, and on exit the engine writes everything out as SE-Game.ini / SE-User.ini, which
 * every later start reads instead. A player's Samsung ended up with a damaged SE file and the game lost its textures; a hand
 * edit of the file fixed it. The original files are known good, so anything the SE files lack or have blanked is put back
 * from them before the engine reads them:
 *
 *  - a file that is not text any more (NUL bytes, or none of the original's sections) is replaced by the original, keeping
 *    its Language= choice;
 *  - a section of the original that is missing is added back;
 *  - a key of the original that is missing, or whose value was blanked, gets the original's value; the player's own
 *    non-empty values (brightness, resolution, keys...) are never touched;
 *  - a key the original lists several times (Paths=, where the game finds its textures, sounds and maps) gets every
 *    original value back, and loses blank copies;
 *  - the engine's own [Engine.SurrealClient] section loses values that are not of their type (the engine then uses its
 *    defaults).
 *
 * A file that was changed is kept as it was in "&lt;name&gt;.flipendo-bak" first. Plain Java (no Android), so the desktop tests
 * (android/tests/run.sh) run it.
 */
final class IniRepair {
    private IniRepair() {
    }

    /** One ini section: its name as written and its key=value lines in order (keys keep their spelling). */
    static final class Section {
        final String name;
        final List<String[]> entries = new ArrayList<>();

        Section(String name) {
            this.name = name;
        }

        List<String> values(String key) {
            List<String> out = new ArrayList<>();
            for (String[] e : entries) {
                if (e[0].equalsIgnoreCase(key)) out.add(e[1]);
            }
            return out;
        }

        int count(String key) {
            return values(key).size();
        }
    }

    /** Parses ini text the way the engine does: [Section] lines, key=value lines, ; and // comments and blank lines skipped. */
    static List<Section> parse(String text) {
        List<Section> out = new ArrayList<>();
        Section current = null;
        for (String raw : text.split("\r\n|\r|\n")) {
            String line = raw.trim();
            if (line.isEmpty() || line.startsWith(";") || line.startsWith("//")) continue;
            if (line.length() >= 2 && line.charAt(0) == '[' && line.charAt(line.length() - 1) == ']') {
                String name = line.substring(1, line.length() - 1);
                current = find(out, name);
                if (current == null) {
                    current = new Section(name);
                    out.add(current);
                }
                continue;
            }
            int eq = line.indexOf('=');
            if (eq <= 0 || current == null) continue;
            current.entries.add(new String[] { line.substring(0, eq).trim(), line.substring(eq + 1).trim() });
        }
        return out;
    }

    static Section find(List<Section> sections, String name) {
        for (Section s : sections) {
            if (s.name.equalsIgnoreCase(name)) return s;
        }
        return null;
    }

    static String write(List<Section> sections) {
        StringBuilder b = new StringBuilder();
        for (Section s : sections) {
            b.append('[').append(s.name).append("]\n");
            for (String[] e : s.entries) b.append(e[0]).append('=').append(e[1]).append('\n');
            b.append('\n');
        }
        return b.toString();
    }

    /** Whether the text is no ini any more: NUL bytes (a half-written or binary file), or none of the reference's sections. */
    static boolean unreadable(String text, List<Section> parsed, List<Section> reference) {
        if (text.indexOf('\0') >= 0) return true;
        if (reference.isEmpty()) return false;
        for (Section r : reference) {
            if (find(parsed, r.name) != null) return false;
        }
        return true;
    }

    private static boolean sameValue(String a, String b) {
        return a.replace('\\', '/').equalsIgnoreCase(b.replace('\\', '/'));
    }

    /**
     * Puts back into `target` what `reference` has and `target` lost (see the class comment). Returns what was fixed, one
     * line each; nothing when the file is fine.
     */
    static List<String> repairSections(List<Section> target, List<Section> reference) {
        List<String> fixed = new ArrayList<>();
        for (Section r : reference) {
            Section t = find(target, r.name);
            if (t == null) {
                t = new Section(r.name);
                for (String[] e : r.entries) t.entries.add(new String[] { e[0], e[1] });
                target.add(t);
                fixed.add("[" + r.name + "] was missing: added");
                continue;
            }
            // the keys of the reference section, each once, in order
            List<String> keys = new ArrayList<>();
            for (String[] e : r.entries) {
                boolean seen = false;
                for (String k : keys) seen |= k.equalsIgnoreCase(e[0]);
                if (!seen) keys.add(e[0]);
            }
            for (String key : keys) {
                List<String> want = r.values(key);
                if (want.size() > 1) {
                    // a list (Paths=): blank copies go, every original value is there
                    for (int i = t.entries.size() - 1; i >= 0; i--) {
                        String[] e = t.entries.get(i);
                        if (e[0].equalsIgnoreCase(key) && e[1].isEmpty()) {
                            t.entries.remove(i);
                            fixed.add("[" + r.name + "] " + key + "= (blank): removed");
                        }
                    }
                    int last = -1;
                    for (int i = 0; i < t.entries.size(); i++) {
                        if (t.entries.get(i)[0].equalsIgnoreCase(key)) last = i;
                    }
                    for (String v : want) {
                        boolean present = false;
                        for (String have : t.values(key)) present |= sameValue(have, v);
                        if (present) continue;
                        int at = last >= 0 ? last + 1 : t.entries.size();
                        t.entries.add(at, new String[] { key, v });
                        last = at;
                        fixed.add("[" + r.name + "] " + key + "=" + v + " was missing: added");
                    }
                    continue;
                }
                String v = want.get(0);
                List<String> have = t.values(key);
                if (have.isEmpty()) {
                    t.entries.add(new String[] { key, v });
                    fixed.add("[" + r.name + "] " + key + " was missing: " + v);
                    continue;
                }
                // a single value written more than once: one stays, the first non-blank one (else the first)
                String[] keep = null;
                for (String[] e : t.entries) {
                    if (e[0].equalsIgnoreCase(key) && !e[1].isEmpty()) {
                        keep = e;
                        break;
                    }
                }
                if (keep == null) keep = t.entries.get(firstIndex(t, key));
                for (int i = t.entries.size() - 1; i >= 0; i--) {
                    String[] e = t.entries.get(i);
                    if (e[0].equalsIgnoreCase(key) && e != keep) {
                        t.entries.remove(i);
                        fixed.add("[" + r.name + "] " + key + "=" + e[1] + ": extra copy removed");
                    }
                }
                if (keep[1].isEmpty() && !v.isEmpty()) {
                    // blanked: the original's value
                    keep[1] = v;
                    fixed.add("[" + r.name + "] " + key + " was blank: " + v);
                }
            }
        }
        fixed.addAll(cleanSurrealClient(target));
        return fixed;
    }

    private static int firstIndex(Section s, String key) {
        for (int i = 0; i < s.entries.size(); i++) {
            if (s.entries.get(i)[0].equalsIgnoreCase(key)) return i;
        }
        return -1;
    }

    private static final String[] INT_KEYS = { "WindowedViewportX", "WindowedViewportY", "WindowedColorBits", "FullscreenViewportX",
        "FullscreenViewportY", "FullscreenColorBits", "MinDesiredFrameRate" };
    private static final String[] BOOL_KEYS = { "StartupFullscreen", "UseJoystick", "UseDirectInput", "Decals", "NoDynamicLights" };
    private static final String[] WORD_KEYS = { "TextureDetail", "SkinDetail" };

    /** The engine's own client section: a value that is not of its key's type goes (the engine then uses its default). */
    static List<String> cleanSurrealClient(List<Section> sections) {
        List<String> fixed = new ArrayList<>();
        Section s = find(sections, "Engine.SurrealClient");
        if (s == null) return fixed;
        for (int i = s.entries.size() - 1; i >= 0; i--) {
            String[] e = s.entries.get(i);
            boolean ok = true;
            if (in(INT_KEYS, e[0])) ok = e[1].matches("-?\\d+(\\.\\d+)?");
            else if (in(BOOL_KEYS, e[0])) ok = e[1].equalsIgnoreCase("True") || e[1].equalsIgnoreCase("False");
            else if (in(WORD_KEYS, e[0])) ok = e[1].matches("[A-Za-z]+");
            else if (e[0].equalsIgnoreCase("Brightness")) ok = e[1].matches("\\d+(\\.\\d+)?");
            if (!ok) {
                s.entries.remove(i);
                fixed.add("[" + s.name + "] " + e[0] + "=" + e[1] + " is not a valid value: removed");
            }
        }
        return fixed;
    }

    private static boolean in(String[] keys, String key) {
        for (String k : keys) {
            if (k.equalsIgnoreCase(key)) return true;
        }
        return false;
    }

    /** The Language= value of an ini's [Engine.Engine] section, or null. */
    private static String language(List<Section> sections) {
        Section e = find(sections, "Engine.Engine");
        if (e == null) return null;
        List<String> v = e.values("Language");
        return v.isEmpty() || v.get(0).isEmpty() ? null : v.get(0);
    }

    /**
     * Repairs `target` against `reference` (both files). Returns what was fixed; nothing when `target` does not exist (the
     * engine then starts from the original itself), when there is no reference, or when the file is fine.
     */
    static List<String> repairFile(File target, File reference) throws IOException {
        List<String> fixed = new ArrayList<>();
        if (target == null || reference == null || !target.isFile() || !reference.isFile()) return fixed;
        String text = read(target);
        List<Section> ref = parse(read(reference));
        List<Section> parsed = parse(text);
        List<Section> result;
        if (unreadable(text, parsed, ref)) {
            // not an ini any more: the original, with the language the player had
            String lang = text.indexOf('\0') >= 0 ? null : language(parsed);
            result = parse(read(reference));
            if (lang != null) {
                Section e = find(result, "Engine.Engine");
                if (e != null) {
                    for (String[] entry : e.entries) {
                        if (entry[0].equalsIgnoreCase("Language")) entry[1] = lang;
                    }
                }
            }
            fixed.add(target.getName() + " was unreadable: replaced by " + reference.getName());
        } else {
            result = parsed;
            fixed.addAll(repairSections(result, ref));
        }
        if (fixed.isEmpty()) return fixed;
        copy(target, new File(target.getPath() + ".flipendo-bak"));
        FileOutputStream out = new FileOutputStream(target);
        try {
            out.write(write(result).getBytes(StandardCharsets.ISO_8859_1));
        } finally {
            out.close();
        }
        return fixed;
    }

    /** HP2: SE-Game.ini against default.ini and SE-User.ini against DefUser.ini, in the game folder's System. */
    static List<String> repairHp2(File gameDir) {
        List<String> fixed = new ArrayList<>();
        File system = child(gameDir, "System");
        if (system == null) return fixed;
        String[][] pairs = { { "SE-Game.ini", "default.ini" }, { "SE-User.ini", "DefUser.ini" } };
        for (String[] p : pairs) {
            File target = child(system, p[0]);
            try {
                for (String f : repairFile(target, child(system, p[1]))) fixed.add(p[0] + ": " + f);
            } catch (IOException e) {
                fixed.add(p[0] + ": could not be checked (" + e + ")");
            }
        }
        return fixed;
    }

    /** A file or folder in `dir` by name, whatever its case (Android's storage is case-sensitive); null when there is none. */
    static File child(File dir, String name) {
        if (dir == null) return null;
        File exact = new File(dir, name);
        if (exact.exists()) return exact;
        String[] names = dir.list();
        if (names == null) return null;
        for (String n : names) {
            if (n.toLowerCase(Locale.ROOT).equals(name.toLowerCase(Locale.ROOT))) return new File(dir, n);
        }
        return null;
    }

    private static String read(File f) throws IOException {
        InputStream in = new FileInputStream(f);
        try {
            byte[] buf = new byte[(int) Math.max(0, f.length())];
            int at = 0;
            while (at < buf.length) {
                int n = in.read(buf, at, buf.length - at);
                if (n < 0) break;
                at += n;
            }
            return new String(buf, 0, at, StandardCharsets.ISO_8859_1);
        } finally {
            in.close();
        }
    }

    private static void copy(File from, File to) throws IOException {
        InputStream in = new FileInputStream(from);
        try {
            FileOutputStream out = new FileOutputStream(to);
            try {
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            } finally {
                out.close();
            }
        } finally {
            in.close();
        }
    }
}
