package io.github.flipendo.spike;

import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.os.Build;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.List;

/**
 * Updates from GitHub Releases: finds the newest published release of the app, downloads its light APK and hands it to the
 * system installer. What to offer is decided by UpdateRules; this class is the network, the file and the install session.
 * Everything here blocks: call it off the UI thread.
 */
final class Updater {
    static final String REPO = "Ihor-Ihorevych/flipendo-droid";
    private static final String RELEASES_URL = "https://api.github.com/repos/" + REPO + "/releases?per_page=20";
    private static final int TIMEOUT_MS = 12000;

    /** Receives the download's progress; returning true cancels it. */
    interface Progress {
        boolean update(long done, long total);
    }

    private Updater() {
    }

    /** The release to offer, or null when the installed version is the latest (or nothing usable is published). */
    static UpdateRules.Candidate check(Context context) throws IOException {
        String json = httpGet(RELEASES_URL);
        List<UpdateRules.Candidate> releases;
        try {
            releases = parseReleases(json);
        } catch (JSONException e) {
            throw new IOException("GitHub answered something else than a list of releases", e);
        }
        return UpdateRules.pick(releases, LogReport.version(context));
    }

    /** The releases in GitHub's answer, each with the APK among its assets (an empty url when it has none). */
    static List<UpdateRules.Candidate> parseReleases(String json) throws JSONException {
        List<UpdateRules.Candidate> out = new ArrayList<>();
        JSONArray array = new JSONArray(json);
        for (int i = 0; i < array.length(); i++) {
            JSONObject r = array.getJSONObject(i);
            UpdateRules.Candidate c = new UpdateRules.Candidate();
            c.tag = r.optString("tag_name", "");
            c.name = r.optString("name", "");
            c.notes = r.optString("body", "");
            c.draft = r.optBoolean("draft", false);
            JSONArray assets = r.optJSONArray("assets");
            for (int a = 0; assets != null && a < assets.length(); a++) {
                JSONObject asset = assets.getJSONObject(a);
                if (!UpdateRules.isApkAsset(asset.optString("name"))) {
                    continue;
                }
                c.apkUrl = asset.optString("browser_download_url", "");
                c.size = asset.optLong("size", 0);
                String digest = asset.optString("digest", ""); // "sha256:<hex>"
                c.sha256 = digest.startsWith("sha256:") ? digest.substring(7).toLowerCase(java.util.Locale.ROOT) : "";
                break;
            }
            out.add(c);
        }
        return out;
    }

