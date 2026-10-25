package io.github.flipendo.spike;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.media.AudioAttributes;
import android.media.MediaPlayer;

/** The sound of the version tap (res/raw/version_sound.mp3): played once, in its own player, which lets go of itself when done. */
final class VersionSound {
    private VersionSound() {
    }

    static void play(Context context) {
        try {
            int id = context.getResources().getIdentifier("version_sound", "raw", context.getPackageName());
            if (id == 0) return;
            final MediaPlayer player = new MediaPlayer();
            player.setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build());
            AssetFileDescriptor fd = context.getResources().openRawResourceFd(id);
            try {
                player.setDataSource(fd.getFileDescriptor(), fd.getStartOffset(), fd.getLength());
            } finally {
                fd.close();
            }
            player.setOnCompletionListener(new MediaPlayer.OnCompletionListener() {
                public void onCompletion(MediaPlayer mp) {
                    mp.release();
                }
            });
            player.setOnErrorListener(new MediaPlayer.OnErrorListener() {
                public boolean onError(MediaPlayer mp, int what, int extra) {
                    mp.release();
                    return true;
                }
            });
            player.prepare();
            player.start();
        } catch (Exception ignored) {
        }
    }
}
