package com.ut2004.android;

import org.libsdl.app.SDLActivity;

/**
 * Harry Potter 3's activity: SDL's activity running the Rust engine (libhp3app.so, entry point SDL_main). The launcher
 * (SetupActivity) starts it in its own process with the game folder in the DATA_ROOT extra; the engine receives that
 * folder as its first argument.
 */
public class GameActivity extends SDLActivity {
    /** The game folder (the one that holds System/Core.u). */
    public static final String EXTRA_DATA_ROOT = "com.ut2004.android.DATA_ROOT";
    /** Optional: the level to open instead of the front end. */
    public static final String EXTRA_MAP = "com.ut2004.android.MAP";

    /** The engine's flag for an open menu (the touch controls step aside for it). */
    static native boolean nativeInMenu();

    /** A command for the engine's game loop: "quicksave", "quickload", "level <map>" or "cheat <name>". */
    static native void nativeCommand(String command);

    /** The maps in the game folder, sorted, without their extension (the level select list). */
    String[] mapNames() {
        String root = getIntent() != null ? getIntent().getStringExtra(EXTRA_DATA_ROOT) : null;
        java.io.File dir = null;
        if (root != null) {
            for (String name : new String[] { "Maps", "maps" }) {
                java.io.File d = new java.io.File(root, name);
                if (d.isDirectory()) { dir = d; break; }
            }
        }
        java.util.ArrayList<String> maps = new java.util.ArrayList<String>();
        java.io.File[] files = dir != null ? dir.listFiles() : null;
        if (files != null) {
            for (java.io.File f : files) {
                String n = f.getName();
                if (n.toLowerCase(java.util.Locale.ROOT).endsWith(".unr")) maps.add(n.substring(0, n.length() - 4));
            }
        }
        java.util.Collections.sort(maps, String.CASE_INSENSITIVE_ORDER);
        return maps.toArray(new String[0]);
    }

    @Override
    protected void onCreate(android.os.Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        addContentView(new Hp3TouchOverlay(this),
            new android.view.ViewGroup.LayoutParams(android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.MATCH_PARENT));
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "hp3app" };
    }

    @Override
    protected String[] getArguments() {
        String root = getIntent() != null ? getIntent().getStringExtra(EXTRA_DATA_ROOT) : null;
        String map = getIntent() != null ? getIntent().getStringExtra(EXTRA_MAP) : null;
        if (root == null || root.isEmpty()) {
            return new String[0];
        }
        return map == null || map.isEmpty() ? new String[] { root } : new String[] { root, map };
    }
}
