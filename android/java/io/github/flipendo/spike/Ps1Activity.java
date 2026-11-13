package io.github.flipendo.spike;

import android.app.AlertDialog;
import android.content.Intent;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.RelativeLayout;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Plays a PS1 disc (Harry Potter 1 or 2) in the SwanStation emulator core. The launcher starts it with the disc on the
 * intent; it runs in its own process (:ps1), on SDL3 like the PC games, with libps1host.so as its entry (jni/ps1_host.cpp).
 * The on-screen pad is Ps1PadView; a menu (back key or its button) saves and loads a state or quits.
 * Design: docs/superpowers/specs/2026-11-05-ps1-swanstation-design.md.
 */
public class Ps1Activity extends SDLActivity {
    /** The disc: a .cue or .chd (an .mdf is converted first). */
    public static final String EXTRA_DISC = "ps1.disc";
    /** "hp1" or "hp2": names the folder of this game's memory cards and states. */
    public static final String EXTRA_GAME = "ps1.game";
    /** Resolution scale of the OpenGL renderer, 1 to 4 (default 2). */
    public static final String EXTRA_SCALE = "ps1.scale";
    /** A BIOS file the player picked, used instead of the bundled OpenBIOS; absent or not 512 KB means OpenBIOS. */
    public static final String EXTRA_BIOS = "ps1.bios";
    /** The core's GPU renderer: "opengl" (default, OpenGL ES, scalable) or "software" (any phone, native resolution). */
    public static final String EXTRA_RENDERER = "ps1.renderer";

    /** Memory cards and states: /sdcard/FlipendoPS1/&lt;game&gt;/saves (the launcher's storage permission covers it). */
    public static final String ROOT = Ps1Setup.ROOT;
    static final String BUNDLED_BIOS = "openbios.bin";
    static final long BIOS_SIZE = Ps1Setup.BIOS_SIZE;

    static final int CMD_SAVE_STATE = 1, CMD_LOAD_STATE = 2, CMD_QUIT = 3;

    static native void nativeSetPad(int mask);
    static native void nativeCommand(int command);
    static native void nativePause(boolean paused);

    static String tr(String en) {
        return Tr.tr(en);
    }

