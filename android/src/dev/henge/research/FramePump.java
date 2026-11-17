package dev.henge.research;

import android.os.Handler;
import java.nio.ByteBuffer;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;

/** Worker-owned native reader and RGBA staging buffer; at most one UI delivery. */
final class FramePump implements AutoCloseable {
    interface Listener {
        void onFrame(Frame frame);
        void onError(String reason);
    }
    static final class Frame {
        final ByteBuffer rgba;
        final int width, height;
        final long sequence, prepareNs, ageNs, preparedNs, preparedThreadId;
        Frame(ByteBuffer rgba, int width, int height, long sequence, long prepareNs, long ageNs) {
            this.rgba=rgba; this.width=width; this.height=height; this.sequence=sequence;
            this.prepareNs=prepareNs; this.ageNs=ageNs; this.preparedNs=System.nanoTime();
            this.preparedThreadId=Thread.currentThread().getId();
        }
    }
    private final ScheduledExecutorService worker = Executors.newSingleThreadScheduledExecutor(r -> {
        Thread t=new Thread(r,"naruvm-frame-reader"); t.setDaemon(true); return t;
    });
    private final AtomicBoolean closed = new AtomicBoolean(), pending = new AtomicBoolean();
    private final Handler ui;
    private final Listener listener;
    private final String path;
    private final long epoch;
    private volatile ScheduledFuture<?> watch;
    // Accessed only by worker; the buffer is lent read-only to the UI callback
    // until pending becomes false. No displayed Bitmap is mutated by worker.
    private NativeFrames reader;
    private ByteBuffer rgba;
    FramePump(Handler ui, String path, long epoch, int periodMs, Listener listener) {
        this.ui=ui; this.path=path; this.epoch=epoch; this.listener=listener;
        watch=worker.scheduleAtFixedRate(this::poll,0,periodMs,TimeUnit.MILLISECONDS);
        if (closed.get()) watch.cancel(false);
    }
    private void poll() {
        if (closed.get() || pending.get()) return;
        try {
            if (reader == null) {
                reader=new NativeFrames(path,epoch);
                if (!reader.ready()) { reader.close(); reader=null; return; }
            }
            long size=reader.size();
            if (size == 0) return;
            int width=(int)(size>>>32), height=(int)size;
            int bytes=Math.multiplyExact(Math.multiplyExact(width,height),4);
            if (rgba == null || rgba.capacity() != bytes) rgba=ByteBuffer.allocateDirect(bytes);
            rgba.clear();
            long began=System.nanoTime(), sequence=reader.copyBuffer(rgba);
            if (sequence <= 0) throw new IllegalStateException("Native frame copy failed");
            Frame frame=new Frame(rgba,width,height,sequence,System.nanoTime()-began,reader.ageNs());
            pending.set(true);
            if (!ui.post(() -> {
                try { if (!closed.get()) listener.onFrame(frame); }
                finally { pending.set(false); }
            })) { pending.set(false); close(); }
        } catch (RuntimeException e) {
            ui.post(() -> listener.onError(e.getClass().getSimpleName()));
            close();
        }
    }
    @Override public void close() {
        if (!closed.compareAndSet(false,true)) return;
        // Cleanup is queued behind any in-flight native copy, never on UI.
        ScheduledFuture<?> task=watch;
        if (task != null) task.cancel(false);
        worker.execute(() -> { if (reader != null) { reader.close(); reader=null; } rgba=null; });
        worker.shutdown();
    }
}
