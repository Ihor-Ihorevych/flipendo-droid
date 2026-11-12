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

    /** The maps in the game folder, sorted, without their extension (the level select list). */
    private String[] mapNames() {
        java.io.File dir = null;
        for (String name : new String[] { "Maps", "maps" }) {
            java.io.File d = new java.io.File(gameDir(), name);
            if (d.isDirectory()) { dir = d; break; }
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

    private static final String[] DEBUG_NAMES = {
        "Go to level…", "God mode (on / off)", "Fly", "Ghost (fly through walls)", "Walk (normal)",
        "All shields, cards and pixies", "100 beans", "10 cauldron cakes (cupcakes)", "10 pumpkin pasties", "Beat the boss", "Heal", "Carpe pulls crabs (on / off)" };
    private static final String[] DEBUG_COMMANDS = {
        null, "cheat god", "cheat fly", "cheat ghost", "cheat walk", "cheat giveall", "cheat beans", "cheat cakes", "cheat pasties", "cheat beatboss", "cheat heal", "cheat carpecrab" };

    /** The engine state: bit 0 the characters' shadow is on, bit 1 Carpe is selected. */
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

    /** HP3's graphics options (Settings > Game > Graphics): the characters' shadow. A choice runs the engine's
     *  `gfx` command (it keeps the choice in its settings) and the dialog comes back showing the new state. */
    @Override
    protected void openHp3Graphics() {
        runOnUiThread(new Runnable() {
            @Override public void run() {
                int state;
                try {
                    state = nativeGfxState();
                } catch (UnsatisfiedLinkError e) {
                    return;
                }
                final String[] items = { "Character shadows: " + ((state & 1) != 0 ? "On" : "Off") + "  (tap to change)" };
                new android.app.AlertDialog.Builder(GameActivity.this).setTitle("Graphics")
                    .setItems(items, new android.content.DialogInterface.OnClickListener() {
                        @Override public void onClick(android.content.DialogInterface d, int which) {
                            debugCommand("gfx shadows");
                            // the engine takes the command on its next frame: show the dialog again with the new state
                            getWindow().getDecorView().postDelayed(new Runnable() {
                                @Override public void run() { openHp3Graphics(); }
                            }, 300);
                        }
                    }).setNegativeButton("Close", null).show();
            }
        });
    }

    /** HP3's debug menu (opened from the settings panel's Debug row): go to a level, or a cheat. */
    @Override
    protected void openHp3Debug() {
        runOnUiThread(new Runnable() {
            @Override public void run() {
                new android.app.AlertDialog.Builder(GameActivity.this).setTitle("HP3 debug")
                    .setItems(DEBUG_NAMES, new android.content.DialogInterface.OnClickListener() {
                        @Override public void onClick(android.content.DialogInterface d, int which) {
                            if (DEBUG_COMMANDS[which] != null) {
                                debugCommand(DEBUG_COMMANDS[which]);
                                return;
                            }
                            final String[] maps = mapNames();
                            if (maps.length == 0) {
                                android.widget.Toast.makeText(GameActivity.this, "No maps found in " + gameDir(), android.widget.Toast.LENGTH_LONG).show();
                                return;
                            }
                            new android.app.AlertDialog.Builder(GameActivity.this).setTitle("Go to level")
                                .setItems(maps, new android.content.DialogInterface.OnClickListener() {
                                    @Override public void onClick(android.content.DialogInterface d2, int i) {
                                        debugCommand("level " + maps[i]);
                                    }
                                }).show();
                        }
                    }).show();
            }
        });
    }
}
