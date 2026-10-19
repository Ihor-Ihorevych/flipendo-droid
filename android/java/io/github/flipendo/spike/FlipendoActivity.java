package io.github.flipendo.spike;

import android.content.Intent;
import android.graphics.Canvas;
import android.net.Uri;
import android.os.Environment;
import android.provider.Settings;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import android.graphics.Paint;
import android.os.Bundle;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.RelativeLayout;
import org.libsdl.app.SDLActivity;

import java.util.HashMap;

/** Spike: SDL activity with an on-screen touch overlay that injects keys and mouse input. */
public class FlipendoActivity extends SDLActivity {
    /**
     * Ratio of the render surface to the screen (the surface is a fixed-size SurfaceView scaled up by the compositor).
     * The "Render scale" slider of the menu changes it; it is kept in the preferences.
     */
    static volatile float surfaceScale = 0.5f;
    static final float RENDER_SCALE_MIN = 0.3f, RENDER_SCALE_MAX = 1.0f;

    /** Size of the subtitles and the HUD, 1 = the default; the engine reads it (android_main.cpp, HP1Canvas.cpp). */
    static float uiScale = 1.0f;
    static final float UI_SCALE_MIN = 0.6f, UI_SCALE_MAX = 1.8f;

    static native void nativeSetUiScale(float scale);

    /** Movement control: false = a floating stick under the left thumb, true = four arrow buttons that are always visible. */
    static volatile boolean dpadMode = false;

    /** Degrees added to the game's field of view (engine side: KW::ViewFovAngle). */
    static float fovOffset = 0f;
    static final float FOV_MIN = -20f, FOV_MAX = 40f;
    /** HP's debug mode (Level Select in the main menu, debug text); off by default. */
    static boolean debugMode = false;

    static native void nativeSetFovOffset(float degrees);

    /** The game's frame limit (Settings > FPS limit; the frame limit mod, src/hp1/mods/FrameLimit.cpp). */
    static int fpsLimit = 60;
    static final int FPS_LIMIT_MIN = 25, FPS_LIMIT_MAX = 60;

    static native void nativeSetFpsLimit(int fps);

    void applyFpsLimit(int fps, boolean save) {
        fpsLimit = Math.max(FPS_LIMIT_MIN, Math.min(FPS_LIMIT_MAX, fps));
        try {
            nativeSetFpsLimit(fpsLimit);
        } catch (UnsatisfiedLinkError ignored) {
        }
        if (save) saveScales();
    }

    static native void nativeSetDebugMode(boolean on);

    /** HP's Auto Jump option (Harry jumps by himself at ledges); on by default. */
    static boolean autoJump = true;

    static native void nativeSetAutoJump(boolean on);

    /** Settings > Invert wand: the wand button's up/down aim (while the spell is held) goes the other way. */
    static boolean invertWand = false;

    void applyAutoJump(boolean on, boolean save) {
        autoJump = on;
        try {
            nativeSetAutoJump(on);
        } catch (UnsatisfiedLinkError ignored) {
        }
        if (save) saveScales();
    }

    void applyFovOffset(float degrees, boolean save) {
        fovOffset = Math.max(FOV_MIN, Math.min(FOV_MAX, degrees));
        try {
            nativeSetFovOffset(fovOffset);
        } catch (UnsatisfiedLinkError ignored) {
        }
        if (save) saveScales();
    }

    /**
     * Switches the game's language (voices, fonts, texts): the wish goes into a file, the launcher (SetupActivity, in its own
     * process) is started, and this process, with the engine in it, is killed so the game starts again from nothing.
     */
    void switchLanguage(String code) {
        if (SetupActivity.isHp2Folder(gameDir())) {
            restartWithIn(new File(gameDir()), SetupActivity.LANG_WANT, code); // HP2 keeps its own choice, in its folder
            return;
        }
        restartWith(SetupActivity.LANG_WANT, code);
    }

    static final String GRAPHICS_FIX_FILE = "/sdcard/FlipendoHP/.flipendo-gltest"; // read by the OpenGL scene shader (GLRenderDevice.cpp)

    /**
     * Settings > Graphics fix: for a GPU whose level walls and floors come out black while the characters are fine. The choice is a
     * file the OpenGL renderer reads when it starts, so the game restarts to apply it.
     */
    void openGraphicsFix() {
        final String[] names = { "Off (the normal picture)", "Without the lightmap (the walls are visible, but not lit)", "Without macro, detail and fog layers",
                "Lightmap with a plain bilinear filter", "Textures without mip levels", "Plain bilinear filter for every texture",
                "Lightmap read at level 0 (textureLod)", "Lightmap read by texel (texelFetch)", "Test: show the lightmap only", "Test: show the lightmap coordinates" };
        final String[] values = { "none", "lightmap", "extra", "lmsampler", "nomip", "plainsamplers", "lmlod", "lmfetch", "lmraw", "lmuv" };
        String current = SetupActivity.readSmallFile(new File(GRAPHICS_FIX_FILE));
        int selected = 0;
        for (int i = 0; i < values.length; i++) {
            if (values[i].equals(current)) selected = i;
        }
        final int was = selected;
        runOnUiThread(new Runnable() {
            public void run() {
                new android.app.AlertDialog.Builder(FlipendoActivity.this)
                        .setTitle("Graphics fix (the game restarts)")
                        .setSingleChoiceItems(names, was, new android.content.DialogInterface.OnClickListener() {
                            public void onClick(android.content.DialogInterface d, int which) {
                                d.dismiss();
                                if (which == was) return;
                                try {
                                    SetupActivity.writeSmallFile(new File(GRAPHICS_FIX_FILE), values[which]);
                                } catch (IOException e) {
                                    android.util.Log.e("flipendo", "could not keep the graphics fix: " + e);
                                    return;
                                }
                                restartWith(null, null);
                            }
                        }).show();
            }
        });
    }

    /** Switches a mod on or off ("none" for the originals), the same way: wish file, launcher, fresh process. */
    void switchMod(String id) {
        restartWith(SetupActivity.MOD_WANT, id);
    }

    /** The game folder this run plays (found by the launcher or picked by the player); the default place if none was passed. */
    String gameDir() {
        String dir = getIntent() != null ? getIntent().getStringExtra(SetupActivity.EXTRA_GAME_DIR) : null;
        return dir != null ? dir : GAME_DIR;
    }

    private static final int REQUEST_MOD_FILE = 11;

