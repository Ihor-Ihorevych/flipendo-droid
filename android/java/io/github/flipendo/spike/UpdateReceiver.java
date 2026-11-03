package io.github.flipendo.spike;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.widget.Toast;

/**
 * The system installer's answers to an update session (Updater.install): the confirmation it needs from the player is shown,
 * a refusal or failure is said in a toast. On success the system restarts the app, so there is nothing to do.
 */
public final class UpdateReceiver extends BroadcastReceiver {
    static final String ACTION = "io.github.flipendo.spike.UPDATE_STATUS";

    @Override
    public void onReceive(Context context, Intent intent) {
        int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
            if (confirm != null) {
                confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                context.startActivity(confirm);
            }
            return;
        }
        if (status == PackageInstaller.STATUS_SUCCESS) {
            return;
        }
        String message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
        if (status == PackageInstaller.STATUS_FAILURE_ABORTED) {
            Toast.makeText(context, Tr.tr("The update was cancelled."), Toast.LENGTH_LONG).show();
        } else {
            Toast.makeText(context, Tr.tr("The update could not be installed: ") + (message != null ? message : status), Toast.LENGTH_LONG).show();
        }
    }
}
