package io.github.flipendo.spike;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/**
 * PS1 disc images for the emulator (plain Java, no Android: android/tests/Ps1DiscConverterTest).
 *
 * SwanStation reads BIN/CUE (and CHD, ISO) but not Alcohol 120% images. HP1's disc is an MDF/MDS pair whose single data
 * track has 2448-byte sectors: 2352 data bytes and 96 subchannel bytes. {@link #convertMdf} keeps the first 2352 bytes of
 * each sector (what a BIN holds) and writes a one-track MODE2/2352 CUE next to it. See docs/ps1.md.
 */
public final class Ps1DiscConverter {
    private Ps1DiscConverter() {}

    static final int DATA_BYTES = 2352;
    private static final int MDS_SESSIONS_OFFSET = 0x50;
    private static final int TRACK_BLOCK_SIZE = 80;

    /**
     * The playable disc in a game folder: a .cue (or .chd) first, then an .mdf that has its .mds, then a bare .iso or .img.
     * A bare .bin is no disc here (it belongs to a .cue). Null if there is none.
     */
    public static File findDisc(File dir) {
        File[] files = dir == null ? null : dir.listFiles();
        if (files == null) return null;
        java.util.Arrays.sort(files);
        File mdf = null, bare = null;
        for (File f : files) {
            String n = f.getName().toLowerCase(java.util.Locale.ROOT);
            if (!f.isFile()) continue;
            if (n.endsWith(".cue") || n.endsWith(".chd")) return f;
            if (n.endsWith(".mdf") && mdf == null && new File(dir, baseName(f) + ".mds").isFile()) mdf = f;
            if ((n.endsWith(".iso") || n.endsWith(".img")) && bare == null) bare = f;
        }
        return mdf != null ? mdf : bare;
    }

    /** {@link #findDisc(File)} in the folder, then in the folders inside it down to depth levels (0 is the folder only). */
    public static File findDisc(File dir, int depth) {
        File found = findDisc(dir);
        if (found != null || depth <= 0) return found;
        File[] children = dir == null ? null : dir.listFiles();
        if (children == null) return null;
        java.util.Arrays.sort(children);
        for (File child : children) {
            if (child.isDirectory() && !child.getName().startsWith(".")) {
                found = findDisc(child, depth - 1);
                if (found != null) return found;
            }
        }
        return null;
    }

    /** True for an image the emulator cannot open as it is. */
    public static boolean needsConversion(File disc) {
        return disc.getName().toLowerCase(java.util.Locale.ROOT).endsWith(".mdf");
    }

    /**
     * Converts mdf (with its .mds beside it) into outDir/&lt;name&gt;.bin and .cue and returns the cue. An image that was
     * converted before (a bin of the right size, newer than the mdf, and a cue) is reused.
     * @throws IOException with a message naming the problem: no .mds, an unknown sector size, a size that is not a whole
     *         number of sectors, or a disk error
     */
    public static File convertMdf(File mdf, File outDir) throws IOException {
        String base = baseName(mdf);
        File mds = new File(mdf.getParentFile(), base + ".mds");
        if (!mds.isFile()) throw new IOException("no " + mds.getName() + " next to " + mdf.getName());
        int sectorSize = readSectorSize(mds);
        if (sectorSize != DATA_BYTES && sectorSize != DATA_BYTES + 96)
            throw new IOException(mdf.getName() + ": unsupported sector size " + sectorSize);
        long length = mdf.length();
        if (length == 0 || length % sectorSize != 0)
            throw new IOException(mdf.getName() + ": size " + length + " is not a whole number of " + sectorSize + "-byte sectors");
        long sectors = length / sectorSize;

        if (!outDir.isDirectory() && !outDir.mkdirs()) throw new IOException("cannot create " + outDir);
        File bin = new File(outDir, base + ".bin");
        File cue = new File(outDir, base + ".cue");
        if (cue.isFile() && bin.isFile() && bin.length() == sectors * DATA_BYTES && bin.lastModified() >= mdf.lastModified())
            return cue;

        File tmp = new File(outDir, base + ".bin.part");
        InputStream in = new FileInputStream(mdf);
        try {
            OutputStream out = new BufferedOutputStream(new FileOutputStream(tmp), 1 << 20);
            try {
                byte[] buf = new byte[sectorSize * 256];
                int have;
                while ((have = readFully(in, buf)) > 0) {
                    for (int o = 0; o + sectorSize <= have; o += sectorSize) out.write(buf, o, DATA_BYTES);
                }
            } finally {
                out.close();
            }
        } finally {
            in.close();
        }
        if (bin.exists() && !bin.delete()) throw new IOException("cannot replace " + bin);
        if (!tmp.renameTo(bin)) throw new IOException("cannot create " + bin);
        String text = "FILE \"" + bin.getName() + "\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n";
        OutputStream c = new FileOutputStream(cue);
        try {
            c.write(text.getBytes(StandardCharsets.US_ASCII));
        } finally {
            c.close();
        }
        return cue;
    }

    /** The sector size of the first data track (point 1 to 99) of an Alcohol 120% descriptor. */
    static int readSectorSize(File mds) throws IOException {
        byte[] b = new byte[(int) Math.min(mds.length(), 1 << 20)];
        InputStream in = new FileInputStream(mds);
        try {
            readFully(in, b);
        } finally {
            in.close();
        }
        if (b.length < 0x60 || !new String(b, 0, 16, StandardCharsets.US_ASCII).equals("MEDIA DESCRIPTOR"))
            throw new IOException(mds.getName() + " is not an Alcohol 120% descriptor");
        int session = intAt(b, MDS_SESSIONS_OFFSET);
        if (session < 0 || session + 24 > b.length) throw new IOException(mds.getName() + ": bad session offset");
        int blocks = b[session + 10] & 0xFF;
        int tracks = intAt(b, session + 20);
        for (int i = 0; i < blocks; i++) {
            int o = tracks + i * TRACK_BLOCK_SIZE;
            if (o < 0 || o + 18 > b.length) break;
            int point = b[o + 4] & 0xFF;
            if (point >= 1 && point <= 99) return (b[o + 16] & 0xFF) | ((b[o + 17] & 0xFF) << 8);
        }
        throw new IOException(mds.getName() + ": no data track");
    }

    private static int intAt(byte[] b, int o) {
        return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8) | ((b[o + 2] & 0xFF) << 16) | ((b[o + 3] & 0xFF) << 24);
    }

    private static int readFully(InputStream in, byte[] buf) throws IOException {
        int total = 0;
        while (total < buf.length) {
            int n = in.read(buf, total, buf.length - total);
            if (n < 0) break;
            total += n;
        }
        return total;
    }

    private static String baseName(File f) {
        String n = f.getName();
        int dot = n.lastIndexOf('.');
        return dot < 0 ? n : n.substring(0, dot);
    }
}
