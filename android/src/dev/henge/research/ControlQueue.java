package dev.henge.research;

import java.util.ArrayDeque;

/** Bounded private-protocol queue. Its owner serializes access and pipe writes. */
final class ControlQueue {
    static final int CAPACITY = 256;
    private static final class Entry {
        final String line;
        long x, y;
        Entry(String line) { this.line = line; }
        Entry(long x, long y) { this.line = null; this.x = x; this.y = y; }
    }
    private final ArrayDeque<Entry> queue = new ArrayDeque<>();
    private boolean stopping;
    long resets, merged, discarded;
    int highWater;

    boolean offer(String line) {
        if (stopping) return false;
        if ("stop".equals(line)) {
            stopping = true;
            discarded += queue.size(); queue.clear();
            queue.add(new Entry("release_all")); queue.add(new Entry("stop"));
            return true;
        }
        long[] delta = movement(line);
        Entry tail = queue.peekLast();
        if (delta != null && tail != null && tail.line == null) {
            try {
                long x = Math.addExact(tail.x, delta[0]), y = Math.addExact(tail.y, delta[1]);
                tail.x = x; tail.y = y; merged++; return true;
            } catch (ArithmeticException overflow) { recover(); }
        }
        if (queue.size() == CAPACITY) recover();
        queue.addLast(delta == null ? new Entry(line) : new Entry(delta[0], delta[1]));
        highWater = Math.max(highWater, queue.size());
        return true;
    }
    private void recover() {
        // An overloaded finite queue cannot retain unlimited state transitions.
        // Explicitly release guest state instead of silently losing a key-up.
        discarded += queue.size(); queue.clear(); resets++;
        queue.add(new Entry("release_all"));
    }
    String poll() {
        Entry e = queue.peekFirst();
        if (e == null) return null;
        if (e.line != null) { queue.removeFirst(); return e.line; }
        int x = (int)Math.max(-32767L, Math.min(32767L, e.x));
        int y = (int)Math.max(-32767L, Math.min(32767L, e.y));
        e.x -= x; e.y -= y;
        if (e.x == 0 && e.y == 0) queue.removeFirst();
        return "live move " + x + " " + y;
    }
    int size() { return queue.size(); }
    void clear() { queue.clear(); stopping = true; }
    private static long[] movement(String line) {
        if (!line.startsWith("live move ")) return null;
        String[] fields = line.substring(10).split(" ", -1);
        if (fields.length != 2) return null;
        try {
            int x = Integer.parseInt(fields[0]), y = Integer.parseInt(fields[1]);
            if (x < -32767 || x > 32767 || y < -32767 || y > 32767) return null;
            return new long[]{x, y};
        } catch (NumberFormatException ignored) { return null; }
    }
}
