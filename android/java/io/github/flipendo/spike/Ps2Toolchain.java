package io.github.flipendo.spike;

import android.content.Context;
import android.system.Os;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * The C++ compiler the PS2 runner uses to translate a game on the phone (docs/ps2.md). clang and lld are in the APK as library files
 * (libps2clang.so, libps2lld.so: Android only lets an app start programs from its native library folder); the sysroot (headers, the
 * C library's stub libraries, clang's own headers) is an asset that is unpacked once. clang finds its linker by the name ld.lld in a
 * folder given with -B, so that folder gets a link of that name to libps2lld.so.
 */
final class Ps2Toolchain {
    private Ps2Toolchain() {}

    static final String SYSROOT_ASSET = "ps2/sysroot.zip";

    /** The folder everything of the toolchain is unpacked into. */
    static File dir(Context c) {
        return new File(c.getFilesDir(), "ps2/toolchain");
    }

    static File clang(Context c) {
        return new File(c.getApplicationInfo().nativeLibraryDir, "libps2clang.so");
    }

    static boolean present(Context c) {
        return clang(c).isFile() && new File(c.getApplicationInfo().nativeLibraryDir, "libps2lld.so").isFile();
    }

    /** The flags that make the compiler a cross-less AArch64 Android one, in the order PS2_LIVE_FLAGS carries them. */
    static String flags(Context c) {
        File tc = dir(c);
        File sysroot = new File(tc, "sysroot");
        return "--driver-mode=g++ --target=aarch64-linux-android29 --sysroot=" + sysroot.getPath()
                + " -resource-dir=" + new File(sysroot, "clang-resource").getPath()
                + " -fuse-ld=lld -B" + new File(tc, "bin").getPath()
                + " -nostdlib++ -isystem " + new File(sysroot, "usr/include/c++/v1").getPath()
                + " -Wno-unused-command-line-argument";
    }

    /**
     * Unpacks the sysroot when it is not there yet (or the app was updated) and makes the ld.lld link. Safe to call at every start: a
     * marker file holds the version code of the app that unpacked it. Returns false when something is missing.
     */
    static boolean ensure(Context c) {
        if (!present(c)) return false;
        File tc = dir(c);
        File marker = new File(tc, ".unpacked");
        String stamp = stamp(c);
        try {
            if (!stamp.equals(read(marker)) || !new File(tc, "sysroot/usr/include").isDirectory()) {
                deleteTree(new File(tc, "sysroot"));
                tc.mkdirs();
                try (ZipInputStream zip = new ZipInputStream(c.getAssets().open(SYSROOT_ASSET))) {
                    byte[] buf = new byte[64 * 1024];
                    for (ZipEntry e; (e = zip.getNextEntry()) != null; ) {
                        File out = new File(new File(tc, "sysroot"), e.getName());
                        if (!out.getCanonicalPath().startsWith(new File(tc, "sysroot").getCanonicalPath())) continue;   // never out of the folder
                        if (e.isDirectory()) {
                            out.mkdirs();
                            continue;
                        }
                        out.getParentFile().mkdirs();
                        try (FileOutputStream o = new FileOutputStream(out)) {
                            int n;
                            while ((n = zip.read(buf)) > 0) o.write(buf, 0, n);
                        }
                    }
                }
                try (FileOutputStream o = new FileOutputStream(marker)) {
                    o.write(stamp.getBytes("UTF-8"));
                }
            }
            File bin = new File(tc, "bin");
            bin.mkdirs();
            File link = new File(bin, "ld.lld");
            String target = new File(c.getApplicationInfo().nativeLibraryDir, "libps2lld.so").getPath();
            if (!link.exists() || !target.equals(readLink(link))) {
                link.delete();
                Os.symlink(target, link.getPath());
            }
            return true;
        } catch (Exception e) {
            android.util.Log.e("flipendo", "PS2 toolchain: " + e);
            return false;
        }
    }

    private static String stamp(Context c) {
        try {
            return String.valueOf(c.getPackageManager().getPackageInfo(c.getPackageName(), 0).lastUpdateTime);
        } catch (Exception e) {
            return "0";
        }
    }

    /** The folder of one game's translated code and caches: files/ps2/<serial>. */
    static File gameCache(Context c, String serial) {
        return new File(c.getFilesDir(), "ps2/" + serial);
    }

    /**
     * Was the game translated and compiled by this version of the app? The runner marks it when the preparation ends well; a newer
     * app (a newer translator) prepares again, which costs seconds when the code of the cache still fits and minutes when it does not.
     */
    static boolean prepared(Context c, String serial) {
        return stamp(c).equals(read(new File(gameCache(c, serial), "prepared.stamp")));
    }

    static void markPrepared(Context c, String serial) {
        try (FileOutputStream o = new FileOutputStream(new File(gameCache(c, serial), "prepared.stamp"))) {
            o.write(stamp(c).getBytes("UTF-8"));
        } catch (IOException e) {
            android.util.Log.e("flipendo", "PS2 prepared mark: " + e);
        }
    }

    private static String read(File f) {
        try (InputStream in = new java.io.FileInputStream(f)) {
            byte[] b = new byte[(int) Math.min(f.length(), 256)];
            int n = in.read(b);
            return new String(b, 0, Math.max(n, 0), "UTF-8");
        } catch (IOException e) {
            return "";
        }
    }

    private static String readLink(File f) {
        try {
            return Os.readlink(f.getPath());
        } catch (Exception e) {
            return "";
        }
    }

    static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null) {
            for (File k : kids) deleteTree(k);
        }
        f.delete();
    }
}
