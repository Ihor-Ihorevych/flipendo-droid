package com.ut2004.android;

import io.github.flipendo.spike.FlipendoActivity;

/**
 * Harry Potter 3's activity: Flipendo's own activity (its touch controls, settings panel and debug menu) running the Rust
 * engine (libhp3app.so, entry point SDL_main). The launcher (SetupActivity) starts it in its own process with the game
 * folder in the DATA_ROOT extra; the engine receives that folder as its first argument. The engine answers the natives
 * FlipendoActivity declares (menu, cutscene, quick save, debug commands...).
 */
public class GameActivity extends FlipendoActivity {
    /** The game folder (the one that holds System/Core.u). */
    public static final String EXTRA_DATA_ROOT = "com.ut2004.android.DATA_ROOT";
    /** Optional: the level to open instead of the front end. */
    public static final String EXTRA_MAP = "com.ut2004.android.MAP";

    /** How many quick saves the engine has written; the activity shows the toast when it changes. */
    static native int nativeSavedCount();

    private int saved;
    private final Runnable savePoll = new Runnable() {
        @Override public void run() {
            int now = nativeSavedCount();
            if (now != saved) {
                saved = now;
                onSaved(0);
            }
            getWindow().getDecorView().postDelayed(this, 300);
        }
    };

    @Override
    protected void onCreate(android.os.Bundle savedInstanceState) {
        // The renderer picked in the launcher or the settings panel, for the engine (HP3_BACKEND, read when it opens the
        // GPU): "opengl" = OpenGL ES only; otherwise Vulkan, falling back to OpenGL ES when this phone's Vulkan fails.
        // Set before the engine starts (super.onCreate); a value already in the environment (adb debugging) is kept.
        try {
            android.system.Os.setenv("HP3_BACKEND", hp3Backend(), false);
        } catch (Exception e) {
            android.util.Log.w("flipendo", "renderer choice: " + e);
        }
        super.onCreate(savedInstanceState);
        saved = nativeSavedCount();
        getWindow().getDecorView().postDelayed(savePoll, 300);
    }

    @Override
    public void onSaved(final int slot) {
        runOnUiThread(new Runnable() {
            @Override public void run() {
                android.widget.Toast.makeText(GameActivity.this, "Quick save written", android.widget.Toast.LENGTH_SHORT).show();
            }
        });
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "hp3app" };
    }

    @Override
    protected boolean isHp3() {
        return true;
    }

    private String root() {
        return getIntent() != null ? getIntent().getStringExtra(EXTRA_DATA_ROOT) : null;
    }

    @Override
    protected String gameDir() {
        String root = root();
        return root != null && !root.isEmpty() ? root : super.gameDir();
    }

    @Override
    protected String[] getArguments() {
        String root = root();
        String map = getIntent() != null ? getIntent().getStringExtra(EXTRA_MAP) : null;
        if (root == null || root.isEmpty()) {
            return new String[0];
        }
        return map == null || map.isEmpty() ? new String[] { root } : new String[] { root, map };
    }

    /** The engine state: bit 0 the characters' shadow is on, bit 1 Carpe is selected, bit 2 the player is Ron, bit 3 the shadow's
     *  High quality. */
    static native int nativeGfxState();

    /** The Carpe button is shown only while the player is Ron: bit 2 of the engine's state. */
    @Override
    protected boolean hp3PlayerIsRon() {
        try {
            return (nativeGfxState() & 4) != 0;
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
    }

    /** The Carpe button is lit while Carpe is selected: bit 1 of the engine's state. */
    @Override
    protected boolean hp3CarpeOn() {
        try {
            return (nativeGfxState() & 2) != 0;
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
    }

    /** HP3's graphics options (Settings > Game > Graphics, the toolbar's graphics button): the engine's own Settings view, the
     *  same screen the PC has (apps/hp3/src/settingsmenu.rs), opened on its Graphics tab. While it is open the engine says a menu
     *  is up, so the touch controls step aside and a finger taps its entries. */
    @Override
    protected void openHp3Graphics() {
        debugCommand("menu graphics");
    }

    /** HP3's debug menu (the settings panel's Debug row): the engine's Settings view on its Cheats tab (its other tabs hold the
     *  level list, Quidditch and the debug actions). */
    @Override
    protected void openHp3Debug() {
        debugCommand("menu cheats");
    }
}
