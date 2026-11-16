package dev.henge.research;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;

public final class MacInputTest {
    static int checks;
    static void check(boolean value, String name) {
        if (!value) throw new AssertionError(name);
        checks++;
    }
    public static void main(String[] args) throws Exception {
        ArrayList<String> sent = new ArrayList<>();
        MacInput.Hardware hardware = new MacInput.Hardware(sent::add);
        hardware.position(100, 100, 2); hardware.position(102, 104, 2);
        hardware.buttons(1); hardware.position(110, 110, 2); hardware.buttons(0);
        check(sent.equals(Arrays.asList("live move 0 0", "live move 1 2", "live button 0 1", "live move 4 3", "live button 0 0")),
            "physical drag keeps button held across motion with no touch tap synthesized");
        sent.clear(); hardware.buttons(6); hardware.buttons(2); hardware.buttons(0);
        check(sent.equals(Arrays.asList("live button 1 1", "live button 2 1", "live button 2 0", "live button 1 0")),
            "independent right and middle buttons release once");
        sent.clear(); hardware.scroll(.25f, -.25f); hardware.scroll(.25f, -.25f);
        hardware.scroll(.5f, -.5f);
        check(sent.equals(Arrays.asList("live scroll 1 -1")), "fractional two-axis wheel accumulates without losing direction");
        sent.clear(); hardware.key(227, true); hardware.key(6, true); hardware.key(6, true); hardware.release();
        check(sent.equals(Arrays.asList("live hid 227 1", "live hid 6 1", "live hid 6 0", "live hid 227 0")),
            "Command chord and repeated down have paired releases on focus loss");
        sent.clear(); hardware.release();
        check(sent.isEmpty(), "focus cleanup is idempotent");
        hardware.buttons(7); sent.clear(); hardware.release();
        check(sent.equals(Arrays.asList("live button 0 0", "live button 1 0", "live button 2 0")), "cancel releases every held mouse button");
        sent.clear(); hardware.delta(Float.NaN, 1, 1); hardware.delta(1, 1, 0); hardware.scroll(Float.POSITIVE_INFINITY, 1);
        check(sent.isEmpty(), "invalid coordinates and axes cannot create protocol input");
        hardware.delta(.25f, -.25f, 1); hardware.delta(.25f, -.25f, 1); hardware.delta(.5f, -.5f, 1);
        check(sent.equals(Arrays.asList("live move 0 0", "live move 1 -1")), "subpixel motion survives both directions");
        sent.clear(); hardware.delta(Float.MAX_VALUE, -Float.MAX_VALUE, 1); hardware.delta(0, 0, 1);
        check(sent.equals(Arrays.asList("live move 32767 -32767")), "oversized event is bounded and cannot leave runaway motion queued");
        sent.clear(); hardware.resetPointer(); hardware.position(400, 200, 1);
        check(sent.isEmpty(), "re-entry/capture starts a new mouse origin without teleporting");
        MacInput.Tap tap = new MacInput.Tap();
        tap.begin(10, 20, 12);
        check(tap.end(10, 20), "stationary press releases as a click without a time cutoff");
        check(!tap.end(10, 20), "one release cannot click twice");
        tap.begin(10, 20, 12); tap.move(14, 22);
        check(tap.end(15, 22), "small finger jitter keeps a click");
        tap.begin(10, 20, 12); tap.move(50, 20); tap.move(10, 20);
        check(!tap.end(10, 20), "drag out and back must not click");
        tap.begin(10, 20, 12); tap.cancel();
        check(!tap.end(10, 20), "cancel or second finger must not click");
        tap.begin(10, 20, 12);
        check(!tap.end(22, 20), "release beyond slop must not click");
        int[] point=MacInput.target(2944,1840,1024,768,1472,920);
        check(point!=null && Math.abs(point[0]-32800)<4 && Math.abs(point[1]-32810)<4,"fit-center maps visual midpoint into guest pixels");
        check(MacInput.target(2944,1840,1024,768,10,920)==null,"pillarbox taps do not click the guest");
        point=MacInput.target(2944,1840,1280,800,0,0);
        check(point!=null && point[0]==0 && point[1]==0,"matching aspect starts at origin");
        check(MacInput.target(100,100,0,0,20,20)==null,"no click before a frame exists");
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        long start = System.nanoTime();
        MacInput.write(out, "button 0 1");
        long heldMs = (System.nanoTime()-start)/1000000;
        MacInput.write(out, "button 0 0");
        check(heldMs >= 115, "button hold must last about 120 ms before release");
        check(new String(out.toByteArray(), StandardCharsets.US_ASCII).equals("button 0 1\nbutton 0 0\n"), "ordered private protocol with exactly one down and up");
        out.reset(); MacInput.write(out, "move -2 3");
        check(out.toString("US-ASCII").equals("move -2 3\n"), "motion does not synthesize a click");
        out.reset(); start=System.nanoTime();
        MacInput.write(out, "input_source");
        check((System.nanoTime()-start)/1000000 >= 300, "input-source chord survives slow guest polling");
        check(out.toString("US-ASCII").equals("hid 224 1\nhid 44 1\nhid 44 0\nhid 224 0\n"), "source chord releases both keys in order");
        out.reset(); Thread.currentThread().interrupt();
        try { MacInput.write(out, "input_source"); throw new AssertionError("interruption expected"); }
        catch (InterruptedException expected) { }
        check(out.toString("US-ASCII").endsWith("hid 44 0\nhid 224 0\n"), "interrupted chord cannot leave Control held");
        out.reset(); Thread.currentThread().interrupt();
        MacInput.write(out, "live button 0 1");
        MacInput.write(out, "live move 12 -3");
        MacInput.write(out, "live button 0 0");
        check(Thread.interrupted(), "physical drag never sleeps or consumes the interrupt flag");
        check(out.toString("US-ASCII").equals("button 0 1\nmove 12 -3\nbutton 0 0\n"),
            "host-only live marker is removed before native protocol delivery");
        check(MacInput.mouseSpeed(1) == 1 && MacInput.mouseSpeed(3) == 2 && MacInput.mouseSpeed(-1) == 1,
            "mouse sensitivity has a stable recommended default and validates saved index");
        System.out.println("MAC_INPUT_TEST checks="+checks+" failures=0 button_hold_ms="+heldMs+" source=authored no_vm=1");
    }
}