    /** Downloads the release's APK into the app's cache and checks it; null when {@code progress} cancelled it. */
    static File download(Context context, UpdateRules.Candidate release, Progress progress) throws IOException {
        File dir = new File(context.getCacheDir(), "update");
        deleteOld(dir);
        if (!dir.isDirectory() && !dir.mkdirs()) {
            throw new IOException("cannot make " + dir);
        }
        File target = new File(dir, "flipendo-droid-" + release.version() + ".apk");
        File part = new File(dir, target.getName() + ".part");
        HttpURLConnection c = open(release.apkUrl);
        MessageDigest sha;
        try {
            sha = MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException e) {
            throw new IOException(e);
        }
        long total = c.getContentLengthLong() > 0 ? c.getContentLengthLong() : release.size;
        long done = 0;
        try (InputStream in = c.getInputStream(); OutputStream out = new FileOutputStream(part)) {
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
                sha.update(buf, 0, n);
                done += n;
                if (progress != null && progress.update(done, total)) {
                    part.delete();
                    return null;
                }
            }
        } finally {
            c.disconnect();
        }
        if (total > 0 && done != total) {
            part.delete();
            throw new IOException("the download ended early (" + done + " of " + total + " bytes)");
        }
        String got = hex(sha.digest());
        if (!release.sha256.isEmpty() && !release.sha256.equals(got)) {
            part.delete();
            throw new IOException("the downloaded file does not match the checksum GitHub lists for it");
        }
        if (!part.renameTo(target)) {
            throw new IOException("cannot move the download into place");
        }
        if (!sameSigner(context, target)) {
            target.delete();
            throw new IOException(Tr.tr("This update is signed with another key than the installed app, so Android will not replace it. "
                    + "Uninstall Flipendo first (the games and saves on the phone stay), then install the update from GitHub."));
        }
        return target;
    }

    /**
     * Whether the APK is signed by the key of the installed app: Android only replaces an app with a build of the same key.
     * True when the signatures cannot be read (the installer then has the last word).
     */
    static boolean sameSigner(Context context, File apk) {
        try {
            android.content.pm.PackageManager pm = context.getPackageManager();
            android.content.pm.PackageInfo archive = pm.getPackageArchiveInfo(apk.getPath(), android.content.pm.PackageManager.GET_SIGNING_CERTIFICATES);
            android.content.pm.PackageInfo installed = pm.getPackageInfo(context.getPackageName(), android.content.pm.PackageManager.GET_SIGNING_CERTIFICATES);
            if (archive == null || archive.signingInfo == null || installed.signingInfo == null) {
                return true;
            }
            android.content.pm.Signature[] a = archive.signingInfo.getApkContentsSigners();
            android.content.pm.Signature[] b = installed.signingInfo.getApkContentsSigners();
            if (a == null || b == null || a.length == 0 || b.length == 0) {
                return true;
            }
            for (android.content.pm.Signature x : a) {
                for (android.content.pm.Signature y : b) {
                    if (x.equals(y)) {
                        return true;
                    }
                }
            }
            return false;
        } catch (android.content.pm.PackageManager.NameNotFoundException | RuntimeException e) {
            return true;
        }
    }

    /** Hands the APK to the system installer; its answer comes to UpdateReceiver. The app is replaced and restarted by the system. */
    static void install(Context context, File apk) throws IOException {
        PackageInstaller installer = context.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(context.getPackageName());
        int id = installer.createSession(params);
        try (PackageInstaller.Session session = installer.openSession(id)) {
            try (InputStream in = new FileInputStream(apk); OutputStream out = session.openWrite("flipendo-droid.apk", 0, apk.length())) {
                byte[] buf = new byte[64 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
                session.fsync(out);
            }
            Intent status = new Intent(context, UpdateReceiver.class).setAction(UpdateReceiver.ACTION).setPackage(context.getPackageName());
            int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
            session.commit(PendingIntent.getBroadcast(context, id, status, flags).getIntentSender());
        } catch (IOException | RuntimeException e) {
            installer.abandonSession(id);
            throw e;
        }
    }

    /** Removes what an earlier update left in the cache. */
    private static void deleteOld(File dir) {
        File[] files = dir.listFiles();
        for (int i = 0; files != null && i < files.length; i++) {
            files[i].delete();
        }
    }

    private static HttpURLConnection open(String url) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(TIMEOUT_MS);
        c.setReadTimeout(TIMEOUT_MS);
        c.setInstanceFollowRedirects(true);
        c.setRequestProperty("User-Agent", "Flipendo-Updater");
        c.setRequestProperty("Accept", "application/vnd.github+json");
        int code = c.getResponseCode();
        if (code != HttpURLConnection.HTTP_OK) {
            c.disconnect();
            throw new IOException("GitHub answered " + code);
        }
        return c;
    }

    private static String httpGet(String url) throws IOException {
        HttpURLConnection c = open(url);
        try (InputStream in = c.getInputStream()) {
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[16 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            return new String(out.toByteArray(), java.nio.charset.StandardCharsets.UTF_8);
        } finally {
            c.disconnect();
        }
    }

    private static String hex(byte[] bytes) {
        StringBuilder s = new StringBuilder();
        for (byte b : bytes) {
            s.append(String.format(java.util.Locale.ROOT, "%02x", b & 0xff));
        }
        return s.toString();
    }
}
