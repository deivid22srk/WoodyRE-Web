// WoodyActivity - SDL's activity plus what the game's first start needs on Android (src/datasetup_posix.c): the system's
// file picker for an ISO image of the CD or a folder with a copy of it, the folder copy, and a progress dialog.
// Testing: am start -n io.github.jjmhalew.woodyre/.WoodyActivity -e args "W1A --nologo" -e env "WOODY_FPSLOG=1;WOODY_X=1"
package io.github.jjmhalew.woodyre;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.system.Os;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

public class WoodyActivity extends SDLActivity {
    private static final String TAG = "WoodyRE";
    private static final int REQ_ISO = 0x5701, REQ_DIR = 0x5702;
    // the CD's parts the game reads (src/datafiles.h); the rest of the disc (setup, DirectX) is not copied
    private static final Set<String> CD_PARTS = new HashSet<>(Arrays.asList("data", "common", "logo", "game", "music.bf"));

    private static final Object sLock = new Object();
    private static boolean sPicked;
    private static Uri sPick;
    private static AlertDialog sProgress;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected String[] getArguments() {                  // testing: the command line of main_engine.c
        String a = getIntent() != null ? getIntent().getStringExtra("args") : null;
        return a == null || a.trim().isEmpty() ? new String[0] : a.trim().split("\\s+");
    }

    @Override
    protected void onCreate(Bundle state) {
        String env = getIntent() != null ? getIntent().getStringExtra("env") : null;   // testing: WOODY_* settings, "K=V;K=V"
        if (env != null) for (String kv : env.split(";")) {
            int i = kv.indexOf('=');
            if (i > 0) try { Os.setenv(kv.substring(0, i).trim(), kv.substring(i + 1), true); } catch (Exception e) { Log.w(TAG, "setenv " + kv, e); }
        }
        super.onCreate(state);
    }

    // ---- called from the game's thread (src/datasetup_posix.c) -------------------------------------------------------

    /** kind 1: an ISO image, returns a file descriptor the caller closes; kind 2: a folder, copied into dest, returns 0.
     *  -1 = cancelled, -2 = failed. Blocks until the user is done. */
    public static int pickGameData(int kind, String dest) {
        final Activity a = (Activity) SDLActivity.getContext();
        if (a == null) return -2;
        synchronized (sLock) { sPicked = false; sPick = null; }
        a.runOnUiThread(() -> {
            Intent i;
            if (kind == 1) { i = new Intent(Intent.ACTION_OPEN_DOCUMENT); i.addCategory(Intent.CATEGORY_OPENABLE); i.setType("*/*"); }
            else i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            try { a.startActivityForResult(i, kind == 1 ? REQ_ISO : REQ_DIR); }
            catch (Exception e) { Log.e(TAG, "no file picker", e); picked(null); }
        });
        Uri u;
        synchronized (sLock) {
            while (!sPicked) try { sLock.wait(); } catch (InterruptedException e) { return -1; }
            u = sPick;
        }
        if (u == null) return -1;
        try {
            if (kind == 1) {
                ParcelFileDescriptor pfd = a.getContentResolver().openFileDescriptor(u, "r");
                return pfd != null ? pfd.detachFd() : -2;
            }
            long[] done = { 0, 0 };
            copyDir(a.getContentResolver(), u, DocumentsContract.getTreeDocumentId(u), new File(dest), true, done);
            progress(null);
            return 0;
        } catch (Exception e) {
            Log.e(TAG, "pickGameData", e);
            progress(null);
            return -2;
        }
    }

    /** a dialog with this text over the game, null closes it */
    public static void progress(final String text) {
        final Activity a = (Activity) SDLActivity.getContext();
        if (a == null) return;
        a.runOnUiThread(() -> {
            if (text == null) { if (sProgress != null) { sProgress.dismiss(); sProgress = null; } return; }
            if (sProgress == null) { sProgress = new AlertDialog.Builder(a, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("WoodyRE").setMessage(text).setCancelable(false).create(); sProgress.show(); }
            else sProgress.setMessage(text);
        });
    }

    /** the game's questions and messages (plat_dialog in src/plat_sdl.c): the text scrolls, the buttons stay on the
     *  screen. Returns 1 for b1, 0 for b2, -1 for b3 (or when there is no activity). Blocks until a button is pressed. */
    public static int dialog(final String text, final String b1, final String b2, final String b3) {
        final Activity a = (Activity) SDLActivity.getContext();
        if (a == null) return -1;
        final Object lock = new Object();
        final int[] r = { -2 };
        a.runOnUiThread(() -> {
            AlertDialog.Builder d = new AlertDialog.Builder(a, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("WoodyRE").setMessage(text).setCancelable(false);
            if (b1 != null) d.setPositiveButton(b1, (x, w) -> answer(lock, r, 1));
            if (b2 != null) d.setNeutralButton(b2, (x, w) -> answer(lock, r, 0));
            if (b3 != null) d.setNegativeButton(b3, (x, w) -> answer(lock, r, -1));
            d.show();
        });
        synchronized (lock) {
            while (r[0] == -2) try { lock.wait(); } catch (InterruptedException e) { return -1; }
        }
        return r[0];
    }

    private static void answer(Object lock, int[] r, int v) {
        synchronized (lock) { r[0] = v; lock.notifyAll(); }
    }

    private static void picked(Uri u) {
        synchronized (sLock) { sPick = u; sPicked = true; sLock.notifyAll(); }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (request == REQ_ISO || request == REQ_DIR) { picked(result == RESULT_OK && data != null ? data.getData() : null); return; }
        super.onActivityResult(request, result, data);
    }

    private static void copyDir(ContentResolver cr, Uri tree, String docId, File dest, boolean top, long[] done) throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        String[] cols = { Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE };
        try (Cursor q = cr.query(children, cols, null, null, null)) {
            if (q == null) throw new IOException("cannot list " + docId);
            while (q.moveToNext()) {
                String id = q.getString(0), name = q.getString(1), mime = q.getString(2);
                if (top && !CD_PARTS.contains(name.toLowerCase(Locale.ROOT))) continue;
                File f = new File(dest, name);
                if (Document.MIME_TYPE_DIR.equals(mime)) {
                    if (!f.isDirectory() && !f.mkdirs()) throw new IOException("cannot create " + f);
                    copyDir(cr, tree, id, f, false, done);
                    continue;
                }
                File part = new File(dest, name + ".part");
                try (InputStream in = cr.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, id));
                     OutputStream out = new FileOutputStream(part)) {
                    if (in == null) throw new IOException("cannot open " + name);
                    byte[] buf = new byte[1 << 20];
                    for (int n; (n = in.read(buf)) > 0; ) {
                        out.write(buf, 0, n);
                        done[0] += n;
                        if (done[0] - done[1] >= 8 << 20) { done[1] = done[0]; progress("Copying the game files... " + (done[0] >> 20) + " MB"); }
                    }
                }
                if (!part.renameTo(f)) throw new IOException("cannot write " + f);
            }
        }
    }
}
