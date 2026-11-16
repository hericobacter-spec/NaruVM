package dev.henge.research;
import android.graphics.Bitmap;

/** Complete-frame shared memory reader. Calls stay on the Activity UI thread. */
final class NativeFrames implements AutoCloseable {
    static { System.loadLibrary("henge_frame_jni"); }
    private long handle;
    NativeFrames(String path, long epoch) { handle = open(path, epoch); }
    boolean ready() { return handle != 0; }
    long size() { return handle == 0 ? 0 : size(handle); }
    long copy(Bitmap bitmap) { return handle == 0 ? 0 : copy(handle, bitmap); }
    long ageNs() { return handle == 0 ? 0 : ageNs(handle); }
    @Override public void close() { if (handle != 0) { close(handle); handle = 0; } }
    private static native long open(String path, long epoch);
    private static native long size(long handle);
    private static native long copy(long handle, Bitmap bitmap);
    private static native long ageNs(long handle);
    private static native void close(long handle);
}