    private File core, disc, systemDir, saveDir;
    private int scale = 2;
    private String renderer = Ps1Setup.RENDERER_OPENGL;
    private String biosName = BUNDLED_BIOS;
    private Ps1PadView pad;
    private boolean menuOpen;

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "ps1host"};
    }

    /** What jni/ps1_host.cpp reads: core, disc, system dir, save dir, scale, BIOS name, renderer. */
    @Override
    protected String[] getArguments() {
        return new String[] {core.getPath(), disc.getPath(), systemDir.getPath(), saveDir.getPath(), String.valueOf(scale), biosName, renderer};
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Tr.init(this);
        prepare(getIntent());
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (mLayout != null) {
            pad = new Ps1PadView(this, new Ps1PadView.Listener() {
                @Override public void onMenu() { showMenu(); }
                @Override public void onEditDone() { nativePause(false); }
            });
            mLayout.addView(pad, new RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            if (padConnected()) pad.onRealPadInput(); // a controller is already connected: start without the touch controls
        }
    }

    /** Works out the paths and puts the BIOS where the core looks for it. */
    private void prepare(Intent intent) {
        String game = intent != null ? intent.getStringExtra(EXTRA_GAME) : null;
        if (game == null) game = "ps1";
        String discPath = intent != null ? intent.getStringExtra(EXTRA_DISC) : null;
        scale = intent != null ? Ps1Setup.clampScale(intent.getIntExtra(EXTRA_SCALE, 2)) : 2;
        renderer = Ps1Setup.renderer(intent != null ? intent.getStringExtra(EXTRA_RENDERER) : null);
        core = new File(getApplicationInfo().nativeLibraryDir, "libswanstation.so");
        systemDir = new File(getFilesDir(), "ps1/system");
        systemDir.mkdirs();

        saveDir = new File(Ps1Setup.defaultDir(game), "saves");
        if (!saveDir.isDirectory() && !saveDir.mkdirs()) {
            File fallback = getExternalFilesDir(null);
            saveDir = new File(fallback != null ? fallback : getFilesDir(), "ps1/" + game + "/saves");
            saveDir.mkdirs();
        }

        disc = discPath != null ? new File(discPath) : new File("");
        if (Ps1DiscConverter.needsConversion(disc)) {
            // the launcher converts before it starts us; this is the fallback for a direct start
            try {
                disc = Ps1DiscConverter.convertMdf(disc, new File(Ps1Setup.defaultDir(game), "disc"));
            } catch (IOException e) {
                android.util.Log.e("flipendo", "PS1 disc conversion failed: " + e.getMessage());
            }
        }

        String userBios = intent != null ? intent.getStringExtra(EXTRA_BIOS) : null;
        try {
            if (userBios != null && new File(userBios).length() == BIOS_SIZE) {
                biosName = "user-bios.bin";
                copy(new java.io.FileInputStream(userBios), new File(systemDir, biosName));
            } else {
                biosName = BUNDLED_BIOS;
                File out = new File(systemDir, BUNDLED_BIOS);
                if (!out.isFile() || out.length() != BIOS_SIZE) copy(getAssets().open("ps1/" + BUNDLED_BIOS), out);
            }
        } catch (IOException e) {
            android.util.Log.e("flipendo", "PS1 BIOS: " + e.getMessage());
        }
    }

    private static void copy(InputStream in, File to) throws IOException {
        try {
            OutputStream out = new FileOutputStream(to);
            try {
                byte[] buf = new byte[64 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            } finally {
                out.close();
            }
        } finally {
            in.close();
        }
    }

    // ---- the menu -------------------------------------------------------------------------------------------------

    void showMenu() {
        if (menuOpen || (pad != null && pad.isEditing())) return;
        menuOpen = true;
        nativePause(true);
        final boolean[] editing = {false};
        final String[] items = {tr("Resume"), tr("Save state"), tr("Load state"), tr("Control layout"), tr("Quit game")};
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(tr("PS1"))
                .setItems(items, new android.content.DialogInterface.OnClickListener() {
                    @Override public void onClick(android.content.DialogInterface d, int which) {
                        if (which == 1) nativeCommand(CMD_SAVE_STATE);
                        else if (which == 2) nativeCommand(CMD_LOAD_STATE);
                        else if (which == 3 && pad != null) {
                            // the game stays paused while the controls are moved; Done resumes it
                            editing[0] = true;
                            pad.startEditing();
                        }
                        else if (which == 4) nativeCommand(CMD_QUIT);
                    }
                })
                .setOnDismissListener(new android.content.DialogInterface.OnDismissListener() {
                    @Override public void onDismiss(android.content.DialogInterface d) {
                        menuOpen = false;
                        if (!editing[0]) nativePause(false);
                    }
                })
                .show();
    }

    /** The back key opens the menu instead of ending the game (in the layout editor it does nothing: Done closes it). */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == KeyEvent.ACTION_UP) showMenu();
            return true;
        }
        if (pad != null && event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0 && isPadKey(event)) {
            pad.onRealPadInput(); // a controller is in use: the touch controls step aside until the next touch
        }
        return super.dispatchKeyEvent(event);
    }

    /** A stick pushed clearly (resting drift is ignored) hides the touch controls too; SDL still gets the event. */
    @Override
    public boolean dispatchGenericMotionEvent(android.view.MotionEvent event) {
        if (pad != null && event.getAction() == android.view.MotionEvent.ACTION_MOVE
                && (event.getSource() & android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK) {
            int[] axes = {android.view.MotionEvent.AXIS_X, android.view.MotionEvent.AXIS_Y, android.view.MotionEvent.AXIS_Z,
                    android.view.MotionEvent.AXIS_RZ, android.view.MotionEvent.AXIS_HAT_X, android.view.MotionEvent.AXIS_HAT_Y,
                    android.view.MotionEvent.AXIS_LTRIGGER, android.view.MotionEvent.AXIS_RTRIGGER};
            for (int axis : axes) {
                if (Math.abs(event.getAxisValue(axis)) >= 0.5f) {
                    pad.onRealPadInput();
                    break;
                }
            }
        }
        return super.dispatchGenericMotionEvent(event);
    }

    private static boolean isPadKey(KeyEvent event) {
        int source = event.getSource();
        return (source & android.view.InputDevice.SOURCE_GAMEPAD) == android.view.InputDevice.SOURCE_GAMEPAD
                || (source & android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK
                || KeyEvent.isGamepadButton(event.getKeyCode());
    }

    /** Whether a gamepad is connected right now (the controls start hidden then). */
    private static boolean padConnected() {
        for (int id : android.view.InputDevice.getDeviceIds()) {
            android.view.InputDevice d = android.view.InputDevice.getDevice(id);
            if (d == null || d.isVirtual()) continue;
            int s = d.getSources();
            if ((s & android.view.InputDevice.SOURCE_GAMEPAD) == android.view.InputDevice.SOURCE_GAMEPAD
                    || (s & android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK) {
                return true;
            }
        }
        return false;
    }
}
