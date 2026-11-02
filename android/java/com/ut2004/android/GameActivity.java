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
