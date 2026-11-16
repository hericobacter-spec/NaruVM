package dev.henge.research;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/** Input rules shared by the live service and the Activity's bounded worker. */
final class MacInput {
    interface Sink { void send(String line); }
    static final String[] MOUSE_SPEED_LABELS = {"0.5배", "1배 · 추천", "1.5배", "2배", "3배"};
    static float mouseSpeed(int index) {
        return new float[]{.5f, 1f, 1.5f, 2f, 3f}[index >= 0 && index < 5 ? index : 1];
    }

    /** Held physical input is independent from touch-to-click gestures. */
    static final class Hardware {
        private final Sink sink;
        private final boolean[] keys = new boolean[256];
        private int buttons;
        private boolean origin, relative;
        private float x, y;
        private double remainderX, remainderY, wheelX, wheelY;
        Hardware(Sink sink) { this.sink = sink; }
        void key(int hid, boolean down) {
            if (hid < 4 || hid > 255 || keys[hid] == down) return;
            keys[hid] = down;
            sink.send("live hid " + hid + " " + (down ? 1 : 0));
        }
        private void relativeMode() {
            if (!relative) { sink.send("live move 0 0"); relative = true; }
        }
        void buttons(int next) {
            next &= 7;
            if (next != buttons) relativeMode();
            for (int i = 0; i < 3; ++i) {
                int bit = 1 << i;
                if (((next ^ buttons) & bit) != 0)
                    sink.send("live button " + i + " " + ((next & bit) != 0 ? 1 : 0));
            }
            buttons = next;
        }
        void delta(float dx, float dy, float scale) {
            if (!Float.isFinite(dx) || !Float.isFinite(dy) || !Float.isFinite(scale) || scale <= 0) return;
            remainderX = Math.max(-32767, Math.min(32767, remainderX + dx / scale));
            remainderY = Math.max(-32767, Math.min(32767, remainderY + dy / scale));
            int px = (int)remainderX, py = (int)remainderY;
            remainderX -= px; remainderY -= py;
            if (px != 0 || py != 0) { relativeMode(); sink.send("live move " + px + " " + py); }
        }
        void position(float px, float py, float scale) {
            if (!Float.isFinite(px) || !Float.isFinite(py)) return;
            if (origin) delta(px - x, py - y, scale);
            x = px; y = py; origin = true;
        }
        void scroll(float horizontal, float vertical) {
            if (!Float.isFinite(horizontal) || !Float.isFinite(vertical)) return;
            wheelX = Math.max(-100, Math.min(100, wheelX + horizontal));
            wheelY = Math.max(-100, Math.min(100, wheelY + vertical));
            int sx = (int)wheelX, sy = (int)wheelY;
            wheelX -= sx; wheelY -= sy;
            if (sx != 0 || sy != 0) sink.send("live scroll " + sx + " " + sy);
        }
        void resetPointer() { origin = false; relative = false; remainderX = remainderY = 0; }
        void release() {
            buttons(0);
            for (int i = 4; i < keys.length; ++i) key(i, false);
            resetPointer(); wheelX = wheelY = 0;
        }
    }

    static void write(OutputStream out, String line) throws IOException, InterruptedException {
        // Physical events already carry the user's actual press/release timing.
        // Keep synthetic toolbar taps paced, but never stall a held mouse drag.
        boolean live = line.startsWith("live ");
        if (live) line = line.substring(5);
        if (line.equals("input_source")) {
            // Keep the chord in one serialized writer task. A slow guest can miss
            // a 10 ms modifier/space overlap; a 300 ms hold was observed to work.
            try {
                write(out, "hid 224 1");
                write(out, "hid 44 1");
                Thread.sleep(300);
            } finally {
                out.write("hid 44 0\nhid 224 0\n".getBytes(StandardCharsets.US_ASCII));
                out.flush();
            }
            return;
        }
        out.write((line + "\n").getBytes(StandardCharsets.US_ASCII));
        out.flush();
        // The single writer keeps the following release behind this hold.
        // host/main.c's click_here uses the same 120 ms for the guest mouse.
        if (!live && line.matches("button [0-2] 1")) Thread.sleep(120);
        else if (!live && line.startsWith("hid ")) Thread.sleep(10);
    }

    static int[] target(float viewWidth, float viewHeight, float imageWidth, float imageHeight, float x, float y) {
        if (viewWidth<=0 || viewHeight<=0 || imageWidth<=1 || imageHeight<=1) return null;
        float scale=Math.min(viewWidth/imageWidth, viewHeight/imageHeight);
        float left=(viewWidth-imageWidth*scale)/2, top=(viewHeight-imageHeight*scale)/2;
        float px=(x-left)/scale, py=(y-top)/scale;
        if (px<0 || py<0 || px>=imageWidth || py>=imageHeight) return null;
        return new int[]{Math.round(Math.min(px,imageWidth-1)*65535/(imageWidth-1)),
            Math.round(Math.min(py,imageHeight-1)*65535/(imageHeight-1))};
    }

    static final class Tap {
        private float x, y, slop;
        private boolean eligible;
        void begin(float x, float y, float slop) {
            this.x=x; this.y=y; this.slop=slop; eligible=true;
        }
        void move(float x, float y) {
            if (Math.hypot(x-this.x, y-this.y) >= slop) eligible=false;
        }
        boolean eligible() { return eligible; }
        void cancel() { eligible=false; }
        boolean end(float x, float y) {
            move(x, y);
            boolean click=eligible; eligible=false;
            return click;
        }
    }
}
