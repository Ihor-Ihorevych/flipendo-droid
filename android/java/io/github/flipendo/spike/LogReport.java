package io.github.flipendo.spike;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.Intent;
import android.content.pm.FeatureInfo;
import android.os.Build;
import android.os.Environment;
import android.util.DisplayMetrics;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * Builds flipendo-report.zip: the engine logs, the crash backtrace, this app's logcat, why earlier runs ended, and
 * what the phone is (model, Android, GPU/Vulkan features). A user sends that one file to the developer.
 */
final class LogReport {
    static final String REPORT_NAME = "flipendo-report.zip";

    private LogReport() {
    }

    static File reportFile(Context context) {
        File dir = context.getExternalCacheDir();
        if (dir == null) {
            dir = context.getCacheDir();
        }
        return new File(dir, REPORT_NAME);
    }

    /** Builds the report (off the UI thread) and opens the share sheet. */
    static void share(final Activity activity) {
        new Thread(new Runnable() {
            public void run() {
                try {
                    final File report = build(activity);
                    activity.runOnUiThread(new Runnable() {
                        public void run() {
                            Intent send = new Intent(Intent.ACTION_SEND);
                            send.setType("application/zip");
                            send.putExtra(Intent.EXTRA_STREAM, ReportProvider.uriFor(report.getName()));
                            send.putExtra(Intent.EXTRA_SUBJECT, "Flipendo report");
                            send.putExtra(Intent.EXTRA_TEXT, "Flipendo log report (" + Build.MANUFACTURER + " " + Build.MODEL + ")");
                            send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                            activity.startActivity(Intent.createChooser(send, "Send the Flipendo report"));
                        }
                    });
                } catch (final IOException e) {
                    activity.runOnUiThread(new Runnable() {
                        public void run() {
                            android.widget.Toast.makeText(activity, "Could not build the report: " + e, android.widget.Toast.LENGTH_LONG).show();
                        }
                    });
                }
            }
        }).start();
    }