    /** Lets the player pick the mod's zip (or its .u files) with the system file picker. */
    void pickModFile() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        try {
            startActivityForResult(intent, REQUEST_MOD_FILE);
        } catch (RuntimeException e) {
            android.widget.Toast.makeText(this, "No file picker on this phone", android.widget.Toast.LENGTH_LONG).show();
        }
    }

    private static final int REQUEST_VOICE_DIR = 12;

    /** Lets the player pick a dubbing's folder (its Sounds folder, or a game folder with one) with the system folder picker. */
    void pickVoiceFolder() {
        try {
            startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_VOICE_DIR);
        } catch (RuntimeException e) {
            android.widget.Toast.makeText(this, "No folder picker on this phone", android.widget.Toast.LENGTH_LONG).show();
        }
    }

    /** The picked dubbing folder is imported (SetupActivity.importVoice) off the UI thread; then the game restarts with it. */
    private void importVoiceFolder(Uri tree) {
        final String folder = SetupActivity.treeUriToPath(tree);
        if (folder == null) {
            android.widget.Toast.makeText(this, "That place can't be read as a folder: pick a folder in the phone's storage",
                    android.widget.Toast.LENGTH_LONG).show();
            return;
        }
        android.widget.Toast.makeText(this, "Importing the dubbing...", android.widget.Toast.LENGTH_SHORT).show();
        new Thread(new Runnable() {
            public void run() {
                final String problem = SetupActivity.importVoice(FlipendoActivity.this, folder, new File(gameDir()));
                runOnUiThread(new Runnable() {
                    public void run() {
                        if (problem != null) {
                            android.util.Log.w("flipendo", "dubbing import: " + problem);
                            android.widget.Toast.makeText(FlipendoActivity.this, problem, android.widget.Toast.LENGTH_LONG).show();
                            return;
                        }
                        android.widget.Toast.makeText(FlipendoActivity.this, "Dubbing imported: restarting with it", android.widget.Toast.LENGTH_SHORT).show();
                        switchLanguage(SetupActivity.VOICE_LANG);
                    }
                });
            }
        }).start();
    }

    /** The picked mod is imported (SetupActivity.importMod) off the UI thread; when it worked the game restarts with it. */
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_VOICE_DIR && resultCode == RESULT_OK && data != null && data.getData() != null) {
            importVoiceFolder(data.getData());
            return;
        }
        if (requestCode != REQUEST_MOD_FILE || resultCode != RESULT_OK || data == null) {
            return;
        }
        final java.util.List<Uri> picked = new java.util.ArrayList<>();
        if (data.getClipData() != null) {
            for (int i = 0; i < data.getClipData().getItemCount(); i++) {
                picked.add(data.getClipData().getItemAt(i).getUri());
            }
        } else if (data.getData() != null) {
            picked.add(data.getData());
        }
        if (picked.isEmpty()) {
            return;
        }
        android.widget.Toast.makeText(this, "Importing the mod...", android.widget.Toast.LENGTH_SHORT).show();
        new Thread(new Runnable() {
            public void run() {
                final String problem = SetupActivity.importMod(FlipendoActivity.this, picked, new File(gameDir()));
                runOnUiThread(new Runnable() {
                    public void run() {
                        if (problem != null) {
                            android.util.Log.w("flipendo", "mod import: " + problem);
                            android.widget.Toast.makeText(FlipendoActivity.this, problem, android.widget.Toast.LENGTH_LONG).show();
                            return;
                        }
                        android.widget.Toast.makeText(FlipendoActivity.this, "Mod imported: restarting with it", android.widget.Toast.LENGTH_SHORT).show();
                        switchMod(SetupActivity.IMPORTED_MOD);
                    }
                });
            }
        }).start();
    }

    void restartWith(String wishFile, String value) {
        restartWithIn(new File(GAME_DIR), wishFile, value);
    }

    /** restartWith with the wish file in a given folder (HP2's language wish lives in its own game folder). */
    void restartWithIn(File wishDir, String wishFile, String value) {
        restartWithGame(wishDir, wishFile, value, null);
    }

    /** The same, starting the given game ("hp1" / "hp2", or null for the one that is running): the launcher lives in its own process
     *  with its own copy of the settings, so the choice goes to it in the intent instead of through the shared preferences. */
    void restartWithGame(File wishDir, String wishFile, String value, String game) {
        wishDir.mkdirs(); // the wish files live there even when the game was picked elsewhere
        if (wishFile != null) {
            try {
                SetupActivity.writeSmallFile(new File(wishDir, wishFile), value);
            } catch (IOException e) {
                android.util.Log.e("flipendo", "could not write the wish " + wishFile + ": " + e);
                return;
            }
        }
        Intent intent = new Intent(this, SetupActivity.class);
        intent.putExtra("restart", true);
        if (game != null) intent.putExtra("game", game);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(intent);
        new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(new Runnable() {
            public void run() {
                android.os.Process.killProcess(android.os.Process.myPid());
            }
        }, 600);
    }

    void applyDebugMode(boolean on, boolean save) {
        debugMode = on;
        try {
            nativeSetDebugMode(on);
        } catch (UnsatisfiedLinkError ignored) {
        }
        if (save) saveScales();
    }

    private void saveScales() {
        getSharedPreferences("flipendo", MODE_PRIVATE).edit()
                .putFloat("renderScale", surfaceScale).putFloat("uiScale", uiScale)
                .putFloat("fovOffset", fovOffset).putBoolean("debugMode", debugMode).putBoolean("showFps", showFps).putBoolean("autoJump", autoJump).putBoolean("invertWand", invertWand).putInt("fpsLimit", fpsLimit).apply();
    }

    /** Resizes the render surface (the engine rebuilds its scene textures and swapchain for the new size). */
    void applyRenderScale(float scale) {
        surfaceScale = Math.max(RENDER_SCALE_MIN, Math.min(RENDER_SCALE_MAX, scale));
        resizeSurface();
        saveScales();
    }

    /** The surface view's size on screen, landscape (long side first): what the render surface is a scale of. */
    int[] viewSize() {
        int w = mSurface != null ? mSurface.getWidth() : 0, h = mSurface != null ? mSurface.getHeight() : 0;
        if (w <= 0 || h <= 0) {
            // not laid out yet: the whole display (the game runs full screen). Not getDisplayMetrics(): that is the area
            // left by the system bars, a navigation bar shorter on handhelds and tablets (below)
            android.graphics.Point p = new android.graphics.Point();
            getWindowManager().getDefaultDisplay().getRealSize(p);
            w = p.x;
            h = p.y;
        }
        return new int[] { Math.max(w, h), Math.min(w, h) };
    }

    /** The render surface at surfaceScale of the view in both directions. Every touch and pad cursor position goes to the
     *  engine as view pixels * surfaceScale, which only lands where it is drawn while the surface has the view's shape:
     *  sized from getDisplayMetrics() it was a navigation bar shorter on handhelds (the bar below in landscape), stretched
     *  over the full-screen view, and the menu cursor hit below where it was drawn. */
    void resizeSurface() {
        if (mSurface == null)
            return;
        int[] size = viewSize();
        int w = Math.max(1, Math.round(size[0] * surfaceScale)), h = Math.max(1, Math.round(size[1] * surfaceScale));
        if (w != surfaceWidth || h != surfaceHeight) {
            surfaceWidth = w;
            surfaceHeight = h;
            mSurface.getHolder().setFixedSize(w, h);
        }
    }

    static volatile int surfaceWidth, surfaceHeight; // the fixed surface size last set (LogReport)
    static volatile String viewSizeText = ""; // the surface view's size on screen (LogReport)

    /** Subtitles and HUD size: takes effect on the next frame. */
    void applyUiScale(float scale, boolean save) {
        uiScale = Math.max(UI_SCALE_MIN, Math.min(UI_SCALE_MAX, scale));
        try {
            nativeSetUiScale(uiScale);
        } catch (UnsatisfiedLinkError ignored) {
        }
        if (save) {
            saveScales();
        }
    }

    /** True while a menu wants the touch cursor (the engine unlocks the mouse); implemented in android_main.cpp. */
    static native boolean nativeMenuActive();

    /** True while a spell lesson is in its Draw state: the finger draws the symbol. */
    static native boolean nativeLessonDrawing();

    /** The finger position (0..1 of the view) that drives the wand in a lesson; implemented in android_main.cpp. */
    static native void nativeSetTouch(float x, float y, boolean down);

    /** Asks the game thread to save in the player's slot (android_main.cpp, HP1::TickSaveRequest). */
    static native void nativeRequestSave();

    /** Called from the game thread when the save is done. */
    public void onSaved(final int slot) {
        runOnUiThread(new Runnable() {
            public void run() {
                android.widget.Toast.makeText(FlipendoActivity.this,
                        "Game saved (slot " + (slot + 1) + ")",
                        android.widget.Toast.LENGTH_SHORT).show();
                if (modOffAfterSave) {
                    // the mod can't aim in this level: switch it off, restart, and the game loads this save by itself
                    modOffAfterSave = false;
                    new File(GAME_DIR).mkdirs();
                    try {
                        SetupActivity.writeSmallFile(new File(GAME_DIR, ".selfpack-autoload"), String.valueOf(slot));
                    } catch (IOException e) {
                        android.util.Log.e("flipendo", "could not write the autoload wish: " + e);
                    }
                    switchMod("none");
                }
            }
        });
    }

    /**
     * Called from the game thread (android_main.cpp) when the game ends. Code 0: the player quit. Otherwise the game
     * stopped on an error: show the screen with the way to send the logs.
     */
    public void onGameStopped(final int code) {
        runOnUiThread(new Runnable() {
            public void run() {
                if (code == 3) {
                    // Vulkan failed on this phone and the engine chose OpenGL ES for next time (android_main.cpp): start again
                    android.widget.Toast.makeText(FlipendoActivity.this, "Vulkan doesn't work on this phone: switching to OpenGL ES",
                            android.widget.Toast.LENGTH_LONG).show();
                    restartWith(null, null);
                    return;
                }
                if (code != 0) {
                    Intent intent = new Intent(FlipendoActivity.this, SetupActivity.class);
                    intent.putExtra("stopped", "exit code " + code);
                    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
                    startActivity(intent);
                }
                finish();
            }
        });
    }

    static native void nativeSetSurfaceAlive(boolean alive);

    static void setSurfaceAlive(boolean alive) {
        try {
            nativeSetSurfaceAlive(alive);
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    /** Frames the engine has drawn (android_main.cpp): the FPS counter. */
    static native int nativeFrameCount();

    /** The game menu's items for the gamepad: per item left, top, right, bottom (0..1 of the view) and kind (android_main.cpp). */
    static native float[] nativeMenuItems();

    /** Moves the menu slider at that index one step left (-1) or right (1), on the game thread. */
    static native void nativeMenuAdjust(int index, int dir);

    /** Spell lessons give this much more drawing time (1.35 with a gamepad). */
    static native void nativeSetLessonTimeScale(float scale);

    /** The FPS counter next to the toolbar (Settings > FPS counter), off by default. */
    static boolean showFps = false;

    /** The map being played (android_main.cpp), for prompts that depend on the level. */
    static native String nativeLevelName();

    static String levelName() {
        try {
            String name = nativeLevelName();
            return name == null ? "" : name;
        } catch (UnsatisfiedLinkError e) {
            return "";
        }
    }

    /** Levels where the game fixes the camera and aims with its own cursor: the movement mod aims along the camera and can't there. */
    static final String[] FIXED_CAMERA_LEVELS = { "lev5_snare" };

    static boolean isFixedCameraLevel(String map) {
        String key = map.toLowerCase();
        key = key.substring(key.lastIndexOf('/') + 1);
        if (key.endsWith(".unr")) key = key.substring(0, key.length() - 4);
        for (String level : FIXED_CAMERA_LEVELS) {
            if (level.equals(key)) return true;
        }
        return false;
    }

    /** The prompt for such a level was accepted: when the save is done, the mod is switched off and the game comes back to the save. */
    static volatile boolean modOffAfterSave;

    /** True while a cutscene holds Harry; implemented in android_main.cpp. */
    static native boolean nativeCutsceneActive();

    /** HP2: a vendor's Yes/No offer (the beans for a duel or a broom) is up: it is clicked with the mouse. */
    static native boolean nativeVendorActive();

    /** True while Harry flies the broom (the player is a BroomHarry); implemented in android_main.cpp. */
    static native boolean nativeOnBroom();

    /** A debug command for the game thread (HP2: "level <map>", "state GSTATE020", "spells"); implemented in android_main.cpp. */
    static native void nativeDebugCommand(String command);

    static void debugCommand(String command) {
        try {
            nativeDebugCommand(command);
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    private static final String[] HP2_STATES = { "GSTATE000", "GSTATE010", "GSTATE020", "GSTATE030", "GSTATE035", "GSTATE040",
            "GSTATE045", "GSTATE050", "GSTATE060", "GSTATE065", "GSTATE070", "GSTATE080", "GSTATE090", "GSTATE095", "GSTATE100",
            "GSTATE110", "GSTATE115", "GSTATE120", "GSTATE130", "GSTATE140", "GSTATE145", "GSTATE150", "GSTATE160", "GSTATE170",
            "GSTATE180" };

    /** HP2's maps (Maps/*.unr of the game folder), by name. */
    private String[] hp2Maps() {
        java.util.TreeSet<String> names = new java.util.TreeSet<>(String.CASE_INSENSITIVE_ORDER);
        File maps = new File(gameDir(), "Maps");
        if (!maps.isDirectory()) maps = new File(gameDir(), "maps");
        File[] files = maps.listFiles();
        if (files != null) {
            for (File f : files) {
                String n = f.getName();
                if (n.toLowerCase(java.util.Locale.ROOT).endsWith(".unr")) names.add(n.substring(0, n.length() - 4));
            }
        }
        return names.toArray(new String[0]);
    }

    /** The HP2 debug menu: go to a level (the level Harry is in is left like any exit: persistent actors travel), set the story
     *  state of the next level load, or fill the spell book. Opened from the settings panel's Debug row. */
    void openHp2Debug() {
        final String[] actions = { "Go to level…", "Set story state…", "Give all spells", "God mode (on / off)", "Fly", "Ghost (fly through walls)", "Walk again" };
        runOnUiThread(new Runnable() {
            public void run() {
                new android.app.AlertDialog.Builder(FlipendoActivity.this)
                        .setTitle("HP2 debug")
                        .setItems(actions, new android.content.DialogInterface.OnClickListener() {
                            public void onClick(android.content.DialogInterface d, int which) {
                                if (which == 0) {
                                    final String[] maps = hp2Maps();
                                    if (maps.length == 0) {
                                        android.widget.Toast.makeText(FlipendoActivity.this, "No maps found in " + gameDir(), android.widget.Toast.LENGTH_LONG).show();
                                        return;
                                    }
                                    new android.app.AlertDialog.Builder(FlipendoActivity.this).setTitle("Go to level")
                                            .setItems(maps, new android.content.DialogInterface.OnClickListener() {
                                                public void onClick(android.content.DialogInterface d2, int i) {
                                                    debugCommand("level " + maps[i]);
                                                }
                                            }).show();
                                } else if (which == 1) {
                                    new android.app.AlertDialog.Builder(FlipendoActivity.this).setTitle("Story state (applies on the next level load)")
                                            .setItems(HP2_STATES, new android.content.DialogInterface.OnClickListener() {
                                                public void onClick(android.content.DialogInterface d2, int i) {
                                                    debugCommand("state " + HP2_STATES[i]);
                                                }
                                            }).show();
                                } else if (which == 2) {
                                    debugCommand("spells");
                                } else if (which == 3) {
                                    debugCommand("god");
                                } else if (which == 4) {
                                    debugCommand("fly");
                                } else if (which == 5) {
                                    debugCommand("ghost");
                                } else {
                                    debugCommand("walk");
                                }
                            }
                        }).show();
            }
        });
    }

    static boolean onBroom() {
        try {
            return nativeOnBroom();
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
    }

    static boolean cutsceneActive() {
        try {
            return nativeCutsceneActive();
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
    }

    static boolean lessonDrawing() {
        try {
            return nativeLessonDrawing();
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
    }

    static boolean menuActive() {
        try {
            return nativeMenuActive() || nativeVendorActive();
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
    }

    /** The game folder: the user's own HP1 install (Maps, System, Textures...) lives here. */
    static final String GAME_DIR = "/sdcard/FlipendoHP";

    /** The renderer choice in GAME_DIR: "vulkan" (default), "opengl" (Settings) or "opengl-auto" (Vulkan failed; android_main.cpp). */
    static final String RENDERER_FILE = ".flipendo-renderer";

    /** Needs "All files access" to read the game data; opens the system page for it on first launch. */
    private boolean ensureStorageAccess() {
        if (SetupActivity.hasStorageAccess(this)) {
            return true;
        }
        if (android.os.Build.VERSION.SDK_INT < 30) {
            return false; // Android 10: the launcher asks for the storage permission (SetupActivity)
        }
        Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:" + getPackageName()));
        startActivity(intent);
        return false;
    }

    /** The engine's own UI resources (SurrealEngine.pk3) ship inside the APK; the engine looks for them in $HOME. */
    private void installResources() {
        File target = new File(GAME_DIR + "/.surrealengine/SurrealEngine.pk3");
        try {
            target.getParentFile().mkdirs();
            try (InputStream in = getAssets().open("SurrealEngine.pk3")) {
                if (target.exists() && target.length() == in.available()) {
                    return;
                }
                try (OutputStream out = new FileOutputStream(target)) {
                    byte[] buffer = new byte[1 << 16];
                    int n;
                    while ((n = in.read(buffer)) > 0) {
                        out.write(buffer, 0, n);
                    }
                }
            }
        } catch (IOException e) {
            android.util.Log.e("flipendo", "could not install SurrealEngine.pk3: " + e);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        android.util.Log.i("flipendo", "Flipendo for Android " + LogReport.version(this));
        if (ensureStorageAccess()) {
            installResources();
        }
        super.onCreate(savedInstanceState);
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // The player's scales (menu sliders), then render at a reduced fixed surface size: the compositor scales it up.
        android.content.SharedPreferences prefs = getSharedPreferences("flipendo", MODE_PRIVATE);
        dpadMode = prefs.getBoolean("dpad", false);
        applyUiScale(prefs.getFloat("uiScale", 1.0f), false);
        applyFovOffset(prefs.getFloat("fovOffset", 0f), false);
        applyDebugMode(prefs.getBoolean("debugMode", false), false);
        showFps = prefs.getBoolean("showFps", false);
        applyAutoJump(prefs.getBoolean("autoJump", true), false);
        invertWand = prefs.getBoolean("invertWand", false);
        applyFpsLimit(prefs.getInt("fpsLimit", 60), false);
        if (mSurface != null) {
            applyRenderScale(prefs.getFloat("renderScale", 0.5f));
            // SDL adds the surface as WRAP_CONTENT, which would shrink the view to the fixed size: stretch it over the screen.
            mSurface.setLayoutParams(new RelativeLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            // Once laid out (and whenever the system bars change it) the surface follows the view's real size
            mSurface.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> {
                viewSizeText = (r - l) + "x" + (b - t);
                if (r - l != or - ol || b - t != ob - ot)
                    resizeSurface();
            });
            // The engine must know when the surface is gone: Vulkan made a surface for a destroyed one and crashed
            // (android_main.cpp, nativeSetSurfaceAlive)
            mSurface.getHolder().addCallback(new android.view.SurfaceHolder.Callback() {
                public void surfaceCreated(android.view.SurfaceHolder holder) { setSurfaceAlive(true); }
                public void surfaceChanged(android.view.SurfaceHolder holder, int format, int width, int height) { setSurfaceAlive(true); }
                public void surfaceDestroyed(android.view.SurfaceHolder holder) { setSurfaceAlive(false); }
            });
        }
        if (mLayout != null) {
            overlay = new Overlay(this);
            mLayout.addView(overlay, new RelativeLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }
    }

    Overlay overlay;

    /** The game folder the launcher found or the player picked (SetupActivity), for the engine (android_main.cpp, argv[1]). */
    @Override
    protected String[] getArguments() {
        String dir = getIntent() != null ? getIntent().getStringExtra(SetupActivity.EXTRA_GAME_DIR) : null;
        return dir != null ? new String[] { dir } : new String[0];
    }

    /** A gamepad's buttons and sticks go to the overlay's pad mapping before SDL sees them (the engine has no gamepad input). */
    static boolean fromPad(InputEvent e) {
        int source = e.getSource();
        return (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (overlay != null && (fromPad(event) || KeyEvent.isGamepadButton(event.getKeyCode())) && overlay.padKey(event)) {
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (overlay != null && fromPad(event) && event.getAction() == MotionEvent.ACTION_MOVE) {
            return overlay.padMotion(event);
        }
        return super.dispatchGenericMotionEvent(event);
    }

    static final class Overlay extends View {
        enum Role { STICK, LOOK, BUTTON, MENU, DRAW, SLIDER }

        // Two sliders in the left margin of the menu: 0 = render scale, 1 = subtitles and HUD size.
        float sliderX0, sliderX1;
        final float[] sliderY = new float[4];
        int activeSlider = -1;
        float pendingRender = 0.5f; // the render scale being dragged; it is applied when the finger lifts

        boolean menuShown;       // last polled menu state: hides the gameplay buttons
        boolean lessonShown;     // last polled lesson state: hides the gameplay buttons too
        boolean cutsceneShown;   // last polled cutscene state: only SKIP, settings and menu are shown, the screen does not steer or look
        // Harry on the broom (the player is a BroomHarry): the wand button is a steering stick that presses nothing (its left
        // mouse is the broom's brake, and the script drops the boost while the brake is held), and JUMP (the right mouse:
        // the boost) is a toggle, so the right thumb steers while the boost stays on
        boolean broomShown;
        long menuDownTime;       // when the menu pointer went down
        float menuX, menuY;      // last menu pointer position, in surface pixels

        final Runnable poll = new Runnable() {
            public void run() {
                boolean m = menuActive(), l = lessonDrawing(), cs = cutsceneActive(), br = onBroom();
                if (m != menuShown || l != lessonShown || cs != cutsceneShown || br != broomShown) {
                    menuShown = m;
                    lessonShown = l;
                    cutsceneShown = cs;
                    if (br != broomShown) {
                        broomShown = br;
                        if (jump.down) setButton(jump, false); // the boost toggle ends with the flight (and vice versa)
                    }
                    if (cs) releaseGameplayControls();
                    invalidate();
                }
                // a level where the game fixes the camera, with the mod on: offer to switch the mod off (once per level)
                levelShown = levelName();
                if (modOn && !promptOpen && !settingsOpen && !menuShown && !lessonShown && !cutsceneShown
                        && isFixedCameraLevel(levelShown) && !levelShown.equalsIgnoreCase(promptDeclinedLevel)) {
                    openPrompt();
                }
                sampleFps();
                if (!menuShown) {
                    menuFocus = -1;
                } else if (padActive && menuFocus >= 0 && (++focusChecks % 3) == 0) {
                    checkMenuFocus();
                }
                postDelayed(this, 150);
            }
        };

        @Override
        protected void onAttachedToWindow() {
            super.onAttachedToWindow();
            requestApplyInsets(); // the safe area (onApplyWindowInsets)
            post(poll);
        }

        @Override
        protected void onDetachedFromWindow() {
            removeCallbacks(poll);
            super.onDetachedFromWindow();
        }

        /** Gameplay buttons are hidden while a menu is open; MENU stays as the way back. */
        boolean visible(Btn b) {
            if (layoutEdit) return b != skip; // the layout editor shows every control
            if (b == menu || b == gear) return true;
            if (b == skip) return cutsceneShown; // the skip button exists only in cutscenes
            if (cutsceneShown) return false;       // and they leave only the skip, settings and menu buttons
            if (padActive && (b == cast || b == jump || isArrow(b))) return false; // a gamepad is in use
            boolean game = !(menuShown || lessonShown);
            for (Btn a : arrows) {
                if (a == b) return dpadMode && game;
            }
            return game;
        }

        final android.graphics.Path arrowPath = new android.graphics.Path();

        int arrowIndex(Btn b) {
            for (int i = 0; i < arrows.length; i++) {
                if (arrows[i] == b) return i;
            }
            return -1;
        }

        boolean isArrow(Btn b) { return arrowIndex(b) >= 0; }

        /** A finger held on an arrow button slides to another arrow without lifting: the old one is released, the new pressed. */
        final java.util.HashSet<Integer> arrowFingers = new java.util.HashSet<>(); // fingers that went down on an arrow

        void slideArrow(int pid, float x, float y) {
            if (!arrowFingers.contains(pid)) return;
            Btn held = null;
            for (Btn a : arrows) {
                if (a.pointer == pid) held = a;
            }
            Btn over = null;
            float best = Float.MAX_VALUE;
            for (Btn a : arrows) {
                float dx = x - a.cx, dy = y - a.cy, d = dx * dx + dy * dy;
                if (d <= a.r * a.r * 1.3f && d < best && (a.pointer == -1 || a == held)) {
                    best = d;
                    over = a;
                }
            }
            if (over == held) return;
            if (held != null) {
                setButton(held, false);
                held.pointer = -1;
            }
            if (over != null) {
                over.pointer = pid;
                setButton(over, true);
            }
            invalidate();
        }

        /** A cutscene starts: let go of whatever the fingers were holding (stick directions, arrows, the wand). */
        void releaseGameplayControls() {
            releaseStick();
            releaseAim();
            arrowFingers.clear();
            for (Btn a : arrows) {
                if (a.down) setButton(a, false);
                a.pointer = -1;
            }
            if (cast.down) setButton(cast, false);
            cast.pointer = -1;
            // fingers that were steering or looking stop being tracked
            java.util.Iterator<java.util.Map.Entry<Integer, Role>> it = roles.entrySet().iterator();
            while (it.hasNext()) {
                Role r = it.next().getValue();
                if (r == Role.STICK || r == Role.LOOK) it.remove();
            }
        }

        // Languages carried by this APK ({code, label}) and the Language button, which opens them as a list; its last row
        // adds a dubbing from a folder (ADD_VOICE, not a language)
        static final String ADD_VOICE = "+voice";
        final java.util.List<String[]> languages = isHp2Game()
                ? SetupActivity.readHp2Languages(new File(((FlipendoActivity) getContext()).gameDir()))
                : withAddVoice(SetupActivity.readLanguages(getContext()));

        java.util.List<String[]> withAddVoice(java.util.List<String[]> list) {
            // the dubbing import is HP1's (its Sounds folder); HP2 takes its texts from its own harry2 folder
            if (!SetupActivity.isHp2Folder(((FlipendoActivity) getContext()).gameDir())) {
        list.add(new String[] { ADD_VOICE, SetupActivity.hasImportedVoice(getContext()) ? "Другая озвучка из папки…" : "Добавить озвучку из папки…" });
            }
            return list;
        }
        final String appVersion = LogReport.version(getContext()); // in the settings header
        private Boolean hp2Game;

        /** Whether the running game is HP2 (its folder has System/Game.exe and no System/HP.exe). */
        boolean isHp2Game() {
            if (hp2Game == null) hp2Game = SetupActivity.isHp2Folder(((FlipendoActivity) getContext()).gameDir());
            return hp2Game;
        }
        boolean langListOpen;       // the list over the settings panel
        int langFocus = -1;         // the row the pad is on
        final java.util.List<android.graphics.RectF> langRows = new java.util.ArrayList<>();
        final android.graphics.RectF langPanel = new android.graphics.RectF();
        float langTitleY, langHintY;

        String currentLanguage() {
            if (isHp2Game()) return SetupActivity.chosenHp2Language(new File(((FlipendoActivity) getContext()).gameDir()));
            return SetupActivity.chosenLanguage(getContext());
        }

        int languageIndex(String code) {
            for (int i = 0; i < languages.size(); i++) {
                if (languages.get(i)[0].equals(code)) return i;
            }
            return 0;
        }

        /** The Language row: opens the list of languages, the pad on the current one. */
        void languageTap() {
            langListOpen = true;
            langFocus = languageIndex(currentLanguage());
            layoutLanguageList();
            invalidate();
        }

        /** A language picked from the list: another one restarts the game in it; the current one just closes the list. */
        void pickLanguage(int index) {
            langListOpen = false;
            invalidate();
            if (index >= 0 && index < languages.size() && ADD_VOICE.equals(languages.get(index)[0])) {
                ((FlipendoActivity) getContext()).pickVoiceFolder();
                return;
            }
            if (index >= 0 && index < languages.size() && index != languageIndex(currentLanguage())) {
                ((FlipendoActivity) getContext()).switchLanguage(languages.get(index)[0]);
            }
        }

        /** The list's card: centred on the settings panel, one row per language, as tall as the rows need (at most the panel). */
        void layoutLanguageList() {
            float h = getHeight();
            int n = languages.size();
            float rowH = languageBtn.isEmpty() ? 0.11f * h : languageBtn.height();
            float gap = 0.2f * rowH, titleH = 1.3f * rowH, hintH = 0.9f * rowH;
            float avail = panel.height() - 0.04f * h;
            float need = titleH + n * rowH + (n - 1) * gap + hintH;
            if (need > avail) {
                float k = avail / need;
                rowH *= k; gap *= k; titleH *= k; hintH *= k;
                need = avail;
            }
            float pw = Math.min(panel.width() - 0.04f * h, Math.max(colR1 - colR0, 0.5f * panel.width()));
            float pad = 0.05f * pw;
            langPanel.set(panel.centerX() - pw / 2f, panel.centerY() - need / 2f, panel.centerX() + pw / 2f, panel.centerY() + need / 2f);
            langTitleY = langPanel.top + 0.62f * titleH;
            langRows.clear();
            float y = langPanel.top + titleH;
            for (int i = 0; i < n; i++) {
                langRows.add(new android.graphics.RectF(langPanel.left + pad, y, langPanel.right - pad, y + rowH));
                y += rowH + gap;
            }
            langHintY = y - gap + 0.55f * hintH;
        }

        void drawLanguageList(Canvas c) {
            if (!langListOpen || !settingsOpen) return;
            layoutLanguageList();
            float u = Math.min(getWidth(), getHeight());
            fill.setColor(0x99000000);
            c.drawRect(0, 0, getWidth(), getHeight(), fill);
            float r = 0.03f * u;
            fill.setColor(CARD);
            c.drawRoundRect(langPanel, r, r, fill);
            line.setColor(0x55FFFFFF);
            line.setStrokeWidth(2f);
            c.drawRoundRect(langPanel, r, r, line);
            float x0 = langRows.get(0).left;
            fill.setColor(GOLD);
            c.drawRoundRect(x0, langTitleY - 0.04f * getHeight(), x0 + 0.008f * u, langTitleY + 0.005f * getHeight(), 4f, 4f, fill);
            titlePaint.setTextAlign(Paint.Align.LEFT);
            c.drawText("Language", x0 + 0.025f * u, langTitleY, titlePaint);
            int current = languageIndex(currentLanguage());
            for (int i = 0; i < langRows.size(); i++) {
                android.graphics.RectF row = langRows.get(i);
                boolean on = i == current;
                drawRowBase(c, row, on);
                rowPaint.setTextAlign(Paint.Align.LEFT);
                c.drawText(languages.get(i)[1], row.left + rowPad(row), row.centerY() + rowPaint.getTextSize() * 0.35f, rowPaint);
                valuePaint.setTextAlign(Paint.Align.RIGHT);
                c.drawText(on ? "✓ Current" : ADD_VOICE.equals(languages.get(i)[0]) ? "Choose folder  ›" : "Restart in it  ›", row.right - rowPad(row), row.centerY() + valuePaint.getTextSize() * 0.35f, valuePaint);
                if (padActive && i == langFocus) drawPadFocus(c, row);
            }
            subPaint.setTextAlign(Paint.Align.CENTER);
            c.drawText("Picking another language restarts the game (unsaved progress is lost)", langPanel.centerX(), langHintY, subPaint);
            subPaint.setTextAlign(Paint.Align.LEFT);
        }

        // The movement mod (AdamJD): a replacement for the game's scripts that the APK can carry (self pack with --mod). While it is
        // installed the wand is aimed with a stick on the right (in the mod, holding CAST aims and lets the camera move).
        final java.util.List<String[]> mods = SetupActivity.readMods(getContext());
        final boolean modOn;
        boolean modAsk;
        long modAskTime;

        {
            String id = SetupActivity.readSmallFile(new File(((FlipendoActivity) getContext()).gameDir(), SetupActivity.MOD_MARKER));
            modOn = id != null && !id.isEmpty() && !id.equals("none");
        }

        /** First tap asks to confirm; a second tap within 5 s switches the mod and restarts the game. */
        void modTap() {
            long now = android.os.SystemClock.uptimeMillis();
            if (modAsk && now - modAskTime < 5000) {
                modAsk = false;
                if (mods.isEmpty()) {
                    ((FlipendoActivity) getContext()).pickModFile(); // no mod yet: import one, then restart with it
                } else {
                    ((FlipendoActivity) getContext()).switchMod(modOn ? "none" : mods.get(0)[0]);
                }
                invalidate();
                return;
            }
            modAsk = true;
            modAskTime = now;
            postDelayed(new Runnable() { public void run() {
                if (modAsk && android.os.SystemClock.uptimeMillis() - modAskTime >= 5000) {
                    modAsk = false;
                    invalidate();
                }
            } }, 5100);
        }

        // The renderer (android_main.cpp reads .flipendo-renderer): Vulkan, or OpenGL ES chosen here or picked by the engine
        // when Vulkan failed on this phone ("opengl-auto"). A switch restarts the game, with a confirmation like the mod's.
        final android.graphics.RectF rendererBtn = new android.graphics.RectF();
        final boolean rendererGL, rendererAuto;
        boolean rendererAsk;
        long rendererAskTime;

        {
            String r = SetupActivity.readSmallFile(new File(GAME_DIR, RENDERER_FILE));
            rendererGL = !"vulkan".equals(r); // OpenGL ES unless Vulkan was picked (android_main.cpp)
            rendererAuto = "opengl-auto".equals(r);
        }

        String rendererLabel(boolean gl) {
            return gl ? "OpenGL ES" : "Vulkan";
        }

        boolean switchAsk;
        long switchAskTime;

        /** The other game, from inside this one: the first tap asks, the second (within 5 s) restarts the engine into it by itself. */
        void switchGameTap() {
            long now = android.os.SystemClock.uptimeMillis();
            if (switchAsk && now - switchAskTime < 5000) {
                switchAsk = false;
                // the launcher starts the other game without waiting for Play
                ((FlipendoActivity) getContext()).restartWithGame(new File(GAME_DIR), null, null, isHp2Game() ? "hp1" : "hp2");
                return;
            }
            switchAsk = true;
            switchAskTime = now;
            postDelayed(new Runnable() { public void run() {
                if (switchAsk && android.os.SystemClock.uptimeMillis() - switchAskTime >= 5000) {
                    switchAsk = false;
                    invalidate();
                }
            } }, 5100);
        }

        void rendererTap() {
            long now = android.os.SystemClock.uptimeMillis();
            if (rendererAsk && now - rendererAskTime < 5000) {
                rendererAsk = false;
                // a switch here is for good: the launcher must not put back the renderer from before a one-launch choice
                getContext().getSharedPreferences("setup", MODE_PRIVATE).edit().remove(SetupActivity.PREF_RESTORE).apply();
                ((FlipendoActivity) getContext()).restartWith(RENDERER_FILE, rendererGL ? "vulkan" : "opengl");
                return;
            }
            rendererAsk = true;
            rendererAskTime = now;
            postDelayed(new Runnable() { public void run() {
                if (rendererAsk && android.os.SystemClock.uptimeMillis() - rendererAskTime >= 5000) {
                    rendererAsk = false;
                    invalidate();
                }
            } }, 5100);
        }

        // The right stick (only with the mod): deflection turns the camera, that is the wand's aim, 60 times a second.
        float aimCx, aimCy, aimR;
        int aimPointer = -1;
        float aimDx, aimDy; // knob offset from the centre, in pixels (clamped to aimR)
        static final float AIM_GAIN = 24f;      // mouse counts per frame at full deflection
        static final float AIM_DEADZONE = 0.12f;

        final Runnable aimTick = new Runnable() {
            public void run() {
                if (aimPointer == -1) return;
                float mag = (float) Math.hypot(aimDx, aimDy) / aimR;
                if (mag > AIM_DEADZONE) {
                    float scaled = (Math.min(mag, 1f) - AIM_DEADZONE) / (1f - AIM_DEADZONE);
                    float length = (float) Math.hypot(aimDx, aimDy); // the knob offset, at most aimR
                    float yFlip = (isHp2Game() ? -1f : 1f) * (invertWand ? -1f : 1f); // HP2: the stick looks the other way up and down than in HP1; Invert wand flips it
                    SDLActivity.onNativeMouse(0, MotionEvent.ACTION_MOVE,
                            aimDx / length * scaled * AIM_GAIN, yFlip * aimDy / length * scaled * AIM_GAIN, true);
                }
                postDelayed(this, 16);
            }
        };

        /** The wand button is also a stick that turns the camera: with the movement mod, on the broom, and in HP2 (with a small range). */
        boolean aimStick() {
            return modOn || broomShown || isHp2Game();
        }

        boolean aimVisible() {
            return aimStick() && !padActive && !settingsOpen && !menuShown && !lessonShown && !cutsceneShown;
        }

        void setAim(float x, float y) {
            float dx = x - aimCx, dy = y - aimCy;
            float d = (float) Math.hypot(dx, dy);
            if (d > aimR) {
                dx *= aimR / d;
                dy *= aimR / d;
            }
            aimDx = dx;
            aimDy = dy;
        }

        void releaseAim() {
            aimPointer = -1;
            aimDx = aimDy = 0;
        }

        // An Xbox-style gamepad, mapped onto what the touch controls send:
        //   left stick / d-pad: walk (arrow keys)        right stick: camera (relative mouse); the cursor in menus; the wand in lessons
        //   A, LB or LT: jump (A: click in menus)    X, RB or RT: the wand (held)    Y: save (hold it in a cutscene: skip)    B or Start: Esc
        // While a pad is in use the on-screen arrows, wand and jump are hidden; a touch brings them back.
        boolean padActive;
        float padLx, padLy, padRx, padRy, padHatX, padHatY;
        boolean padCastKey, padCastBumper, padCastTrigger, padCasting;
        boolean padA, padJumpBumper, padJumpTrigger, padJumping; // A, LB, LT: the jump group
        float padCursorX = -1, padCursorY = -1;   // the menu cursor, in view pixels
        static final float PAD_DEADZONE = 0.18f, PAD_WALK = 0.4f, PAD_LOOK = 34f, PAD_DRAW = 4f;
        float padDrawX, padDrawY; // the lesson wand's mouse counts not sent yet (fractions)

        static void setLessonTimeScale(float scale) {
            try {
                nativeSetLessonTimeScale(scale);
            } catch (UnsatisfiedLinkError ignored) {
            }
        }

        void padUsed() {
            if (!padActive) {
                padActive = true;
                setLessonTimeScale(1.35f); // drawing a spell with a stick takes longer than with a finger
                releaseStick();
                for (Btn a : arrows) {
                    if (a.down) setButton(a, false);
                    a.pointer = -1;
                }
                invalidate();
            }
        }

        static float deadzone(float v) {
            float a = Math.abs(v);
            if (a < PAD_DEADZONE) return 0f;
            return Math.signum(v) * (a - PAD_DEADZONE) / (1f - PAD_DEADZONE);
        }

        /** Sticks, triggers and the d-pad hat. */
        boolean padMotion(MotionEvent e) {
            padUsed();
            padLx = deadzone(e.getAxisValue(MotionEvent.AXIS_X));
            padLy = deadzone(e.getAxisValue(MotionEvent.AXIS_Y));
            padRx = deadzone(e.getAxisValue(MotionEvent.AXIS_Z));
            padRy = deadzone(e.getAxisValue(MotionEvent.AXIS_RZ));
            padHatX = e.getAxisValue(MotionEvent.AXIS_HAT_X);
            padHatY = e.getAxisValue(MotionEvent.AXIS_HAT_Y);
            float rt = Math.max(e.getAxisValue(MotionEvent.AXIS_RTRIGGER), e.getAxisValue(MotionEvent.AXIS_GAS));
            float lt = Math.max(e.getAxisValue(MotionEvent.AXIS_LTRIGGER), e.getAxisValue(MotionEvent.AXIS_BRAKE));
            padCastTrigger = rt > 0.5f;
            padJumpTrigger = lt > 0.5f;
            updatePadCast();
            updatePadJump();
            updatePadWalk();
            updatePadNav();
            removeCallbacks(padTick);
            post(padTick);
            return true;
        }

        /** Buttons. Returns true when the key was the pad's. */
        boolean padKey(KeyEvent e) {
            int code = e.getKeyCode();
            boolean down = e.getAction() == KeyEvent.ACTION_DOWN;
            if (e.getAction() != KeyEvent.ACTION_DOWN && e.getAction() != KeyEvent.ACTION_UP) return false;
            if (down && e.getRepeatCount() > 0) return true;
            padUsed();
            if (helpOpen && settingsOpen) {
                if (down) helpOpen = false;
                invalidate();
                return true;
            }
            switch (code) {
                case KeyEvent.KEYCODE_DPAD_UP: padHatY = down ? -1 : 0; updatePadWalk(); updatePadNav(); return true;
                case KeyEvent.KEYCODE_DPAD_DOWN: padHatY = down ? 1 : 0; updatePadWalk(); updatePadNav(); return true;
                case KeyEvent.KEYCODE_DPAD_LEFT: padHatX = down ? -1 : 0; updatePadWalk(); updatePadNav(); return true;
                case KeyEvent.KEYCODE_DPAD_RIGHT: padHatX = down ? 1 : 0; updatePadWalk(); updatePadNav(); return true;
                case KeyEvent.KEYCODE_BUTTON_A:
                case KeyEvent.KEYCODE_DPAD_CENTER:
                    padButtonA(down);
                    return true;
                case KeyEvent.KEYCODE_BUTTON_X:
                    padCastKey = down;
                    updatePadCast();
                    return true;
                case KeyEvent.KEYCODE_BUTTON_Y:
                    if (cutsceneShown) {
                        padSkipHold(down); // a cutscene is skipped by holding Y, so a touch on a stick or button skips nothing
                    } else if (down && !menuShown && !lessonShown) {
                        nativeRequestSave();
                    }
                    return true;
                case KeyEvent.KEYCODE_BUTTON_B:
                case KeyEvent.KEYCODE_BACK:
                case KeyEvent.KEYCODE_BUTTON_START:
                case KeyEvent.KEYCODE_MENU:
                    if (settingsOpen || promptOpen) {
                        if (!down) {
                            if (settingsOpen && langListOpen) {
                                langListOpen = false; // B closes the language list, the settings stay
                                invalidate();
                                return true;
                            }
                            if (promptOpen) {
                                promptOpen = false; // B on the Devil's Snare prompt: keep the mod
                                promptDeclinedLevel = levelShown;
                            }
                            settingsOpen = false;
                            invalidate();
                        }
                        return true;
                    }
                    key(KeyEvent.KEYCODE_ESCAPE, down);
                    return true;
                case KeyEvent.KEYCODE_BUTTON_R2:
                    padCastTrigger = down;
                    updatePadCast();
                    return true;
                case KeyEvent.KEYCODE_BUTTON_R1:
                    padCastBumper = down;
                    updatePadCast();
                    return true;
                case KeyEvent.KEYCODE_BUTTON_L1:
                    padJumpBumper = down;
                    updatePadJump();
                    return true;
                case KeyEvent.KEYCODE_BUTTON_L2:
                    padJumpTrigger = down;
                    updatePadJump();
                    return true;
                case KeyEvent.KEYCODE_BUTTON_SELECT:
                    // View: the port's settings panel, steered with the d-pad
                    if (!down && !promptOpen) {
                        if (settingsOpen) {
                            settingsOpen = false;
                        } else {
                            openSettings();
                            settingsFocus = 0;
                        }
                        invalidate();
                    }
                    return true;
                case KeyEvent.KEYCODE_BUTTON_THUMBL:
                case KeyEvent.KEYCODE_BUTTON_THUMBR:
                    if (down && isHp2Game() && !menuShown && !lessonShown && !cutsceneShown && !settingsOpen && !promptOpen && !layoutEdit) drinkPotion(); // HP2: the Wiggenweld potion
                    return true; // otherwise not used: keep them away from the game
                default:
                    return false;
            }
        }

        boolean padClicking; // A is clicking in a menu (pressed after the cursor moved there)

        /** A: a click in menus (the cursor goes there first, the press follows a frame later, as the menus need); else the jump group. */
        void padButtonA(boolean down) {
            if (padA == down) return;
            padA = down;
            if (settingsOpen) {
                if (down) settingsActivate();
                return;
            }
            if (promptOpen) {
                if (down) acceptPrompt(); // A on the Devil's Snare prompt: switch the mod off
                return;
            }
            if (menuShown || padClicking) {
                ensurePadCursor();
                final float x = padCursorX * surfaceScale, y = padCursorY * surfaceScale;
                if (down) {
                    padClicking = true;
                    SDLActivity.onNativeMouse(0, MotionEvent.ACTION_MOVE, x, y, false);
                    postDelayed(new Runnable() { public void run() {
                        if (padA) SDLActivity.onNativeMouse(MotionEvent.BUTTON_PRIMARY, MotionEvent.ACTION_DOWN, x, y, false);
                    } }, 60);
                } else {
                    postDelayed(new Runnable() { public void run() {
                        padClicking = false;
                        SDLActivity.onNativeMouse(0, MotionEvent.ACTION_UP, x, y, false);
                    } }, 120);
                }
                return;
            }
            updatePadJump();
        }

        /** A, LB or LT held: the jump in the game (nothing in a cutscene: that is skipped by holding Y). */
        void updatePadJump() {
            boolean want = ((padA && !menuShown) || padJumpBumper || padJumpTrigger) && !settingsOpen && !promptOpen && !cutsceneShown;
            if (menuShown && !padJumping) want = false;
            if (want == padJumping) return;
            padJumping = want;
            setButton(jump, want);
        }

        static final int PAD_SKIP_HOLD_MS = 700;
        boolean padSkipHeld;

        final Runnable padSkipFire = new Runnable() {
            public void run() {
                if (!padSkipHeld || !cutsceneShown) return;
                key(KeyEvent.KEYCODE_SPACE, true);
                postDelayed(new Runnable() { public void run() { key(KeyEvent.KEYCODE_SPACE, false); } }, 120);
            }
        };

        /** Y in a cutscene: Space (the skip) only once it has been held for a moment. */
        void padSkipHold(boolean down) {
            padSkipHeld = down;
            removeCallbacks(padSkipFire);
            if (down) postDelayed(padSkipFire, PAD_SKIP_HOLD_MS);
        }

        /** The menu cursor while a pad drives it: the game's menus draw none on a touch screen. */
        void drawPadCursor(Canvas c) {
            if (!padActive || !menuShown || settingsOpen) return;
            if (menuFocus >= 0) drawPadFocus(c, menuFocusRect);
            ensurePadCursor();
            float u = Math.min(getWidth(), getHeight()), s = 0.05f * u;
            float x = padCursorX, y = padCursorY;
            arrowPath.reset();
            arrowPath.moveTo(x, y);
            arrowPath.lineTo(x, y + s);
            arrowPath.lineTo(x + 0.28f * s, y + 0.74f * s);
            arrowPath.lineTo(x + 0.5f * s, y + 1.12f * s);
            arrowPath.lineTo(x + 0.66f * s, y + 1.04f * s);
            arrowPath.lineTo(x + 0.46f * s, y + 0.68f * s);
            arrowPath.lineTo(x + 0.8f * s, y + 0.68f * s);
            arrowPath.close();
            fill.setColor(padA ? GOLD : 0xFFFFFFFF);
            c.drawPath(arrowPath, fill);
            line.setColor(0xFF1A1A1A);
            line.setStrokeWidth(0.004f * u);
            line.setStrokeJoin(Paint.Join.ROUND);
            c.drawPath(arrowPath, line);
            line.setStrokeJoin(Paint.Join.MITER);
        }

        /** X, RB or RT held: the wand; in a lesson it draws at the pad's wand point. */
        void updatePadCast() {
            boolean want = (padCastKey || padCastBumper || padCastTrigger) && !menuShown && !settingsOpen && !promptOpen;
            if (want == padCasting) return;
            padCasting = want;
            if (lessonShown) {
                // the lesson's own wand: the script moves it from the mouse (padTick) and draws while AltFire is held, from
                // the symbol's first point; no finger position is set
                SDLActivity.onNativeMouse(want ? MotionEvent.BUTTON_PRIMARY : 0, want ? MotionEvent.ACTION_DOWN : MotionEvent.ACTION_UP, 0f, 0f, true);
            } else {
                setButton(cast, want);
            }
        }

        void updatePadWalk() {
            float x = padHatX != 0 ? padHatX : padLx, y = padHatY != 0 ? padHatY : padLy;
            // not in a lesson either: the lesson moves the wand with the walking keys too, at full speed
            boolean walk = !menuShown && !cutsceneShown && !lessonShown && !settingsOpen && !promptOpen;
            setDir(0, walk && y < -PAD_WALK);
            setDir(1, walk && y > PAD_WALK);
            setDir(2, walk && x < -PAD_WALK);
            setDir(3, walk && x > PAD_WALK);
        }

        void ensurePadCursor() {
            if (padCursorX < 0) {
                padCursorX = getWidth() / 2f;
                padCursorY = getHeight() / 2f;
            }
        }

        /** 60 times a second while the right stick is out: the camera, the menu cursor or the lesson's wand. */
        final Runnable padTick = new Runnable() {
            public void run() {
                if (padRx == 0f && padRy == 0f) return;
                float speed = (float) Math.hypot(padRx, padRy);
                float curve = speed * speed / Math.max(speed, 1e-3f); // squared response: fine aim near the centre
                float dx = padRx * curve, dy = padRy * curve;
                if (settingsOpen || promptOpen) {
                    // panels are steered with the d-pad / left stick
                } else if (menuShown) {
                    ensurePadCursor();
                    if (menuFocus >= 0) {
                        menuFocus = -1; // the stick takes over from the item highlight
                    }
                    float step = 0.025f * Math.min(getWidth(), getHeight());
                    padCursorX = Math.max(0, Math.min(getWidth() - 1, padCursorX + dx * step));
                    padCursorY = Math.max(0, Math.min(getHeight() - 1, padCursorY + dy * step));
                    SDLActivity.onNativeMouse(padA ? MotionEvent.BUTTON_PRIMARY : 0, MotionEvent.ACTION_MOVE,
                            padCursorX * surfaceScale, padCursorY * surfaceScale, false);
                    invalidate(); // the cursor is drawn by the overlay
                } else if (lessonShown) {
                    // the lesson moves its wand by SmoothMouse * 30 * dt / 64000: 4 counts a frame at full tilt, slow and precise
                    // (the stick's squared response makes small tilts slower still)
                    // the engine takes whole mouse counts: the fractions add up here, so a slow stick still moves the wand
                    padDrawX += dx * PAD_DRAW;
                    padDrawY += dy * PAD_DRAW;
                    int sx = (int) padDrawX, sy = (int) padDrawY;
                    padDrawX -= sx;
                    padDrawY -= sy;
                    if (sx != 0 || sy != 0) SDLActivity.onNativeMouse(0, MotionEvent.ACTION_MOVE, sx, sy, true);
                } else if (!cutsceneShown) {
                    SDLActivity.onNativeMouse(0, MotionEvent.ACTION_MOVE, dx * PAD_LOOK, dy * PAD_LOOK, true);
                }
                postDelayed(this, 16);
            }
        };

        // Directions for menus and the settings panel: the d-pad or the left stick, one step per press, repeating while held.
        int navX, navY;
        static final float PAD_NAV = 0.6f;

        final Runnable navRepeat = new Runnable() {
            public void run() {
                if (navX == 0 && navY == 0) return;
                padNav(navX, navY);
                postDelayed(this, 150);
            }
        };

        boolean navigating() {
            return menuShown || settingsOpen || promptOpen;
        }

        void updatePadNav() {
            int x = 0, y = 0;
            if (navigating()) {
                float sx = padHatX != 0 ? padHatX : padLx, sy = padHatY != 0 ? padHatY : padLy;
                if (Math.abs(sx) >= Math.abs(sy)) {
                    x = sx > PAD_NAV ? 1 : sx < -PAD_NAV ? -1 : 0;
                } else {
                    y = sy > PAD_NAV ? 1 : sy < -PAD_NAV ? -1 : 0;
                }
            }
            if (x == navX && y == navY) return;
            navX = x;
            navY = y;
            removeCallbacks(navRepeat);
            if (x != 0 || y != 0) {
                padNav(x, y);
                postDelayed(navRepeat, 380);
            }
        }

        void padNav(int dx, int dy) {
            if (settingsOpen && langListOpen) {
                if (dy != 0) {
                    langFocus = Math.max(0, Math.min(languages.size() - 1, (langFocus < 0 ? 0 : langFocus) + dy));
                    invalidate();
                }
            } else if (settingsOpen) {
                settingsNav(dx, dy);
            } else if (menuShown && !promptOpen) {
                menuNav(dx, dy);
            }
        }

        /**
         * The next rectangle in a direction from a point: ahead of it along the direction, the closest with sideways distance
         * counting more. Returns its index, or -1.
         */
        static int nextInDirection(java.util.List<android.graphics.RectF> rects, float ox, float oy, int dx, int dy, int skip) {
            int best = -1;
            float bestScore = Float.MAX_VALUE;
            for (int i = 0; i < rects.size(); i++) {
                if (i == skip) continue;
                android.graphics.RectF r = rects.get(i);
                float vx = r.centerX() - ox, vy = r.centerY() - oy;
                float ahead = vx * dx + vy * dy, side = Math.abs(vx * dy - vy * dx);
                if (ahead <= 8f) continue;
                float score = ahead + 2.5f * side;
                if (score < bestScore) {
                    bestScore = score;
                    best = i;
                }
            }
            return best;
        }

        // The game's menu: the item the pad is on (index into the last list), its rectangle in view pixels and kind
        int menuFocus = -1;
        int menuFocusKind;
        int focusChecks;
        final android.graphics.RectF menuFocusRect = new android.graphics.RectF();
        final java.util.List<android.graphics.RectF> menuRects = new java.util.ArrayList<>();
        final java.util.List<Integer> menuKinds = new java.util.ArrayList<>();

        boolean readMenuItems() {
            float[] items;
            try {
                items = nativeMenuItems();
            } catch (UnsatisfiedLinkError e) {
                return false;
            }
            menuRects.clear();
            menuKinds.clear();
            if (items == null) return false;
            float w = getWidth(), h = getHeight();
            for (int i = 0; i + 4 < items.length; i += 5) {
                menuRects.add(new android.graphics.RectF(items[i] * w, items[i + 1] * h, items[i + 2] * w, items[i + 3] * h));
                menuKinds.add((int) items[i + 4]);
            }
            return !menuRects.isEmpty();
        }

        void menuNav(int dx, int dy) {
            if (!readMenuItems()) return;
            if (menuFocus >= 0 && menuFocus < menuKinds.size() && menuKinds.get(menuFocus) == 1 && dx != 0 && dy == 0
                    && menuRects.get(menuFocus).equals(menuFocusRect)) {
                nativeMenuAdjust(menuFocus, dx); // left / right on a slider moves it
                return;
            }
            int next;
            ensurePadCursor();
            if (menuFocus < 0 || menuFocus >= menuRects.size() || !menuRects.get(menuFocus).equals(menuFocusRect)) {
                // no item yet (or the page changed): the one nearest the cursor
                next = -1;
                float bestDistance = Float.MAX_VALUE;
                for (int i = 0; i < menuRects.size(); i++) {
                    float d = (float) Math.hypot(menuRects.get(i).centerX() - padCursorX, menuRects.get(i).centerY() - padCursorY);
                    if (d < bestDistance) {
                        bestDistance = d;
                        next = i;
                    }
                }
            } else {
                next = nextInDirection(menuRects, menuFocusRect.centerX(), menuFocusRect.centerY(), dx, dy, menuFocus);
            }
            if (next < 0) return;
            menuFocus = next;
            menuFocusKind = menuKinds.get(next);
            menuFocusRect.set(menuRects.get(next));
            padCursorX = menuFocusRect.centerX();
            padCursorY = menuFocusRect.centerY();
            SDLActivity.onNativeMouse(0, MotionEvent.ACTION_MOVE, padCursorX * surfaceScale, padCursorY * surfaceScale, false);
            invalidate();
        }

        /** The item the pad is on is gone (another page): drop the highlight. */
        void checkMenuFocus() {
            if (menuFocus < 0) return;
            if (!menuShown || !readMenuItems() || menuFocus >= menuRects.size() || !menuRects.get(menuFocus).equals(menuFocusRect)) {
                boolean found = false;
                for (int i = 0; i < menuRects.size(); i++) {
                    if (menuRects.get(i).equals(menuFocusRect)) {
                        menuFocus = i;
                        found = true;
                    }
                }
                if (!found) menuFocus = -1;
                invalidate();
            }
        }

        // The port's settings panel: the entry the pad is on
        static final int SET_RENDER = 0, SET_UI = 1, SET_FOV = 2, SET_FPS = 3, SET_SEG = 4, SET_JUMP = 5, SET_LANG = 6, SET_MOD = 7,
                SET_DEBUG = 8, SET_TELEGRAM = 9, SET_DONE = 10, SET_RENDERER = 11, SET_FPS_LIMIT = 12, SET_LAYOUT = 13, SET_HELP = 14, SET_INVERT = 15, SET_SWITCH = 16;
        int settingsFocus = -1;
        final java.util.List<android.graphics.RectF> setRects = new java.util.ArrayList<>();
        final java.util.List<Integer> setIds = new java.util.ArrayList<>();

        void buildSettingsEntries() {
            setRects.clear();
            setIds.clear();
            float h = getHeight();
            for (int i = 0; i < SLIDERS; i++) {
                setRects.add(new android.graphics.RectF(colL0 - 0.01f * h, sliderLabelY[i] - 0.045f * h, colL1 + 0.01f * h, sliderY[i] + 0.03f * h));
                setIds.add(sliderId(i));
            }
            setRects.add(new android.graphics.RectF(fpsBtn)); setIds.add(SET_FPS);
            setRects.add(new android.graphics.RectF(segStick.left, segStick.top, segDpad.right, segDpad.bottom)); setIds.add(SET_SEG);
            setRects.add(new android.graphics.RectF(autoJumpBtn)); setIds.add(SET_JUMP);
            if (aimStick() || isHp2Game()) { setRects.add(new android.graphics.RectF(invertBtn)); setIds.add(SET_INVERT); }
            setRects.add(new android.graphics.RectF(layoutBtn)); setIds.add(SET_LAYOUT);
            if (languages.size() > 1) { setRects.add(new android.graphics.RectF(languageBtn)); setIds.add(SET_LANG); }
            if (!isHp2Game()) { setRects.add(new android.graphics.RectF(modBtn)); setIds.add(SET_MOD); }
            setRects.add(new android.graphics.RectF(debugBtn)); setIds.add(SET_DEBUG);
            setRects.add(new android.graphics.RectF(helpBtn)); setIds.add(SET_HELP);
            setRects.add(new android.graphics.RectF(switchBtn)); setIds.add(SET_SWITCH);
            setRects.add(new android.graphics.RectF(telegramBtn)); setIds.add(SET_TELEGRAM);
            setRects.add(new android.graphics.RectF(rendererBtn)); setIds.add(SET_RENDERER);
            setRects.add(new android.graphics.RectF(closeBtn)); setIds.add(SET_DONE);
        }

        int focusedSetting() {
            return settingsFocus >= 0 && settingsFocus < setIds.size() ? setIds.get(settingsFocus) : -1;
        }

        void settingsNav(int dx, int dy) {
            buildSettingsEntries();
            int id = focusedSetting();
            if (id < 0) {
                settingsFocus = 0;
                invalidate();
                return;
            }
            int slider = sliderOfId(id);
            if (dx != 0 && dy == 0 && slider >= 0) {
                // left / right on a slider changes it by its step
                float v = Math.max(sliderMin(slider), Math.min(sliderMax(slider), sliderValue(slider) + dx * sliderStep(slider)));
                FlipendoActivity activity = (FlipendoActivity) getContext();
                if (slider == 1) activity.applyUiScale(v, true);
                else if (slider == 2) activity.applyFovOffset(v, true);
                else if (slider == 3) activity.applyFpsLimit(Math.round(v), true);
                else activity.applyRenderScale(v);
                invalidate();
                return;
            }
            android.graphics.RectF from = setRects.get(settingsFocus);
            int next = nextInDirection(setRects, from.centerX(), from.centerY(), dx, dy, settingsFocus);
            if (next >= 0) {
                settingsFocus = next;
                invalidate();
            }
        }

        /** A on a settings entry: what a tap on it does. */
        void settingsActivate() {
            if (langListOpen) {
                pickLanguage(langFocus); // A on a language: restart in it (the current one just closes the list)
                return;
            }
            buildSettingsEntries();
            FlipendoActivity activity = (FlipendoActivity) getContext();
            switch (focusedSetting()) {
                case SET_FPS: showFps = !showFps; fpsFrames = -1; activity.saveScales(); break;
                case SET_SEG: setDpadMode(!dpadMode); break;
                case SET_JUMP: activity.applyAutoJump(!autoJump, true); break;
                case SET_INVERT: invertWand = !invertWand; activity.saveScales(); break;
                case SET_LAYOUT: startLayoutEdit(); break;
                case SET_HELP: helpOpen = true; break;
                case SET_SWITCH: switchGameTap(); break;
                case SET_LANG: languageTap(); break;
                case SET_MOD: if (!isHp2Game()) modTap(); break;
                case SET_DEBUG:
                    if (isHp2Game()) { settingsOpen = false; activity.openHp2Debug(); } else activity.applyDebugMode(!debugMode, true);
                    break;
                case SET_TELEGRAM:
                    try {
                        getContext().startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(SetupActivity.CHANNEL_URL)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                    } catch (RuntimeException ignored) {
                    }
                    break;
                case SET_RENDERER: rendererTap(); break;
                case SET_DONE: settingsOpen = false; break;
                default: break;
            }
            invalidate();
        }

        /** The gold ring around what the pad is on (settings entry or menu item), only while a pad is in use. */
        void drawPadFocus(Canvas c, android.graphics.RectF r) {
            float u = Math.min(getWidth(), getHeight());
            line.setColor(GOLD);
            line.setStrokeWidth(0.006f * u);
            float rr = Math.min(r.height() / 2f, 0.03f * u);
            c.drawRoundRect(r.left - 0.006f * u, r.top - 0.006f * u, r.right + 0.006f * u, r.bottom + 0.006f * u, rr, rr, line);
            line.setStrokeWidth(4f);
        }

        void openSettings() {
            settingsFocus = -1; // the pad's View button puts it on the first entry
            langListOpen = false;
            releaseStick();
            for (Btn a : arrows) {
                if (a.down) setButton(a, false);
                a.pointer = -1;
            }
            arrowFingers.clear();
            settingsOpen = true;
            invalidate();
        }

        void setDpadMode(boolean on) {
            if (dpadMode == on) return;
            releaseStick();
            dpadMode = on;
            ((FlipendoActivity) getContext()).getSharedPreferences("flipendo", MODE_PRIVATE).edit().putBoolean("dpad", on).apply();
            invalidate();
        }

        static final class Btn {
            final String label;
            final int key;
            final int mouse;
            final boolean toggle;
            android.graphics.Bitmap icon;
            boolean tint; // black glyph: drawn white (yellow while pressed) so it shows on dark scenes
            float cx, cy, r;
            boolean down;
            int pointer = -1;

            Btn(String label, int key, int mouse, boolean toggle) {
                this.label = label;
                this.key = key;
                this.mouse = mouse;
                this.toggle = toggle;
            }
        }

        final Btn cast = new Btn("CAST", 0, MotionEvent.BUTTON_PRIMARY, false);
        final Btn jump = new Btn("JUMP", 0, MotionEvent.BUTTON_SECONDARY, false);
        final Btn menu = new Btn("MENU", KeyEvent.KEYCODE_ESCAPE, 0, false);
        final Btn save = new Btn("SAVE", 0, 0, false); // save anywhere (the icon, images/android/save.png, is optional)
        final Btn skip = new Btn("SKIP", KeyEvent.KEYCODE_SPACE, 0, false); // skips a cutscene (Space); shown only in cutscenes
        final Btn gear = new Btn("SET", 0, 0, false); // opens the settings panel
        // arrow buttons (movement in dpadMode): up, down, left, right, in the order of DIR_KEYS
        final Btn[] arrows = {
            new Btn("UP", KeyEvent.KEYCODE_DPAD_UP, 0, false), new Btn("DOWN", KeyEvent.KEYCODE_DPAD_DOWN, 0, false),
            new Btn("LEFT", KeyEvent.KEYCODE_DPAD_LEFT, 0, false), new Btn("RIGHT", KeyEvent.KEYCODE_DPAD_RIGHT, 0, false) };
        final Btn[] buttons = { cast, jump, menu, save, gear, skip, arrows[0], arrows[1], arrows[2], arrows[3] };

        // The prompt for a fixed-camera level (the movement mod can't aim there): a small panel that takes every touch.
        boolean promptOpen;
        String levelShown = "";
        String promptDeclinedLevel = "";
        final android.graphics.RectF promptPanel = new android.graphics.RectF(), promptYes = new android.graphics.RectF(), promptNo = new android.graphics.RectF();

        void openPrompt() {
            releaseGameplayControls();
            promptOpen = true;
            invalidate();
        }

        void acceptPrompt() {
            promptOpen = false;
            promptDeclinedLevel = levelShown; // whatever happens, no second prompt for this level
            modOffAfterSave = true;
            nativeRequestSave(); // the game thread saves at its next tick: onSaved switches the mod off and restarts
            postDelayed(new Runnable() { public void run() {
                if (modOffAfterSave) {
                    modOffAfterSave = false;
                    android.widget.Toast.makeText(getContext(), "Could not save: the mod stays on", android.widget.Toast.LENGTH_LONG).show();
                }
            } }, 8000);
            invalidate();
        }

        void drawPrompt(Canvas c) {
            if (!promptOpen) return;
            float u = Math.min(getWidth(), getHeight());
            fill.setColor(0xB0000000);
            c.drawRect(0, 0, getWidth(), getHeight(), fill);
            fill.setColor(0xF01C1C26);
            c.drawRoundRect(promptPanel, 0.03f * u, 0.03f * u, fill);
            line.setColor(0x66FFFFFF);
            line.setStrokeWidth(3f);
            c.drawRoundRect(promptPanel, 0.03f * u, 0.03f * u, line);
            float size = text.getTextSize();
            float cx = promptPanel.centerX();
            text.setTextSize(size * 1.4f);
            text.setColor(0xFFFFFFFF);
            c.drawText("Fixed camera level", cx, promptPanel.top + 0.12f * promptPanel.height(), text);
            text.setTextSize(size);
            text.setColor(0xCCFFFFFF);
            c.drawText("The game fixes the camera here and the movement mod aims along it,", cx, promptPanel.top + 0.30f * promptPanel.height(), text);
            c.drawText("so you can't aim. Switch the mod off? The game saves,", cx, promptPanel.top + 0.42f * promptPanel.height(), text);
            c.drawText("restarts and loads this save by itself.", cx, promptPanel.top + 0.54f * promptPanel.height(), text);
            drawSegment(c, promptYes, "Switch off and restart", true);
            drawSegment(c, promptNo, "Keep the mod", false);
        }

        // the settings card's geometry (onSizeChanged) and its text styles
        float headerY, dividerY, colL0, colL1, colR0, colR1, secDisplayY, secControlsY, secGameY;
        final float[] sliderLabelY = new float[4];
        static final int GOLD = 0xFFE8C15A, CARD = 0xF2181722, ROW = 0x14FFFFFF, MUTED = 0x99FFFFFF;
        final Paint titlePaint = new Paint(Paint.ANTI_ALIAS_FLAG), sectionPaint = new Paint(Paint.ANTI_ALIAS_FLAG),
                rowPaint = new Paint(Paint.ANTI_ALIAS_FLAG), subPaint = new Paint(Paint.ANTI_ALIAS_FLAG),
                valuePaint = new Paint(Paint.ANTI_ALIAS_FLAG);

        {
            titlePaint.setColor(0xFFFFFFFF);
            titlePaint.setFakeBoldText(true);
            sectionPaint.setColor(GOLD);
            sectionPaint.setFakeBoldText(true);
            sectionPaint.setLetterSpacing(0.12f);
            rowPaint.setColor(0xFFFFFFFF);
            subPaint.setColor(MUTED);
            valuePaint.setColor(GOLD);
            valuePaint.setTextAlign(Paint.Align.RIGHT);
        }

        boolean settingsOpen;    // the settings panel is up: it takes every touch
        boolean helpOpen;        // the controls help card over the settings panel
        final android.graphics.RectF panel = new android.graphics.RectF();
        final android.graphics.RectF segStick = new android.graphics.RectF(), segDpad = new android.graphics.RectF();
        final android.graphics.RectF fixBtn = new android.graphics.RectF(), switchBtn = new android.graphics.RectF(), helpBtn = new android.graphics.RectF(), layoutBtn = new android.graphics.RectF(), closeBtn = new android.graphics.RectF(), debugBtn = new android.graphics.RectF(), autoJumpBtn = new android.graphics.RectF(), invertBtn = new android.graphics.RectF(), telegramBtn = new android.graphics.RectF(), languageBtn = new android.graphics.RectF(), modBtn = new android.graphics.RectF(), fpsBtn = new android.graphics.RectF();

        final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
        final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
        final Paint iconPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        final Paint whitePaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        final Paint yellowPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        final android.graphics.RectF iconRect = new android.graphics.RectF();

        final HashMap<Integer, Role> roles = new HashMap<>();
        final HashMap<Integer, float[]> last = new HashMap<>();   // pointer -> last x,y
        final HashMap<Integer, float[]> start = new HashMap<>();  // pointer -> start x,y
        final HashMap<Integer, Long> downAt = new HashMap<>();    // pointer -> time it went down (two-finger tap)
        final java.util.HashSet<Integer> twoTapDone = new java.util.HashSet<>(); // fingers whose tap was the second half of a two-finger tap
        int stickPointer = -1;
        float stickX, stickY, knobX, knobY;
        final boolean[] dirDown = new boolean[4]; // up, down, left, right
        static final int[] DIR_KEYS = { KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN,
                KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT };

        Runnable menuHeld; // pending "hold MENU to send the logs"

        Overlay(android.content.Context c) {
            super(c);
            fill.setStyle(Paint.Style.FILL);
            line.setStyle(Paint.Style.STROKE);
            line.setStrokeWidth(4f);
            text.setTextAlign(Paint.Align.CENTER);
            text.setColor(0xCCFFFFFF);
            setFocusable(false);
            whitePaint.setColorFilter(new android.graphics.PorterDuffColorFilter(0xFFFFFFFF, android.graphics.PorterDuff.Mode.SRC_IN));
            yellowPaint.setColorFilter(new android.graphics.PorterDuffColorFilter(0xFFFFD84A, android.graphics.PorterDuff.Mode.SRC_IN));
            cast.icon = loadIcon(c, "wand.png");
            jump.icon = loadIcon(c, "jump.png");
            jump.tint = true;
            menu.icon = loadIcon(c, "menu.png");
            menu.tint = true;
            save.icon = loadIcon(c, "save.png"); // a coloured picture (book and quill), drawn like the wand: on a light disc
        }

        /** The button icons ship in the APK (assets/icons/, copied from images/android/ by build-apk.sh). */
        static android.graphics.Bitmap loadIcon(android.content.Context c, String name) {
            try {
                // decode at about 256 px: the pictures can be much larger than the buttons (save.png is 2500x2500)
                android.graphics.BitmapFactory.Options opts = new android.graphics.BitmapFactory.Options();
                opts.inJustDecodeBounds = true;
                try (InputStream in = c.getAssets().open("icons/" + name)) {
                    android.graphics.BitmapFactory.decodeStream(in, null, opts);
                }
                opts.inJustDecodeBounds = false;
                opts.inSampleSize = 1;
                while (Math.max(opts.outWidth, opts.outHeight) / (opts.inSampleSize * 2) >= 256) opts.inSampleSize *= 2;
                try (InputStream in = c.getAssets().open("icons/" + name)) {
                    return android.graphics.BitmapFactory.decodeStream(in, null, opts);
                }
            } catch (IOException e) {
                return null; // the button falls back to its text label
            }
        }

        @Override
        protected void onSizeChanged(int w, int h, int ow, int oh) {
            float u = Math.min(w, h);
            float dp = getResources().getDisplayMetrics().density;
            // the safe area: no control under a camera cutout, a visible system bar or the top edge's swipe zone (onApplyWindowInsets)
            float sl = insL, st = insT, sr = w - insR, sb = h - insB;
            // Controls, in the settings card's style, with one margin from every edge of the safe area; sizes follow the screen but
            // never get smaller than a fingertip
            float m = 0.05f * u;
            // top right: a toolbar (settings | save or skip | menu), three slots in one pill
            float barR = Math.max(0.05f * u, 22f * dp), barSlot = Math.max(0.12f * u, 54f * dp), barHalf = Math.max(0.055f * u, 25f * dp);
            float barY = st + m * 0.6f + barHalf;
            menu.cx = sr - m - barSlot / 2f; menu.cy = barY; menu.r = barR;
            save.cx = menu.cx - barSlot; save.cy = barY; save.r = barR;
            skip.cx = save.cx; skip.cy = save.cy; skip.r = save.r; // the book's place: the book is hidden in cutscenes
            gear.cx = save.cx - barSlot; gear.cy = barY; gear.r = barR;
            toolbar.set(gear.cx - barSlot / 2f, barY - barHalf, menu.cx + barSlot / 2f, barY + barHalf);
            // bottom right: the wand, and the jump to its lower left; with the mod the wand is a stick whose range ring must
            // clear the jump button: it sits higher
            cast.r = Math.max(0.104f * u, 40f * dp);
            cast.cx = sr - m - cast.r - 0.03f * u; cast.cy = sb - (modOn ? 0.40f : 0.21f) * u;
            jump.r = Math.max(0.08f * u, 32f * dp);
            jump.cx = cast.cx - cast.r - jump.r - 0.04f * u; jump.cy = sb - m - jump.r;
            text.setTextSize(0.035f * u);
            aimR = (isHp2Game() && !modOn && !broomShown ? 0.045f : 0.12f) * u; // how far the wand button follows the finger (HP2: a small ring)

            // bottom left: the arrows on one round pad
            float ad = 0.15f * u;
            padR = ad + 0.085f * u;
            padX = sl + m + padR; padY = sb - m - padR;
            arrows[0].cx = padX; arrows[0].cy = padY - ad;
            arrows[1].cx = padX; arrows[1].cy = padY + ad;
            arrows[2].cx = padX - ad; arrows[2].cy = padY;
            arrows[3].cx = padX + ad; arrows[3].cy = padY;
            for (Btn a : arrows) a.r = 0.08f * u;
            applyLayout(w, h);

            // The settings panel: a card with a header, two columns (Display: the sliders | Controls and Game: switches) and a footer.
            float pw = Math.min(0.94f * (sr - sl), 2.1f * h);
            float ph = Math.min(0.9f * h, sb - st);
            float pcx = (sl + sr) / 2f, pcy = (st + sb) / 2f;
            panel.set(pcx - pw / 2f, pcy - ph / 2f, pcx + pw / 2f, pcy + ph / 2f);
            float pad = 0.035f * pw;
            float gap = 0.02f * pw;
            headerY = panel.top + 0.1f * h;
            dividerY = panel.top + 0.145f * h;
            float contentTop = dividerY + 0.03f * h;
            float footerTop = panel.bottom - 0.115f * h;
            colL0 = panel.left + pad;
            colL1 = panel.centerX() - pad / 2f;
            colR0 = panel.centerX() + pad / 2f;
            colR1 = panel.right - pad;

            // left: Display, three sliders sharing the column's height
            secDisplayY = contentTop + 0.03f * h;
            float knobR = 0.026f * u;
            sliderX0 = colL0 + knobR;
            sliderX1 = colL1 - knobR;
            // the FPS counter switch at the bottom of the column, the sliders share what is above it
            fpsBtn.set(colL0, footerTop - 0.035f * h - 0.078f * h, colL1, footerTop - 0.035f * h);
            float slot = (fpsBtn.top - 0.015f * h - (secDisplayY + 0.015f * h)) / SLIDERS;
            for (int s = 0; s < SLIDERS; s++) {
                sliderLabelY[s] = secDisplayY + 0.015f * h + s * slot + 0.047f * h;
                sliderY[s] = sliderLabelY[s] + 0.043f * h;
            }

            // right: Controls (movement segments, Auto jump) and Game (Language, Movement mod, Debug mode)
            float rowH = 0.058f * h, rowGap = 0.006f * h;
            secControlsY = contentTop + 0.03f * h;
            float y = secControlsY + 0.025f * h;
            segStick.set(colR0, y, (colR0 + colR1) / 2f, y + rowH);
            segDpad.set((colR0 + colR1) / 2f, y, colR1, y + rowH);
            y += rowH + rowGap;
            autoJumpBtn.set(colR0, y, colR1, y + rowH);
            y += rowH + rowGap;
            invertBtn.set(colR0, y, colR1, y + rowH);
            y += rowH + rowGap;
            layoutBtn.set(colR0, y, colR1, y + rowH);
            y += rowH + 0.012f * h;
            secGameY = y + 0.03f * h;
            y = secGameY + 0.02f * h;
            if (languages.size() > 1) {
                languageBtn.set(colR0, y, colR1, y + rowH);
                y += rowH + rowGap;
            } else {
                languageBtn.setEmpty();
            }
            if (isHp2Game()) {
                modBtn.setEmpty(); // the movement mod is HP1's: no row in HP2
            } else {
                modBtn.set(colR0, y, colR1, y + rowH); // always: without a mod it imports one from a file
                y += rowH + rowGap;
            }
            debugBtn.set(colR0, y, colR1, y + rowH);

            // footer: the channel on the left, Done on the right
            float footH = 0.075f * h;
            float footY = panel.bottom - 0.03f * h - footH;
            telegramBtn.set(colL0, footY, colL0 + 0.36f * pw, footY + footH);
            closeBtn.set(colR1 - 0.2f * pw, footY, colR1, footY + footH);
            rendererBtn.set(telegramBtn.right + gap, footY, closeBtn.left - gap, footY + footH); // between the channel and Done

            float ppw = Math.min(0.8f * w, 1.7f * h);
            promptPanel.set((w - ppw) / 2f, 0.22f * h, (w + ppw) / 2f, 0.78f * h);
            promptYes.set(promptPanel.left + 0.05f * ppw, promptPanel.bottom - 0.28f * promptPanel.height(), w / 2f - gap, promptPanel.bottom - 0.08f * promptPanel.height());
            promptNo.set(w / 2f + gap, promptYes.top, promptPanel.right - 0.05f * ppw, promptYes.bottom);

            helpBtn.set(colL0 + 0.34f * u, headerY - 0.052f * h, colL0 + 0.56f * u, headerY + 0.008f * h);
            fixBtn.set(helpBtn.right + 0.02f * u, helpBtn.top, helpBtn.right + 0.30f * u, helpBtn.bottom);
            switchBtn.set(fixBtn.right + 0.02f * u, helpBtn.top, fixBtn.right + 0.38f * u, helpBtn.bottom);
            versionRect.set(colR1 - 0.5f * u, headerY - 0.07f * h, colR1 + 0.01f * u, headerY + 0.03f * h); // the "Flipendo for Android x.y.z" text
            layoutEditRects(w, h);

            titlePaint.setTextSize(0.055f * u);
            sectionPaint.setTextSize(0.026f * u);
            rowPaint.setTextSize(0.033f * u);
            subPaint.setTextSize(0.024f * u);
            valuePaint.setTextSize(0.031f * u);
            label.setTextSize(0.03f * u);
            label.setColor(0xFFFFFFFF);
            label.setShadowLayer(4f, 0f, 0f, 0xFF000000);
        }

        final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);

        // sliders: 0 = render scale, 1 = subtitles and HUD size, 2 = field of view offset, 3 = FPS limit
        static final int SLIDERS = 4;

        float sliderMin(int i) { return i == 0 ? RENDER_SCALE_MIN : i == 1 ? UI_SCALE_MIN : i == 2 ? FOV_MIN : FPS_LIMIT_MIN; }

        float sliderMax(int i) { return i == 0 ? RENDER_SCALE_MAX : i == 1 ? UI_SCALE_MAX : i == 2 ? FOV_MAX : FPS_LIMIT_MAX; }

        float sliderStep(int i) { return i == 0 ? 0.05f : i == 1 ? 0.1f : 5f; }

        float sliderValue(int i) {
            return i == 0 ? (activeSlider == 0 ? pendingRender : surfaceScale) : i == 1 ? uiScale : i == 2 ? fovOffset : fpsLimit;
        }

        /** The settings entry id of a slider (the pad's entries) and back: sliders 0-2 are ids 0-2, the FPS limit is SET_FPS_LIMIT. */
        int sliderId(int i) { return i == 3 ? SET_FPS_LIMIT : i; }

        int sliderOfId(int id) { return id == SET_FPS_LIMIT ? 3 : id <= SET_FOV ? id : -1; }

        /** The slider under a touch (menu open only), or -1. */
        int hitSlider(float x, float y) {
            if (!settingsOpen) return -1;
            float u = Math.min(getWidth(), getHeight());
            for (int i = 0; i < SLIDERS; i++) {
                if (Math.abs(y - sliderY[i]) < 0.045f * u && x > sliderX0 - 0.05f * u && x < sliderX1 + 0.05f * u) return i;
            }
            return -1;
        }

        /** Moves a slider to the finger; the UI size is live, the render scale is applied when the finger lifts (commit). */
        void setSliderFromX(int i, float x, boolean commit) {
            float t = Math.max(0f, Math.min(1f, (x - sliderX0) / (sliderX1 - sliderX0)));
            float v = sliderMin(i) + t * (sliderMax(i) - sliderMin(i));
            float step = sliderStep(i);
            v = Math.round(v / step) * step;
            FlipendoActivity activity = (FlipendoActivity) getContext();
            if (i == 1) {
                activity.applyUiScale(v, commit);
            } else if (i == 2) {
                activity.applyFovOffset(v, commit);
            } else if (i == 3) {
                activity.applyFpsLimit(Math.round(v), commit);
            } else {
                pendingRender = v;
                if (commit) activity.applyRenderScale(v);
            }
            invalidate();
        }

        void drawSegment(Canvas c, android.graphics.RectF r, String name, boolean on) {
            fill.setColor(on ? 0xFFFFD84A : 0x33FFFFFF);
            c.drawRoundRect(r, r.height() / 2f, r.height() / 2f, fill);
            text.setColor(on ? 0xFF000000 : 0xFFFFFFFF);
            c.drawText(name, r.centerX(), r.centerY() + text.getTextSize() * 0.35f, text);
            text.setColor(0xCCFFFFFF);
        }

        void drawSettings(Canvas c) {
            if (!settingsOpen) return;
            float u = Math.min(getWidth(), getHeight());
            float h = getHeight();
            fill.setColor(0xB8000000);
            c.drawRect(0, 0, getWidth(), h, fill);
            float r = 0.035f * u;
            fill.setColor(CARD);
            c.drawRoundRect(panel, r, r, fill);
            line.setColor(0x33FFFFFF);
            line.setStrokeWidth(2f);
            c.drawRoundRect(panel, r, r, line);

            // header: a gold accent, the title, and a hairline under it
            fill.setColor(GOLD);
            c.drawRoundRect(colL0, headerY - 0.045f * h, colL0 + 0.008f * u, headerY + 0.005f * h, 4f, 4f, fill);
            titlePaint.setTextAlign(Paint.Align.LEFT);
            c.drawText("Settings", colL0 + 0.025f * u, headerY, titlePaint);
            subPaint.setTextAlign(Paint.Align.RIGHT);
            c.drawText("Flipendo for Android " + appVersion, colR1, headerY, subPaint);
            subPaint.setTextAlign(Paint.Align.LEFT);
            line.setColor(0x22FFFFFF);
            line.setStrokeWidth(2f);
            c.drawLine(colL0, dividerY, colR1, dividerY, line);
            c.drawLine(panel.centerX(), dividerY + 0.03f * h, panel.centerX(), telegramBtn.top - 0.03f * h, line);

            // left column
            drawSection(c, "DISPLAY", colL0, secDisplayY);
            drawSliders(c);
            drawSwitchRow(c, fpsBtn, "FPS counter", "Frames per second next to the toolbar", showFps, false);

            // right column
            drawSection(c, "CONTROLS", colR0, secControlsY);
            drawSegments(c);
            drawSwitchRow(c, autoJumpBtn, "Auto jump", "Harry jumps by himself at ledges", autoJump, false);
            drawSwitchRow(c, invertBtn, "Invert wand", "Flip the up/down aim of the wand button while the spell is held", invertWand, false);
            drawValueRow(c, layoutBtn, "Control layout", "Move and resize the buttons", "Edit", false);
            drawSection(c, "GAME", colR0, secGameY);
            if (languages.size() > 1) {
                String current = languages.get(languageIndex(currentLanguage()))[1];
                drawValueRow(c, languageBtn, "Language", "Game texts (and voices, where the build has them)", current, langListOpen);
            }
            if (isHp2Game()) {
                // no movement mod in HP2
            } else if (mods.isEmpty()) {
                drawValueRow(c, modBtn, "Movement mod", modAsk
                        ? "Tap again: pick the mod's zip, the game restarts (unsaved progress is lost)"
                        : "By AdamJD: camera, climbing, aim. Pick its zip (or .u files)", "Choose file", modAsk);
            } else {
                drawSwitchRow(c, modBtn, "Movement mod",
                        modAsk ? "Tap again to restart " + (modOn ? "without" : "with") + " it (unsaved progress is lost)" : "By AdamJD: camera, climbing, aim",
                        modOn, modAsk);
            }
            if (isHp2Game()) drawValueRow(c, debugBtn, "Debug menu", "Level select, story state, all spells", "Open", false);
            else drawSwitchRow(c, debugBtn, "Debug mode", "Level Select in the main menu", debugMode, false);

            // footer
            fill.setColor(0x334A9EE8);
            c.drawRoundRect(telegramBtn, telegramBtn.height() / 2f, telegramBtn.height() / 2f, fill);
            rowPaint.setTextAlign(Paint.Align.CENTER);
            rowPaint.setColor(0xFF8CC8FF);
            c.drawText("Telegram: t.me/flipendodroid", telegramBtn.centerX(), telegramBtn.centerY() + rowPaint.getTextSize() * 0.35f, rowPaint);
            fill.setColor(GOLD);
            c.drawRoundRect(closeBtn, closeBtn.height() / 2f, closeBtn.height() / 2f, fill);
            rowPaint.setColor(0xFF1A1A1A);
            c.drawText("Done", closeBtn.centerX(), closeBtn.centerY() + rowPaint.getTextSize() * 0.35f, rowPaint);
            fill.setColor(rendererAsk ? 0x33E8C15A : ROW);
            c.drawRoundRect(rendererBtn, rendererBtn.height() / 2f, rendererBtn.height() / 2f, fill);
            rowPaint.setColor(rendererAsk ? GOLD : 0xFFFFFFFF);
            c.drawText(rendererAsk ? "Tap again: restart with " + rendererLabel(!rendererGL) : "Renderer: " + rendererLabel(rendererGL) + (rendererAuto ? " (auto)" : ""),
                    rendererBtn.centerX(), rendererBtn.centerY() + rowPaint.getTextSize() * 0.35f, rowPaint);
            rowPaint.setColor(0xFFFFFFFF);
            rowPaint.setTextAlign(Paint.Align.LEFT);
            // the help pill in the header
            fill.setColor(0x33E8C15A);
            c.drawRoundRect(helpBtn, helpBtn.height() / 2f, helpBtn.height() / 2f, fill);
            rowPaint.setTextAlign(Paint.Align.CENTER);
            rowPaint.setColor(GOLD);
            c.drawText("? How to play", helpBtn.centerX(), helpBtn.centerY() + rowPaint.getTextSize() * 0.35f, rowPaint);
            fill.setColor(0x22FFFFFF);
            c.drawRoundRect(fixBtn, fixBtn.height() / 2f, fixBtn.height() / 2f, fill);
            rowPaint.setColor(0xFFFFFFFF);
            c.drawText("Graphics fix", fixBtn.centerX(), fixBtn.centerY() + rowPaint.getTextSize() * 0.35f, rowPaint);
            fill.setColor(switchAsk ? 0x33E8C15A : 0x22FFFFFF);
            c.drawRoundRect(switchBtn, switchBtn.height() / 2f, switchBtn.height() / 2f, fill);
            rowPaint.setColor(switchAsk ? GOLD : 0xFFFFFFFF);
            c.drawText(switchAsk ? "Tap again" : (isHp2Game() ? "Play HP1" : "Play HP2"), switchBtn.centerX(), switchBtn.centerY() + rowPaint.getTextSize() * 0.35f, rowPaint);
            rowPaint.setColor(GOLD);
            rowPaint.setColor(0xFFFFFFFF);
            rowPaint.setTextAlign(Paint.Align.LEFT);
            if (padActive && settingsFocus >= 0 && !langListOpen) {
                buildSettingsEntries();
                if (focusedSetting() >= 0) drawPadFocus(c, setRects.get(settingsFocus));
            }
            if (helpOpen) drawHelp(c);
        }

        /** The controls card (Settings > ? How to play): what every control does, and the HP2 extras. */
        void drawHelp(Canvas c) {
            float u = Math.min(getWidth(), getHeight());
            float h = getHeight();
            fill.setColor(0xDD000000);
            c.drawRect(0, 0, getWidth(), h, fill);
            fill.setColor(CARD);
            c.drawRoundRect(panel, 0.035f * u, 0.035f * u, fill);
            line.setColor(0x33FFFFFF);
            line.setStrokeWidth(2f);
            c.drawRoundRect(panel, 0.035f * u, 0.035f * u, line);
            titlePaint.setTextAlign(Paint.Align.LEFT);
            c.drawText("How to play", colL0, headerY, titlePaint);
            boolean hp2 = isHp2Game();
            String[][] rows = {
                { "Left thumb", "Drag anywhere on the left half to walk (or the arrow pad, if Settings > Controls has it)" },
                { "Right thumb", "Drag to look around. In menus the finger is the mouse: tap, drag the sliders" },
                { "Wand button", "Cast the spell. In a spell lesson draw the symbol with your finger" },
                { "Jump button", "Jump (Auto jump does it at ledges). The same key skips a cutscene" },
                { "Toolbar", "Settings, save anywhere (the book) and the pause menu (Esc). Hold Esc to send the logs" },
                { "Control layout", "Settings > Control layout: drag the buttons where you want them, and resize them" },
                hp2 ? new String[] { "Two-finger tap", "Tap the screen with two fingers: drink a Wiggenweld potion" } : null,
                { "Gamepad", hp2 ? "A jump, X wand, Y save, B menu, a stick click drinks the potion. Hold Y to skip a cutscene" : "A jump, X wand, Y save, B menu. Hold Y to skip a cutscene" },
            };
            float y = dividerY + 0.07f * h;
            float step = (panel.bottom - 0.12f * h - y) / (hp2 ? 8 : 7);
            for (String[] row : rows) {
                if (row == null) continue;
                rowPaint.setTextAlign(Paint.Align.LEFT);
                rowPaint.setColor(GOLD);
                c.drawText(row[0], colL0, y, rowPaint);
                subPaint.setTextAlign(Paint.Align.LEFT);
                subPaint.setColor(0xDDFFFFFF);
                c.drawText(row[1], colL0 + 0.27f * (colR1 - colL0), y, subPaint);
                y += step;
            }
            subPaint.setColor(MUTED);
            subPaint.setTextAlign(Paint.Align.CENTER);
            c.drawText("Touch anywhere to close", panel.centerX(), panel.bottom - 0.04f * h, subPaint);
            subPaint.setTextAlign(Paint.Align.LEFT);
            rowPaint.setColor(0xFFFFFFFF);
        }

        void drawSection(Canvas c, String name, float x, float y) {
            sectionPaint.setTextAlign(Paint.Align.LEFT);
            c.drawText(name, x, y, sectionPaint);
        }

        final android.graphics.RectF segAll = new android.graphics.RectF();

        /** Stick | Arrows as one control: a track with the chosen half filled. */
        void drawSegments(Canvas c) {
            segAll.set(segStick.left, segStick.top, segDpad.right, segDpad.bottom);
            float rr = segAll.height() / 2f;
            fill.setColor(ROW);
            c.drawRoundRect(segAll, rr, rr, fill);
            android.graphics.RectF on = dpadMode ? segDpad : segStick;
            float inset = 0.08f * segAll.height();
            fill.setColor(GOLD);
            c.drawRoundRect(on.left + inset, on.top + inset, on.right - inset, on.bottom - inset, rr - inset, rr - inset, fill);
            rowPaint.setTextAlign(Paint.Align.CENTER);
            float base = rowPaint.getTextSize() * 0.35f;
            rowPaint.setColor(dpadMode ? 0xFFFFFFFF : 0xFF1A1A1A);
            c.drawText("Floating stick", segStick.centerX(), segStick.centerY() + base, rowPaint);
            rowPaint.setColor(dpadMode ? 0xFF1A1A1A : 0xFFFFFFFF);
            c.drawText("Arrow buttons", segDpad.centerX(), segDpad.centerY() + base, rowPaint);
            rowPaint.setColor(0xFFFFFFFF);
            rowPaint.setTextAlign(Paint.Align.LEFT);
        }

        float rowPad(android.graphics.RectF row) {
            return 0.02f * (colR1 - colR0) + 0.035f * row.height();
        }

        /** A row with a title, a hint under it and a switch on the right; pending: a confirmation is asked (gold outline). */
        void drawSwitchRow(Canvas c, android.graphics.RectF row, String title, String hint, boolean on, boolean pending) {
            drawRowBase(c, row, pending);
            drawRowTexts(c, row, title, hint, pending);
            float th = 0.42f * row.height(), tw = 1.8f * th;
            float tx1 = row.right - rowPad(row), tx0 = tx1 - tw, ty0 = row.centerY() - th / 2f, ty1 = ty0 + th;
            fill.setColor(on ? GOLD : 0x33FFFFFF);
            c.drawRoundRect(tx0, ty0, tx1, ty1, th / 2f, th / 2f, fill);
            fill.setColor(0xFFFFFFFF);
            c.drawCircle(on ? tx1 - th / 2f : tx0 + th / 2f, row.centerY(), 0.38f * th, fill);
        }

        /** A row with a title, a hint and a value on the right (opens a choice). */
        void drawValueRow(Canvas c, android.graphics.RectF row, String title, String hint, String value, boolean pending) {
            drawRowBase(c, row, pending);
            drawRowTexts(c, row, title, hint, pending);
            c.drawText(value + "  ›", row.right - rowPad(row), row.centerY() + valuePaint.getTextSize() * 0.35f, valuePaint);
        }

        void drawRowBase(Canvas c, android.graphics.RectF row, boolean pending) {
            float rr = 0.25f * row.height();
            fill.setColor(pending ? 0x22E8C15A : ROW);
            c.drawRoundRect(row, rr, rr, fill);
            if (pending) {
                line.setColor(GOLD);
                line.setStrokeWidth(2f);
                c.drawRoundRect(row, rr, rr, line);
            }
        }

        void drawRowTexts(Canvas c, android.graphics.RectF row, String title, String hint, boolean pending) {
            float x = row.left + rowPad(row);
            rowPaint.setTextAlign(Paint.Align.LEFT);
            c.drawText(title, x, row.centerY() - 0.04f * row.height(), rowPaint);
            subPaint.setTextAlign(Paint.Align.LEFT);
            subPaint.setColor(pending ? GOLD : MUTED);
            c.drawText(hint, x, row.centerY() + subPaint.getTextSize() * 1.15f, subPaint);
            subPaint.setColor(MUTED);
        }

        void drawSliders(Canvas c) {
            float u = Math.min(getWidth(), getHeight());
            int[] size = ((FlipendoActivity) getContext()).viewSize();
            int longSide = size[0], shortSide = size[1];
            for (int i = 0; i < SLIDERS; i++) {
                float v = sliderValue(i);
                String name = i == 0 ? "Render scale" : i == 1 ? "Subtitles and HUD size" : i == 2 ? "Field of view" : "FPS limit";
                String value = i == 0 ? Math.round(v * 100) + "%  ·  " + (int) (longSide * v) + "×" + (int) (shortSide * v)
                        : i == 1 ? Math.round(v * 100) + "%"
                        : i == 3 ? Math.round(v) + " FPS"
                        : (v > 0 ? "+" : "") + Math.round(v) + "°" + (v == 0 ? "  (default)" : "");
                rowPaint.setTextAlign(Paint.Align.LEFT);
                c.drawText(name, colL0, sliderLabelY[i], rowPaint);
                c.drawText(value, colL1, sliderLabelY[i], valuePaint);
                float t = (v - sliderMin(i)) / (sliderMax(i) - sliderMin(i));
                float kx = sliderX0 + t * (sliderX1 - sliderX0);
                line.setStrokeCap(Paint.Cap.ROUND);
                line.setStrokeWidth(0.01f * u);
                line.setColor(0x33FFFFFF);
                c.drawLine(sliderX0, sliderY[i], sliderX1, sliderY[i], line);
                line.setColor(GOLD);
                c.drawLine(sliderX0, sliderY[i], kx, sliderY[i], line);
                line.setStrokeCap(Paint.Cap.BUTT);
                float kr = activeSlider == i ? 0.03f * u : 0.024f * u;
                fill.setColor(0xFFFFFFFF);
                c.drawCircle(kx, sliderY[i], kr, fill);
                line.setColor(GOLD);
                line.setStrokeWidth(0.005f * u);
                c.drawCircle(kx, sliderY[i], kr, line);
            }
            line.setStrokeWidth(4f);
        }

        Btn hitButton(float x, float y) {
            for (Btn b : buttons) {
                if (!visible(b)) continue;
                float dx = x - b.cx, dy = y - b.cy;
                if (dx * dx + dy * dy <= b.r * b.r * 1.3f) return b;
            }
            return null;
        }

        void key(int keycode, boolean down) {
            if (down) SDLActivity.onNativeKeyDown(keycode); else SDLActivity.onNativeKeyUp(keycode);
        }

        void setButton(Btn b, boolean down) {
            if (b == save) {
                b.down = down;
                if (down) nativeRequestSave(); // saved by the game thread at its next tick, see onSaved
                return;
            }
            if (b == gear) {
                b.down = false;
                if (down) openSettings();
                return;
            }
            b.down = down;
            if (b.mouse != 0)
                SDLActivity.onNativeMouse(down ? b.mouse : 0, down ? MotionEvent.ACTION_DOWN : MotionEvent.ACTION_UP,
                        0f, 0f, true); // a click without moving the cursor: an absolute position here turned the mod's camera
            else
                key(b.key, down);
        }

        void setDir(int i, boolean down) {
            if (dirDown[i] != down) {
                dirDown[i] = down;
                key(DIR_KEYS[i], down);
                if (down) eggInput(i);
            }
        }

        // Two easter eggs: a code on the directions (the stick, the arrows or the pad's d-pad, one press after another, at most 2.5 s
        // apart) opens a link. Up, up, down, down, left, right, left, right in Chamber of Secrets; down, down, up, up, right, left,
        // right, left in Philosopher's Stone. Directions: 0 up, 1 down, 2 left, 3 right (DIR_KEYS).
        static final int[] EGG_HP2 = { 0, 0, 1, 1, 2, 3, 2, 3 };
        static final int[] EGG_HP1 = { 1, 1, 0, 0, 3, 2, 3, 2 };
        static final String EGG_HP2_URL = "https://www.youtube.com/watch?v=VlTIbCgpIKE";
        static final String EGG_HP1_URL = "https://www.youtube.com/watch?v=Ud9I7yAmZ4o";
        final int[] eggBuffer = new int[8];
        int eggCount;
        long eggLast, eggOpened;

        // The simple way in: five taps in a row on the version text in the settings header (like Android's build number).
        final android.graphics.RectF versionRect = new android.graphics.RectF();
        int versionTaps;
        long versionTapTime;

        void versionTap() {
            long now = android.os.SystemClock.uptimeMillis();
            if (now - versionTapTime > 1500) versionTaps = 0;
            versionTapTime = now;
            versionTaps++;
            if (versionTaps >= 5) {
                versionTaps = 0;
                openEgg();
            }
        }

        void openEgg() {
            eggOpened = android.os.SystemClock.uptimeMillis();
            try {
                getContext().startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(isHp2Game() ? EGG_HP2_URL : EGG_HP1_URL)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            } catch (RuntimeException ignored) {
            }
        }

        void eggInput(int dir) {
            if (menuShown || settingsOpen || promptOpen || layoutEdit || cutsceneShown) {
                eggCount = 0;
                return;
            }
            long now = android.os.SystemClock.uptimeMillis();
            if (now - eggLast > 2500) eggCount = 0;
            eggLast = now;
            if (eggCount == eggBuffer.length) {
                System.arraycopy(eggBuffer, 1, eggBuffer, 0, eggBuffer.length - 1);
                eggCount--;
            }
            eggBuffer[eggCount++] = dir;
            if (eggCount < eggBuffer.length || now - eggOpened < 30000) return;
            int[] code = isHp2Game() ? EGG_HP2 : EGG_HP1;
            for (int k = 0; k < code.length; k++) {
                if (eggBuffer[k] != code[k]) return;
            }
            eggOpened = now;
            eggCount = 0;
            try {
                getContext().startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(isHp2Game() ? EGG_HP2_URL : EGG_HP1_URL)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            } catch (RuntimeException ignored) {
            }
        }

        void updateStick() {
            float dx = knobX - stickX, dy = knobY - stickY;
            float dead = 0.04f * Math.min(getWidth(), getHeight());
            setDir(0, dy < -dead);
            setDir(1, dy > dead);
            setDir(2, dx < -dead);
            setDir(3, dx > dead);
        }

        void releaseStick() {
            stickPointer = -1;
            for (int i = 0; i < 4; i++) setDir(i, false);
        }

        /** A finger lifting after a short press without moving, while another finger of the same kind is still down in the same
         *  way: a two-finger tap. It drinks the Wiggenweld potion (HP2 binds it to Q; the game ignores it at full health). */
        boolean twoFingerTap(MotionEvent e, int idx, float[] s, float slop) {
            long now = e.getEventTime();
            Long d = downAt.get(e.getPointerId(idx));
            if (d == null || now - d > 400 || Math.hypot(e.getX(idx) - s[0], e.getY(idx) - s[1]) > slop) return false;
            for (int i = 0; i < e.getPointerCount(); i++) {
                if (i == idx) continue;
                int pid = e.getPointerId(i);
                Role r = roles.get(pid);
                float[] s2 = start.get(pid);
                Long d2 = downAt.get(pid);
                if ((r != Role.STICK && r != Role.LOOK) || s2 == null || d2 == null || pid == stickPointer) continue;
                if (now - d2 > 600 || Math.hypot(e.getX(i) - s2[0], e.getY(i) - s2[1]) > slop) continue;
                twoTapDone.add(pid);
                drinkPotion();
                return true;
            }
            return false;
        }

        void drinkPotion() {
            key(KeyEvent.KEYCODE_Q, true);
            postDelayed(new Runnable() { public void run() { key(KeyEvent.KEYCODE_Q, false); } }, 90);
        }

        void click(final float sx, final float sy) {
            // The menu reads the cursor position once per frame: move first, then press and release on later frames.
            final float x = sx * surfaceScale, y = sy * surfaceScale;
            SDLActivity.onNativeMouse(0, MotionEvent.ACTION_MOVE, x, y, false);
            postDelayed(new Runnable() { public void run() {
                SDLActivity.onNativeMouse(MotionEvent.BUTTON_PRIMARY, MotionEvent.ACTION_DOWN, x, y, false);
            } }, 80);
            postDelayed(new Runnable() { public void run() {
                SDLActivity.onNativeMouse(0, MotionEvent.ACTION_UP, x, y, false);
            } }, 200);
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            int action = e.getActionMasked();
            int idx = e.getActionIndex();
            int id = e.getPointerId(idx);
            if (layoutEdit) {
                layoutTouch(e);
                return true;
            }
            float slop = 0.03f * Math.min(getWidth(), getHeight());
            switch (action) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_POINTER_DOWN: {
                    float x = e.getX(idx), y = e.getY(idx);
                    if (padActive) {
                        padActive = false; // a touch: the on-screen controls are back
                        setLessonTimeScale(1f);
                        menuFocus = -1;
                        settingsFocus = -1;
                        invalidate();
                    }
                    if (promptOpen) {
                        // the prompt takes every touch
                        if (promptYes.contains(x, y)) {
                            acceptPrompt();
                        } else if (promptNo.contains(x, y)) {
                            promptOpen = false;
                            promptDeclinedLevel = levelShown;
                        }
                        break;
                    }
                    if (settingsOpen && langListOpen) {
                        // the language list takes every touch: a row picks it, outside closes the list
                        int picked = -1;
                        for (int i = 0; i < langRows.size(); i++) {
                            if (langRows.get(i).contains(x, y)) picked = i;
                        }
                        if (picked >= 0) {
                            pickLanguage(picked);
                        } else if (!langPanel.contains(x, y)) {
                            langListOpen = false;
                        }
                        invalidate();
                        break;
                    }
                    if (settingsOpen && helpOpen) {
                        helpOpen = false; // any touch closes the help card
                        break;
                    }
                    if (settingsOpen) {
                        // the panel takes every touch
                        if (helpBtn.contains(x, y)) {
                            helpOpen = true;
                            break;
                        }
                        if (switchBtn.contains(x, y)) {
                            switchGameTap();
                            break;
                        }
                        if (fixBtn.contains(x, y)) {
                            settingsOpen = false;
                            ((FlipendoActivity) getContext()).openGraphicsFix();
                            break;
                        }
                        if (versionRect.contains(x, y)) {
                            versionTap();
                            break;
                        }
                        if (hitSlider(x, y) >= 0) {
                            roles.put(id, Role.SLIDER);
                            activeSlider = hitSlider(x, y);
                            pendingRender = surfaceScale;
                            setSliderFromX(activeSlider, x, false);
                        } else if (segStick.contains(x, y)) {
                            setDpadMode(false);
                        } else if (segDpad.contains(x, y)) {
                            setDpadMode(true);
                        } else if (autoJumpBtn.contains(x, y)) {
                            ((FlipendoActivity) getContext()).applyAutoJump(!autoJump, true);
                        } else if (invertBtn.contains(x, y)) {
                            invertWand = !invertWand;
                            ((FlipendoActivity) getContext()).saveScales();
                        } else if (layoutBtn.contains(x, y)) {
                            startLayoutEdit();
                        } else if (fpsBtn.contains(x, y)) {
                            showFps = !showFps;
                            fpsFrames = -1;
                            ((FlipendoActivity) getContext()).saveScales();
                        } else if (debugBtn.contains(x, y)) {
                            if (isHp2Game()) {
                                settingsOpen = false;
                                ((FlipendoActivity) getContext()).openHp2Debug();
                            } else {
                                ((FlipendoActivity) getContext()).applyDebugMode(!debugMode, true);
                            }
                        } else if (!isHp2Game() && modBtn.contains(x, y)) {
                            modTap();
                        } else if (languages.size() > 1 && languageBtn.contains(x, y)) {
                            languageTap();
                        } else if (rendererBtn.contains(x, y)) {
                            rendererTap();
                        } else if (telegramBtn.contains(x, y)) {
                            try {
                                getContext().startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(SetupActivity.CHANNEL_URL)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                            } catch (RuntimeException ignored) {
                            }
                        } else if (closeBtn.contains(x, y) || !panel.contains(x, y)) {
                            settingsOpen = false;
                        }
                        break;
                    }
                    Btn b = hitButton(x, y);
                    if (b != null) {
                        roles.put(id, Role.BUTTON);
                        b.pointer = id;
                        if (isArrow(b)) arrowFingers.add(id);
                        if (b == cast && aimStick()) {
                            // the wand button is a stick too: where the finger went down is its centre
                            aimPointer = id;
                            aimCx = x;
                            aimCy = y;
                            aimDx = aimDy = 0;
                            post(aimTick);
                        }
                        if (broomShown && b == cast) {
                            // on the broom it only steers: no brake
                        } else if (broomShown && b == jump) {
                            setButton(b, !b.down); // the boost toggles
                        } else if (b.toggle) setButton(b, !b.down); else setButton(b, true);
                        if (b == menu) {
                            // Holding MENU for a moment sends the logs (also when the game is stuck or black).
                            menuHeld = new Runnable() { public void run() {
                                menuHeld = null;
                                LogReport.share((FlipendoActivity) getContext());
                            } };
                            postDelayed(menuHeld, 1500);
                        }
                    } else if (menuActive()) {
                        // Menu: the finger is the mouse. Move the cursor now, press a few frames later and keep
                        // following the finger so sliders can be dragged.
                        roles.put(id, Role.MENU);
                        menuDownTime = e.getEventTime();
                        menuX = x * surfaceScale;
                        menuY = y * surfaceScale;
                        SDLActivity.onNativeMouse(0, MotionEvent.ACTION_MOVE, menuX, menuY, false);
                        postDelayed(new Runnable() { public void run() {
                            SDLActivity.onNativeMouse(MotionEvent.BUTTON_PRIMARY, MotionEvent.ACTION_DOWN, menuX, menuY, false);
                        } }, 60);
                    } else if (lessonDrawing()) {
                        // Spell lesson: the finger is the wand. Put it under the finger, then hold AltFire (left mouse).
                        roles.put(id, Role.DRAW);
                        nativeSetTouch(x / getWidth(), y / getHeight(), true);
                        SDLActivity.onNativeMouse(MotionEvent.BUTTON_PRIMARY, MotionEvent.ACTION_DOWN,
                                getWidth() / 2f * surfaceScale, getHeight() / 2f * surfaceScale, false);
                    } else if (cutsceneShown) {
                        // a cutscene holds Harry: the screen does not steer or look (the buttons above still work)
                    } else {
                        roles.put(id, !dpadMode && x < getWidth() / 2f ? Role.STICK : Role.LOOK);
                        last.put(id, new float[] { x, y });
                        start.put(id, new float[] { x, y });
                        downAt.put(id, e.getEventTime());
                    }
                    break;
                }
                case MotionEvent.ACTION_MOVE:
                    for (int i = 0; i < e.getPointerCount(); i++) {
                        int pid = e.getPointerId(i);
                        Role role = roles.get(pid);
                        float x = e.getX(i), y = e.getY(i);
                        if (role == Role.SLIDER) {
                            setSliderFromX(activeSlider, x, false);
                            continue;
                        }
                        if (role == Role.BUTTON) {
                            slideArrow(pid, x, y);
                            if (aimPointer == pid) setAim(x, y); // the wand button as a stick (mod on)
                            continue;
                        }
                        if (role == Role.DRAW) {
                            nativeSetTouch(x / getWidth(), y / getHeight(), true);
                            continue;
                        }
                        if (role == Role.MENU) {
                            menuX = x * surfaceScale;
                            menuY = y * surfaceScale;
                            SDLActivity.onNativeMouse(MotionEvent.BUTTON_PRIMARY, MotionEvent.ACTION_MOVE, menuX, menuY, false);
                            continue;
                        }
                        float[] s = start.get(pid), l = last.get(pid);
                        if (role == null || s == null || l == null) continue;
                        float moved = (float) Math.hypot(x - s[0], y - s[1]);
                        if (role == Role.STICK) {
                            if (stickPointer == -1 && moved > slop) {
                                stickPointer = pid;
                                stickX = s[0];
                                stickY = s[1];
                            }
                            if (stickPointer == pid) {
                                knobX = x;
                                knobY = y;
                                updateStick();
                            }
                        } else if (role == Role.LOOK && moved > slop) {
                            SDLActivity.onNativeMouse(0, MotionEvent.ACTION_MOVE,
                                    (x - l[0]) * 1.5f, (y - l[1]) * 1.5f, true);
                        }
                        l[0] = x;
                        l[1] = y;
                    }
                    break;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_POINTER_UP:
                case MotionEvent.ACTION_CANCEL: {
                    Role role = roles.remove(id);
                    float[] s = start.remove(id);
                    last.remove(id);
                    boolean cancelled = action == MotionEvent.ACTION_CANCEL;
                    boolean twoTap = false; // a two-finger tap (HP2: drink the Wiggenweld potion): neither finger clicks
                    if ((role == Role.STICK || role == Role.LOOK) && s != null && !cancelled) {
                        if (twoTapDone.remove(id)) twoTap = true;
                        else if (isHp2Game() && twoFingerTap(e, idx, s, slop)) twoTap = true;
                    }
                    downAt.remove(id);
                    if (role == Role.SLIDER) {
                        if (!cancelled) setSliderFromX(activeSlider, e.getX(idx), true);
                        activeSlider = -1;
                        invalidate();
                    } else if (role == Role.DRAW) {
                        nativeSetTouch(e.getX(idx) / getWidth(), e.getY(idx) / getHeight(), false);
                        SDLActivity.onNativeMouse(0, MotionEvent.ACTION_UP,
                                getWidth() / 2f * surfaceScale, getHeight() / 2f * surfaceScale, false);
                    } else if (role == Role.MENU) {
                        // Release after the press has been sent (it goes out 60 ms after the touch) and has had a frame.
                        long held = e.getEventTime() - menuDownTime;
                        long delay = Math.max(40, 140 - held);
                        final float ux = menuX, uy = menuY;
                        postDelayed(new Runnable() { public void run() {
                            SDLActivity.onNativeMouse(0, MotionEvent.ACTION_UP, ux, uy, false);
                        } }, delay);
                    } else if (role == Role.BUTTON) {
                        arrowFingers.remove(id);
                        if (aimPointer == id) releaseAim();
                        for (Btn b : buttons) {
                            if (b.pointer == id) {
                                b.pointer = -1;
                                if (b == menu && menuHeld != null) {
                                    removeCallbacks(menuHeld);
                                    menuHeld = null;
                                }
                                if (broomShown && (b == cast || b == jump)) {
                                    // the steering stick pressed nothing; the boost stays as toggled
                                } else if (!b.toggle) setButton(b, false);
                            }
                        }
                    } else if (role == Role.STICK) {
                        if (stickPointer == id) releaseStick();
                        else if (s != null && !cancelled && !twoTap) click(s[0], s[1]);
                    } else if (role == Role.LOOK && s != null && !cancelled && !twoTap) {
                        float moved = (float) Math.hypot(e.getX(idx) - s[0], e.getY(idx) - s[1]);
                        if (moved <= slop) click(s[0], s[1]);
                    }
                    break;
                }
                default:
                    break;
            }
            invalidate();
            return true;
        }

        // The FPS counter: frames the engine drew over the last half second (nativeFrameCount), shown left of the toolbar.
        int fpsFrames = -1;
        long fpsTime;
        int fpsValue = -1;

        void sampleFps() {
            if (!showFps) {
                fpsFrames = -1;
                return;
            }
            int frames;
            try {
                frames = nativeFrameCount();
            } catch (UnsatisfiedLinkError e) {
                return;
            }
            long now = android.os.SystemClock.uptimeMillis();
            if (fpsFrames < 0) {
                fpsFrames = frames;
                fpsTime = now;
            } else if (now - fpsTime >= 500) {
                fpsValue = Math.round((frames - fpsFrames) * 1000f / (now - fpsTime));
                fpsFrames = frames;
                fpsTime = now;
                invalidate();
            }
        }

        final android.graphics.RectF fpsPill = new android.graphics.RectF();

        void drawFps(Canvas c) {
            if (!showFps || fpsValue < 0) return;
            float u = Math.min(getWidth(), getHeight());
            fpsPill.set(toolbar.left - 0.02f * u - 0.2f * u, toolbar.top, toolbar.left - 0.02f * u, toolbar.bottom);
            float r = fpsPill.height() / 2f;
            fill.setColor(DISC);
            c.drawRoundRect(fpsPill, r, r, fill);
            line.setColor(DISC_RIM);
            line.setStrokeWidth(3f);
            c.drawRoundRect(fpsPill, r, r, line);
            String number = String.valueOf(fpsValue);
            valuePaint.setTextAlign(Paint.Align.LEFT);
            int numberColor = fpsValue >= 50 ? 0xFF7FD88A : fpsValue >= 30 ? GOLD : 0xFFFF7A6B;
            valuePaint.setColor(numberColor);
            float numberW = valuePaint.measureText(number), unitW = subPaint.measureText(" FPS");
            float x = fpsPill.centerX() - (numberW + unitW) / 2f, baseY = fpsPill.centerY() + valuePaint.getTextSize() * 0.35f;
            c.drawText(number, x, baseY, valuePaint);
            subPaint.setTextAlign(Paint.Align.LEFT);
            c.drawText(" FPS", x + numberW, baseY, subPaint);
            valuePaint.setColor(GOLD);
            valuePaint.setTextAlign(Paint.Align.RIGHT);
        }

        // The safe area's insets, in pixels: a camera cutout, a visible system bar, the top edge's swipe-down zone. SDL draws under
        // the cutout (fullscreen), so without them a control can sit where the phone takes the touches (the toolbar on a Redmi 13).
        float insL, insT, insR, insB;

        @Override
        public android.view.WindowInsets onApplyWindowInsets(android.view.WindowInsets in) {
            float dp = getResources().getDisplayMetrics().density;
            int l = in.getSystemWindowInsetLeft(), t = in.getSystemWindowInsetTop(), r = in.getSystemWindowInsetRight(), b = in.getSystemWindowInsetBottom();
            android.view.DisplayCutout cutout = in.getDisplayCutout();
            if (cutout != null) {
                l = Math.max(l, cutout.getSafeInsetLeft());
                t = Math.max(t, cutout.getSafeInsetTop());
                r = Math.max(r, cutout.getSafeInsetRight());
                b = Math.max(b, cutout.getSafeInsetBottom());
            }
            android.graphics.Insets gestures = in.getMandatorySystemGestureInsets();
            t = Math.max(t, Math.min(gestures.top, (int) (32 * dp)));
            b = Math.max(b, Math.min(gestures.bottom, (int) (24 * dp)));
            if (l != insL || t != insT || r != insR || b != insB) {
                insL = l;
                insT = t;
                insR = r;
                insB = b;
                String info = "screen " + getWidth() + "x" + getHeight() + ", density " + dp + ", safe insets l" + l + " t" + t + " r" + r + " b" + b
                        + (cutout != null ? ", cutout " + cutout.getBoundingRects() : ", no cutout")
                        + ", gesture insets " + gestures;
                android.util.Log.i("flipendo", "layout: " + info);
                getContext().getSharedPreferences("flipendo", MODE_PRIVATE).edit().putString("layoutInfo", info).apply();
                if (getWidth() > 0) {
                    onSizeChanged(getWidth(), getHeight(), getWidth(), getHeight());
                    invalidate();
                }
            }
            return super.onApplyWindowInsets(in);
        }

        // ---- Control layout (Settings > Control layout): each group of controls can be moved and resized ----
        // groups: the wand button, the jump button, the arrow pad, the toolbar. A group keeps an offset from where it sits by
        // default (fractions of the screen) and a scale; both are saved (preferences "flipendo", ctl_<group>_x / _y / _s).
        static final int G_CAST = 0, G_JUMP = 1, G_PAD = 2, G_BAR = 3, GROUPS = 4;
        static final String[] GROUP_NAMES = { "Wand button", "Jump button", "Arrow pad", "Toolbar" };
        static final float CTL_MIN = 0.6f, CTL_MAX = 1.8f, CTL_STEP = 0.1f;
        final float[][] ctl = new float[GROUPS][3]; // dx, dy, scale
        boolean ctlLoaded;
        boolean layoutEdit;
        int layoutSel = G_CAST, layoutDragPointer = -1;
        float layoutLastX, layoutLastY;
        final android.graphics.RectF leMinus = new android.graphics.RectF(), lePlus = new android.graphics.RectF(), leReset = new android.graphics.RectF(),
                leDone = new android.graphics.RectF(), lePanel = new android.graphics.RectF();

        void resetLayoutValues() {
            for (float[] g : ctl) { g[0] = 0f; g[1] = 0f; g[2] = 1f; }
        }

        void loadLayout() {
            if (ctlLoaded) return;
            ctlLoaded = true;
            resetLayoutValues();
            android.content.SharedPreferences prefs = getContext().getSharedPreferences("flipendo", MODE_PRIVATE);
            for (int i = 0; i < GROUPS; i++) {
                ctl[i][0] = prefs.getFloat("ctl_" + i + "_x", 0f);
                ctl[i][1] = prefs.getFloat("ctl_" + i + "_y", 0f);
                ctl[i][2] = Math.max(CTL_MIN, Math.min(CTL_MAX, prefs.getFloat("ctl_" + i + "_s", 1f)));
            }
        }

        void saveLayout() {
            android.content.SharedPreferences.Editor ed = getContext().getSharedPreferences("flipendo", MODE_PRIVATE).edit();
            for (int i = 0; i < GROUPS; i++) {
                ed.putFloat("ctl_" + i + "_x", ctl[i][0]);
                ed.putFloat("ctl_" + i + "_y", ctl[i][1]);
                ed.putFloat("ctl_" + i + "_s", ctl[i][2]);
            }
            ed.apply();
        }

        /** Moves and scales the groups from their default places (onSizeChanged has just put every control there). */
        void applyLayout(float w, float h) {
            loadLayout();
            float[] g = ctl[G_CAST];
            cast.cx += g[0] * w; cast.cy += g[1] * h; cast.r *= g[2]; aimR *= g[2];
            g = ctl[G_JUMP];
            jump.cx += g[0] * w; jump.cy += g[1] * h; jump.r *= g[2];
            g = ctl[G_PAD];
            float ox = g[0] * w, oy = g[1] * h, s = g[2];
            for (Btn a : arrows) {
                a.cx = padX + ox + (a.cx - padX) * s;
                a.cy = padY + oy + (a.cy - padY) * s;
                a.r *= s;
            }
            padX += ox; padY += oy; padR *= s;
            g = ctl[G_BAR];
            ox = g[0] * w; oy = g[1] * h; s = g[2];
            float tcx = toolbar.centerX(), tcy = toolbar.centerY(), hw = toolbar.width() / 2f * s, hh = toolbar.height() / 2f * s;
            for (Btn b : new Btn[] { gear, save, skip, menu }) {
                b.cx = tcx + ox + (b.cx - tcx) * s;
                b.cy = tcy + oy + (b.cy - tcy) * s;
                b.r *= s;
            }
            toolbar.set(tcx + ox - hw, tcy + oy - hh, tcx + ox + hw, tcy + oy + hh);
        }

        /** The area a group covers now. */
        void groupBounds(int group, android.graphics.RectF out) {
            switch (group) {
                case G_CAST: out.set(cast.cx - cast.r, cast.cy - cast.r, cast.cx + cast.r, cast.cy + cast.r); break;
                case G_JUMP: out.set(jump.cx - jump.r, jump.cy - jump.r, jump.cx + jump.r, jump.cy + jump.r); break;
                case G_PAD: out.set(padX - padR, padY - padR, padX + padR, padY + padR); break;
                default: out.set(toolbar); break;
            }
        }

        /** The group under a touch, or -1. */
        int groupAt(float x, float y) {
            float u = Math.min(getWidth(), getHeight());
            android.graphics.RectF r = new android.graphics.RectF();
            int best = -1;
            float bestD = Float.MAX_VALUE;
            for (int g = 0; g < GROUPS; g++) {
                groupBounds(g, r);
                r.inset(-0.015f * u, -0.015f * u);
                if (!r.contains(x, y)) continue;
                float d = (x - r.centerX()) * (x - r.centerX()) + (y - r.centerY()) * (y - r.centerY());
                if (d < bestD) { bestD = d; best = g; }
            }
            return best;
        }

        /** Lays the controls out again, keeping the selected group inside the safe area (its offset is shifted back). */
        void relayoutKeepingInside(int group) {
            float w = getWidth(), h = getHeight();
            onSizeChanged(getWidth(), getHeight(), getWidth(), getHeight());
            android.graphics.RectF r = new android.graphics.RectF();
            groupBounds(group, r);
            float sx = 0f, sy = 0f;
            float l = insL, tp = insT, rt = w - insR, bt = h - insB;
            if (r.width() > rt - l) sx = (l + rt) / 2f - r.centerX(); else if (r.left < l) sx = l - r.left; else if (r.right > rt) sx = rt - r.right;
            if (r.height() > bt - tp) sy = (tp + bt) / 2f - r.centerY(); else if (r.top < tp) sy = tp - r.top; else if (r.bottom > bt) sy = bt - r.bottom;
            if (sx != 0f || sy != 0f) {
                ctl[group][0] += sx / w;
                ctl[group][1] += sy / h;
                onSizeChanged(getWidth(), getHeight(), getWidth(), getHeight());
            }
        }

        void layoutEditRects(float w, float h) {
            float pw = Math.min(0.56f * w, 1.25f * h), ph = 0.3f * h;
            float top = insT + 0.03f * h;
            lePanel.set((w - pw) / 2f, top, (w + pw) / 2f, top + ph);
            float bh = 0.085f * h;
            float bx = lePanel.left + 0.03f * pw, bw = 0.12f * pw, gap = 0.02f * pw;
            leMinus.set(bx, lePanel.bottom - 0.03f * h - bh, bx + bw, lePanel.bottom - 0.03f * h);
            lePlus.set(leMinus.right + gap, leMinus.top, leMinus.right + gap + bw, leMinus.bottom);
            leDone.set(lePanel.right - 0.03f * pw - 0.24f * pw, leMinus.top, lePanel.right - 0.03f * pw, leMinus.bottom);
            leReset.set(leDone.left - gap - 0.24f * pw, leMinus.top, leDone.left - gap, leMinus.bottom);
        }

        void startLayoutEdit() {
            settingsOpen = false;
            langListOpen = false;
            releaseGameplayControls();
            layoutEdit = true;
            layoutSel = G_CAST;
            layoutDragPointer = -1;
            onSizeChanged(getWidth(), getHeight(), getWidth(), getHeight());
            invalidate();
        }

        void layoutTouch(MotionEvent e) {
            int action = e.getActionMasked(), idx = e.getActionIndex(), id = e.getPointerId(idx);
            float x = e.getX(idx), y = e.getY(idx);
            switch (action) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_POINTER_DOWN:
                    if (leDone.contains(x, y)) {
                        layoutEdit = false;
                        layoutDragPointer = -1;
                        saveLayout();
                        onSizeChanged(getWidth(), getHeight(), getWidth(), getHeight());
                    } else if (leReset.contains(x, y)) {
                        resetLayoutValues();
                        saveLayout();
                        onSizeChanged(getWidth(), getHeight(), getWidth(), getHeight());
                    } else if (leMinus.contains(x, y) || lePlus.contains(x, y)) {
                        float s = ctl[layoutSel][2] + (lePlus.contains(x, y) ? CTL_STEP : -CTL_STEP);
                        ctl[layoutSel][2] = Math.max(CTL_MIN, Math.min(CTL_MAX, Math.round(s * 10f) / 10f));
                        relayoutKeepingInside(layoutSel);
                        saveLayout();
                    } else if (!lePanel.contains(x, y) && layoutDragPointer == -1) {
                        int g = groupAt(x, y);
                        if (g >= 0) {
                            layoutSel = g;
                            layoutDragPointer = id;
                            layoutLastX = x;
                            layoutLastY = y;
                        }
                    }
                    break;
                case MotionEvent.ACTION_MOVE:
                    if (layoutDragPointer != -1) {
                        int i = e.findPointerIndex(layoutDragPointer);
                        if (i >= 0) {
                            float nx = e.getX(i), ny = e.getY(i);
                            ctl[layoutSel][0] += (nx - layoutLastX) / getWidth();
                            ctl[layoutSel][1] += (ny - layoutLastY) / getHeight();
                            layoutLastX = nx;
                            layoutLastY = ny;
                            relayoutKeepingInside(layoutSel);
                        }
                    }
                    break;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_POINTER_UP:
                case MotionEvent.ACTION_CANCEL:
                    if (id == layoutDragPointer || action == MotionEvent.ACTION_CANCEL) {
                        layoutDragPointer = -1;
                        saveLayout();
                    }
                    break;
                default:
                    break;
            }
            invalidate();
        }

        void drawLayoutEdit(Canvas c) {
            if (!layoutEdit) return;
            float u = Math.min(getWidth(), getHeight());
            android.graphics.RectF r = new android.graphics.RectF();
            for (int g = 0; g < GROUPS; g++) {
                groupBounds(g, r);
                r.inset(-0.012f * u, -0.012f * u);
                line.setColor(g == layoutSel ? GOLD : 0x66FFFFFF);
                line.setStrokeWidth(g == layoutSel ? 5f : 2f);
                c.drawRoundRect(r, 0.03f * u, 0.03f * u, line);
            }
            line.setStrokeWidth(4f);
            fill.setColor(CARD);
            c.drawRoundRect(lePanel, 0.03f * u, 0.03f * u, fill);
            line.setColor(0x33FFFFFF);
            line.setStrokeWidth(2f);
            c.drawRoundRect(lePanel, 0.03f * u, 0.03f * u, line);
            rowPaint.setTextAlign(Paint.Align.CENTER);
            rowPaint.setColor(0xFFFFFFFF);
            c.drawText("Drag a control to move it", lePanel.centerX(), lePanel.top + 0.075f * getHeight(), rowPaint);
            subPaint.setTextAlign(Paint.Align.CENTER);
            subPaint.setColor(MUTED);
            c.drawText(GROUP_NAMES[layoutSel] + "  ·  size " + Math.round(ctl[layoutSel][2] * 100) + "%", lePanel.centerX(), lePanel.top + 0.135f * getHeight(), subPaint);
            subPaint.setTextAlign(Paint.Align.LEFT);
            fill.setColor(0x33FFFFFF);
            c.drawRoundRect(leMinus, leMinus.height() / 2f, leMinus.height() / 2f, fill);
            c.drawRoundRect(lePlus, lePlus.height() / 2f, lePlus.height() / 2f, fill);
            c.drawRoundRect(leReset, leReset.height() / 2f, leReset.height() / 2f, fill);
            fill.setColor(GOLD);
            c.drawRoundRect(leDone, leDone.height() / 2f, leDone.height() / 2f, fill);
            c.drawText("–", leMinus.centerX(), leMinus.centerY() + rowPaint.getTextSize() * 0.35f, rowPaint);
            c.drawText("+", lePlus.centerX(), lePlus.centerY() + rowPaint.getTextSize() * 0.35f, rowPaint);
            c.drawText("Reset", leReset.centerX(), leReset.centerY() + rowPaint.getTextSize() * 0.35f, rowPaint);
            rowPaint.setColor(0xFF1A1A1A);
            c.drawText("Done", leDone.centerX(), leDone.centerY() + rowPaint.getTextSize() * 0.35f, rowPaint);
            rowPaint.setColor(0xFFFFFFFF);
            rowPaint.setTextAlign(Paint.Align.LEFT);
        }

        // the controls' geometry (onSizeChanged) and look: the settings card's colours on the game
        final android.graphics.RectF toolbar = new android.graphics.RectF();
        float padR, padX, padY;
        static final int DISC = 0x80181722, DISC_RIM = 0x55FFFFFF, DARK = 0xFF1A1A1A;
        final Paint darkPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);

        {
            darkPaint.setColorFilter(new android.graphics.PorterDuffColorFilter(DARK, android.graphics.PorterDuff.Mode.SRC_IN));
        }

        /** A round button: a dark translucent disc with a thin rim, gold while pressed. */
        void drawDisc(Canvas c, float cx, float cy, float r, boolean down) {
            fill.setColor(down ? 0xE6E8C15A : DISC);
            c.drawCircle(cx, cy, r, fill);
            line.setColor(down ? GOLD : DISC_RIM);
            line.setStrokeWidth(3f);
            c.drawCircle(cx, cy, r, line);
        }

        /** A button's icon: black glyphs white (dark on gold while pressed), coloured pictures as they are. */
        void drawIcon(Canvas c, Btn b, float cx, float cy, float size) {
            iconRect.set(cx - size, cy - size, cx + size, cy + size);
            c.drawBitmap(b.icon, null, iconRect, b.tint ? (b.down ? darkPaint : whitePaint) : iconPaint);
        }

        void drawGear(Canvas c, float cx, float cy, float r, int color) {
            line.setColor(color);
            line.setStrokeWidth(r * 0.2f);
            c.drawCircle(cx, cy, r * 0.32f, line);
            line.setStrokeCap(Paint.Cap.ROUND);
            for (int k = 0; k < 8; k++) {
                double a = k * Math.PI / 4;
                float cs = (float) Math.cos(a), sn = (float) Math.sin(a);
                c.drawLine(cx + cs * r * 0.5f, cy + sn * r * 0.5f, cx + cs * r * 0.66f, cy + sn * r * 0.66f, line);
            }
            line.setStrokeCap(Paint.Cap.BUTT);
        }

        /** A chevron pointing up, rotated: up, down, left, right. */
        void drawChevron(Canvas c, Btn b, int color) {
            float[] angle = { 0f, 180f, 270f, 90f };
            float k = b.r * 0.38f;
            arrowPath.reset();
            arrowPath.moveTo(b.cx - k, b.cy + k * 0.45f);
            arrowPath.lineTo(b.cx, b.cy - k * 0.55f);
            arrowPath.lineTo(b.cx + k, b.cy + k * 0.45f);
            line.setColor(color);
            line.setStrokeWidth(b.r * 0.16f);
            line.setStrokeCap(Paint.Cap.ROUND);
            line.setStrokeJoin(Paint.Join.ROUND);
            c.save();
            c.rotate(angle[arrowIndex(b)], b.cx, b.cy);
            c.drawPath(arrowPath, line);
            c.restore();
            line.setStrokeCap(Paint.Cap.BUTT);
            line.setStrokeJoin(Paint.Join.MITER);
        }

        @Override
        protected void onDraw(Canvas c) {
            float u = Math.min(getWidth(), getHeight());
            if (layoutEdit) {
                fill.setColor(0x66000000);
                c.drawRect(0, 0, getWidth(), getHeight(), fill);
            }

            // top right: the toolbar, 30% visible (gear | save or skip | menu)
            int layer = c.saveLayerAlpha(toolbar.left - 4f, toolbar.top - 4f, toolbar.right + 4f, toolbar.bottom + 4f, 77);
            float tr = toolbar.height() / 2f;
            fill.setColor(DISC);
            c.drawRoundRect(toolbar, tr, tr, fill);
            line.setColor(DISC_RIM);
            line.setStrokeWidth(3f);
            c.drawRoundRect(toolbar, tr, tr, line);
            line.setColor(0x33FFFFFF);
            line.setStrokeWidth(2f);
            float sepTop = toolbar.top + 0.25f * toolbar.height(), sepBottom = toolbar.bottom - 0.25f * toolbar.height();
            float sep1 = (gear.cx + save.cx) / 2f, sep2 = (save.cx + menu.cx) / 2f;
            c.drawLine(sep1, sepTop, sep1, sepBottom, line);
            c.drawLine(sep2, sepTop, sep2, sepBottom, line);
            for (Btn b : new Btn[] { gear, save, skip, menu }) {
                if (!visible(b)) continue;
                if (b.down) {
                    fill.setColor(0xE6E8C15A);
                    c.drawCircle(b.cx, b.cy, 0.9f * b.r, fill);
                }
                if (b == gear) {
                    drawGear(c, b.cx, b.cy, b.r, b.down ? DARK : 0xFFFFFFFF);
                } else if (b == skip) {
                    sectionPaint.setTextAlign(Paint.Align.CENTER);
                    sectionPaint.setColor(b.down ? DARK : GOLD);
                    c.drawText(padActive ? "Hold Y" : "SKIP ›", b.cx, b.cy + sectionPaint.getTextSize() * 0.35f, sectionPaint);
                    sectionPaint.setColor(GOLD);
                    sectionPaint.setTextAlign(Paint.Align.LEFT);
                } else if (b.icon != null) {
                    drawIcon(c, b, b.cx, b.cy, b.r * 0.8f);
                } else {
                    text.setColor(b.down ? DARK : 0xFFFFFFFF);
                    c.drawText(b.label, b.cx, b.cy + text.getTextSize() * 0.35f, text);
                    text.setColor(0xCCFFFFFF);
                }
            }
            c.restoreToCount(layer);
            drawFps(c);

            // bottom left: the arrows on one round pad
            if (visible(arrows[0])) {
                fill.setColor(0x55181722);
                c.drawCircle(padX, padY, padR, fill);
                line.setColor(0x33FFFFFF);
                line.setStrokeWidth(3f);
                c.drawCircle(padX, padY, padR, line);
                fill.setColor(0x22FFFFFF);
                c.drawCircle(padX, padY, 0.03f * u, fill);
                for (Btn a : arrows) {
                    if (a.down) {
                        fill.setColor(0xE6E8C15A);
                        c.drawCircle(a.cx, a.cy, a.r * 0.85f, fill);
                    }
                    drawChevron(c, a, a.down ? DARK : 0xE6FFFFFF);
                }
            }

            // bottom right: jump and the wand
            if (visible(jump)) {
                drawDisc(c, jump.cx, jump.cy, jump.down ? jump.r * 0.94f : jump.r, jump.down);
                if (jump.icon != null) drawIcon(c, jump, jump.cx, jump.cy, jump.r * 0.62f);
            }
            if (visible(cast)) {
                if (aimVisible()) {
                    // the range of the wand stick: the finger's offset from where it went down turns the camera
                    line.setColor(aimPointer != -1 ? 0x99E8C15A : 0x26FFFFFF);
                    line.setStrokeWidth(3f);
                    c.drawCircle(cast.cx, cast.cy, aimR + cast.r, line);
                }
                c.save();
                if (aimStick() && aimPointer != -1) c.translate(aimDx, aimDy); // the wand button is the stick: it follows the finger
                drawDisc(c, cast.cx, cast.cy, cast.down ? cast.r * 0.94f : cast.r, cast.down);
                if (cast.icon != null) drawIcon(c, cast, cast.cx, cast.cy, cast.r * 0.66f);
                c.restore();
            }

            // the floating stick, where the finger went down
            if (stickPointer != -1) {
                float r = 0.12f * u;
                fill.setColor(0x55181722);
                c.drawCircle(stickX, stickY, r, fill);
                line.setColor(0x44FFFFFF);
                line.setStrokeWidth(3f);
                c.drawCircle(stickX, stickY, r, line);
                fill.setColor(0xCCE8C15A);
                c.drawCircle(knobX, knobY, r * 0.4f, fill);
            }
            drawLayoutEdit(c);
            drawPadCursor(c);
            drawSettings(c);
            drawLanguageList(c);
            drawPrompt(c);
        }
    }
}