    static File build(Context context) throws IOException {
        File out = reportFile(context);
        File game = new File(FlipendoActivity.GAME_DIR);
        try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(out))) {
            addText(zip, "device-info.txt", deviceInfo(context));
            addText(zip, "exit-info.txt", exitInfo(context, zip));
            addText(zip, "logcat.txt", logcat());
            addText(zip, "game-folder.txt", gameFolder(game));
            for (String name : new String[] { "flipendo.log", "flipendo.prev.log", "crash.txt", "crash.prev.txt" }) {
                File f = new File(game, name);
                if (f.isFile()) {
                    addFile(zip, name, f);
                }
            }
        }
        // a copy where the user can find it without a share target
        try {
            File download = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), REPORT_NAME);
            try (InputStream in = new FileInputStream(out); OutputStream os = new FileOutputStream(download)) {
                copy(in, os);
            }
        } catch (IOException ignored) {
        }
        return out;
    }

    private static void addText(ZipOutputStream zip, String name, String text) throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(text.getBytes("UTF-8"));
        zip.closeEntry();
    }

    private static void addFile(ZipOutputStream zip, String name, File file) throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        try (InputStream in = new FileInputStream(file)) {
            copy(in, zip);
        }
        zip.closeEntry();
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[1 << 16];
        int n;
        while ((n = in.read(buffer)) > 0) {
            out.write(buffer, 0, n);
        }
    }

    private static String deviceInfo(Context context) {
        StringBuilder s = new StringBuilder();
        s.append("manufacturer: ").append(Build.MANUFACTURER).append('\n');
        s.append("brand: ").append(Build.BRAND).append('\n');
        s.append("model: ").append(Build.MODEL).append('\n');
        s.append("device: ").append(Build.DEVICE).append('\n');
        s.append("hardware: ").append(Build.HARDWARE).append('\n');
        s.append("board: ").append(Build.BOARD).append('\n');
        if (Build.VERSION.SDK_INT >= 31) {
            s.append("soc: ").append(Build.SOC_MANUFACTURER).append(' ').append(Build.SOC_MODEL).append('\n');
        }
        s.append("android: ").append(Build.VERSION.RELEASE).append(" (sdk ").append(Build.VERSION.SDK_INT).append(")\n");
        s.append("security patch: ").append(Build.VERSION.SECURITY_PATCH).append('\n');
        s.append("fingerprint: ").append(Build.FINGERPRINT).append('\n');
        s.append("abis: ").append(java.util.Arrays.toString(Build.SUPPORTED_ABIS)).append('\n');

        ActivityManager am = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo mem = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(mem);
        s.append("ram: ").append(mem.totalMem >> 20).append(" MB total, ").append(mem.availMem >> 20).append(" MB free, low=").append(mem.lowMemory).append('\n');

        DisplayMetrics dm = context.getResources().getDisplayMetrics();
        s.append("screen: ").append(dm.widthPixels).append('x').append(dm.heightPixels).append(" @ ").append(dm.densityDpi).append(" dpi\n");
        FeatureInfo[] features = context.getPackageManager().getSystemAvailableFeatures();
        if (features != null) {
            for (FeatureInfo f : features) {
                if (f.name != null && (f.name.contains("vulkan") || f.name.contains("opengles"))) {
                    s.append("feature: ").append(f.name).append(" version=0x").append(Integer.toHexString(f.version)).append('\n');
                }
            }
            for (FeatureInfo f : features) {
                if (f.name == null && f.reqGlEsVersion != 0) {
                    s.append("opengl es: 0x").append(Integer.toHexString(f.reqGlEsVersion)).append('\n');
                }
            }
        }
        s.append("all files access: ").append(Environment.isExternalStorageManager()).append('\n');
        try {
            s.append("build: ").append(readAsset(context, "build-info.txt")).append('\n');
            s.append("self pack: ").append(readAsset(context, "gamedata/.stamp") != null).append('\n');
        } catch (RuntimeException ignored) {
        }
        s.append("(the GPU name and Vulkan version are in flipendo.log: lines starting 'Vulkan')\n");
        return s.toString();
    }

    private static String readAsset(Context context, String name) {
        try (InputStream in = context.getAssets().open(name)) {
            ByteArrayOutputStream b = new ByteArrayOutputStream();
            copy(in, b);
            return b.toString("UTF-8").trim();
        } catch (IOException e) {
            return null;
        }
    }

    private static String gameFolder(File game) {
        StringBuilder s = new StringBuilder();
        s.append(game).append(" exists=").append(game.isDirectory()).append('\n');
        String[] names = game.list();
        if (names != null) {
            java.util.Arrays.sort(names);
            for (String n : names) {
                File f = new File(game, n);
                s.append(f.isDirectory() ? "[dir]  " : "[file] ").append(n);
                if (f.isFile()) {
                    s.append("  ").append(f.length());
                } else {
                    String[] inner = f.list();
                    s.append("  (").append(inner == null ? 0 : inner.length).append(" entries)");
                }
                s.append('\n');
            }
        }
        for (String check : new String[] { "System/HP.exe", "system/HP.exe", "System/Engine.u", "system/Engine.u", "Maps/Startup.unr", "maps/Startup.unr" }) {
            File f = new File(game, check);
            s.append(check).append(": ").append(f.isFile() ? f.length() + " bytes" : "missing").append('\n');
        }
        return s.toString();
    }

    static String exitReason(int reason) {
        switch (reason) {
            case ApplicationExitInfo.REASON_ANR: return "ANR";
            case ApplicationExitInfo.REASON_CRASH: return "CRASH (java)";
            case ApplicationExitInfo.REASON_CRASH_NATIVE: return "CRASH_NATIVE";
            case ApplicationExitInfo.REASON_EXIT_SELF: return "EXIT_SELF";
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE: return "INITIALIZATION_FAILURE";
            case ApplicationExitInfo.REASON_LOW_MEMORY: return "LOW_MEMORY";
            case ApplicationExitInfo.REASON_SIGNALED: return "SIGNALED";
            case ApplicationExitInfo.REASON_USER_REQUESTED: return "USER_REQUESTED";
            case ApplicationExitInfo.REASON_USER_STOPPED: return "USER_STOPPED";
            case ApplicationExitInfo.REASON_PERMISSION_CHANGE: return "PERMISSION_CHANGE";
            case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE: return "EXCESSIVE_RESOURCE_USAGE";
            case ApplicationExitInfo.REASON_DEPENDENCY_DIED: return "DEPENDENCY_DIED";
            case ApplicationExitInfo.REASON_OTHER: return "OTHER";
            default: return "reason " + reason;
        }
    }

    /** Why the last few runs of the app ended (Android 11+); a native crash comes with Android's own tombstone. */
    private static String exitInfo(Context context, ZipOutputStream zip) {
        if (Build.VERSION.SDK_INT < 30) {
            return "needs Android 11\n";
        }
        StringBuilder s = new StringBuilder();
        ActivityManager am = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        List<ApplicationExitInfo> list = am.getHistoricalProcessExitReasons(context.getPackageName(), 0, 8);
        boolean tombstone = false;
        for (ApplicationExitInfo info : list) {
            s.append(new java.util.Date(info.getTimestamp())).append("  ").append(exitReason(info.getReason()))
                    .append("  status=").append(info.getStatus())
                    .append("  pss=").append(info.getPss() >> 10).append("MB rss=").append(info.getRss() >> 10).append("MB")
                    .append("  ").append(info.getDescription()).append('\n');
            if (!tombstone && info.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE) {
                try (InputStream in = info.getTraceInputStream()) {
                    if (in != null) {
                        tombstone = true;
                        zip.putNextEntry(new ZipEntry("tombstone-" + info.getTimestamp() + ".pb"));
                        copy(in, zip);
                        zip.closeEntry();
                    }
                } catch (IOException ignored) {
                }
            }
        }
        if (list.isEmpty()) {
            s.append("no earlier runs recorded\n");
        }
        return s.toString();
    }

    private static String logcat() {
        StringBuilder s = new StringBuilder();
        try {
            Process p = new ProcessBuilder("logcat", "-b", "all", "-d", "-v", "threadtime", "-t", "5000").redirectErrorStream(true).start();
            try (BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream(), "UTF-8"))) {
                String line;
                while ((line = r.readLine()) != null) {
                    s.append(line).append('\n');
                }
            }
        } catch (IOException e) {
            s.append("logcat failed: ").append(e).append('\n');
        }
        return s.toString();
    }

    /** The FATAL line the engine wrote last, for the "game stopped" screen. */
    static String lastFatal() {
        File log = new File(FlipendoActivity.GAME_DIR, "flipendo.log");
        if (!log.isFile()) {
            return null;
        }
        String found = null;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(log), "UTF-8"))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (line.startsWith("FATAL")) {
                    found = line;
                }
            }
        } catch (IOException ignored) {
        }
        return found;
    }
}
